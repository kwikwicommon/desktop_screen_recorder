#include "desktop_screen_recorder_windows_plugin_c_api.h"

#include "desktop_screen_recorder_windows_plugin.h"

void DesktopScreenRecorderWindowsPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  DesktopScreenRecorderWindowsPluginRegisterWithRegistrar(registrar);
}
