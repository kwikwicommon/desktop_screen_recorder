#include "windows_native_bridge_internal.h"

EncodableValue WindowsNativeBridge::HandleVideoEditorMethodCall(
    const flutter::MethodCall<EncodableValue>& method_call) {
  const auto& method = method_call.method_name();
  const EncodableMap* arguments = ArgumentsAsMap(method_call);

  if (method == "getPreviewFrame") {
    return VideoPreviewFramePayload(arguments);
  }
  if (method == "exportVideo") {
    return ExportVideoPayload(arguments);
  }
  if (method == "play" || method == "pause" || method == "seek" ||
      method == "setVolume") {
    return MakeFailure(
        "unsupported",
        "Windows video playback controls are not implemented.",
        "Use static preview frame seeking and export trimming on Windows.");
  }
  if (method == "getPosition") {
    return MakeFailure(
        "unsupported",
        "Windows video playback position is not available.",
        "Use static preview frame seeking and export trimming on Windows.");
  }

  return MakeFailure("unsupported",
                     "This video editor method is not implemented on Windows.",
                     "Use the shared Dart contract and handle unsupported responses.");
}
