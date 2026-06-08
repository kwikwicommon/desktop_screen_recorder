#include "recording_overlay.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

bool ApplyGdiToFrame(BitmapFrame* frame,
                     const std::function<void(HDC)>& draw_callback) {
  if (frame == nullptr || frame->width <= 0 || frame->height <= 0 ||
      frame->bgra.empty()) {
    return false;
  }
  size_t safe_buffer_size = 0;
  if (!TryGetBgraBufferSize(frame->width, frame->height,
                            &safe_buffer_size) ||
      frame->bgra.size() < safe_buffer_size) {
    return false;
  }

  BITMAPINFO bitmap_info = {};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = frame->width;
  bitmap_info.bmiHeader.biHeight = -frame->height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) {
    return false;
  }
  HDC memory_dc = CreateCompatibleDC(screen_dc);
  if (memory_dc == nullptr) {
    ReleaseDC(nullptr, screen_dc);
    return false;
  }

  void* bits = nullptr;
  HBITMAP bitmap =
      CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr,
                       0);
  if (bitmap == nullptr || bits == nullptr) {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    return false;
  }

  std::memcpy(bits, frame->bgra.data(), safe_buffer_size);
  HGDIOBJ previous = SelectObject(memory_dc, bitmap);
  draw_callback(memory_dc);
  GdiFlush();
  SelectObject(memory_dc, previous);
  const auto* data = static_cast<const uint8_t*>(bits);
  frame->bgra.assign(data, data + safe_buffer_size);

  DeleteObject(bitmap);
  DeleteDC(memory_dc);
  ReleaseDC(nullptr, screen_dc);
  return true;
}

void BlendPixel(BitmapFrame* frame,
                int x,
                int y,
                uint8_t red,
                uint8_t green,
                uint8_t blue,
                float alpha) {
  if (x < 0 || y < 0 || x >= frame->width || y >= frame->height ||
      alpha <= 0.0f) {
    return;
  }
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  const size_t offset = (static_cast<size_t>(y) * frame->width + x) * 4;
  frame->bgra[offset] = static_cast<uint8_t>(
      frame->bgra[offset] * (1.0f - alpha) + blue * alpha);
  frame->bgra[offset + 1] = static_cast<uint8_t>(
      frame->bgra[offset + 1] * (1.0f - alpha) + green * alpha);
  frame->bgra[offset + 2] = static_cast<uint8_t>(
      frame->bgra[offset + 2] * (1.0f - alpha) + red * alpha);
  frame->bgra[offset + 3] = 255;
}

void DrawCircleRing(BitmapFrame* frame,
                    int center_x,
                    int center_y,
                    int radius,
                    int thickness,
                    float alpha) {
  const int min_x = center_x - radius - thickness;
  const int max_x = center_x + radius + thickness;
  const int min_y = center_y - radius - thickness;
  const int max_y = center_y + radius + thickness;
  const int inner = std::max(0, radius - thickness);
  const int outer_sq = radius * radius;
  const int inner_sq = inner * inner;
  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      const int dx = x - center_x;
      const int dy = y - center_y;
      const int distance_sq = dx * dx + dy * dy;
      if (distance_sq <= outer_sq && distance_sq >= inner_sq) {
        BlendPixel(frame, x, y, 255, 214, 64, alpha);
      }
    }
  }
}

std::wstring KeyLabelForVirtualKey(int virtual_key) {
  switch (virtual_key) {
    case VK_SPACE:
      return L"Space";
    case VK_RETURN:
      return L"Enter";
    case VK_ESCAPE:
      return L"Esc";
    case VK_BACK:
      return L"Backspace";
    case VK_TAB:
      return L"Tab";
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
      return L"Shift";
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
      return L"Ctrl";
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
      return L"Alt";
    default:
      break;
  }

  if (virtual_key >= 'A' && virtual_key <= 'Z') {
    return std::wstring(1, static_cast<wchar_t>(virtual_key));
  }
  if (virtual_key >= '0' && virtual_key <= '9') {
    return std::wstring(1, static_cast<wchar_t>(virtual_key));
  }
  if (virtual_key >= VK_F1 && virtual_key <= VK_F24) {
    return L"F" + std::to_wstring(virtual_key - VK_F1 + 1);
  }

  const UINT scan_code = MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC);
  wchar_t buffer[64] = {};
  if (GetKeyNameTextW(static_cast<LONG>(scan_code << 16), buffer, 64) > 0) {
    return buffer;
  }
  return L"";
}

class CameraOverlaySource {
 public:
  ~CameraOverlaySource() { Stop(); }

  void Stop() {
    reader_.Reset();
    if (media_source_ != nullptr) {
      media_source_->Shutdown();
      media_source_.Reset();
    }
    camera_width_ = 0;
    camera_height_ = 0;
    active_ = false;
  }

  bool Initialize(const CameraOverlayConfig& config,
                  std::vector<std::string>* recovery_events) {
    Stop();
    config_ = config;
    if (!config.enabled) {
      return false;
    }

    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }
    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }

    IMFActivate** devices = nullptr;
    UINT32 device_count = 0;
    hr = MFEnumDeviceSources(attributes.Get(), &devices, &device_count);
    if (FAILED(hr) || device_count == 0) {
      if (devices != nullptr) {
        CoTaskMemFree(devices);
      }
      return ReportFailure(FAILED(hr) ? hr : MF_E_NOT_FOUND, recovery_events);
    }

    hr = devices[0]->ActivateObject(IID_PPV_ARGS(&media_source_));
    for (UINT32 index = 0; index < device_count; ++index) {
      devices[index]->Release();
    }
    CoTaskMemFree(devices);
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }

    hr = MFCreateSourceReaderFromMediaSource(media_source_.Get(), nullptr,
                                             &reader_);
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }

    ComPtr<IMFMediaType> type;
    hr = MFCreateMediaType(&type);
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }
    hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }
    hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }
    hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }
    hr = reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }
    hr = reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                      nullptr, type.Get());
    if (FAILED(hr)) {
      return ReportFailure(hr, recovery_events);
    }

    ComPtr<IMFMediaType> current_type;
    hr = reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                      &current_type);
    if (SUCCEEDED(hr)) {
      UINT32 width = 0;
      UINT32 height = 0;
      if (SUCCEEDED(MFGetAttributeSize(current_type.Get(), MF_MT_FRAME_SIZE,
                                       &width, &height)) &&
          width > 0 && height > 0) {
        camera_width_ = static_cast<int>(width);
        camera_height_ = static_cast<int>(height);
      }
    }

    if (camera_width_ <= 0 || camera_height_ <= 0) {
      camera_width_ = 640;
      camera_height_ = 480;
    }
    if (recovery_events != nullptr) {
      recovery_events->push_back("Windows camera overlay source started.");
    }
    active_ = true;
    return true;
  }

  bool Capture(BitmapFrame* frame) {
    if (!active_ || reader_ == nullptr || frame == nullptr) {
      return false;
    }

    DWORD stream_index = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    ComPtr<IMFSample> sample;
    HRESULT hr = reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                     &stream_index, &flags, &timestamp,
                                     &sample);
    if (FAILED(hr) || sample == nullptr ||
        (flags & MF_SOURCE_READERF_ERROR) != 0) {
      active_ = false;
      return false;
    }

    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
      return false;
    }
    BYTE* data = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    hr = buffer->Lock(&data, &max_length, &current_length);
    if (FAILED(hr)) {
      return false;
    }

    size_t expected = 0;
    if (!TryGetBgraBufferSize(camera_width_, camera_height_, &expected) ||
        expected > std::numeric_limits<DWORD>::max() ||
        current_length < expected) {
      buffer->Unlock();
      return false;
    }
    frame->width = camera_width_;
    frame->height = camera_height_;
    frame->bgra.assign(data, data + expected);
    buffer->Unlock();
    return true;
  }

 private:
  bool ReportFailure(HRESULT hr, std::vector<std::string>* recovery_events) {
    if (recovery_events != nullptr) {
      recovery_events->push_back("Windows camera overlay is unavailable: " +
                                 HResultToString(hr));
    }
    return false;
  }

  CameraOverlayConfig config_;
  ComPtr<IMFMediaSource> media_source_;
  ComPtr<IMFSourceReader> reader_;
  int camera_width_ = 0;
  int camera_height_ = 0;
  bool active_ = false;
};

struct ClickPulse {
  POINT point = {};
  std::chrono::steady_clock::time_point created_at;
};

struct KeyBadge {
  std::wstring label;
  std::chrono::steady_clock::time_point created_at;
};

}  // namespace

class RecordingOverlayRendererImpl {
 public:
  void Initialize(const RecordingConfig& config,
                  std::vector<std::string>* recovery_events) {
    config_ = config;
    previous_keys_.fill(false);
    camera_active_ = camera_.Initialize(config.camera_overlay, recovery_events);
  }

  bool camera_active() const { return camera_active_; }

  void Shutdown() {
    camera_.Stop();
    camera_active_ = false;
  }

  void Apply(BitmapFrame* frame, const RECT& capture_rect) {
    if (frame == nullptr) {
      return;
    }
    UpdateInputState(capture_rect);
    if (config_.show_cursor) {
      DrawCursor(frame, capture_rect);
    }
    if (config_.highlight_clicks) {
      DrawClicks(frame);
    }
    if (config_.show_keystrokes) {
      DrawKeystrokes(frame);
    }
    if (config_.camera_overlay.enabled) {
      DrawCamera(frame);
    }
  }

 private:
  void UpdateInputState(const RECT& capture_rect) {
    const auto now = std::chrono::steady_clock::now();
    if (config_.highlight_clicks) {
      POINT cursor = {};
      if (GetCursorPos(&cursor)) {
        const bool left_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const bool right_down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        const bool middle_down = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        if ((left_down && !previous_left_) ||
            (right_down && !previous_right_) ||
            (middle_down && !previous_middle_)) {
          POINT local = {cursor.x - capture_rect.left,
                         cursor.y - capture_rect.top};
          clicks_.push_back(ClickPulse{local, now});
        }
        previous_left_ = left_down;
        previous_right_ = right_down;
        previous_middle_ = middle_down;
      }
      clicks_.erase(std::remove_if(clicks_.begin(), clicks_.end(),
                                   [now](const ClickPulse& pulse) {
                                     return now - pulse.created_at >
                                            std::chrono::milliseconds(700);
                                   }),
                    clicks_.end());
    }

    if (config_.show_keystrokes) {
      for (int key = 1; key < 256; ++key) {
        if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON) {
          continue;
        }
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down && !previous_keys_[key]) {
          std::wstring label = KeyLabelForVirtualKey(key);
          if (!label.empty()) {
            key_badges_.push_back(KeyBadge{label, now});
            if (key_badges_.size() > 4) {
              key_badges_.erase(key_badges_.begin());
            }
          }
        }
        previous_keys_[key] = down;
      }
      key_badges_.erase(std::remove_if(key_badges_.begin(), key_badges_.end(),
                                       [now](const KeyBadge& badge) {
                                         return now - badge.created_at >
                                                std::chrono::milliseconds(1400);
                                       }),
                        key_badges_.end());
    }
  }

  void DrawCursor(BitmapFrame* frame, const RECT& capture_rect) {
    CURSORINFO cursor_info = {};
    cursor_info.cbSize = sizeof(cursor_info);
    if (!GetCursorInfo(&cursor_info) ||
        (cursor_info.flags & CURSOR_SHOWING) == 0 ||
        cursor_info.hCursor == nullptr) {
      return;
    }

    ICONINFO icon_info = {};
    if (!GetIconInfo(cursor_info.hCursor, &icon_info)) {
      return;
    }
    const int x = cursor_info.ptScreenPos.x - capture_rect.left -
                  static_cast<int>(icon_info.xHotspot);
    const int y = cursor_info.ptScreenPos.y - capture_rect.top -
                  static_cast<int>(icon_info.yHotspot);
    ApplyGdiToFrame(frame, [&](HDC dc) {
      DrawIconEx(dc, x, y, cursor_info.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
    });
    if (icon_info.hbmMask != nullptr) {
      DeleteObject(icon_info.hbmMask);
    }
    if (icon_info.hbmColor != nullptr) {
      DeleteObject(icon_info.hbmColor);
    }
  }

  void DrawClicks(BitmapFrame* frame) {
    const auto now = std::chrono::steady_clock::now();
    for (const auto& pulse : clicks_) {
      const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now - pulse.created_at)
                              .count();
      const float alpha = std::max(0.0f, 1.0f - age_ms / 700.0f);
      const int radius = 18 + static_cast<int>(age_ms / 35);
      DrawCircleRing(frame, pulse.point.x, pulse.point.y, radius, 4, alpha);
    }
  }

  void DrawKeystrokes(BitmapFrame* frame) {
    if (key_badges_.empty()) {
      return;
    }
    std::wstring text;
    for (size_t index = 0; index < key_badges_.size(); ++index) {
      if (index > 0) {
        text += L"  ";
      }
      text += key_badges_[index].label;
    }

    ApplyGdiToFrame(frame, [&](HDC dc) {
      HFONT font = CreateFontW(
          -24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
      HGDIOBJ old_font = font == nullptr ? nullptr : SelectObject(dc, font);
      SetBkMode(dc, TRANSPARENT);
      SIZE text_size = {};
      GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()),
                            &text_size);
      const int padding_x = 18;
      const int padding_y = 10;
      RECT rect = {};
      rect.left = std::max(16, (frame->width - text_size.cx) / 2 - padding_x);
      rect.right = std::min(frame->width - 16,
                            rect.left + text_size.cx + padding_x * 2);
      rect.bottom = frame->height - 28;
      rect.top = rect.bottom - text_size.cy - padding_y * 2;

      HBRUSH brush = CreateSolidBrush(RGB(16, 16, 16));
      HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
      HGDIOBJ old_brush = SelectObject(dc, brush);
      HGDIOBJ old_pen = SelectObject(dc, pen);
      RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 12, 12);
      SelectObject(dc, old_brush);
      SelectObject(dc, old_pen);
      DeleteObject(brush);
      DeleteObject(pen);

      SetTextColor(dc, RGB(255, 255, 255));
      RECT text_rect = {rect.left + padding_x, rect.top + padding_y,
                        rect.right - padding_x, rect.bottom - padding_y};
      DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &text_rect,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      if (old_font != nullptr) {
        SelectObject(dc, old_font);
      }
      if (font != nullptr) {
        DeleteObject(font);
      }
    });
  }

  RECT CameraTargetRect(const BitmapFrame& frame, const BitmapFrame& camera) {
    const int max_overlay_size = std::max(1, std::min(frame.width, frame.height));
    const int overlay_size =
        std::clamp(config_.camera_overlay.size, 1, max_overlay_size);
    int width = overlay_size;
    int height = std::max(
        1, static_cast<int>(std::lround(
               static_cast<double>(camera.height) * overlay_size /
               std::max(1, camera.width))));
    if (height > overlay_size) {
      height = overlay_size;
      width = std::max(
          1, static_cast<int>(std::lround(
                 static_cast<double>(camera.width) * overlay_size /
                 std::max(1, camera.height))));
    }
    if (config_.camera_overlay.shape == CameraOverlayShape::circle) {
      width = overlay_size;
      height = overlay_size;
    }
    const int margin = std::min(24, std::max(0, overlay_size / 4));
    RECT rect = {};
    switch (config_.camera_overlay.position) {
      case RecordingOverlayPosition::top_left:
        rect.left = margin;
        rect.top = margin;
        break;
      case RecordingOverlayPosition::top_right:
        rect.left = frame.width - width - margin;
        rect.top = margin;
        break;
      case RecordingOverlayPosition::bottom_left:
        rect.left = margin;
        rect.top = frame.height - height - margin;
        break;
      case RecordingOverlayPosition::bottom_right:
        rect.left = frame.width - width - margin;
        rect.top = frame.height - height - margin;
        break;
    }
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
    if (rect.left < 0) {
      rect.right -= rect.left;
      rect.left = 0;
    }
    if (rect.top < 0) {
      rect.bottom -= rect.top;
      rect.top = 0;
    }
    if (rect.right > frame.width) {
      rect.left = std::max(0, frame.width - width);
      rect.right = frame.width;
    }
    if (rect.bottom > frame.height) {
      rect.top = std::max(0, frame.height - height);
      rect.bottom = frame.height;
    }
    return rect;
  }

  void DrawCamera(BitmapFrame* frame) {
    BitmapFrame camera_frame;
    if (!camera_.Capture(&camera_frame)) {
      return;
    }
    const RECT target = CameraTargetRect(*frame, camera_frame);
    const int target_width = target.right - target.left;
    const int target_height = target.bottom - target.top;
    const double radius_x = target_width / 2.0;
    const double radius_y = target_height / 2.0;

    for (int y = 0; y < target_height; ++y) {
      for (int x = 0; x < target_width; ++x) {
        if (config_.camera_overlay.shape == CameraOverlayShape::circle) {
          const double dx = (x + 0.5 - radius_x) / std::max(1.0, radius_x);
          const double dy = (y + 0.5 - radius_y) / std::max(1.0, radius_y);
          if (dx * dx + dy * dy > 1.0) {
            continue;
          }
        }
        const int source_x = std::min(
            camera_frame.width - 1,
            static_cast<int>(
                static_cast<int64_t>(x) * camera_frame.width / target_width));
        const int source_y = std::min(
            camera_frame.height - 1,
            static_cast<int>(
                static_cast<int64_t>(y) * camera_frame.height / target_height));
        const size_t source_offset =
            (static_cast<size_t>(source_y) * camera_frame.width + source_x) * 4;
        BlendPixel(frame, target.left + x, target.top + y,
                   camera_frame.bgra[source_offset + 2],
                   camera_frame.bgra[source_offset + 1],
                   camera_frame.bgra[source_offset], 1.0f);
      }
    }

    const int border_radius = std::max(target_width, target_height) / 2;
    if (config_.camera_overlay.shape == CameraOverlayShape::circle) {
      DrawCircleRing(frame, target.left + target_width / 2,
                     target.top + target_height / 2, border_radius, 3, 0.9f);
    } else {
      ApplyGdiToFrame(frame, [&](HDC dc) {
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, target.left, target.top, target.right, target.bottom);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
        DeleteObject(pen);
      });
    }
  }

  RecordingConfig config_;
  CameraOverlaySource camera_;
  std::array<bool, 256> previous_keys_ = {};
  bool previous_left_ = false;
  bool previous_right_ = false;
  bool previous_middle_ = false;
  bool camera_active_ = false;
  std::vector<ClickPulse> clicks_;
  std::vector<KeyBadge> key_badges_;
};

RecordingOverlayRenderer::RecordingOverlayRenderer()
    : impl_(std::make_unique<RecordingOverlayRendererImpl>()) {}

RecordingOverlayRenderer::~RecordingOverlayRenderer() = default;

void RecordingOverlayRenderer::Initialize(
    const RecordingConfig& config,
    std::vector<std::string>* recovery_events) {
  impl_->Initialize(config, recovery_events);
}

bool RecordingOverlayRenderer::camera_active() const {
  return impl_->camera_active();
}

void RecordingOverlayRenderer::Shutdown() {
  impl_->Shutdown();
}

void RecordingOverlayRenderer::Apply(BitmapFrame* frame,
                                     const RECT& capture_rect) {
  impl_->Apply(frame, capture_rect);
}
