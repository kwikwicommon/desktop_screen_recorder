import 'package:desktop_screen_recorder_platform_interface/desktop_screen_recorder_platform_interface.dart';
import 'package:desktop_screen_recorder_windows/desktop_screen_recorder_windows.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('registerWith sets the Windows platform implementation', () {
    final previous = DesktopScreenRecorderPlatform.instance;

    try {
      DesktopScreenRecorderWindows.registerWith();
      expect(
        DesktopScreenRecorderPlatform.instance,
        isA<DesktopScreenRecorderWindows>(),
      );
    } finally {
      DesktopScreenRecorderPlatform.instance = previous;
    }
  });

  test('Windows implementation delegates through method channel', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
      expect(call.method, DesktopNativeMethod.listAudioOutputs);
      return {
        'status': 'success',
        'payload': [
          {
            'id': 'loopback',
            'name': 'System Audio',
            'isInput': false,
            'isAvailable': true,
          },
        ],
      };
    });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await DesktopScreenRecorderWindows().listAudioOutputs();

    expect(result.isSuccess, isTrue);
    expect(result.requireValue.single.id, 'loopback');
  });
}

