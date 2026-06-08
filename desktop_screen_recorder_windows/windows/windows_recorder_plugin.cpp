#include "desktop_screen_recorder_windows_plugin.h"

#include "windows_native_runtime.h"

#include <flutter/standard_method_codec.h>

#include <exception>
#include <memory>
#include <string>
#include <utility>

DesktopScreenRecorderWindowsPlugin::DesktopScreenRecorderWindowsPlugin() =
    default;

DesktopScreenRecorderWindowsPlugin::~DesktopScreenRecorderWindowsPlugin() =
    default;

void DesktopScreenRecorderWindowsPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows* registrar) {
  auto plugin = std::make_unique<DesktopScreenRecorderWindowsPlugin>();
  plugin->channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          registrar->messenger(), "desktop_screen_recorder/native",
          &flutter::StandardMethodCodec::GetInstance());

  auto* plugin_pointer = plugin.get();
  plugin->channel_->SetMethodCallHandler(
      [plugin_pointer](
          const flutter::MethodCall<flutter::EncodableValue>& call,
          std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>>
              result) {
        plugin_pointer->HandleMethodCall(call, std::move(result));
      });

  registrar->AddPlugin(std::move(plugin));
}

void DesktopScreenRecorderWindowsPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
  try {
    result->Success(bridge_.HandleMethodCall(method_call));
  } catch (const std::exception& error) {
    result->Success(MakeFailure("nativeFailure", error.what(),
                                "Restart the app and try again."));
  } catch (...) {
    result->Success(MakeFailure(
        "nativeFailure",
        "An unexpected Windows native recorder error occurred.",
        "Restart the app and try again."));
  }
}

void DesktopScreenRecorderWindowsPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  DesktopScreenRecorderWindowsPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
