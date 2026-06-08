#ifndef FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_WINDOWS_NATIVE_BRIDGE_INTERNAL_H_
#define FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_WINDOWS_NATIVE_BRIDGE_INTERNAL_H_

#include "windows_native_bridge.h"
#include "windows_native_runtime.h"

#include <dwmapi.h>
#include <flutter/encodable_value.h>
#include <flutter/method_call.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmdeviceapi.h>
#include <propsys.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using EncodableList = flutter::EncodableList;
using EncodableMap = flutter::EncodableMap;
using EncodableValue = flutter::EncodableValue;

inline constexpr LONGLONG kMediaFoundationSecond = 10000000;
inline constexpr GUID kWicContainerFormatWebp = {
    0xe094b0e2,
    0x67f2,
    0x45b3,
    {0xb0, 0xea, 0x11, 0x53, 0x37, 0xca, 0x7c, 0xf3},
};
inline constexpr int kOutputAudioSampleRate = 48000;
inline constexpr int kOutputAudioChannelCount = 2;
inline constexpr int kOutputAudioBitsPerSample = 16;
inline constexpr int kOutputAudioBytesPerFrame =
    kOutputAudioChannelCount * kOutputAudioBitsPerSample / 8;
inline constexpr int kOutputAudioBitrate = 192000;

enum class RecordingOverlayPosition {
  top_left,
  top_right,
  bottom_left,
  bottom_right,
};

enum class CameraOverlayShape {
  circle,
  rectangle,
};

struct GifConfig {
  int max_width = 960;
  int frame_rate = 12;
  bool optimize_palette = true;
};

struct CameraOverlayConfig {
  bool enabled = false;
  CameraOverlayShape shape = CameraOverlayShape::circle;
  RecordingOverlayPosition position = RecordingOverlayPosition::bottom_right;
  int size = 220;
};

struct MonitorDescriptor {
  int id = 0;
  HMONITOR handle = nullptr;
  RECT bounds = {};
  double scale_factor = 1.0;
  bool is_primary = false;
  std::string name;
};

struct BitmapFrame {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> bgra;
};

struct RegionSelectionResult {
  bool completed = false;
  RECT rect = {};
  MonitorDescriptor monitor = {};
  RECT virtual_bounds = {};
};

struct RecordingConfig {
  std::string mode = "fullscreen";
  std::string format = "mp4";
  int frame_rate = 30;
  int video_bitrate = 8000000;
  int quality_max_height = 0;
  bool capture_audio = false;
  bool capture_microphone = false;
  bool show_cursor = true;
  bool highlight_clicks = false;
  bool show_keystrokes = false;
  int64_t display_id = 0;
  int64_t window_id = 0;
  bool has_region = false;
  RECT region = {};
  GifConfig gif = {};
  CameraOverlayConfig camera_overlay = {};
};

struct CapabilityProbeResult {
  bool available = false;
  std::string status = "notConfigured";
  std::string message;
};

bool TryGetBgraBufferSize(int width, int height, size_t* buffer_size);
bool TryGetBgraBufferSize(UINT width, UINT height, size_t* buffer_size);
bool TryGetPackedImageBufferSize(UINT width,
                                 UINT height,
                                 UINT bytes_per_pixel,
                                 size_t* stride,
                                 size_t* buffer_size);
std::string WideToUtf8(const std::wstring& value);
std::string WidePtrToUtf8(const wchar_t* value);
std::wstring Utf8ToWide(const std::string& value);
std::wstring GoodCleanShotTempDirectory(const wchar_t* child_directory);

const EncodableValue* FindMapValue(const EncodableMap& map, const char* key);
const EncodableMap* ReadMap(const EncodableMap& map, const char* key);
int64_t ReadInt(const EncodableValue* value, int64_t fallback = 0);
int ReadInt32(const EncodableValue* value, int fallback = 0);
double ReadDouble(const EncodableValue* value, double fallback = 0.0);
bool ReadBool(const EncodableValue* value, bool fallback = false);
std::string ReadString(const EncodableValue* value,
                       const std::string& fallback = "");
const EncodableMap* ArgumentsAsMap(
    const flutter::MethodCall<EncodableValue>& method_call);
bool ReadPhysicalRect(const EncodableMap& map, const char* key, RECT* rect);

EncodableValue MakeBoundsPayload(const RECT& rect);

std::vector<MonitorDescriptor> EnumerateMonitors();
const MonitorDescriptor* FindMonitorById(
    const std::vector<MonitorDescriptor>& monitors,
    int64_t display_id);
int DisplayIdForRect(const RECT& rect,
                     const std::vector<MonitorDescriptor>& monitors);
RECT VirtualScreenBounds();
bool IntersectRects(const RECT& first, const RECT& second, RECT* output);
bool WindowVisibleCaptureBounds(HWND window, RECT* bounds);
std::string WindowRetargetRecovery();

bool CaptureRectBgra(const RECT& rect, BitmapFrame* frame, std::string* error);
bool CaptureWindowBgra(HWND window, BitmapFrame* frame, std::string* error);
HRESULT EncodePng(const BitmapFrame& frame, std::vector<uint8_t>* bytes);
EncodableValue MakeImagePayload(const BitmapFrame& frame,
                                int64_t display_id,
                                int64_t window_id,
                                double scale_factor);
EncodableValue EncodeImagePayload(const EncodableMap* arguments);
EncodableValue CopyPngToClipboard(const EncodableMap& arguments);
EncodableValue ListDisplaysPayload();
EncodableValue ListWindowsPayload();
EncodableValue SelectRegionPayload();
EncodableValue CaptureDisplayPayload(const EncodableMap* arguments);
EncodableValue CaptureRegionPayload(const EncodableMap* arguments);
EncodableValue CaptureWindowPayload(const EncodableMap* arguments);

EncodableValue PerformScrollStepPayload(const EncodableMap* arguments);

EncodableValue ListAudioDevices(bool input);
CapabilityProbeResult AudioEndpointProbe(EDataFlow data_flow, bool loopback);

bool ResolveRecordingRect(const RecordingConfig& config,
                          RECT* output,
                          std::string* error);
RecordingConfig ReadRecordingConfig(const EncodableMap* arguments);
std::wstring RecordingOutputPath(const std::string& format);
int64_t FileSizeForPath(const std::wstring& path);
HRESULT CreateRecordingSinkWriter(const std::wstring& path,
                                  int width,
                                  int height,
                                  int frame_rate,
                                  int bitrate,
                                  bool include_audio,
                                  IMFSinkWriter** writer,
                                  DWORD* video_stream_index,
                                  DWORD* audio_stream_index);
HRESULT WriteVideoFrame(IMFSinkWriter* writer,
                        DWORD stream_index,
                        const BitmapFrame& frame,
                        LONGLONG sample_time,
                        LONGLONG sample_duration);
HRESULT WriteAudioSample(IMFSinkWriter* writer,
                         DWORD stream_index,
                         const std::vector<int16_t>& frames,
                         int64_t start_frame);
int EvenDimension(double value);
BitmapFrame ResizeFrameToDimensions(const BitmapFrame& frame,
                                    int target_width,
                                    int target_height);
BitmapFrame ResizeFrameToMaxWidth(const BitmapFrame& frame, int max_width);

EncodableValue VideoPreviewFramePayload(const EncodableMap* arguments);
EncodableValue ExportVideoPayload(const EncodableMap* arguments);
EncodableValue MakeCapabilitySnapshot();
EncodableValue RequestPermissionPayload(const EncodableMap* arguments);

#endif  // FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_WINDOWS_NATIVE_BRIDGE_INTERNAL_H_
