import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';
import 'package:flutter/material.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final recorder = DesktopScreenRecorder.instance;
  final startResult = await recorder.start(
    const RecordConfig(
      mode: RecordMode.fullscreen,
      format: RecordFormat.mp4,
      showCursor: true,
    ),
  );
  if (startResult.isFailure) {
    debugPrint(startResult.requireFailure.toString());
  }
}
