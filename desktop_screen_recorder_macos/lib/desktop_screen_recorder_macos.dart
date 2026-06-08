library desktop_screen_recorder_macos;

import 'package:desktop_screen_recorder_platform_interface/desktop_screen_recorder_platform_interface.dart';

class DesktopScreenRecorderMacos extends MethodChannelDesktopScreenRecorder {
  DesktopScreenRecorderMacos();

  static void registerWith() {
    DesktopScreenRecorderPlatform.instance = DesktopScreenRecorderMacos();
  }
}
