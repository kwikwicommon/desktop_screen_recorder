#include "windows_native_bridge_internal.h"

EncodableValue WindowsNativeBridge::HandleCapabilityMethod(
    const std::string& method,
    const EncodableMap* arguments) {
  if (method == "capability.queryPermissions" ||
      method == "capability.queryCapabilities" ||
      method == "capability.queryFeatureAvailability") {
    return MakeCapabilitySnapshot();
  }
  if (method == "capability.requestPermission") {
    return RequestPermissionPayload(arguments);
  }
  return MakeFailure("unsupported",
                     "This capability method is not implemented on Windows.",
                     "Use the shared Dart contract and handle unsupported responses.");
}
