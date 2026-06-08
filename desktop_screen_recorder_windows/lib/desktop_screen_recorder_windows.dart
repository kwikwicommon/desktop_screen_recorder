library desktop_screen_recorder_windows;

import 'package:desktop_screen_recorder_platform_interface/desktop_screen_recorder_platform_interface.dart';

class DesktopScreenRecorderWindows extends MethodChannelDesktopScreenRecorder {
  DesktopScreenRecorderWindows();

  static void registerWith() {
    DesktopScreenRecorderPlatform.instance = DesktopScreenRecorderWindows();
  }
}
