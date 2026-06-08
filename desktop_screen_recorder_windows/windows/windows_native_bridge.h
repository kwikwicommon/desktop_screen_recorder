#ifndef RUNNER_WINDOWS_NATIVE_BRIDGE_H_
#define RUNNER_WINDOWS_NATIVE_BRIDGE_H_

#include <flutter/encodable_value.h>
#include <flutter/method_call.h>

#include <memory>
#include <mutex>
#include <string>

class RecordingSession;

struct RecordingSessionDeleter {
  void operator()(RecordingSession* session) const;
};

class WindowsNativeBridge {
 public:
  WindowsNativeBridge();
  ~WindowsNativeBridge();

  WindowsNativeBridge(const WindowsNativeBridge&) = delete;
  WindowsNativeBridge& operator=(const WindowsNativeBridge&) = delete;

  flutter::EncodableValue HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call);

  flutter::EncodableValue HandleScrollDriverMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call);

  flutter::EncodableValue HandleVideoEditorMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call);

 private:
  flutter::EncodableValue HandleCaptureMethod(
      const std::string& method,
      const flutter::EncodableMap* arguments);

  flutter::EncodableValue HandleClipboardMethod(
      const std::string& method,
      const flutter::EncodableMap* arguments);

  flutter::EncodableValue HandleRecordingMethod(
      const std::string& method,
      const flutter::EncodableMap* arguments);

  flutter::EncodableValue HandleCapabilityMethod(
      const std::string& method,
      const flutter::EncodableMap* arguments);

  std::mutex recording_mutex_;
  bool recording_stopping_ = false;
  std::unique_ptr<RecordingSession, RecordingSessionDeleter> recording_session_;
};

#endif  // RUNNER_WINDOWS_NATIVE_BRIDGE_H_
