#ifndef FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_WINDOWS_PLUGIN_H_
#define FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_WINDOWS_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>

#include <memory>

#include "windows_native_bridge.h"

class DesktopScreenRecorderWindowsPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows* registrar);

  DesktopScreenRecorderWindowsPlugin();
  ~DesktopScreenRecorderWindowsPlugin() override;

  DesktopScreenRecorderWindowsPlugin(
      const DesktopScreenRecorderWindowsPlugin&) = delete;
  DesktopScreenRecorderWindowsPlugin& operator=(
      const DesktopScreenRecorderWindowsPlugin&) = delete;

 private:
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue>& method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  WindowsNativeBridge bridge_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;
};

void DesktopScreenRecorderWindowsPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar);

#endif  // FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_WINDOWS_PLUGIN_H_
