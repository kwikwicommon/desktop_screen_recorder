import 'package:desktop_screen_recorder_macos/desktop_screen_recorder_macos.dart';
import 'package:desktop_screen_recorder_platform_interface/desktop_screen_recorder_platform_interface.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('registerWith sets the macOS platform implementation', () {
    final previous = DesktopScreenRecorderPlatform.instance;

    try {
      DesktopScreenRecorderMacos.registerWith();
      expect(
        DesktopScreenRecorderPlatform.instance,
        isA<DesktopScreenRecorderMacos>(),
      );
    } finally {
      DesktopScreenRecorderPlatform.instance = previous;
    }
  });

  test('macOS implementation delegates through method channel', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
      expect(call.method, DesktopNativeMethod.listAudioInputs);
      return {
        'status': 'success',
        'payload': [
          {
            'id': 'mic',
            'name': 'Microphone',
            'isInput': true,
            'isAvailable': true,
          },
        ],
      };
    });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await DesktopScreenRecorderMacos().listAudioInputs();

    expect(result.isSuccess, isTrue);
    expect(result.requireValue.single.id, 'mic');
  });
}

