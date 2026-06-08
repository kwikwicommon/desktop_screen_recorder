#include "windows_native_bridge_internal.h"

EncodableValue WindowsNativeBridge::HandleCaptureMethod(
    const std::string& method,
    const EncodableMap* arguments) {
  if (method == "capture.listDisplays") {
    return ListDisplaysPayload();
  }
  if (method == "capture.listWindows") {
    return ListWindowsPayload();
  }
  if (method == "capture.captureDisplay") {
    ScopedComInitialization com;
    if (!com.ok()) {
      return MakeFailure("nativeFailure",
                         "COM could not be initialized for Windows capture.",
                         "Restart the app and try again.",
                         HResultToString(com.hr()));
    }
    return CaptureDisplayPayload(arguments);
  }
  if (method == "capture.captureWindow") {
    ScopedComInitialization com;
    if (!com.ok()) {
      return MakeFailure("nativeFailure",
                         "COM could not be initialized for Windows capture.",
                         "Restart the app and try again.",
                         HResultToString(com.hr()));
    }
    return CaptureWindowPayload(arguments);
  }
  if (method == "capture.captureRegion") {
    ScopedComInitialization com;
    if (!com.ok()) {
      return MakeFailure("nativeFailure",
                         "COM could not be initialized for Windows capture.",
                         "Restart the app and try again.",
                         HResultToString(com.hr()));
    }
    return CaptureRegionPayload(arguments);
  }
  if (method == "capture.selectRegion") {
    return SelectRegionPayload();
  }
  if (method == "capture.encodeImage") {
    ScopedComInitialization com;
    if (!com.ok()) {
      return MakeFailure("nativeFailure",
                         "COM could not be initialized for Windows image encoding.",
                         "Restart the app and try again.",
                         HResultToString(com.hr()));
    }
    return EncodeImagePayload(arguments);
  }
  return MakeFailure("unsupported",
                     "This capture method is not implemented on Windows.",
                     "Use the shared Dart contract and handle unsupported responses.");
}
