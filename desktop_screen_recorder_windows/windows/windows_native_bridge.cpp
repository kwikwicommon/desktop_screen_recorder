#include "windows_native_bridge.h"

#include "windows_native_bridge_internal.h"

#include <audioclient.h>
#include <dwmapi.h>
#include <ks.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <objidl.h>
#include <oleauto.h>
#include <propidl.h>
#include <propsys.h>
#include <shellapi.h>
#include <wincodec.h>
#include <windows.h>
#include <windowsx.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

using Microsoft::WRL::ComPtr;

bool TryGetBgraBufferSize(int width, int height, size_t* buffer_size) {
  if (buffer_size == nullptr || width <= 0 || height <= 0) {
    return false;
  }
  return TryGetBgraBufferSize(static_cast<UINT>(width),
                              static_cast<UINT>(height), buffer_size);
}

bool TryGetBgraBufferSize(UINT width, UINT height, size_t* buffer_size) {
  size_t stride = 0;
  return TryGetPackedImageBufferSize(width, height, 4, &stride, buffer_size);
}

bool TryGetPackedImageBufferSize(UINT width,
                                 UINT height,
                                 UINT bytes_per_pixel,
                                 size_t* stride,
                                 size_t* buffer_size) {
  if (stride == nullptr || buffer_size == nullptr || width == 0 ||
      height == 0 || bytes_per_pixel == 0) {
    return false;
  }
  const size_t safe_width = static_cast<size_t>(width);
  const size_t safe_height = static_cast<size_t>(height);
  const size_t safe_bytes_per_pixel = static_cast<size_t>(bytes_per_pixel);
  if (safe_width > std::numeric_limits<DWORD>::max() / safe_bytes_per_pixel) {
    return false;
  }
  const size_t safe_stride = safe_width * safe_bytes_per_pixel;
  if (safe_stride > std::numeric_limits<DWORD>::max() ||
      safe_height > std::numeric_limits<size_t>::max() / safe_stride ||
      safe_stride * safe_height > std::numeric_limits<DWORD>::max()) {
    return false;
  }
  *stride = safe_stride;
  *buffer_size = safe_stride * safe_height;
  return true;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return "";
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    return "";
  }
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

std::string WidePtrToUtf8(const wchar_t* value) {
  return value == nullptr ? "" : WideToUtf8(std::wstring(value));
}

EncodableValue MakeBoundsPayload(const RECT& rect) {
  EncodableMap bounds;
  bounds[EncodableValue("x")] = EncodableValue(rect.left);
  bounds[EncodableValue("y")] = EncodableValue(rect.top);
  bounds[EncodableValue("width")] = EncodableValue(rect.right - rect.left);
  bounds[EncodableValue("height")] = EncodableValue(rect.bottom - rect.top);
  return EncodableValue(bounds);
}

const EncodableValue* FindMapValue(const EncodableMap& map,
                                   const char* key) {
  const auto entry = map.find(EncodableValue(key));
  return entry == map.end() ? nullptr : &entry->second;
}

const EncodableMap* ReadMap(const EncodableMap& map, const char* key) {
  const auto* value = FindMapValue(map, key);
  return value == nullptr ? nullptr : std::get_if<EncodableMap>(value);
}

int64_t ReadInt(const EncodableValue* value, int64_t fallback) {
  if (value == nullptr) {
    return fallback;
  }
  if (const auto* typed = std::get_if<int32_t>(value)) {
    return *typed;
  }
  if (const auto* typed = std::get_if<int64_t>(value)) {
    return *typed;
  }
  if (const auto* typed = std::get_if<double>(value)) {
    constexpr double kMinInt64AsDouble = -9223372036854775808.0;
    constexpr double kMaxInt64ExclusiveAsDouble = 9223372036854775808.0;
    if (!std::isfinite(*typed) ||
        *typed < kMinInt64AsDouble ||
        *typed >= kMaxInt64ExclusiveAsDouble) {
      return fallback;
    }
    return static_cast<int64_t>(*typed);
  }
  return fallback;
}

int ReadInt32(const EncodableValue* value, int fallback) {
  const int64_t result = ReadInt(value, fallback);
  if (result < std::numeric_limits<int>::min() ||
      result > std::numeric_limits<int>::max()) {
    return fallback;
  }
  return static_cast<int>(result);
}

double ReadDouble(const EncodableValue* value, double fallback) {
  if (value == nullptr) {
    return fallback;
  }
  if (const auto* typed = std::get_if<double>(value)) {
    return std::isfinite(*typed) ? *typed : fallback;
  }
  if (const auto* typed = std::get_if<int32_t>(value)) {
    return static_cast<double>(*typed);
  }
  if (const auto* typed = std::get_if<int64_t>(value)) {
    return static_cast<double>(*typed);
  }
  return fallback;
}

bool ReadBool(const EncodableValue* value, bool fallback) {
  if (value == nullptr) {
    return fallback;
  }
  if (const auto* typed = std::get_if<bool>(value)) {
    return *typed;
  }
  return fallback;
}

std::string ReadString(const EncodableValue* value,
                       const std::string& fallback) {
  if (value == nullptr) {
    return fallback;
  }
  if (const auto* typed = std::get_if<std::string>(value)) {
    return *typed;
  }
  return fallback;
}

const EncodableMap* ArgumentsAsMap(
    const flutter::MethodCall<EncodableValue>& method_call) {
  return std::get_if<EncodableMap>(method_call.arguments());
}

bool ReadPhysicalRect(const EncodableMap& map, const char* key, RECT* rect) {
  const auto* value = FindMapValue(map, key);
  if (value == nullptr) {
    return false;
  }
  const auto* rect_map = std::get_if<EncodableMap>(value);
  if (rect_map == nullptr) {
    return false;
  }

  const int left = ReadInt32(FindMapValue(*rect_map, "left"));
  const int top = ReadInt32(FindMapValue(*rect_map, "top"));
  const int width = ReadInt32(FindMapValue(*rect_map, "width"));
  const int height = ReadInt32(FindMapValue(*rect_map, "height"));
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (left > std::numeric_limits<int>::max() - width ||
      top > std::numeric_limits<int>::max() - height) {
    return false;
  }

  rect->left = left;
  rect->top = top;
  rect->right = left + width;
  rect->bottom = top + height;
  return true;
}

double MonitorScaleFactor(HMONITOR monitor) {
  using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
  static HMODULE shcore = LoadLibraryW(L"Shcore.dll");
  static auto get_dpi_for_monitor =
      shcore == nullptr
          ? nullptr
          : reinterpret_cast<GetDpiForMonitorFn>(
                GetProcAddress(shcore, "GetDpiForMonitor"));

  if (get_dpi_for_monitor != nullptr) {
    UINT dpi_x = 96;
    UINT dpi_y = 96;
    if (SUCCEEDED(get_dpi_for_monitor(monitor, 0, &dpi_x, &dpi_y)) &&
        dpi_x > 0) {
      return static_cast<double>(dpi_x) / 96.0;
    }
  }

  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) {
    return 1.0;
  }
  const int dpi = GetDeviceCaps(screen_dc, LOGPIXELSX);
  ReleaseDC(nullptr, screen_dc);
  return dpi > 0 ? static_cast<double>(dpi) / 96.0 : 1.0;
}

std::string MonitorNameFromInfo(const MONITORINFOEXW& info, int index) {
  DISPLAY_DEVICEW device = {};
  device.cb = sizeof(device);
  if (EnumDisplayDevicesW(info.szDevice, 0, &device, 0) &&
      device.DeviceString[0] != L'\0') {
    return WidePtrToUtf8(device.DeviceString);
  }
  return "Display " + std::to_string(index);
}

BOOL CALLBACK MonitorEnumProc(HMONITOR monitor,
                              HDC,
                              LPRECT,
                              LPARAM user_data) {
  auto* monitors = reinterpret_cast<std::vector<MonitorDescriptor>*>(user_data);
  MONITORINFOEXW info = {};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    return TRUE;
  }

  const int index = static_cast<int>(monitors->size()) + 1;
  MonitorDescriptor descriptor;
  descriptor.id = index;
  descriptor.handle = monitor;
  descriptor.bounds = info.rcMonitor;
  descriptor.scale_factor = MonitorScaleFactor(monitor);
  descriptor.is_primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
  descriptor.name = MonitorNameFromInfo(info, index);
  monitors->push_back(descriptor);
  return TRUE;
}

std::vector<MonitorDescriptor> EnumerateMonitors() {
  std::vector<MonitorDescriptor> monitors;
  EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
                      reinterpret_cast<LPARAM>(&monitors));
  return monitors;
}

const MonitorDescriptor* FindMonitorById(
    const std::vector<MonitorDescriptor>& monitors,
    int64_t display_id) {
  if (monitors.empty()) {
    return nullptr;
  }
  if (display_id > 0) {
    for (const auto& monitor : monitors) {
      if (monitor.id == display_id) {
        return &monitor;
      }
    }
  }
  for (const auto& monitor : monitors) {
    if (monitor.is_primary) {
      return &monitor;
    }
  }
  return &monitors.front();
}

int DisplayIdForRect(const RECT& rect,
                     const std::vector<MonitorDescriptor>& monitors) {
  HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
  for (const auto& item : monitors) {
    if (item.handle == monitor) {
      return item.id;
    }
  }
  return 0;
}

RECT VirtualScreenBounds() {
  RECT rect = {};
  rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  return rect;
}

bool IntersectRects(const RECT& first, const RECT& second, RECT* output) {
  output->left = std::max(first.left, second.left);
  output->top = std::max(first.top, second.top);
  output->right = std::min(first.right, second.right);
  output->bottom = std::min(first.bottom, second.bottom);
  return output->right > output->left && output->bottom > output->top;
}

bool TryOffsetRect(RECT* rect, int dx, int dy) {
  if (rect == nullptr) {
    return false;
  }
  const auto can_add = [](int value, int delta) {
    if (delta > 0) {
      return value <= std::numeric_limits<int>::max() - delta;
    }
    if (delta < 0) {
      return value >= std::numeric_limits<int>::min() - delta;
    }
    return true;
  };
  if (!can_add(rect->left, dx) || !can_add(rect->right, dx) ||
      !can_add(rect->top, dy) || !can_add(rect->bottom, dy)) {
    return false;
  }
  rect->left += dx;
  rect->right += dx;
  rect->top += dy;
  rect->bottom += dy;
  return true;
}

RECT NormalizedRect(POINT first, POINT second) {
  RECT rect = {};
  rect.left = std::min(first.x, second.x);
  rect.top = std::min(first.y, second.y);
  rect.right = std::max(first.x, second.x);
  rect.bottom = std::max(first.y, second.y);
  return rect;
}

bool RectIsLargeEnough(const RECT& rect, int min_size = 5) {
  return rect.right - rect.left >= min_size && rect.bottom - rect.top >= min_size;
}

bool GetWindowFrameBounds(HWND window, RECT* rect) {
  if (window == nullptr || !IsWindow(window)) {
    return false;
  }
  HRESULT hr = DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, rect,
                                     sizeof(RECT));
  if (FAILED(hr) || rect->right <= rect->left || rect->bottom <= rect->top) {
    return GetWindowRect(window, rect) == TRUE;
  }
  return true;
}

bool IsWindowCloaked(HWND window) {
  BOOL cloaked = FALSE;
  DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
  return cloaked == TRUE;
}

bool WindowVisibleCaptureBounds(HWND window, RECT* bounds) {
  if (window == nullptr || !IsWindow(window) || !IsWindowVisible(window) ||
      IsIconic(window) || IsWindowCloaked(window)) {
    return false;
  }
  RECT frame = {};
  if (!GetWindowFrameBounds(window, &frame) || !RectIsLargeEnough(frame)) {
    return false;
  }
  RECT visible = {};
  if (!IntersectRects(frame, VirtualScreenBounds(), &visible) ||
      !RectIsLargeEnough(visible)) {
    return false;
  }
  if (bounds != nullptr) {
    *bounds = frame;
  }
  return true;
}

std::string WindowRetargetRecovery() {
  return "Refresh the window list, select the window again, and retry. "
         "Restore minimized windows before selecting them.";
}

std::string ProcessNameForWindow(HWND window) {
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id == 0) {
    return "";
  }

  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               process_id);
  if (process == nullptr) {
    return "";
  }

  wchar_t path[MAX_PATH] = {};
  DWORD size = MAX_PATH;
  std::string result;
  if (QueryFullProcessImageNameW(process, 0, path, &size)) {
    std::wstring full_path(path, size);
    const size_t slash = full_path.find_last_of(L"\\/");
    result = WideToUtf8(slash == std::wstring::npos
                            ? full_path
                            : full_path.substr(slash + 1));
  }
  CloseHandle(process);
  return result;
}

bool CaptureRectBgra(const RECT& rect, BitmapFrame* frame, std::string* error) {
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) {
    if (error != nullptr) {
      *error = "The requested capture rectangle is empty.";
    }
    return false;
  }
  size_t buffer_size = 0;
  if (!TryGetBgraBufferSize(width, height, &buffer_size)) {
    if (error != nullptr) {
      *error = "The requested capture rectangle is too large.";
    }
    return false;
  }

  HDC source_dc = GetDC(nullptr);
  if (source_dc == nullptr) {
    if (error != nullptr) {
      *error = "The screen device context could not be opened.";
    }
    return false;
  }

  HDC memory_dc = CreateCompatibleDC(source_dc);
  if (memory_dc == nullptr) {
    ReleaseDC(nullptr, source_dc);
    if (error != nullptr) {
      *error = "The capture device context could not be created.";
    }
    return false;
  }

  BITMAPINFO bitmap_info = {};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = width;
  bitmap_info.bmiHeader.biHeight = -height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP bitmap =
      CreateDIBSection(source_dc, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr,
                       0);
  if (bitmap == nullptr || bits == nullptr) {
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, source_dc);
    if (error != nullptr) {
      *error = "The capture bitmap could not be allocated.";
    }
    return false;
  }

  HGDIOBJ previous = SelectObject(memory_dc, bitmap);
  const BOOL copied =
      BitBlt(memory_dc, 0, 0, width, height, source_dc, rect.left, rect.top,
             SRCCOPY | CAPTUREBLT);
  GdiFlush();

  SelectObject(memory_dc, previous);
  if (copied) {
    const auto* data = static_cast<const uint8_t*>(bits);
    frame->width = width;
    frame->height = height;
    frame->bgra.assign(data, data + buffer_size);
  }

  DeleteObject(bitmap);
  DeleteDC(memory_dc);
  ReleaseDC(nullptr, source_dc);

  if (!copied && error != nullptr) {
    *error = "The screen pixels could not be copied.";
  }
  return copied == TRUE;
}

bool CaptureWindowBgra(HWND window, BitmapFrame* frame, std::string* error) {
  RECT rect = {};
  if (!GetWindowFrameBounds(window, &rect)) {
    if (error != nullptr) {
      *error = "The selected window bounds could not be read.";
    }
    return false;
  }

  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (width <= 0 || height <= 0) {
    if (error != nullptr) {
      *error = "The selected window is empty.";
    }
    return false;
  }
  size_t buffer_size = 0;
  if (!TryGetBgraBufferSize(width, height, &buffer_size)) {
    if (error != nullptr) {
      *error = "The selected window is too large to capture.";
    }
    return false;
  }

  HDC screen_dc = GetDC(nullptr);
  HDC memory_dc = screen_dc == nullptr ? nullptr : CreateCompatibleDC(screen_dc);
  if (screen_dc == nullptr || memory_dc == nullptr) {
    if (memory_dc != nullptr) {
      DeleteDC(memory_dc);
    }
    if (screen_dc != nullptr) {
      ReleaseDC(nullptr, screen_dc);
    }
    if (error != nullptr) {
      *error = "The window capture device context could not be created.";
    }
    return false;
  }

  BITMAPINFO bitmap_info = {};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = width;
  bitmap_info.bmiHeader.biHeight = -height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HBITMAP bitmap =
      CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr,
                       0);
  if (bitmap == nullptr || bits == nullptr) {
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    if (error != nullptr) {
      *error = "The window capture bitmap could not be allocated.";
    }
    return false;
  }

  HGDIOBJ previous = SelectObject(memory_dc, bitmap);
  BOOL copied = PrintWindow(window, memory_dc, PW_RENDERFULLCONTENT);
  if (!copied) {
    copied = BitBlt(memory_dc, 0, 0, width, height, screen_dc, rect.left,
                    rect.top, SRCCOPY | CAPTUREBLT);
  }
  GdiFlush();

  SelectObject(memory_dc, previous);
  if (copied) {
    const auto* data = static_cast<const uint8_t*>(bits);
    frame->width = width;
    frame->height = height;
    frame->bgra.assign(data, data + buffer_size);
  }

  DeleteObject(bitmap);
  DeleteDC(memory_dc);
  ReleaseDC(nullptr, screen_dc);

  if (!copied && error != nullptr) {
    *error = "The selected window pixels could not be copied.";
  }
  return copied == TRUE;
}

HRESULT EncodePng(const BitmapFrame& frame, std::vector<uint8_t>* bytes) {
  if (frame.width <= 0 || frame.height <= 0 || frame.bgra.empty()) {
    return E_INVALIDARG;
  }
  size_t safe_buffer_size = 0;
  if (!TryGetBgraBufferSize(frame.width, frame.height, &safe_buffer_size) ||
      frame.bgra.size() < safe_buffer_size) {
    return E_INVALIDARG;
  }

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CreateWicImagingFactory(&factory);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IStream> stream;
  hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) {
    return hr;
  }

  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICBitmapFrameEncode> frame_encoder;
  hr = encoder->CreateNewFrame(&frame_encoder, nullptr);
  if (FAILED(hr)) {
    return hr;
  }

  hr = frame_encoder->Initialize(nullptr);
  if (FAILED(hr)) {
    return hr;
  }

  hr = frame_encoder->SetSize(static_cast<UINT>(frame.width),
                              static_cast<UINT>(frame.height));
  if (FAILED(hr)) {
    return hr;
  }

  WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
  hr = frame_encoder->SetPixelFormat(&pixel_format);
  if (FAILED(hr)) {
    return hr;
  }

  size_t safe_stride = 0;
  if (!TryGetPackedImageBufferSize(static_cast<UINT>(frame.width),
                                   static_cast<UINT>(frame.height), 4,
                                   &safe_stride, &safe_buffer_size)) {
    return E_INVALIDARG;
  }
  const UINT stride = static_cast<UINT>(safe_stride);
  const UINT buffer_size = static_cast<UINT>(safe_buffer_size);
  hr = frame_encoder->WritePixels(
      static_cast<UINT>(frame.height), stride, buffer_size,
      const_cast<BYTE*>(reinterpret_cast<const BYTE*>(frame.bgra.data())));
  if (FAILED(hr)) {
    return hr;
  }

  hr = frame_encoder->Commit();
  if (FAILED(hr)) {
    return hr;
  }

  hr = encoder->Commit();
  if (FAILED(hr)) {
    return hr;
  }

  HGLOBAL memory = nullptr;
  hr = GetHGlobalFromStream(stream.Get(), &memory);
  if (FAILED(hr) || memory == nullptr) {
    return FAILED(hr) ? hr : E_FAIL;
  }

  const SIZE_T size = GlobalSize(memory);
  void* data = GlobalLock(memory);
  if (data == nullptr || size == 0) {
    if (data != nullptr) {
      GlobalUnlock(memory);
    }
    return E_FAIL;
  }
  const auto* start = static_cast<const uint8_t*>(data);
  bytes->assign(start, start + size);
  GlobalUnlock(memory);
  return S_OK;
}

HRESULT DecodeImageBgra(const std::vector<uint8_t>& image_bytes,
                        BitmapFrame* frame) {
  if (image_bytes.empty() || frame == nullptr) {
    return E_INVALIDARG;
  }

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CreateWicImagingFactory(&factory);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICStream> stream;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    return hr;
  }
  if (image_bytes.size() > std::numeric_limits<DWORD>::max()) {
    return E_INVALIDARG;
  }
  hr = stream->InitializeFromMemory(
      const_cast<BYTE*>(reinterpret_cast<const BYTE*>(image_bytes.data())),
      static_cast<DWORD>(image_bytes.size()));
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                        WICDecodeMetadataCacheOnLoad,
                                        &decoder);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICBitmapFrameDecode> decoded_frame;
  hr = decoder->GetFrame(0, &decoded_frame);
  if (FAILED(hr)) {
    return hr;
  }

  UINT width = 0;
  UINT height = 0;
  hr = decoded_frame->GetSize(&width, &height);
  if (FAILED(hr) || width == 0 || height == 0) {
    return FAILED(hr) ? hr : E_FAIL;
  }

  ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) {
    return hr;
  }
  hr = converter->Initialize(decoded_frame.Get(), GUID_WICPixelFormat32bppBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    return hr;
  }

  size_t safe_stride = 0;
  size_t safe_buffer_size = 0;
  if (!TryGetPackedImageBufferSize(width, height, 4, &safe_stride,
                                   &safe_buffer_size)) {
    return E_INVALIDARG;
  }
  const UINT stride = static_cast<UINT>(safe_stride);
  const UINT buffer_size = static_cast<UINT>(safe_buffer_size);
  std::vector<uint8_t> pixels(safe_buffer_size);
  hr = converter->CopyPixels(nullptr, stride, buffer_size, pixels.data());
  if (FAILED(hr)) {
    return hr;
  }

  frame->width = static_cast<int>(width);
  frame->height = static_cast<int>(height);
  frame->bgra = std::move(pixels);
  return S_OK;
}

void FlattenFrameToMatte(BitmapFrame* frame, uint32_t argb) {
  if (frame == nullptr || frame->bgra.empty()) {
    return;
  }
  const uint8_t matte_b = static_cast<uint8_t>(argb & 0xFF);
  const uint8_t matte_g = static_cast<uint8_t>((argb >> 8) & 0xFF);
  const uint8_t matte_r = static_cast<uint8_t>((argb >> 16) & 0xFF);
  for (size_t index = 0; index + 3 < frame->bgra.size(); index += 4) {
    const uint8_t alpha = frame->bgra[index + 3];
    const uint16_t inverse = static_cast<uint16_t>(255 - alpha);
    frame->bgra[index] = static_cast<uint8_t>(
        (frame->bgra[index] * alpha + matte_b * inverse) / 255);
    frame->bgra[index + 1] = static_cast<uint8_t>(
        (frame->bgra[index + 1] * alpha + matte_g * inverse) / 255);
    frame->bgra[index + 2] = static_cast<uint8_t>(
        (frame->bgra[index + 2] * alpha + matte_r * inverse) / 255);
    frame->bgra[index + 3] = 255;
  }
}

HRESULT EncodeWicContainer(const BitmapFrame& frame,
                           const GUID& container_format,
                           double quality,
                           std::vector<uint8_t>* bytes) {
  if (frame.width <= 0 || frame.height <= 0 || frame.bgra.empty()) {
    return E_INVALIDARG;
  }
  size_t safe_bgra_size = 0;
  if (!TryGetBgraBufferSize(frame.width, frame.height, &safe_bgra_size) ||
      frame.bgra.size() < safe_bgra_size) {
    return E_INVALIDARG;
  }

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CreateWicImagingFactory(&factory);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IStream> stream;
  hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(container_format, nullptr, &encoder);
  if (FAILED(hr)) {
    return hr;
  }
  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICBitmapFrameEncode> frame_encoder;
  ComPtr<IPropertyBag2> property_bag;
  hr = encoder->CreateNewFrame(&frame_encoder, &property_bag);
  if (FAILED(hr)) {
    return hr;
  }
  if (property_bag != nullptr) {
    PROPBAG2 option = {};
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value = {};
    VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = static_cast<FLOAT>(quality);
    property_bag->Write(1, &option, &value);
    VariantClear(&value);
  }
  hr = frame_encoder->Initialize(property_bag.Get());
  if (FAILED(hr)) {
    return hr;
  }
  hr = frame_encoder->SetSize(static_cast<UINT>(frame.width),
                              static_cast<UINT>(frame.height));
  if (FAILED(hr)) {
    return hr;
  }

  WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
  hr = frame_encoder->SetPixelFormat(&pixel_format);
  if (FAILED(hr)) {
    return hr;
  }

  std::vector<uint8_t> bgr;
  const BYTE* source = reinterpret_cast<const BYTE*>(frame.bgra.data());
  size_t safe_stride = 0;
  size_t safe_buffer_size = 0;
  if (!TryGetPackedImageBufferSize(static_cast<UINT>(frame.width),
                                   static_cast<UINT>(frame.height), 4,
                                   &safe_stride, &safe_buffer_size)) {
    return E_INVALIDARG;
  }
  UINT stride = static_cast<UINT>(safe_stride);
  UINT buffer_size = static_cast<UINT>(safe_buffer_size);
  if (IsEqualGUID(pixel_format, GUID_WICPixelFormat24bppBGR)) {
    if (!TryGetPackedImageBufferSize(static_cast<UINT>(frame.width),
                                     static_cast<UINT>(frame.height), 3,
                                     &safe_stride, &safe_buffer_size)) {
      return E_INVALIDARG;
    }
    bgr.resize(safe_buffer_size);
    for (int y = 0; y < frame.height; ++y) {
      for (int x = 0; x < frame.width; ++x) {
        const size_t source_index = (static_cast<size_t>(y) * frame.width + x) * 4;
        const size_t target_index = (static_cast<size_t>(y) * frame.width + x) * 3;
        bgr[target_index] = frame.bgra[source_index];
        bgr[target_index + 1] = frame.bgra[source_index + 1];
        bgr[target_index + 2] = frame.bgra[source_index + 2];
      }
    }
    source = bgr.data();
    stride = static_cast<UINT>(safe_stride);
    buffer_size = static_cast<UINT>(safe_buffer_size);
  }

  hr = frame_encoder->WritePixels(static_cast<UINT>(frame.height), stride,
                                  buffer_size, const_cast<BYTE*>(source));
  if (FAILED(hr)) {
    return hr;
  }
  hr = frame_encoder->Commit();
  if (FAILED(hr)) {
    return hr;
  }
  hr = encoder->Commit();
  if (FAILED(hr)) {
    return hr;
  }

  HGLOBAL memory = nullptr;
  hr = GetHGlobalFromStream(stream.Get(), &memory);
  if (FAILED(hr) || memory == nullptr) {
    return FAILED(hr) ? hr : E_FAIL;
  }

  const SIZE_T size = GlobalSize(memory);
  void* data = GlobalLock(memory);
  if (data == nullptr || size == 0) {
    if (data != nullptr) {
      GlobalUnlock(memory);
    }
    return E_FAIL;
  }
  const auto* start = static_cast<const uint8_t*>(data);
  bytes->assign(start, start + size);
  GlobalUnlock(memory);
  return S_OK;
}

EncodableValue MakeEncodedImagePayload(const BitmapFrame& frame,
                                       const std::string& format,
                                       const std::string& extension,
                                       const std::string& mime_type,
                                       std::vector<uint8_t> bytes) {
  EncodableMap payload;
  payload[EncodableValue("imageBytes")] = EncodableValue(std::move(bytes));
  payload[EncodableValue("width")] = EncodableValue(frame.width);
  payload[EncodableValue("height")] = EncodableValue(frame.height);
  payload[EncodableValue("format")] = EncodableValue(format);
  payload[EncodableValue("extension")] = EncodableValue(extension);
  payload[EncodableValue("mimeType")] = EncodableValue(mime_type);
  return MakeSuccess(EncodableValue(payload));
}

EncodableValue EncodeImagePayload(const EncodableMap* arguments) {
  if (arguments == nullptr) {
    return MakeFailure("invalidInput", "Image bytes are required for image encoding.",
                       "Capture an image and try again.");
  }
  const auto* bytes_value = FindMapValue(*arguments, "imageBytes");
  const auto* image_bytes = bytes_value == nullptr
                                ? nullptr
                                : std::get_if<std::vector<uint8_t>>(bytes_value);
  if (image_bytes == nullptr || image_bytes->empty()) {
    return MakeFailure("invalidInput", "Image bytes are required for image encoding.",
                       "Capture an image and try again.");
  }

  BitmapFrame frame;
  HRESULT hr = DecodeImageBgra(*image_bytes, &frame);
  if (FAILED(hr)) {
    return MakeFailure("ioFailure",
                       "The capture image could not be decoded for encoding.",
                       "Try the capture again.", HResultToString(hr));
  }

  const std::string format = ReadString(FindMapValue(*arguments, "format"), "png");
  const int quality_percent =
      std::clamp(ReadInt32(FindMapValue(*arguments, "quality"), 90), 1, 100);
  const uint32_t matte_color = static_cast<uint32_t>(
      ReadInt(FindMapValue(*arguments, "matteColor"), 0xFFFFFFFF));
  const double quality = static_cast<double>(quality_percent) / 100.0;
  std::vector<uint8_t> encoded;
  if (format == "png") {
    hr = EncodePng(frame, &encoded);
    if (FAILED(hr)) {
      return MakeFailure("ioFailure", "The capture image could not be encoded as PNG.",
                         "Try the capture again.", HResultToString(hr));
    }
    return MakeEncodedImagePayload(frame, "png", "png", "image/png",
                                   std::move(encoded));
  }
  if (format == "jpeg") {
    BitmapFrame flattened = frame;
    FlattenFrameToMatte(&flattened, matte_color);
    hr = EncodeWicContainer(flattened, GUID_ContainerFormatJpeg, quality, &encoded);
    if (FAILED(hr)) {
      return MakeFailure("ioFailure", "The capture image could not be encoded as JPEG.",
                         "Try the capture again.", HResultToString(hr));
    }
    return MakeEncodedImagePayload(flattened, "jpeg", "jpeg", "image/jpeg",
                                   std::move(encoded));
  }
  if (format == "webp") {
    hr = EncodeWicContainer(frame, kWicContainerFormatWebp, quality, &encoded);
    if (FAILED(hr)) {
      return MakeFailure("unsupported",
                         "WebP encoding is not available on this Windows build.",
                         "Use PNG or JPEG output.", HResultToString(hr));
    }
    return MakeEncodedImagePayload(frame, "webp", "webp", "image/webp",
                                   std::move(encoded));
  }

  return MakeFailure("invalidInput", "The requested image format is not supported.",
                     "Choose PNG, JPEG, or WebP.");
}

EncodableValue MakeImagePayload(const BitmapFrame& frame,
                                int64_t display_id,
                                int64_t window_id,
                                double scale_factor) {
  std::vector<uint8_t> png_bytes;
  HRESULT hr = EncodePng(frame, &png_bytes);
  if (FAILED(hr)) {
    return MakeFailure("ioFailure", "The captured image could not be encoded.",
                       "Try the capture again.", HResultToString(hr));
  }

  EncodableMap payload;
  payload[EncodableValue("imageBytes")] = EncodableValue(std::move(png_bytes));
  payload[EncodableValue("width")] = EncodableValue(frame.width);
  payload[EncodableValue("height")] = EncodableValue(frame.height);
  if (display_id > 0) {
    payload[EncodableValue("displayId")] =
        EncodableValue(static_cast<int64_t>(display_id));
  }
  if (window_id > 0) {
    payload[EncodableValue("windowId")] =
        EncodableValue(static_cast<int64_t>(window_id));
  }
  payload[EncodableValue("scaleFactor")] = EncodableValue(scale_factor);
  payload[EncodableValue("isTemporary")] = EncodableValue(false);
  return MakeSuccess(EncodableValue(payload));
}

HRESULT DecodePngToClipboardDib(const std::vector<uint8_t>& png_bytes,
                                HGLOBAL* output) {
  *output = nullptr;
  if (png_bytes.empty()) {
    return E_INVALIDARG;
  }

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CreateWicImagingFactory(&factory);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICStream> stream;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    return hr;
  }

  if (png_bytes.size() > std::numeric_limits<DWORD>::max()) {
    return E_INVALIDARG;
  }
  hr = stream->InitializeFromMemory(
      const_cast<BYTE*>(reinterpret_cast<const BYTE*>(png_bytes.data())),
      static_cast<DWORD>(png_bytes.size()));
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromStream(stream.Get(), nullptr,
                                        WICDecodeMetadataCacheOnLoad,
                                        &decoder);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr)) {
    return hr;
  }

  UINT width = 0;
  UINT height = 0;
  hr = frame->GetSize(&width, &height);
  if (FAILED(hr) || width == 0 || height == 0) {
    return FAILED(hr) ? hr : E_FAIL;
  }

  ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr)) {
    return hr;
  }

  hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    return hr;
  }

  size_t safe_stride = 0;
  size_t safe_image_size = 0;
  if (!TryGetPackedImageBufferSize(width, height, 4, &safe_stride,
                                   &safe_image_size)) {
    return E_INVALIDARG;
  }
  const UINT stride = static_cast<UINT>(safe_stride);
  const UINT image_size = static_cast<UINT>(safe_image_size);
  std::vector<uint8_t> top_down(safe_image_size);
  hr = converter->CopyPixels(nullptr, stride, image_size, top_down.data());
  if (FAILED(hr)) {
    return hr;
  }

  const SIZE_T header_size = sizeof(BITMAPINFOHEADER);
  if (safe_image_size > std::numeric_limits<SIZE_T>::max() - header_size) {
    return E_OUTOFMEMORY;
  }
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, header_size + safe_image_size);
  if (memory == nullptr) {
    return E_OUTOFMEMORY;
  }

  auto* data = static_cast<uint8_t*>(GlobalLock(memory));
  if (data == nullptr) {
    GlobalFree(memory);
    return E_FAIL;
  }

  auto* header = reinterpret_cast<BITMAPINFOHEADER*>(data);
  std::memset(header, 0, sizeof(BITMAPINFOHEADER));
  header->biSize = sizeof(BITMAPINFOHEADER);
  header->biWidth = static_cast<LONG>(width);
  header->biHeight = static_cast<LONG>(height);
  header->biPlanes = 1;
  header->biBitCount = 32;
  header->biCompression = BI_RGB;
  header->biSizeImage = image_size;

  uint8_t* pixels = data + header_size;
  for (UINT y = 0; y < height; ++y) {
    const uint8_t* source_row = top_down.data() + static_cast<size_t>(y) * stride;
    uint8_t* destination_row =
        pixels + static_cast<size_t>(height - y - 1) * stride;
    std::memcpy(destination_row, source_row, stride);
  }

  GlobalUnlock(memory);
  *output = memory;
  return S_OK;
}

EncodableValue CopyPngToClipboard(const EncodableMap& arguments) {
  const auto* bytes_value = FindMapValue(arguments, "pngBytes");
  const auto* bytes = bytes_value == nullptr
                          ? nullptr
                          : std::get_if<std::vector<uint8_t>>(bytes_value);
  if (bytes == nullptr || bytes->empty()) {
    return MakeFailure("nativeFailure",
                       "The image bytes could not be decoded for clipboard copy.",
                       "Capture the image again and retry the copy.");
  }

  HGLOBAL dib = nullptr;
  HRESULT hr = DecodePngToClipboardDib(*bytes, &dib);
  if (FAILED(hr) || dib == nullptr) {
    return MakeFailure("nativeFailure",
                       "The image could not be prepared for the Windows clipboard.",
                       "Save the capture manually and try again.",
                       HResultToString(hr));
  }

  if (!OpenClipboard(nullptr)) {
    GlobalFree(dib);
    return MakeFailure("nativeFailure",
                       "The Windows clipboard could not be opened.",
                       "Close other clipboard tools and try again.");
  }

  EmptyClipboard();
  if (SetClipboardData(CF_DIB, dib) == nullptr) {
    CloseClipboard();
    GlobalFree(dib);
    return MakeFailure("nativeFailure",
                       "The image could not be written to the Windows clipboard.",
                       "Save the capture manually and try again.");
  }

  CloseClipboard();
  return MakeSuccess();
}

EncodableValue ListDisplaysPayload() {
  EncodableList displays;
  for (const auto& monitor : EnumerateMonitors()) {
    EncodableMap payload;
    payload[EncodableValue("id")] = EncodableValue(monitor.id);
    payload[EncodableValue("name")] = EncodableValue(monitor.name);
    payload[EncodableValue("bounds")] = MakeBoundsPayload(monitor.bounds);
    payload[EncodableValue("scaleFactor")] =
        EncodableValue(monitor.scale_factor);
    payload[EncodableValue("isPrimary")] = EncodableValue(monitor.is_primary);
    payload[EncodableValue("isInternal")] = EncodableValue(false);
    displays.emplace_back(EncodableValue(payload));
  }
  return MakeSuccess(EncodableValue(displays));
}

struct WindowEnumerationContext {
  std::vector<MonitorDescriptor> monitors;
  EncodableList windows;
};

BOOL CALLBACK WindowEnumProc(HWND window, LPARAM user_data) {
  if (!IsWindowVisible(window) || window == GetShellWindow()) {
    return TRUE;
  }

  BOOL cloaked = FALSE;
  DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
  if (cloaked) {
    return TRUE;
  }

  RECT rect = {};
  if (!GetWindowFrameBounds(window, &rect)) {
    return TRUE;
  }
  if (rect.right - rect.left < 20 || rect.bottom - rect.top < 20) {
    return TRUE;
  }

  const int title_length = GetWindowTextLengthW(window);
  std::wstring title;
  if (title_length > 0) {
    title.resize(static_cast<size_t>(title_length) + 1);
    const int copied =
        GetWindowTextW(window, title.data(), title_length + 1);
    title.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
  }

  auto* context = reinterpret_cast<WindowEnumerationContext*>(user_data);
  const std::string app_name = ProcessNameForWindow(window);
  std::string resolved_title = WideToUtf8(title);
  if (resolved_title.empty()) {
    resolved_title = app_name;
  }
  if (resolved_title.empty()) {
    return TRUE;
  }

  EncodableMap payload;
  payload[EncodableValue("id")] =
      EncodableValue(static_cast<int64_t>(reinterpret_cast<intptr_t>(window)));
  payload[EncodableValue("title")] = EncodableValue(resolved_title);
  payload[EncodableValue("appName")] = EncodableValue(app_name);
  payload[EncodableValue("bounds")] = MakeBoundsPayload(rect);
  payload[EncodableValue("displayId")] =
      EncodableValue(DisplayIdForRect(rect, context->monitors));
  payload[EncodableValue("isVisible")] = EncodableValue(true);
  payload[EncodableValue("isOnScreen")] = EncodableValue(true);
  context->windows.emplace_back(EncodableValue(payload));
  return TRUE;
}

EncodableValue ListWindowsPayload() {
  WindowEnumerationContext context;
  context.monitors = EnumerateMonitors();
  EnumWindows(WindowEnumProc, reinterpret_cast<LPARAM>(&context));
  return MakeSuccess(EncodableValue(context.windows));
}

struct RecordableWindowCounter {
  int count = 0;
};

BOOL CALLBACK CountRecordableWindowProc(HWND window, LPARAM user_data) {
  if (!IsWindowVisible(window) || window == GetShellWindow()) {
    return TRUE;
  }

  BOOL cloaked = FALSE;
  DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
  if (cloaked) {
    return TRUE;
  }

  RECT rect = {};
  if (!GetWindowFrameBounds(window, &rect)) {
    return TRUE;
  }
  if (rect.right - rect.left < 20 || rect.bottom - rect.top < 20) {
    return TRUE;
  }

  const int title_length = GetWindowTextLengthW(window);
  const std::string app_name = ProcessNameForWindow(window);
  if (title_length <= 0 && app_name.empty()) {
    return TRUE;
  }

  auto* counter = reinterpret_cast<RecordableWindowCounter*>(user_data);
  ++counter->count;
  return counter->count > 0 ? FALSE : TRUE;
}

bool HasRecordableWindow() {
  RecordableWindowCounter counter;
  EnumWindows(CountRecordableWindowProc, reinterpret_cast<LPARAM>(&counter));
  return counter.count > 0;
}

struct RegionSelectionContext {
  RECT virtual_bounds = {};
  std::vector<MonitorDescriptor> monitors;
  POINT start = {};
  POINT current = {};
  bool dragging = false;
  bool done = false;
  bool cancelled = false;
};

LRESULT CALLBACK RegionSelectionWindowProc(HWND window,
                                           UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam) {
  auto* context =
      reinterpret_cast<RegionSelectionContext*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    return TRUE;
  }
  if (context == nullptr) {
    return DefWindowProcW(window, message, wparam, lparam);
  }

  switch (message) {
    case WM_SETCURSOR:
      SetCursor(LoadCursorW(nullptr, IDC_CROSS));
      return TRUE;
    case WM_KEYDOWN:
      if (wparam == VK_ESCAPE) {
        context->cancelled = true;
        context->done = true;
        DestroyWindow(window);
        return 0;
      }
      if (wparam == VK_RETURN || wparam == VK_SPACE) {
        RECT rect = NormalizedRect(context->start, context->current);
        if (RectIsLargeEnough(rect)) {
          context->done = true;
        } else {
          context->cancelled = true;
          context->done = true;
        }
        DestroyWindow(window);
        return 0;
      }
      break;
    case WM_LBUTTONDOWN:
      context->dragging = true;
      context->start = {
          GET_X_LPARAM(lparam) + context->virtual_bounds.left,
          GET_Y_LPARAM(lparam) + context->virtual_bounds.top,
      };
      context->current = context->start;
      SetCapture(window);
      InvalidateRect(window, nullptr, TRUE);
      return 0;
    case WM_MOUSEMOVE:
      if (context->dragging) {
        context->current = {
            GET_X_LPARAM(lparam) + context->virtual_bounds.left,
            GET_Y_LPARAM(lparam) + context->virtual_bounds.top,
        };
        InvalidateRect(window, nullptr, TRUE);
      }
      return 0;
    case WM_LBUTTONUP: {
      if (context->dragging) {
        context->dragging = false;
        context->current = {
            GET_X_LPARAM(lparam) + context->virtual_bounds.left,
            GET_Y_LPARAM(lparam) + context->virtual_bounds.top,
        };
        ReleaseCapture();
        RECT rect = NormalizedRect(context->start, context->current);
        context->cancelled = !RectIsLargeEnough(rect);
        context->done = true;
        DestroyWindow(window);
      }
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC dc = BeginPaint(window, &paint);
      RECT client = {};
      GetClientRect(window, &client);
      HBRUSH overlay_brush = CreateSolidBrush(RGB(0, 0, 0));
      FillRect(dc, &client, overlay_brush);
      DeleteObject(overlay_brush);

      RECT selected = NormalizedRect(context->start, context->current);
      TryOffsetRect(&selected, -context->virtual_bounds.left,
                    -context->virtual_bounds.top);
      if (RectIsLargeEnough(selected, 1)) {
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(34, 197, 94));
        HGDIOBJ previous_pen = SelectObject(dc, pen);
        HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, selected.left, selected.top, selected.right, selected.bottom);
        SelectObject(dc, previous_brush);
        SelectObject(dc, previous_pen);
        DeleteObject(pen);
      }
      EndPaint(window, &paint);
      return 0;
    }
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

bool EnsureRegionSelectionWindowClass(HINSTANCE instance) {
  constexpr wchar_t kClassName[] = L"GoodCleanShotRegionSelectionWindow";
  WNDCLASSW existing = {};
  if (GetClassInfoW(instance, kClassName, &existing)) {
    return true;
  }

  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = RegionSelectionWindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kClassName;
  window_class.hCursor = LoadCursorW(nullptr, IDC_CROSS);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  return RegisterClassW(&window_class) != 0;
}

EncodableValue RegionSelectionPayload(const RegionSelectionResult& result) {
  RECT local = result.rect;
  if (!TryOffsetRect(&local, -result.monitor.bounds.left,
                     -result.monitor.bounds.top)) {
    return MakeFailure("invalidInput",
                       "The selected region could not be normalized.",
                       "Select an area inside a display and try again.");
  }

  EncodableMap rect;
  rect[EncodableValue("left")] = EncodableValue(local.left);
  rect[EncodableValue("top")] = EncodableValue(local.top);
  rect[EncodableValue("width")] = EncodableValue(local.right - local.left);
  rect[EncodableValue("height")] = EncodableValue(local.bottom - local.top);

  EncodableMap source_size;
  source_size[EncodableValue("width")] =
      EncodableValue(result.monitor.bounds.right - result.monitor.bounds.left);
  source_size[EncodableValue("height")] =
      EncodableValue(result.monitor.bounds.bottom - result.monitor.bounds.top);

  EncodableMap payload;
  payload[EncodableValue("rect")] = EncodableValue(rect);
  payload[EncodableValue("sourceSize")] = EncodableValue(source_size);
  payload[EncodableValue("displayId")] =
      EncodableValue(static_cast<int64_t>(result.monitor.id));
  payload[EncodableValue("displayBoundsLogical")] =
      MakeBoundsPayload(result.monitor.bounds);
  payload[EncodableValue("virtualScreenBoundsLogical")] =
      MakeBoundsPayload(result.virtual_bounds);
  payload[EncodableValue("scaleFactor")] =
      EncodableValue(result.monitor.scale_factor);
  payload[EncodableValue("coordinateSpace")] =
      EncodableValue("displayPhysical");
  return MakeSuccess(EncodableValue(payload));
}

EncodableValue SelectRegionPayload() {
  RegionSelectionContext context;
  context.virtual_bounds = VirtualScreenBounds();
  context.monitors = EnumerateMonitors();
  if (context.monitors.empty()) {
    return MakeFailure("nativeFailure", "No display is available for region selection.",
                       "Connect a display and try again.");
  }

  HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!EnsureRegionSelectionWindowClass(instance)) {
    return MakeFailure("nativeFailure", "The region selection window could not be registered.",
                       "Restart the app and try again.");
  }

  constexpr wchar_t kClassName[] = L"GoodCleanShotRegionSelectionWindow";
  HWND window = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
      kClassName,
      L"GoodCleanShot Region Selection",
      WS_POPUP,
      context.virtual_bounds.left,
      context.virtual_bounds.top,
      context.virtual_bounds.right - context.virtual_bounds.left,
      context.virtual_bounds.bottom - context.virtual_bounds.top,
      nullptr,
      nullptr,
      instance,
      &context);
  if (window == nullptr) {
    return MakeFailure("nativeFailure", "The region selection window could not be opened.",
                       "Try the capture again.");
  }

  SetLayeredWindowAttributes(window, 0, 96, LWA_ALPHA);
  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);
  SetForegroundWindow(window);
  SetFocus(window);

  MSG message = {};
  while (!context.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  if (!context.done || context.cancelled) {
    return MakeFailure("cancelled", "Region selection was cancelled.",
                       "Start a new capture when ready.");
  }

  RECT selected = NormalizedRect(context.start, context.current);
  const int display_id = DisplayIdForRect(selected, context.monitors);
  const MonitorDescriptor* monitor = FindMonitorById(context.monitors, display_id);
  if (monitor == nullptr) {
    return MakeFailure("invalidInput", "The selected display could not be resolved.",
                       "Select an area inside a display and try again.");
  }

  RECT clamped = {};
  if (!IntersectRects(selected, monitor->bounds, &clamped) ||
      !RectIsLargeEnough(clamped)) {
    return MakeFailure("invalidInput", "The selected region crosses display boundaries.",
                       "Select an area inside a single display.");
  }

  RegionSelectionResult result;
  result.completed = true;
  result.rect = clamped;
  result.monitor = *monitor;
  result.virtual_bounds = context.virtual_bounds;
  return RegionSelectionPayload(result);
}

EncodableValue CaptureDisplayPayload(const EncodableMap* arguments) {
  const int64_t display_id =
      arguments == nullptr ? 0 : ReadInt(FindMapValue(*arguments, "displayId"));
  const auto monitors = EnumerateMonitors();
  const MonitorDescriptor* monitor = FindMonitorById(monitors, display_id);
  if (monitor == nullptr) {
    return MakeFailure("nativeFailure", "No display is available to capture.",
                       "Connect a display and try again.");
  }

  BitmapFrame frame;
  std::string error;
  if (!CaptureRectBgra(monitor->bounds, &frame, &error)) {
    return MakeFailure("nativeFailure", error,
                       "Check the selected display and try again.");
  }

  return MakeImagePayload(frame, monitor->id, 0, monitor->scale_factor);
}

EncodableValue CaptureRegionPayload(const EncodableMap* arguments) {
  if (arguments == nullptr) {
    return MakeFailure("invalidInput",
                       "A physical pixel region is required for region capture.",
                       "Select a region and retry the capture.");
  }

  RECT region = {};
  if (!ReadPhysicalRect(*arguments, "region", &region)) {
    return MakeFailure("invalidInput",
                       "A physical pixel region is required for region capture.",
                       "Select a region and retry the capture.");
  }

  const int64_t display_id = ReadInt(FindMapValue(*arguments, "displayId"));
  double scale_factor = 1.0;
  if (display_id > 0) {
    const auto monitors = EnumerateMonitors();
    const MonitorDescriptor* monitor = FindMonitorById(monitors, display_id);
    if (monitor == nullptr) {
      return MakeFailure("nativeFailure", "The target display was not found.",
                         "Refresh the display list and try again.");
    }
    if (!TryOffsetRect(&region, monitor->bounds.left, monitor->bounds.top)) {
      return MakeFailure("invalidInput",
                         "The selected region is outside the target display.",
                         "Select an area inside the display and try again.");
    }
    RECT clamped = {};
    if (!IntersectRects(region, monitor->bounds, &clamped)) {
      return MakeFailure("invalidInput",
                         "The selected region is outside the target display.",
                         "Select an area inside the display and try again.");
    }
    region = clamped;
    scale_factor = monitor->scale_factor;
  } else {
    RECT clamped = {};
    if (!IntersectRects(region, VirtualScreenBounds(), &clamped)) {
      return MakeFailure("invalidInput",
                         "The selected region is outside the desktop.",
                         "Select a visible area and try again.");
    }
    region = clamped;
  }

  BitmapFrame frame;
  std::string error;
  if (!CaptureRectBgra(region, &frame, &error)) {
    return MakeFailure("nativeFailure", error,
                       "Check the selected region and try again.");
  }
  return MakeImagePayload(frame, display_id, 0, scale_factor);
}

EncodableValue CaptureWindowPayload(const EncodableMap* arguments) {
  const int64_t window_id =
      arguments == nullptr ? 0 : ReadInt(FindMapValue(*arguments, "windowId"));
  HWND window =
      reinterpret_cast<HWND>(static_cast<intptr_t>(window_id));
  if (window == nullptr || !IsWindow(window)) {
    return MakeFailure("nativeFailure", "The selected window was not found.",
                       WindowRetargetRecovery());
  }
  if (IsIconic(window)) {
    return MakeFailure("nativeFailure", "The selected window is minimized.",
                       WindowRetargetRecovery());
  }
  if (!IsWindowVisible(window) || IsWindowCloaked(window)) {
    return MakeFailure("nativeFailure",
                       "The selected window is not currently capturable.",
                       WindowRetargetRecovery());
  }

  RECT window_bounds = {};
  if (!WindowVisibleCaptureBounds(window, &window_bounds)) {
    return MakeFailure("nativeFailure",
                       "The selected window is outside the visible desktop.",
                       WindowRetargetRecovery());
  }

  BitmapFrame frame;
  std::string error;
  if (!CaptureWindowBgra(window, &frame, &error)) {
    return MakeFailure("nativeFailure", error, WindowRetargetRecovery());
  }

  return MakeImagePayload(frame, 0, window_id, 1.0);
}

struct ScrollDriverRequest {
  int step_index = 0;
  double delta_y = 720.0;
  int settle_delay_ms = 450;
  std::string strategy = "wheel";
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;
  double source_width = 0.0;
  double source_height = 0.0;
  int64_t display_id = 0;
  int64_t window_id = 0;
};

double ClampDouble(double value, double minimum, double maximum) {
  return std::min(maximum, std::max(minimum, value));
}

int ClampInt(int value, int minimum, int maximum) {
  return std::min(maximum, std::max(minimum, value));
}

bool ReadScrollRequest(const EncodableMap* arguments,
                       ScrollDriverRequest* request) {
  if (arguments == nullptr || request == nullptr) {
    return false;
  }
  const auto* target_value = FindMapValue(*arguments, "target");
  const auto* target = target_value == nullptr
                           ? nullptr
                           : std::get_if<EncodableMap>(target_value);
  if (target == nullptr) {
    return false;
  }

  request->step_index = ReadInt32(FindMapValue(*arguments, "stepIndex"));
  request->delta_y = ReadDouble(FindMapValue(*arguments, "deltaY"), 720.0);
  request->settle_delay_ms =
      std::max(0, ReadInt32(FindMapValue(*arguments, "settleDelayMs"), 450));
  request->strategy = ReadString(FindMapValue(*arguments, "strategy"), "wheel");
  request->x = ReadDouble(FindMapValue(*target, "x"));
  request->y = ReadDouble(FindMapValue(*target, "y"));
  request->width = ReadDouble(FindMapValue(*target, "width"));
  request->height = ReadDouble(FindMapValue(*target, "height"));
  request->source_width = ReadDouble(FindMapValue(*target, "sourceWidth"));
  request->source_height = ReadDouble(FindMapValue(*target, "sourceHeight"));
  request->display_id = ReadInt(FindMapValue(*target, "displayId"));
  request->window_id = ReadInt(FindMapValue(*target, "windowId"));
  return request->width >= 1.0 && request->height >= 1.0;
}

const MonitorDescriptor* SelectMonitorForScrollRequest(
    const ScrollDriverRequest& request,
    const std::vector<MonitorDescriptor>& monitors) {
  if (monitors.empty()) {
    return nullptr;
  }
  if (request.display_id > 0) {
    if (const MonitorDescriptor* monitor =
            FindMonitorById(monitors, request.display_id)) {
      return monitor;
    }
  }

  if (request.source_width > 0 && request.source_height > 0) {
    for (const auto& monitor : monitors) {
      const double physical_width =
          static_cast<double>(monitor.bounds.right - monitor.bounds.left);
      const double physical_height =
          static_cast<double>(monitor.bounds.bottom - monitor.bounds.top);
      const double logical_width = physical_width / monitor.scale_factor;
      const double logical_height = physical_height / monitor.scale_factor;
      if ((std::abs(logical_width - request.source_width) <= 1.0 &&
           std::abs(logical_height - request.source_height) <= 1.0) ||
          (std::abs(physical_width - request.source_width) <= 1.0 &&
           std::abs(physical_height - request.source_height) <= 1.0)) {
        return &monitor;
      }
    }
  }

  for (const auto& monitor : monitors) {
    if (monitor.is_primary) {
      return &monitor;
    }
  }
  return &monitors.front();
}

POINT ScrollTargetPoint(const ScrollDriverRequest& request,
                        EncodableMap* diagnostics) {
  const auto monitors = EnumerateMonitors();
  const MonitorDescriptor* monitor =
      SelectMonitorForScrollRequest(request, monitors);

  double scale_x = 1.0;
  double scale_y = 1.0;
  double origin_x = 0.0;
  double origin_y = 0.0;
  if (monitor != nullptr) {
    origin_x = static_cast<double>(monitor->bounds.left);
    origin_y = static_cast<double>(monitor->bounds.top);
    const double monitor_width =
        static_cast<double>(monitor->bounds.right - monitor->bounds.left);
    const double monitor_height =
        static_cast<double>(monitor->bounds.bottom - monitor->bounds.top);
    if (request.source_width > 0 && request.source_height > 0) {
      scale_x = monitor_width / request.source_width;
      scale_y = monitor_height / request.source_height;
    } else {
      scale_x = monitor->scale_factor;
      scale_y = monitor->scale_factor;
    }
    if (diagnostics != nullptr) {
      (*diagnostics)[EncodableValue("displayId")] =
          EncodableValue(monitor->id);
      (*diagnostics)[EncodableValue("displayScaleX")] =
          EncodableValue(scale_x);
      (*diagnostics)[EncodableValue("displayScaleY")] =
          EncodableValue(scale_y);
    }
  }

  POINT point = {};
  point.x = static_cast<LONG>(
      std::lround(origin_x + (request.x + request.width / 2.0) * scale_x));
  point.y = static_cast<LONG>(
      std::lround(origin_y + (request.y + request.height / 2.0) * scale_y));
  if (diagnostics != nullptr) {
    (*diagnostics)[EncodableValue("targetPointX")] = EncodableValue(point.x);
    (*diagnostics)[EncodableValue("targetPointY")] = EncodableValue(point.y);
  }
  return point;
}

EncodableMap ScrollDiagnostics(const ScrollDriverRequest& request) {
  EncodableMap diagnostics;
  diagnostics[EncodableValue("stepIndex")] =
      EncodableValue(request.step_index);
  diagnostics[EncodableValue("requestedDeltaY")] =
      EncodableValue(request.delta_y);
  diagnostics[EncodableValue("strategy")] = EncodableValue(request.strategy);
  diagnostics[EncodableValue("targetWidth")] =
      EncodableValue(request.width);
  diagnostics[EncodableValue("targetHeight")] =
      EncodableValue(request.height);
  if (request.window_id > 0) {
    diagnostics[EncodableValue("windowId")] =
        EncodableValue(request.window_id);
  }
  return diagnostics;
}

EncodableValue MakeScrollStepSuccess(bool reached_end,
                                     double applied_delta_y,
                                     int settle_delay_ms,
                                     bool movement_verified,
                                     bool movement_verifiable,
                                     EncodableMap diagnostics) {
  EncodableMap payload;
  payload[EncodableValue("reachedEnd")] = EncodableValue(reached_end);
  payload[EncodableValue("appliedDeltaY")] =
      EncodableValue(applied_delta_y);
  payload[EncodableValue("settleDelayMs")] =
      EncodableValue(settle_delay_ms);
  payload[EncodableValue("movementVerified")] =
      EncodableValue(movement_verified);
  payload[EncodableValue("movementVerifiable")] =
      EncodableValue(movement_verifiable);
  payload[EncodableValue("diagnostics")] = EncodableValue(diagnostics);
  return MakeSuccess(EncodableValue(payload));
}

HRESULT FindScrollPattern(IUIAutomation* automation,
                          IUIAutomationElement* start,
                          IUIAutomationScrollPattern** output) {
  if (automation == nullptr || start == nullptr || output == nullptr) {
    return E_POINTER;
  }

  ComPtr<IUIAutomationTreeWalker> walker;
  HRESULT hr = automation->get_ControlViewWalker(&walker);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IUIAutomationElement> current = start;
  for (int depth = 0; depth < 10 && current != nullptr; ++depth) {
    ComPtr<IUnknown> raw_pattern;
    hr = current->GetCurrentPattern(UIA_ScrollPatternId, &raw_pattern);
    if (SUCCEEDED(hr) && raw_pattern != nullptr) {
      ComPtr<IUIAutomationScrollPattern> pattern;
      if (SUCCEEDED(raw_pattern.As(&pattern)) && pattern != nullptr) {
        BOOL vertically_scrollable = FALSE;
        if (SUCCEEDED(pattern->get_CurrentVerticallyScrollable(
                &vertically_scrollable)) &&
            vertically_scrollable) {
          *output = pattern.Detach();
          return S_OK;
        }
      }
    }

    ComPtr<IUIAutomationElement> parent;
    if (FAILED(walker->GetParentElement(current.Get(), &parent)) ||
        parent == nullptr) {
      break;
    }
    current = parent;
  }

  return UIA_E_ELEMENTNOTAVAILABLE;
}

bool ScrollPatternReachedBoundary(double percent, double delta_y) {
  if (percent == UIA_ScrollPatternNoScroll) {
    return false;
  }
  return delta_y >= 0 ? percent >= 99.5 : percent <= 0.5;
}

bool TryUiaScroll(const ScrollDriverRequest& request,
                  const POINT& point,
                  EncodableMap* diagnostics,
                  bool* reached_end,
                  double* applied_delta_y,
                  int* settle_delay_ms) {
  if (diagnostics == nullptr || reached_end == nullptr ||
      applied_delta_y == nullptr || settle_delay_ms == nullptr) {
    return false;
  }

  ScopedComInitialization com(COINIT_APARTMENTTHREADED);
  if (!com.ok()) {
    (*diagnostics)[EncodableValue("uiaError")] =
        EncodableValue("comInitializationFailed");
    (*diagnostics)[EncodableValue("nativeCode")] =
        EncodableValue(HResultToString(com.hr()));
    return false;
  }

  ComPtr<IUIAutomation> automation;
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&automation));
  if (FAILED(hr) || automation == nullptr) {
    (*diagnostics)[EncodableValue("uiaError")] =
        EncodableValue("automationUnavailable");
    (*diagnostics)[EncodableValue("nativeCode")] =
        EncodableValue(HResultToString(hr));
    return false;
  }

  ComPtr<IUIAutomationElement> element;
  hr = automation->ElementFromPoint(point, &element);
  if (FAILED(hr) || element == nullptr) {
    (*diagnostics)[EncodableValue("uiaError")] =
        EncodableValue("elementFromPointFailed");
    (*diagnostics)[EncodableValue("nativeCode")] =
        EncodableValue(HResultToString(hr));
    return false;
  }

  ComPtr<IUIAutomationScrollPattern> scroll_pattern;
  hr = FindScrollPattern(automation.Get(), element.Get(), &scroll_pattern);
  if (FAILED(hr) || scroll_pattern == nullptr) {
    (*diagnostics)[EncodableValue("uiaError")] =
        EncodableValue("scrollPatternNotFound");
    (*diagnostics)[EncodableValue("nativeCode")] =
        EncodableValue(HResultToString(hr));
    return false;
  }

  (*diagnostics)[EncodableValue("driverMethod")] =
      EncodableValue("uiAutomation");
  (*diagnostics)[EncodableValue("fallbackUsed")] = EncodableValue(false);
  (*diagnostics)[EncodableValue("movementVerifiable")] =
      EncodableValue(true);

  double before_percent = UIA_ScrollPatternNoScroll;
  double after_percent = UIA_ScrollPatternNoScroll;
  double view_size = 0.0;
  scroll_pattern->get_CurrentVerticalScrollPercent(&before_percent);
  scroll_pattern->get_CurrentVerticalViewSize(&view_size);

  if (before_percent != UIA_ScrollPatternNoScroll && view_size >= 99.5) {
    *reached_end = true;
    *applied_delta_y = 0.0;
    *settle_delay_ms = std::max(request.settle_delay_ms, 320);
    (*diagnostics)[EncodableValue("movementVerified")] =
        EncodableValue(false);
    (*diagnostics)[EncodableValue("boundaryState")] =
        EncodableValue("notScrollable");
    (*diagnostics)[EncodableValue("scrollPercent")] =
        EncodableValue(before_percent);
    return true;
  }

  if (before_percent != UIA_ScrollPatternNoScroll &&
      ScrollPatternReachedBoundary(before_percent, request.delta_y)) {
    *reached_end = true;
    *applied_delta_y = 0.0;
    *settle_delay_ms = std::max(request.settle_delay_ms, 320);
    (*diagnostics)[EncodableValue("movementVerified")] =
        EncodableValue(false);
    (*diagnostics)[EncodableValue("boundaryState")] =
        EncodableValue(request.delta_y >= 0 ? "bottom" : "top");
    (*diagnostics)[EncodableValue("scrollPercent")] =
        EncodableValue(before_percent);
    return true;
  }

  bool handled = false;
  if (before_percent != UIA_ScrollPatternNoScroll) {
    const double step_percent = ClampDouble(
        std::abs(request.delta_y) / std::max(1.0, request.height) * 100.0,
        8.0, 45.0);
    const double next_percent = ClampDouble(
        before_percent + (request.delta_y >= 0 ? step_percent : -step_percent),
        0.0, 100.0);
    hr = scroll_pattern->SetScrollPercent(UIA_ScrollPatternNoScroll,
                                          next_percent);
    if (SUCCEEDED(hr)) {
      scroll_pattern->get_CurrentVerticalScrollPercent(&after_percent);
      handled = std::abs(after_percent - before_percent) >= 0.1 ||
                ScrollPatternReachedBoundary(after_percent, request.delta_y);
    }
  }

  if (!handled) {
    const ScrollAmount amount = request.delta_y >= 0
                                    ? ScrollAmount_LargeIncrement
                                    : ScrollAmount_LargeDecrement;
    hr = scroll_pattern->Scroll(ScrollAmount_NoAmount, amount);
    if (SUCCEEDED(hr)) {
      scroll_pattern->get_CurrentVerticalScrollPercent(&after_percent);
      handled = after_percent == UIA_ScrollPatternNoScroll ||
                std::abs(after_percent - before_percent) >= 0.1 ||
                ScrollPatternReachedBoundary(after_percent, request.delta_y);
    }
  }

  if (!handled) {
    (*diagnostics)[EncodableValue("uiaError")] =
        EncodableValue("scrollPatternMoveFailed");
    (*diagnostics)[EncodableValue("nativeCode")] =
        EncodableValue(HResultToString(hr));
    return false;
  }

  *reached_end = ScrollPatternReachedBoundary(after_percent, request.delta_y);
  *applied_delta_y = request.delta_y;
  *settle_delay_ms = std::max(request.settle_delay_ms, 420);
  const bool movement_verified =
      before_percent == UIA_ScrollPatternNoScroll ||
      after_percent == UIA_ScrollPatternNoScroll ||
      std::abs(after_percent - before_percent) >= 0.1;
  (*diagnostics)[EncodableValue("movementVerified")] =
      EncodableValue(movement_verified);
  (*diagnostics)[EncodableValue("boundaryState")] =
      EncodableValue(*reached_end
                         ? (request.delta_y >= 0 ? "bottom" : "top")
                         : "middle");
  (*diagnostics)[EncodableValue("scrollPercentBefore")] =
      EncodableValue(before_percent);
  (*diagnostics)[EncodableValue("scrollPercentAfter")] =
      EncodableValue(after_percent);
  (*diagnostics)[EncodableValue("verticalViewSize")] =
      EncodableValue(view_size);

  return true;
}

bool SendWheelFallback(const ScrollDriverRequest& request,
                       const POINT& point,
                       EncodableMap* diagnostics) {
  SetCursorPos(point.x, point.y);
  const int click_count = ClampInt(
      static_cast<int>(std::lround(std::abs(request.delta_y) / 360.0)), 1, 8);
  const LONG wheel_delta =
      (request.delta_y >= 0 ? -WHEEL_DELTA : WHEEL_DELTA) * click_count;

  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = MOUSEEVENTF_WHEEL;
  input.mi.mouseData = static_cast<DWORD>(wheel_delta);
  const UINT sent = SendInput(1, &input, sizeof(INPUT));

  if (diagnostics != nullptr) {
    (*diagnostics)[EncodableValue("driverMethod")] =
        EncodableValue("sendInputWheel");
    (*diagnostics)[EncodableValue("fallbackUsed")] = EncodableValue(true);
    (*diagnostics)[EncodableValue("movementVerified")] =
        EncodableValue(false);
    (*diagnostics)[EncodableValue("movementVerifiable")] =
        EncodableValue(false);
    (*diagnostics)[EncodableValue("boundaryState")] =
        EncodableValue("unknown");
    (*diagnostics)[EncodableValue("wheelDelta")] =
        EncodableValue(static_cast<int>(wheel_delta));
    if (sent != 1) {
      (*diagnostics)[EncodableValue("sendInputError")] =
          EncodableValue(static_cast<int>(GetLastError()));
    }
  }
  return sent == 1;
}

EncodableValue PerformScrollStepPayload(const EncodableMap* arguments) {
  ScrollDriverRequest request;
  if (!ReadScrollRequest(arguments, &request)) {
    return MakeFailure("invalidInput",
                       "A valid scroll target is required.",
                       "Select a scrollable area and try again.");
  }

  EncodableMap diagnostics = ScrollDiagnostics(request);
  const POINT point = ScrollTargetPoint(request, &diagnostics);

  HWND point_window = WindowFromPoint(point);
  HWND point_root = nullptr;
  if (point_window != nullptr) {
    point_root = GetAncestor(point_window, GA_ROOT);
    diagnostics[EncodableValue("pointWindow")] =
        EncodableValue(static_cast<int64_t>(
            reinterpret_cast<intptr_t>(
                point_root == nullptr ? point_window : point_root)));
  }
  if (request.window_id > 0) {
    HWND requested_window =
        reinterpret_cast<HWND>(static_cast<intptr_t>(request.window_id));
    const bool target_window_valid =
        requested_window != nullptr && IsWindow(requested_window) == TRUE;
    diagnostics[EncodableValue("targetWindowValid")] =
        EncodableValue(target_window_valid);
    if (!target_window_valid) {
      return MakeFailure("nativeFailure",
                         "The selected scroll window was not found.",
                         "Refresh the window list, reselect the target, or continue guided capture.");
    }
    RECT requested_bounds = {};
    if (!WindowVisibleCaptureBounds(requested_window, &requested_bounds)) {
      return MakeFailure("nativeFailure",
                         "The selected scroll window is not currently capturable.",
                         "Restore the window, reselect it, or continue guided capture.");
    }
    const bool target_window_hit =
        point_root == requested_window ||
        (point_window != nullptr && IsChild(requested_window, point_window));
    diagnostics[EncodableValue("targetWindowHit")] =
        EncodableValue(target_window_hit);
    if (!target_window_hit) {
      return MakeFailure("nativeFailure",
                         "The selected scroll target no longer matches the active window.",
                         "Refresh the window list, reselect the target, or continue guided capture.");
    }
  }

  bool reached_end = false;
  double applied_delta_y = 0.0;
  int settle_delay_ms = request.settle_delay_ms;
  if (TryUiaScroll(request, point, &diagnostics, &reached_end,
                   &applied_delta_y, &settle_delay_ms)) {
    const bool movement_verified =
        ReadBool(FindMapValue(diagnostics, "movementVerified"), false);
    return MakeScrollStepSuccess(reached_end, applied_delta_y, settle_delay_ms,
                                 movement_verified, true,
                                 diagnostics);
  }

  if (SendWheelFallback(request, point, &diagnostics)) {
    return MakeScrollStepSuccess(false, request.delta_y,
                                 std::max(request.settle_delay_ms, 520),
                                 false, false,
                                 diagnostics);
  }

  return MakeFailure("nativeFailure",
                     "Windows could not scroll the selected target.",
                     "Try guided capture for this app.");
}

CapabilityProbeResult CameraSourceProbe() {
  ScopedComInitialization com;
  if (!com.ok()) {
    return {false, "notConfigured",
            "Windows camera probe could not initialize COM."};
  }

  ScopedMediaFoundation media_foundation;
  HRESULT hr = media_foundation.Startup();
  if (FAILED(hr)) {
    return {false, "notConfigured",
            "Windows Media Foundation is unavailable: " + HResultToString(hr)};
  }

  ComPtr<IMFAttributes> attributes;
  hr = MFCreateAttributes(&attributes, 1);
  if (SUCCEEDED(hr)) {
    hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  }
  if (FAILED(hr)) {
    return {false, "notConfigured",
            "Windows camera probe could not select video devices: " +
                HResultToString(hr)};
  }

  IMFActivate** devices = nullptr;
  UINT32 device_count = 0;
  hr = MFEnumDeviceSources(attributes.Get(), &devices, &device_count);
  if (FAILED(hr) || device_count == 0) {
    if (devices != nullptr) {
      CoTaskMemFree(devices);
    }
    return {false, "notConfigured",
            FAILED(hr) ? "Windows camera devices could not be listed: " +
                             HResultToString(hr)
                       : "No Windows camera device is available."};
  }

  HRESULT last_hr = E_FAIL;
  bool saw_permission_denied = false;
  for (UINT32 index = 0; index < device_count; ++index) {
    ComPtr<IMFMediaSource> media_source;
    hr = devices[index]->ActivateObject(IID_PPV_ARGS(&media_source));
    if (SUCCEEDED(hr) && media_source != nullptr) {
      ComPtr<IMFSourceReader> reader;
      hr = MFCreateSourceReaderFromMediaSource(media_source.Get(), nullptr,
                                               &reader);
      media_source->Shutdown();
      if (SUCCEEDED(hr) && reader != nullptr) {
        for (UINT32 release_index = 0; release_index < device_count;
             ++release_index) {
          devices[release_index]->Release();
        }
        CoTaskMemFree(devices);
        return {true, "available", "Windows camera source is ready."};
      }
    }
    saw_permission_denied = saw_permission_denied || IsPermissionDenied(hr);
    last_hr = hr;
  }

  for (UINT32 index = 0; index < device_count; ++index) {
    devices[index]->Release();
  }
  CoTaskMemFree(devices);

  return {false, saw_permission_denied ? "requiresPermission"
                                       : "notConfigured",
          "Windows camera source is unavailable or in use: " +
              HResultToString(last_hr)};
}

std::string PermissionStateForProbe(const CapabilityProbeResult& probe) {
  if (probe.available) {
    return "granted";
  }
  return probe.status == "requiresPermission" ? "denied" : "restricted";
}

EncodableValue MakePermissionPayloadFromProbe(
    const std::string& type,
    const CapabilityProbeResult& probe) {
  return MakePermissionPayload(type, PermissionStateForProbe(probe),
                               probe.message);
}

bool ResolveRecordingRect(const RecordingConfig& config,
                          RECT* output,
                          std::string* error) {
  if (config.mode == "window") {
    if (config.window_id <= 0) {
      if (error != nullptr) {
        *error = "A valid target window is required for window recording.";
      }
      return false;
    }
    HWND window = reinterpret_cast<HWND>(
        static_cast<intptr_t>(config.window_id));
    if (!WindowVisibleCaptureBounds(window, output)) {
      if (error != nullptr) {
        *error = "The target recording window is not currently capturable.";
      }
      return false;
    }
    return true;
  }

  if (config.mode == "region" && config.has_region) {
    *output = config.region;
    if (config.display_id > 0) {
      const auto monitors = EnumerateMonitors();
      const MonitorDescriptor* monitor =
          FindMonitorById(monitors, config.display_id);
      if (monitor == nullptr) {
        if (error != nullptr) {
          *error = "The target recording display could not be resolved.";
        }
        return false;
      }
      if (!TryOffsetRect(output, monitor->bounds.left, monitor->bounds.top)) {
        if (error != nullptr) {
          *error = "The recording region is outside the target display.";
        }
        return false;
      }
      RECT clamped = {};
      if (!IntersectRects(*output, monitor->bounds, &clamped)) {
        if (error != nullptr) {
          *error = "The recording region is outside the target display.";
        }
        return false;
      }
      *output = clamped;
    } else {
      RECT clamped = {};
      if (!IntersectRects(*output, VirtualScreenBounds(), &clamped)) {
        if (error != nullptr) {
          *error = "The recording region is outside the desktop.";
        }
        return false;
      }
      *output = clamped;
    }
    return true;
  }

  const auto monitors = EnumerateMonitors();
  const MonitorDescriptor* monitor =
      FindMonitorById(monitors, config.display_id);
  if (monitor == nullptr) {
    if (error != nullptr) {
      *error = "No display is available to record.";
    }
    return false;
  }
  *output = monitor->bounds;
  return true;
}

RecordingConfig ReadRecordingConfig(const EncodableMap* arguments) {
  RecordingConfig config;
  if (arguments == nullptr) {
    return config;
  }

  config.mode = ReadString(FindMapValue(*arguments, "mode"), config.mode);
  config.format = ReadString(FindMapValue(*arguments, "format"), config.format);
  config.frame_rate =
      std::clamp(ReadInt32(FindMapValue(*arguments, "frameRate"), 30), 1, 120);
  config.video_bitrate =
      std::max(ReadInt32(FindMapValue(*arguments, "videoBitrate"), 8000000),
               1000000);
  config.quality_max_height =
      ReadInt32(FindMapValue(*arguments, "qualityMaxHeight"), 0);
  config.capture_audio =
      ReadBool(FindMapValue(*arguments, "captureAudio"), false);
  config.capture_microphone =
      ReadBool(FindMapValue(*arguments, "captureMicrophone"), false);
  config.show_cursor = ReadBool(FindMapValue(*arguments, "showCursor"), true);
  config.highlight_clicks =
      ReadBool(FindMapValue(*arguments, "highlightClicks"), false);
  config.show_keystrokes =
      ReadBool(FindMapValue(*arguments, "showKeystrokes"), false);
  config.display_id = ReadInt(FindMapValue(*arguments, "displayId"));
  config.window_id = ReadInt(FindMapValue(*arguments, "windowId"));
  config.has_region = ReadPhysicalRect(*arguments, "region", &config.region);
  if (const EncodableMap* gif = ReadMap(*arguments, "gif")) {
    config.gif.max_width =
        std::max(1, ReadInt32(FindMapValue(*gif, "maxWidth"), 960));
    config.gif.frame_rate =
        std::clamp(ReadInt32(FindMapValue(*gif, "frameRate"), 12), 1, 30);
    config.gif.optimize_palette =
        ReadBool(FindMapValue(*gif, "optimizePalette"), true);
  }
  if (const EncodableMap* camera = ReadMap(*arguments, "cameraOverlay")) {
    config.camera_overlay.enabled =
        ReadBool(FindMapValue(*camera, "enabled"), false);
    const std::string shape =
        ReadString(FindMapValue(*camera, "shape"), "circle");
    config.camera_overlay.shape = shape == "rectangle"
                                      ? CameraOverlayShape::rectangle
                                      : CameraOverlayShape::circle;
    const std::string position =
        ReadString(FindMapValue(*camera, "position"), "bottomRight");
    if (position == "topLeft") {
      config.camera_overlay.position = RecordingOverlayPosition::top_left;
    } else if (position == "topRight") {
      config.camera_overlay.position = RecordingOverlayPosition::top_right;
    } else if (position == "bottomLeft") {
      config.camera_overlay.position = RecordingOverlayPosition::bottom_left;
    } else {
      config.camera_overlay.position = RecordingOverlayPosition::bottom_right;
    }
    config.camera_overlay.size =
        std::clamp(ReadInt32(FindMapValue(*camera, "size"), 220), 64, 640);
  }
  return config;
}

std::wstring RecordingOutputPath(const std::string& format) {
  const std::wstring record_dir = GoodCleanShotTempDirectory(L"recordings");

  SYSTEMTIME time = {};
  GetSystemTime(&time);
  wchar_t file_name[96] = {};
  const wchar_t* extension = format == "gif" ? L"gif" : L"mp4";
  std::swprintf(file_name, 96,
                L"recording_%04u%02u%02u_%02u%02u%02u_%llu.%s",
                static_cast<unsigned int>(time.wYear),
                static_cast<unsigned int>(time.wMonth),
                static_cast<unsigned int>(time.wDay),
                static_cast<unsigned int>(time.wHour),
                static_cast<unsigned int>(time.wMinute),
                static_cast<unsigned int>(time.wSecond),
                static_cast<unsigned long long>(GetTickCount64()), extension);
  return record_dir + L"\\" + file_name;
}

int64_t FileSizeForPath(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA attributes = {};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard,
                            &attributes)) {
    return 0;
  }
  ULARGE_INTEGER size = {};
  size.HighPart = attributes.nFileSizeHigh;
  size.LowPart = attributes.nFileSizeLow;
  return static_cast<int64_t>(size.QuadPart);
}

HRESULT CreateRecordingSinkWriter(const std::wstring& path,
                                  int width,
                                  int height,
                                  int frame_rate,
                                  int bitrate,
                                  bool include_audio,
                                  IMFSinkWriter** writer,
                                  DWORD* video_stream_index,
                                  DWORD* audio_stream_index) {
  ComPtr<IMFSinkWriter> sink_writer;
  HRESULT hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr,
                                         &sink_writer);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IMFMediaType> output_type;
  hr = MFCreateMediaType(&output_type);
  if (FAILED(hr)) {
    return hr;
  }
  output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  output_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
  output_type->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
  output_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  MFSetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE, width, height);
  MFSetAttributeRatio(output_type.Get(), MF_MT_FRAME_RATE, frame_rate, 1);
  MFSetAttributeRatio(output_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

  hr = sink_writer->AddStream(output_type.Get(), video_stream_index);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IMFMediaType> input_type;
  hr = MFCreateMediaType(&input_type);
  if (FAILED(hr)) {
    return hr;
  }
  input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  input_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  input_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  MFSetAttributeSize(input_type.Get(), MF_MT_FRAME_SIZE, width, height);
  MFSetAttributeRatio(input_type.Get(), MF_MT_FRAME_RATE, frame_rate, 1);
  MFSetAttributeRatio(input_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

  hr = sink_writer->SetInputMediaType(*video_stream_index, input_type.Get(),
                                      nullptr);
  if (FAILED(hr)) {
    return hr;
  }

  *audio_stream_index = MF_SINK_WRITER_INVALID_STREAM_INDEX;
  if (include_audio) {
    ComPtr<IMFMediaType> audio_output_type;
    hr = MFCreateMediaType(&audio_output_type);
    if (FAILED(hr)) {
      return hr;
    }
    audio_output_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    audio_output_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    audio_output_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    audio_output_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                                 kOutputAudioSampleRate);
    audio_output_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,
                                 kOutputAudioChannelCount);
    audio_output_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                 kOutputAudioBitrate / 8);
    audio_output_type->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION,
                                 0x29);

    hr = sink_writer->AddStream(audio_output_type.Get(), audio_stream_index);
    if (FAILED(hr)) {
      return hr;
    }

    ComPtr<IMFMediaType> audio_input_type;
    hr = MFCreateMediaType(&audio_input_type);
    if (FAILED(hr)) {
      return hr;
    }
    audio_input_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    audio_input_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    audio_input_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,
                                kOutputAudioBitsPerSample);
    audio_input_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                                kOutputAudioSampleRate);
    audio_input_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,
                                kOutputAudioChannelCount);
    audio_input_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,
                                kOutputAudioBytesPerFrame);
    audio_input_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                kOutputAudioSampleRate *
                                    kOutputAudioBytesPerFrame);
    audio_input_type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

    hr = sink_writer->SetInputMediaType(*audio_stream_index,
                                        audio_input_type.Get(), nullptr);
    if (FAILED(hr)) {
      return hr;
    }
  }

  hr = sink_writer->BeginWriting();
  if (FAILED(hr)) {
    return hr;
  }

  *writer = sink_writer.Detach();
  return S_OK;
}

HRESULT WriteVideoFrame(IMFSinkWriter* writer,
                        DWORD stream_index,
                        const BitmapFrame& frame,
                        LONGLONG sample_time,
                        LONGLONG sample_duration) {
  size_t safe_buffer_size = 0;
  if (!TryGetBgraBufferSize(frame.width, frame.height, &safe_buffer_size) ||
      safe_buffer_size > std::numeric_limits<DWORD>::max() ||
      frame.bgra.size() < safe_buffer_size) {
    return E_INVALIDARG;
  }

  const DWORD buffer_size = static_cast<DWORD>(safe_buffer_size);
  ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = MFCreateMemoryBuffer(buffer_size, &buffer);
  if (FAILED(hr)) {
    return hr;
  }

  BYTE* data = nullptr;
  hr = buffer->Lock(&data, nullptr, nullptr);
  if (FAILED(hr)) {
    return hr;
  }
  std::memcpy(data, frame.bgra.data(), safe_buffer_size);
  buffer->Unlock();
  buffer->SetCurrentLength(buffer_size);

  ComPtr<IMFSample> sample;
  hr = MFCreateSample(&sample);
  if (FAILED(hr)) {
    return hr;
  }
  sample->AddBuffer(buffer.Get());
  sample->SetSampleTime(sample_time);
  sample->SetSampleDuration(sample_duration);
  return writer->WriteSample(stream_index, sample.Get());
}

HRESULT WriteAudioSample(IMFSinkWriter* writer,
                         DWORD stream_index,
                         const std::vector<int16_t>& frames,
                         int64_t start_frame) {
  if (frames.empty()) {
    return S_OK;
  }
  if (frames.size() >
      std::numeric_limits<DWORD>::max() / sizeof(int16_t)) {
    return E_INVALIDARG;
  }

  const DWORD buffer_size =
      static_cast<DWORD>(frames.size() * sizeof(int16_t));
  ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = MFCreateMemoryBuffer(buffer_size, &buffer);
  if (FAILED(hr)) {
    return hr;
  }

  BYTE* data = nullptr;
  hr = buffer->Lock(&data, nullptr, nullptr);
  if (FAILED(hr)) {
    return hr;
  }
  std::memcpy(data, frames.data(), buffer_size);
  buffer->Unlock();
  buffer->SetCurrentLength(buffer_size);

  ComPtr<IMFSample> sample;
  hr = MFCreateSample(&sample);
  if (FAILED(hr)) {
    return hr;
  }
  sample->AddBuffer(buffer.Get());

  const int64_t frame_count =
      static_cast<int64_t>(frames.size() / kOutputAudioChannelCount);
  const LONGLONG sample_time =
      (start_frame * kMediaFoundationSecond) / kOutputAudioSampleRate;
  const LONGLONG sample_duration =
      (frame_count * kMediaFoundationSecond) / kOutputAudioSampleRate;
  sample->SetSampleTime(sample_time);
  sample->SetSampleDuration(sample_duration);
  return writer->WriteSample(stream_index, sample.Get());
}

int EvenDimension(double value) {
  int rounded = std::max(2, static_cast<int>(std::lround(value)));
  if ((rounded % 2) != 0) {
    --rounded;
  }
  return std::max(2, rounded);
}

BitmapFrame ResizeFrameToDimensions(const BitmapFrame& frame,
                                    int target_width,
                                    int target_height) {
  size_t buffer_size = 0;
  if (target_width <= 0 || target_height <= 0 ||
      frame.width <= 0 || frame.height <= 0 || frame.bgra.empty() ||
      (frame.width == target_width && frame.height == target_height) ||
      !TryGetBgraBufferSize(target_width, target_height, &buffer_size)) {
    return frame;
  }

  BitmapFrame resized;
  resized.width = target_width;
  resized.height = target_height;
  resized.bgra.resize(buffer_size);
  for (int y = 0; y < resized.height; ++y) {
    const int source_y =
        std::min(frame.height - 1,
                 static_cast<int>(
                     static_cast<int64_t>(y) * frame.height / resized.height));
    for (int x = 0; x < resized.width; ++x) {
      const int source_x =
          std::min(frame.width - 1,
                   static_cast<int>(
                       static_cast<int64_t>(x) * frame.width / resized.width));
      const size_t source_offset =
          (static_cast<size_t>(source_y) * frame.width + source_x) * 4;
      const size_t target_offset =
          (static_cast<size_t>(y) * resized.width + x) * 4;
      std::memcpy(resized.bgra.data() + target_offset,
                  frame.bgra.data() + source_offset, 4);
    }
  }
  return resized;
}

BitmapFrame ResizeFrameToMaxWidth(const BitmapFrame& frame, int max_width) {
  if (max_width <= 0 || frame.width <= max_width || frame.width <= 0 ||
      frame.height <= 0) {
    return frame;
  }

  const int target_height = std::max(
      1, static_cast<int>(std::lround(
             static_cast<double>(frame.height) * max_width / frame.width)));
  return ResizeFrameToDimensions(frame, max_width, target_height);
}

using OcrInitializeFn = int32_t(__cdecl*)(const char*, const char*);
using OcrRecognizePngFn = char*(__cdecl*)(const uint8_t*, intptr_t, const char*);
using OcrLastErrorFn = char*(__cdecl*)();
using OcrFreeStringFn = void(__cdecl*)(char*);
using OcrDisposeFn = void(__cdecl*)();

struct OcrRuntimeSymbols {
  OcrInitializeFn initialize = nullptr;
  OcrRecognizePngFn recognize_png = nullptr;
  OcrLastErrorFn last_error = nullptr;
  OcrFreeStringFn free_string = nullptr;
  OcrDisposeFn dispose = nullptr;
};

std::mutex g_ocr_mutex;
HMODULE g_ocr_library = nullptr;
OcrRuntimeSymbols g_ocr_symbols;
std::string g_ocr_library_path;
std::string g_ocr_language = "auto";
std::string g_ocr_load_error;

std::wstring ExecutableDirectory() {
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD size = GetModuleFileNameW(nullptr, buffer.data(),
                                  static_cast<DWORD>(buffer.size()));
  while (size == buffer.size()) {
    buffer.resize(buffer.size() * 2);
    size = GetModuleFileNameW(nullptr, buffer.data(),
                              static_cast<DWORD>(buffer.size()));
  }
  buffer.resize(size);
  const size_t separator = buffer.find_last_of(L"\\/");
  return separator == std::wstring::npos ? L"." : buffer.substr(0, separator);
}

std::wstring BundledOcrRuntimePath() {
  return ExecutableDirectory() + L"\\good_clean_shot_ocr.dll";
}

bool BundledOcrRuntimeExists() {
  const std::wstring path = BundledOcrRuntimePath();
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void UnloadOcrLocked() {
  if (g_ocr_symbols.dispose != nullptr) {
    g_ocr_symbols.dispose();
  }
  if (g_ocr_library != nullptr) {
    FreeLibrary(g_ocr_library);
  }
  g_ocr_library = nullptr;
  g_ocr_symbols = OcrRuntimeSymbols();
  g_ocr_library_path.clear();
  g_ocr_language = "auto";
}

std::string TakeOcrStringLocked(char* value) {
  if (value == nullptr) {
    return "";
  }
  std::string output(value);
  if (g_ocr_symbols.free_string != nullptr) {
    g_ocr_symbols.free_string(value);
  }
  return output;
}

std::string LastOcrErrorLocked() {
  if (g_ocr_symbols.last_error == nullptr) {
    return g_ocr_load_error;
  }
  const std::string runtime_error =
      TakeOcrStringLocked(g_ocr_symbols.last_error());
  return runtime_error.empty() ? g_ocr_load_error : runtime_error;
}

bool LoadOcrRuntimeLocked(const std::string& library_path) {
  if (g_ocr_library != nullptr && g_ocr_library_path == library_path) {
    return true;
  }

  UnloadOcrLocked();
  HMODULE library = LoadLibraryA(library_path.c_str());
  if (library == nullptr) {
    g_ocr_load_error = "The OCR runtime library could not be loaded.";
    return false;
  }

  OcrRuntimeSymbols symbols;
  symbols.initialize = reinterpret_cast<OcrInitializeFn>(
      GetProcAddress(library, "gcs_ocr_initialize"));
  symbols.recognize_png = reinterpret_cast<OcrRecognizePngFn>(
      GetProcAddress(library, "gcs_ocr_recognize_png"));
  symbols.last_error = reinterpret_cast<OcrLastErrorFn>(
      GetProcAddress(library, "gcs_ocr_last_error"));
  symbols.free_string = reinterpret_cast<OcrFreeStringFn>(
      GetProcAddress(library, "gcs_ocr_free_string"));
  symbols.dispose = reinterpret_cast<OcrDisposeFn>(
      GetProcAddress(library, "gcs_ocr_dispose"));

  if (symbols.initialize == nullptr || symbols.recognize_png == nullptr ||
      symbols.last_error == nullptr || symbols.free_string == nullptr ||
      symbols.dispose == nullptr) {
    FreeLibrary(library);
    g_ocr_load_error = "The OCR runtime ABI is incomplete.";
    return false;
  }

  g_ocr_library = library;
  g_ocr_symbols = symbols;
  g_ocr_library_path = library_path;
  g_ocr_load_error.clear();
  return true;
}

EncodableValue OcrInitializePayload(const EncodableMap* arguments) {
  if (arguments == nullptr) {
    return MakeFailure("invalidInput",
                       "OCR initialization arguments are required.",
                       "Pass the runtime library and model directory paths.");
  }

  const std::string library_path =
      ReadString(FindMapValue(*arguments, "libraryPath"));
  const std::string model_directory =
      ReadString(FindMapValue(*arguments, "modelDirectory"));
  const std::string language =
      ReadString(FindMapValue(*arguments, "language"), "auto");
  if (library_path.empty() || model_directory.empty()) {
    return MakeFailure("invalidInput",
                       "OCR initialization requires libraryPath and modelDirectory.",
                       "Resolve the OCR runtime and model paths before initializing.");
  }

  std::lock_guard<std::mutex> lock(g_ocr_mutex);
  if (!LoadOcrRuntimeLocked(library_path)) {
    return MakeFailure("notConfigured",
                       LastOcrErrorLocked(),
                       "Reinstall the GoodCleanShot Windows build that bundles a matching good_clean_shot_ocr.dll.");
  }

  const int32_t status =
      g_ocr_symbols.initialize(model_directory.c_str(), language.c_str());
  if (status != 0) {
    const std::string recovery = status == 2
        ? "Repair the default OCR model, then retry initialization."
        : "Install the required Windows OCR language pack or switch to an installed OCR model.";
    return MakeFailure("nativeFailure",
                       LastOcrErrorLocked(),
                       recovery,
                       std::to_string(status));
  }

  g_ocr_language = language;
  return MakeSuccess();
}

EncodableValue OcrRecognizePayload(const EncodableMap* arguments) {
  if (arguments == nullptr) {
    return MakeFailure("invalidInput",
                       "OCR recognition arguments are required.",
                       "Pass PNG image bytes before starting OCR.");
  }

  const auto* bytes_value = FindMapValue(*arguments, "imageBytes");
  const auto* bytes = bytes_value == nullptr
                          ? nullptr
                          : std::get_if<std::vector<uint8_t>>(bytes_value);
  if (bytes == nullptr || bytes->empty()) {
    return MakeFailure("invalidInput",
                       "OCR recognition requires PNG image bytes.",
                       "Capture an image and retry OCR.");
  }

  const std::string language =
      ReadString(FindMapValue(*arguments, "language"), g_ocr_language);
  const auto started_at = std::chrono::steady_clock::now();
  if (bytes->size() >
      static_cast<size_t>(std::numeric_limits<intptr_t>::max())) {
    return MakeFailure("invalidInput",
                       "OCR image bytes are too large.",
                       "Capture a smaller image and retry OCR.");
  }

  std::lock_guard<std::mutex> lock(g_ocr_mutex);
  if (g_ocr_symbols.recognize_png == nullptr) {
    return MakeFailure("notConfigured",
                       "The OCR runtime has not been initialized.",
                       "Initialize OCR before recognizing an image.");
  }

  char* raw_output = g_ocr_symbols.recognize_png(
      bytes->data(), static_cast<intptr_t>(bytes->size()), language.c_str());
  if (raw_output == nullptr) {
    return MakeFailure("nativeFailure",
                       LastOcrErrorLocked(),
                       "Repair the OCR runtime or selected model, then retry recognition.");
  }

  const std::string output = TakeOcrStringLocked(raw_output);
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_at);

  EncodableMap payload;
  payload[EncodableValue("rawOutput")] = EncodableValue(output);
  payload[EncodableValue("fullText")] = EncodableValue("");
  payload[EncodableValue("lines")] = EncodableValue(EncodableList());
  payload[EncodableValue("processingTimeMs")] =
      EncodableValue(static_cast<int64_t>(elapsed_ms.count()));
  payload[EncodableValue("imageWidth")] = EncodableValue(0);
  payload[EncodableValue("imageHeight")] = EncodableValue(0);
  payload[EncodableValue("modelId")] = EncodableValue("");
  payload[EncodableValue("language")] = EncodableValue(language);
  return MakeSuccess(EncodableValue(payload));
}

EncodableValue OcrDisposePayload() {
  std::lock_guard<std::mutex> lock(g_ocr_mutex);
  UnloadOcrLocked();
  return MakeSuccess();
}

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return L"";
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0);
  if (size <= 0) {
    return L"";
  }
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

std::wstring GoodCleanShotTempDirectory(const wchar_t* child_directory) {
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()),
                              buffer.data());
  if (length >= buffer.size()) {
    buffer.assign(static_cast<size_t>(length) + 1, L'\0');
    length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
  }

  std::wstring root;
  if (length == 0 || length >= buffer.size()) {
    root = L".\\";
  } else {
    buffer.resize(length);
    root = std::move(buffer);
  }

  if (root.empty()) {
    root = L".\\";
  }
  const wchar_t last = root.back();
  if (last != L'\\' && last != L'/') {
    root.push_back(L'\\');
  }

  std::wstring app_dir = root + L"GoodCleanShot";
  CreateDirectoryW(app_dir.c_str(), nullptr);
  if (child_directory == nullptr || child_directory[0] == L'\0') {
    return app_dir;
  }

  std::wstring target = app_dir + L"\\" + child_directory;
  CreateDirectoryW(target.c_str(), nullptr);
  return target;
}

std::wstring VideoEditOutputPath() {
  const std::wstring edit_dir = GoodCleanShotTempDirectory(L"edits");

  SYSTEMTIME time = {};
  GetSystemTime(&time);
  wchar_t file_name[96] = {};
  std::swprintf(file_name, 96,
                L"edit_%04u%02u%02u_%02u%02u%02u_%llu.mp4",
                static_cast<unsigned int>(time.wYear),
                static_cast<unsigned int>(time.wMonth),
                static_cast<unsigned int>(time.wDay),
                static_cast<unsigned int>(time.wHour),
                static_cast<unsigned int>(time.wMinute),
                static_cast<unsigned int>(time.wSecond),
                static_cast<unsigned long long>(GetTickCount64()));
  return edit_dir + L"\\" + file_name;
}

HRESULT CreateVideoSourceReader(const std::wstring& input_path,
                                IMFSourceReader** reader,
                                int* width,
                                int* height,
                                bool* has_audio) {
  ComPtr<IMFAttributes> attributes;
  HRESULT hr = MFCreateAttributes(&attributes, 2);
  if (FAILED(hr)) {
    return hr;
  }
  attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

  ComPtr<IMFSourceReader> source_reader;
  hr = MFCreateSourceReaderFromURL(input_path.c_str(), attributes.Get(),
                                   &source_reader);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IMFMediaType> video_type;
  hr = MFCreateMediaType(&video_type);
  if (FAILED(hr)) {
    return hr;
  }
  video_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  video_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  video_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  source_reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
  source_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
  hr = source_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                          nullptr, video_type.Get());
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IMFMediaType> current_video_type;
  hr = source_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                          &current_video_type);
  if (FAILED(hr)) {
    return hr;
  }
  UINT32 video_width = 0;
  UINT32 video_height = 0;
  hr = MFGetAttributeSize(current_video_type.Get(), MF_MT_FRAME_SIZE,
                          &video_width, &video_height);
  if (FAILED(hr) || video_width == 0 || video_height == 0) {
    return FAILED(hr) ? hr : MF_E_INVALIDMEDIATYPE;
  }

  bool audio_available = false;
  if (SUCCEEDED(source_reader->SetStreamSelection(
          MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) {
    ComPtr<IMFMediaType> audio_type;
    if (SUCCEEDED(MFCreateMediaType(&audio_type))) {
      audio_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
      audio_type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
      audio_type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,
                            kOutputAudioBitsPerSample);
      audio_type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                            kOutputAudioSampleRate);
      audio_type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,
                            kOutputAudioChannelCount);
      audio_type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,
                            kOutputAudioBytesPerFrame);
      audio_type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                            kOutputAudioSampleRate *
                                kOutputAudioBytesPerFrame);
      audio_available = SUCCEEDED(source_reader->SetCurrentMediaType(
          MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, audio_type.Get()));
    }
  }

  *width = static_cast<int>(video_width);
  *height = static_cast<int>(video_height);
  *has_audio = audio_available;
  *reader = source_reader.Detach();
  return S_OK;
}

HRESULT SeekSourceReader(IMFSourceReader* reader, LONGLONG position) {
  PROPVARIANT seek_position;
  PropVariantInit(&seek_position);
  seek_position.vt = VT_I8;
  seek_position.hVal.QuadPart = position;
  HRESULT hr = reader->SetCurrentPosition(GUID_NULL, seek_position);
  PropVariantClear(&seek_position);
  return hr;
}

void ApplyAudioVolume(IMFSample* sample, double volume, bool muted) {
  const double gain = muted ? 0.0 : std::clamp(volume, 0.0, 1.0);
  DWORD buffer_count = 0;
  if (sample == nullptr || FAILED(sample->GetBufferCount(&buffer_count))) {
    return;
  }
  for (DWORD index = 0; index < buffer_count; ++index) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->GetBufferByIndex(index, &buffer))) {
      continue;
    }
    BYTE* data = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    if (FAILED(buffer->Lock(&data, &max_length, &current_length))) {
      continue;
    }
    auto* samples = reinterpret_cast<int16_t*>(data);
    const DWORD sample_count =
        current_length / static_cast<DWORD>(sizeof(int16_t));
    for (DWORD sample_index = 0; sample_index < sample_count; ++sample_index) {
      const int scaled =
          static_cast<int>(std::lrint(samples[sample_index] * gain));
      samples[sample_index] = static_cast<int16_t>(
          std::clamp(scaled, static_cast<int>(INT16_MIN),
                     static_cast<int>(INT16_MAX)));
    }
    buffer->Unlock();
  }
}

EncodableValue VideoPreviewFramePayload(const EncodableMap* arguments) {
  if (arguments == nullptr) {
    return MakeFailure("invalidInput", "A video path is required.",
                       "Select a recorded MP4 file and try again.");
  }

  const std::wstring input_path =
      Utf8ToWide(ReadString(FindMapValue(*arguments, "inputPath")));
  const LONGLONG position =
      ReadInt(FindMapValue(*arguments, "positionMs")) * 10000;
  if (input_path.empty()) {
    return MakeFailure("invalidInput", "A video path is required.",
                       "Select a recorded MP4 file and try again.");
  }

  ScopedComInitialization com;
  if (!com.ok()) {
    return MakeFailure("nativeFailure",
                       "COM could not be initialized for video preview.",
                       "Restart the app and try again.",
                       HResultToString(com.hr()));
  }
  ScopedMediaFoundation media_foundation;
  HRESULT hr = media_foundation.Startup();
  if (FAILED(hr)) {
    return MakeFailure("nativeFailure",
                       "Media Foundation could not be initialized.",
                       "Restart the app and try again.", HResultToString(hr));
  }

  ComPtr<IMFSourceReader> reader;
  int width = 0;
  int height = 0;
  bool has_audio = false;
  hr = CreateVideoSourceReader(input_path, &reader, &width, &height,
                               &has_audio);
  if (SUCCEEDED(hr)) {
    hr = SeekSourceReader(reader.Get(), std::max<LONGLONG>(0, position));
  }

  BitmapFrame frame;
  if (SUCCEEDED(hr)) {
    for (int attempts = 0; attempts < 40; ++attempts) {
      DWORD stream_index = 0;
      DWORD flags = 0;
      LONGLONG sample_time = 0;
      ComPtr<IMFSample> sample;
      hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                              &stream_index, &flags, &sample_time, &sample);
      if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
        break;
      }
      if (sample == nullptr) {
        continue;
      }
      ComPtr<IMFMediaBuffer> buffer;
      hr = sample->ConvertToContiguousBuffer(&buffer);
      if (FAILED(hr)) {
        break;
      }
      BYTE* data = nullptr;
      DWORD max_length = 0;
      DWORD current_length = 0;
      hr = buffer->Lock(&data, &max_length, &current_length);
      if (FAILED(hr)) {
        break;
      }
      size_t expected = 0;
      if (TryGetBgraBufferSize(width, height, &expected) &&
          current_length >= expected) {
        frame.width = width;
        frame.height = height;
        frame.bgra.assign(data, data + expected);
      }
      buffer->Unlock();
      break;
    }
  }

  EncodableValue result;
  if (SUCCEEDED(hr) && !frame.bgra.empty()) {
    result = MakeImagePayload(frame, 0, 0, 1.0);
  } else {
    result = MakeFailure("nativeFailure",
                         "A Windows video preview frame could not be decoded.",
                         "Try a different MP4 recording.",
                         HResultToString(FAILED(hr) ? hr : E_FAIL));
  }

  return result;
}

EncodableValue ExportVideoPayload(const EncodableMap* arguments) {
  if (arguments == nullptr) {
    return MakeFailure("invalidInput", "A video export request is required.",
                       "Choose a recorded MP4 file and try again.");
  }
  const std::wstring input_path =
      Utf8ToWide(ReadString(FindMapValue(*arguments, "inputPath")));
  const LONGLONG start_time =
      std::max<int64_t>(0, ReadInt(FindMapValue(*arguments, "startMs"))) *
      10000;
  const LONGLONG end_time =
      std::max<int64_t>(0, ReadInt(FindMapValue(*arguments, "endMs"))) *
      10000;
  const double volume =
      std::clamp(ReadDouble(FindMapValue(*arguments, "volume"), 1.0), 0.0, 1.0);
  const bool muted = ReadBool(FindMapValue(*arguments, "muted"), false);
  if (input_path.empty()) {
    return MakeFailure("invalidInput", "A video path is required.",
                       "Choose a recorded MP4 file and try again.");
  }
  if (end_time <= start_time) {
    return MakeFailure("invalidInput", "The trim range is invalid.",
                       "Move the end trim handle after the start handle.");
  }

  ScopedComInitialization com;
  if (!com.ok()) {
    return MakeFailure("nativeFailure",
                       "COM could not be initialized for video export.",
                       "Restart the app and try again.",
                       HResultToString(com.hr()));
  }
  ScopedMediaFoundation media_foundation;
  HRESULT hr = media_foundation.Startup();
  if (FAILED(hr)) {
    return MakeFailure("nativeFailure",
                       "Media Foundation could not be initialized for video export.",
                       "Restart the app and try again.", HResultToString(hr));
  }

  std::string failure_message = "Video export failed.";
  std::string failure_recovery = "Try a different MP4 recording.";
  std::string failure_status = "nativeFailure";
  ComPtr<IMFSourceReader> reader;
  int width = 0;
  int height = 0;
  bool has_audio = false;
  hr = CreateVideoSourceReader(input_path, &reader, &width, &height,
                               &has_audio);
  if (FAILED(hr)) {
    failure_message = "The input video could not be opened.";
    failure_recovery = "Choose an accessible MP4 recording and try again.";
  }
  if (SUCCEEDED(hr)) {
    hr = SeekSourceReader(reader.Get(), start_time);
    if (FAILED(hr)) {
      failure_message = "The input video could not seek to the trim start.";
      failure_recovery = "Choose a valid trim range and try again.";
    }
  }

  const std::wstring output_path = VideoEditOutputPath();
  ComPtr<IMFSinkWriter> writer;
  DWORD video_stream = 0;
  DWORD audio_stream = MF_SINK_WRITER_INVALID_STREAM_INDEX;
  if (SUCCEEDED(hr)) {
    hr = CreateRecordingSinkWriter(output_path, width, height, 30, 12000000,
                                   has_audio, writer.GetAddressOf(),
                                   &video_stream, &audio_stream);
    if (FAILED(hr)) {
      failure_message = "The output video writer could not be created.";
      failure_recovery = "Check disk access and try exporting again.";
      failure_status = "ioFailure";
    }
  }

  if (SUCCEEDED(hr)) {
    while (true) {
      DWORD stream_index = 0;
      DWORD flags = 0;
      LONGLONG sample_time = 0;
      ComPtr<IMFSample> sample;
      hr = reader->ReadSample(MF_SOURCE_READER_ANY_STREAM, 0, &stream_index,
                              &flags, &sample_time, &sample);
      if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
        if (FAILED(hr)) {
          failure_message = "The input video could not be decoded.";
          failure_recovery = "Try a different MP4 recording.";
        }
        break;
      }
      if (sample == nullptr) {
        continue;
      }
      if (sample_time < start_time) {
        continue;
      }
      if (sample_time >= end_time) {
        break;
      }
      sample->SetSampleTime(sample_time - start_time);
      if (stream_index == MF_SOURCE_READER_FIRST_VIDEO_STREAM) {
        hr = writer->WriteSample(video_stream, sample.Get());
      } else if (stream_index == MF_SOURCE_READER_FIRST_AUDIO_STREAM &&
                 has_audio &&
                 audio_stream != MF_SINK_WRITER_INVALID_STREAM_INDEX) {
        ApplyAudioVolume(sample.Get(), volume, muted);
        hr = writer->WriteSample(audio_stream, sample.Get());
      }
      if (FAILED(hr)) {
        failure_message = "The exported video could not be written.";
        failure_recovery = "Check disk access and try exporting again.";
        failure_status = "ioFailure";
        break;
      }
    }
  }

  if (SUCCEEDED(hr) && writer != nullptr) {
    hr = writer->Finalize();
    if (FAILED(hr)) {
      failure_message = "The exported video could not be finalized.";
      failure_recovery = "Check disk access and try exporting again.";
      failure_status = "ioFailure";
    }
  }

  EncodableValue result =
      SUCCEEDED(hr)
          ? MakeSuccess(EncodableValue(WideToUtf8(output_path)))
          : MakeFailure(failure_status, failure_message, failure_recovery,
                        HResultToString(hr));
  return result;
}

EncodableValue MakeCapabilitySnapshot() {
  const bool has_recordable_window = HasRecordableWindow();
  const CapabilityProbeResult system_audio = AudioEndpointProbe(eRender, true);
  const CapabilityProbeResult microphone = AudioEndpointProbe(eCapture, false);
  const CapabilityProbeResult camera = CameraSourceProbe();

  EncodableList permissions;
  permissions.emplace_back(
      MakePermissionPayload("screenRecording", "granted"));
  permissions.emplace_back(
      MakePermissionPayloadFromProbe("microphone", microphone));
  permissions.emplace_back(MakePermissionPayloadFromProbe("camera", camera));
  permissions.emplace_back(MakePermissionPayload(
      "accessibility", "unsupported",
      "Windows accessibility permission is not required for these features."));
  permissions.emplace_back(MakePermissionPayload("fileSystem", "granted"));
  permissions.emplace_back(
      MakePermissionPayloadFromProbe("systemAudio", system_audio));

  EncodableList features;
  features.emplace_back(MakeFeaturePayload("displayEnumeration", "available"));
  features.emplace_back(MakeFeaturePayload("windowEnumeration", "available"));
  features.emplace_back(MakeFeaturePayload("captureDisplay", "available"));
  features.emplace_back(MakeFeaturePayload("captureWindow", "available"));
  features.emplace_back(MakeFeaturePayload("captureRegion", "available"));
  features.emplace_back(MakeFeaturePayload("recordDisplay", "available"));
  features.emplace_back(MakeFeaturePayload("recordRegion", "available"));
  features.emplace_back(
      has_recordable_window
          ? MakeFeaturePayload(
                "recordWindow", "available",
                "A recordable window is available right now.")
          : MakeFeaturePayload(
                "recordWindow", "degraded",
                "No recordable window is currently available; refresh the window list or use region recording."));
  features.emplace_back(
      MakeFeaturePayload("recordSystemAudio", system_audio.status,
                         system_audio.message, "systemAudio"));
  features.emplace_back(MakeFeaturePayload(
      "recordMicrophone", microphone.status, microphone.message, "microphone"));
  features.emplace_back(MakeFeaturePayload(
      "recordCameraOverlay", camera.status, camera.message, "camera"));
  features.emplace_back(MakeFeaturePayload("imageClipboard", "available"));
  features.emplace_back(MakeFeaturePayload(
      "scrollAutomation", "degraded",
      "Automatic scrolling is validated per target at run time; guided capture is used when movement cannot be verified."));
  features.emplace_back(
      BundledOcrRuntimeExists()
          ? MakeFeaturePayload("ocr", "available")
          : MakeFeaturePayload("ocr", "requiresDependency",
                               "Install a build that bundles good_clean_shot_ocr.dll."));

  EncodableMap snapshot;
  snapshot[EncodableValue("permissions")] = EncodableValue(permissions);
  snapshot[EncodableValue("features")] = EncodableValue(features);
  return MakeSuccess(EncodableValue(snapshot));
}

EncodableValue RequestPermissionPayload(const EncodableMap* arguments) {
  const std::string type = arguments == nullptr
                               ? "screenRecording"
                               : ReadString(FindMapValue(*arguments, "type"),
                                            "screenRecording");
  if (type == "accessibility") {
    return MakeSuccess(MakePermissionPayload(
        type, "unsupported", "This Windows permission is not used."));
  }
  if (type == "microphone") {
    return MakeSuccess(MakePermissionPayloadFromProbe(
        type, AudioEndpointProbe(eCapture, false)));
  }
  if (type == "systemAudio") {
    return MakeSuccess(
        MakePermissionPayloadFromProbe(type, AudioEndpointProbe(eRender, true)));
  }
  if (type == "camera") {
    return MakeSuccess(MakePermissionPayloadFromProbe(type, CameraSourceProbe()));
  }
  return MakeSuccess(MakePermissionPayload(type, "granted"));
}

WindowsNativeBridge::WindowsNativeBridge() = default;

WindowsNativeBridge::~WindowsNativeBridge() = default;

EncodableValue WindowsNativeBridge::HandleMethodCall(
    const flutter::MethodCall<EncodableValue>& method_call) {
  const auto& method = method_call.method_name();
  const EncodableMap* arguments = ArgumentsAsMap(method_call);

  if (method == "capture.copyImageToClipboard" || method == "share.files") {
    return HandleClipboardMethod(method, arguments);
  }
  if (method.rfind("capture.", 0) == 0) {
    return HandleCaptureMethod(method, arguments);
  }
  if (method.rfind("record.", 0) == 0) {
    return HandleRecordingMethod(method, arguments);
  }
  if (method == "ocr.initialize") {
    return OcrInitializePayload(arguments);
  }
  if (method == "ocr.recognizeImage") {
    return OcrRecognizePayload(arguments);
  }
  if (method == "ocr.dispose") {
    return OcrDisposePayload();
  }
  if (method.rfind("capability.", 0) == 0) {
    return HandleCapabilityMethod(method, arguments);
  }

  return MakeFailure("unsupported",
                     "This desktop native capability is not implemented on Windows.",
                     "Use the shared Dart contract and handle unsupported responses.");
}

EncodableValue WindowsNativeBridge::HandleScrollDriverMethodCall(
    const flutter::MethodCall<EncodableValue>& method_call) {
  const auto& method = method_call.method_name();
  const EncodableMap* arguments = ArgumentsAsMap(method_call);

  if (method == "scroll.performStep") {
    return PerformScrollStepPayload(arguments);
  }

  return MakeFailure("unsupported",
                     "This scroll driver method is not implemented on Windows.",
                     "Use guided capture when automatic scrolling is unavailable.");
}
