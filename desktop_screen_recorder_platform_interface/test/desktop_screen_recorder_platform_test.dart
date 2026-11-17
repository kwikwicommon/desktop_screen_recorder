
import 'package:desktop_screen_recorder_platform_interface/desktop_screen_recorder_platform_interface.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('default instance uses the method channel implementation', () {
    expect(
      DesktopScreenRecorderPlatform.instance,
      isA<MethodChannelDesktopScreenRecorder>(),
    );
  });

  test('token verification rejects an invalid platform implementation', () {
    expect(
      () => DesktopScreenRecorderPlatform.instance = _InvalidPlatform(),
      throwsA(isA<AssertionError>()),
    );
  });

  test('mock platform can replace instance', () {
    final previous = DesktopScreenRecorderPlatform.instance;
    final fake = _FakePlatform();

    try {
      DesktopScreenRecorderPlatform.instance = fake;
      expect(DesktopScreenRecorderPlatform.instance, same(fake));
    } finally {
      DesktopScreenRecorderPlatform.instance = previous;
    }
  });

  test('method channel parses successful stop payloads', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    final platform = MethodChannelDesktopScreenRecorder(channel: channel);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          expect(call.method, DesktopNativeMethod.stopRecording);
          return {
            'status': 'success',
            'payload': {
              'filePath': '/tmp/test.mp4',
              'durationMs': 1500,
              'format': 'mp4',
              'width': 1280,
              'height': 720,
              'fileSizeBytes': 2048,
            },
          };
        });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await platform.stopRecording();

    expect(result.isSuccess, isTrue);
    expect(result.requireValue.filePath, '/tmp/test.mp4');
    expect(result.requireValue.duration, const Duration(milliseconds: 1500));
  });

  test('method channel parses list payloads with typed map entries', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    final platform = MethodChannelDesktopScreenRecorder(channel: channel);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          expect(call.method, DesktopNativeMethod.listAudioOutputs);
          return <String, Object?>{
            'status': 'success',
            'payload': <Object?>[
              <String, Object?>{
                'id': 'speaker',
                'name': 'Speaker',
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

    final result = await platform.listAudioOutputs();

    expect(result.isSuccess, isTrue);
    expect(result.requireValue.single.id, 'speaker');
    expect(result.requireValue.single.isInput, isFalse);
  });

  test('method channel parses byte list thumbnails', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    final platform = MethodChannelDesktopScreenRecorder(channel: channel);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          return {
            'status': 'success',
            'payload': {
              'filePath': '/tmp/test.mp4',
              'durationMs': 1500,
              'format': 'mp4',
              'width': 1280,
              'height': 720,
              'fileSizeBytes': 2048,
              'thumbnail': <int>[1, 2, 3],
            },
          };
        });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await platform.stopRecording();

    expect(result.isSuccess, isTrue);
    expect(result.requireValue.thumbnail, Uint8List.fromList([1, 2, 3]));
  });

  test('method channel maps failure envelopes to AppFailure', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    final platform = MethodChannelDesktopScreenRecorder(channel: channel);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          return {
            'status': 'permissionDenied',
            'failure': {
              'message': 'Screen recording permission is denied.',
              'recoverySuggestion': 'Grant screen recording permission.',
              'domain': 'permission',
              'recoverability': 'userActionRequired',
            },
          };
        });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await platform.startRecording(const RecordConfig());

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.permissionDenied);
    expect(result.requireFailure.message, contains('permission'));
    expect(result.requireFailure.recoverySuggestion, isNotEmpty);
  });

  test('method channel reports malformed payloads', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    final platform = MethodChannelDesktopScreenRecorder(channel: channel);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          return {'status': 'success', 'payload': 'not-a-map'};
        });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await platform.stopRecording();

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.serialization);
  });

  test('method channel reports malformed envelopes', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    final platform = MethodChannelDesktopScreenRecorder(channel: channel);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          return 'not-an-envelope';
        });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await platform.startRecording(const RecordConfig());

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.serialization);
  });

  test('method channel maps restart-required failure envelopes', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    final platform = MethodChannelDesktopScreenRecorder(channel: channel);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          return {
            'status': 'permissionDenied',
            'failure': {
              'message': 'Permission requires app restart.',
              'recoverySuggestion': 'Restart the app.',
              'requiresRestart': true,
            },
          };
        });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await platform.startRecording(const RecordConfig());

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.requiresRestart);
  });

  test('method channel invokes the capability query method', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    final platform = MethodChannelDesktopScreenRecorder(channel: channel);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          expect(call.method, DesktopNativeMethod.queryCapabilities);
          return {
            'status': 'success',
            'payload': {'permissions': <Object?>[], 'features': <Object?>[]},
          };
        });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await platform.queryCapabilities();

    expect(result.isSuccess, isTrue);
  });

  test('method channel rejects unknown capability features', () async {
    const channel = MethodChannel(
      MethodChannelDesktopScreenRecorder.channelName,
    );
    final platform = MethodChannelDesktopScreenRecorder(channel: channel);
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
          return {
            'status': 'success',
            'payload': {
              'permissions': <Object?>[],
              'features': [
                {'feature': 'recordDisplayExperimental', 'status': 'available'},
              ],
            },
          };
        });
    addTearDown(() {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(channel, null);
    });

    final result = await platform.queryCapabilities();

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.serialization);
  });
}

class _FakePlatform extends DesktopScreenRecorderPlatform {}

class _InvalidPlatform implements DesktopScreenRecorderPlatform {
  @override
  dynamic noSuchMethod(Invocation invocation) => super.noSuchMethod(invocation);
}
