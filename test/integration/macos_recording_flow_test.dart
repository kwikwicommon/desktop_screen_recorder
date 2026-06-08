import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('macOS recording flow contract', () async {
    final native = _FakePlatform(format: 'mp4');
    final service = RecordServiceImpl(
      platform: native,
      isSupportedPlatform: () => true,
    );

    expect(
      (await service.start(const RecordConfig())).isSuccess,
      isTrue,
    );
    expect((await service.pause()).isSuccess, isTrue);
    expect((await service.resume()).isSuccess, isTrue);

    final stop = await service.stop();
    expect(stop.isSuccess, isTrue);
    expect(stop.requireValue.filePath, endsWith('.mp4'));
    expect(native.calls, ['start', 'pause', 'resume', 'stop']);
  });
}

class _FakePlatform extends DesktopScreenRecorderPlatform {
  final String format;
  final List<String> calls = [];

  _FakePlatform({required this.format});

  @override
  Future<AppResult<void>> startRecording(RecordConfig config) async {
    calls.add('start');
    return const AppResult.success();
  }

  @override
  Future<AppResult<void>> pauseRecording() async {
    calls.add('pause');
    return const AppResult.success();
  }

  @override
  Future<AppResult<void>> resumeRecording() async {
    calls.add('resume');
    return const AppResult.success();
  }

  @override
  Future<AppResult<RecordResult>> stopRecording() async {
    calls.add('stop');
    return AppResult.success(
      RecordResult(
        filePath: '/tmp/test.$format',
        duration: const Duration(seconds: 1),
        format: RecordFormat.mp4,
        resolution: const Size(1280, 720),
        fileSizeBytes: 1024,
      ),
    );
  }

  @override
  Future<AppResult<List<AudioDevice>>> listAudioInputs() async {
    return const AppResult.success(<AudioDevice>[]);
  }

  @override
  Future<AppResult<List<AudioDevice>>> listAudioOutputs() async {
    return const AppResult.success(<AudioDevice>[]);
  }

  @override
  Future<AppResult<DesktopCapabilitySnapshot>> queryCapabilities() async {
    return AppResult.success(_availableCapabilities());
  }

  @override
  Future<AppResult<DesktopPermissionInfo>> requestPermission(
    DesktopPermissionType permissionType,
  ) async {
    return AppResult.success(
      DesktopPermissionInfo(
        type: permissionType,
        state: DesktopPermissionState.granted,
      ),
    );
  }
}

DesktopCapabilitySnapshot _availableCapabilities() {
  return DesktopCapabilitySnapshot(
    permissions: const {},
    features: {
      for (final feature in DesktopFeatureKey.values)
        feature: DesktopFeatureAvailability(
          feature: feature,
          status: DesktopFeatureAvailabilityStatus.available,
        ),
    },
  );
}
