#include "windows_native_bridge_internal.h"

#include <shellapi.h>

EncodableValue WindowsNativeBridge::HandleClipboardMethod(
    const std::string& method,
    const EncodableMap* arguments) {
  if (method == "capture.copyImageToClipboard") {
    ScopedComInitialization com;
    if (!com.ok()) {
      return MakeFailure("nativeFailure",
                         "COM could not be initialized for Windows clipboard image copy.",
                         "Restart the app and try again.",
                         HResultToString(com.hr()));
    }
    return arguments == nullptr
               ? MakeFailure("invalidInput",
                             "Image bytes are required for clipboard copy.",
                             "Capture an image and try again.")
               : CopyPngToClipboard(*arguments);
  }
  if (method == "share.files") {
    if (arguments == nullptr) {
      return MakeFailure("invalidInput",
                         "File paths are required for system sharing.",
                         "Save the asset first and try again.");
    }
    const auto* raw_paths = FindMapValue(*arguments, "paths");
    const auto* paths = raw_paths == nullptr
                            ? nullptr
                            : std::get_if<EncodableList>(raw_paths);
    if (paths == nullptr || paths->empty()) {
      return MakeFailure("invalidInput",
                         "No files were provided for system sharing.",
                         "Save the asset first and try again.");
    }
    std::vector<std::wstring> wide_paths;
    for (const auto& item : *paths) {
      const std::string path = ReadString(&item);
      if (path.empty()) {
        return MakeFailure("invalidInput",
                           "A shared file path is empty.",
                           "Save the asset first and try again.");
      }
      const std::wstring wide_path = Utf8ToWide(path);
      if (GetFileAttributesW(wide_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return MakeFailure("ioFailure",
                           "A shared file could not be found.",
                           path);
      }
      wide_paths.push_back(wide_path);
    }

    ScopedComInitialization com(COINIT_APARTMENTTHREADED |
                                COINIT_DISABLE_OLE1DDE);
    if (FAILED(com.hr())) {
      return MakeFailure("nativeFailure",
                         "COM could not be initialized for Windows sharing.",
                         "Restart the app and try again.",
                         HResultToString(com.hr()));
    }

    for (const auto& wide_path : wide_paths) {
      HINSTANCE launched = ShellExecuteW(nullptr, L"share", wide_path.c_str(),
                                         nullptr, nullptr, SW_SHOWNORMAL);
      if (reinterpret_cast<intptr_t>(launched) <= 32) {
        return MakeFailure("nativeFailure",
                           "The Windows share sheet could not be opened.",
                           "Copy the file path or reveal the file instead.");
      }
    }
    return MakeSuccess();
  }
  return MakeFailure("unsupported",
                     "This clipboard method is not implemented on Windows.",
                     "Use the shared Dart contract and handle unsupported responses.");
}
