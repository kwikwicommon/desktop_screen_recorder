import 'dart:async';

import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('record service delegates start pause resume and stop to platform', () async {
    final platform = _FakePlatform();
    final service = RecordServiceImpl(
      platform: platform,
      isSupportedPlatform: () => true,
    );

    expect((await service.start(const RecordConfig())).isSuccess, isTrue);
    expect((await service.pause()).isSuccess, isTrue);
    expect((await service.resume()).isSuccess, isTrue);
    final stopped = await service.stop();

    expect(stopped.isSuccess, isTrue);
    expect(stopped.requireValue.filePath, '/tmp/recording.mp4');
    expect(platform.calls, ['start', 'pause', 'resume', 'stop']);
  });

  test('record service rejects gif audio before platform call', () async {
    final platform = _FakePlatform();
    final service = RecordServiceImpl(
      platform: platform,
      isSupportedPlatform: () => true,
    );

    final result = await service.start(
      const RecordConfig(format: RecordFormat.gif, captureAudio: true),
    );

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.invalidInput);
    expect(platform.calls, isEmpty);
  });

  test('record service rejects window mode without window id', () async {
    final platform = _FakePlatform();
    final service = RecordServiceImpl(
      platform: platform,
      isSupportedPlatform: () => true,
    );

    final result = await service.start(
      const RecordConfig(mode: RecordMode.window),
    );

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.invalidInput);
    expect(platform.calls, isEmpty);
  });

  test('record service rejects overlapping starts', () async {
    final startGate = Completer<void>();
    final platform = _FakePlatform(startGate: startGate);
    final service = RecordServiceImpl(
      platform: platform,
      isSupportedPlatform: () => true,
    );

    final firstStart = service.start(const RecordConfig());
    await platform.startCalled.future;

    final secondStart = await service.start(const RecordConfig());
    startGate.complete();
    final firstResult = await firstStart;

    expect(firstResult.isSuccess, isTrue);
    expect(secondStart.isFailure, isTrue);
    expect(secondStart.requireFailure.code, AppFailureCode.invalidInput);
    expect(platform.calls, ['start']);
  });

  test('record service rejects region mode without valid region', () async {
    final platform = _FakePlatform();
    final service = RecordServiceImpl(
      platform: platform,
      isSupportedPlatform: () => true,
    );

    final result = await service.start(
      const RecordConfig(mode: RecordMode.region),
    );

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.invalidInput);
    expect(platform.calls, isEmpty);
  });

  test('record service converts platform exceptions to failures', () async {
    final platform = _FakePlatform(throwOnStart: true);
    final service = RecordServiceImpl(
      platform: platform,
      isSupportedPlatform: () => true,
    );

    final result = await service.start(const RecordConfig());

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.nativeFailure);
    expect(service.lastFailure, same(result.requireFailure));
  });

  test('dispose resets state and requests native stop', () async {
    final platform = _FakePlatform();
    final service = RecordServiceImpl(
      platform: platform,
      isSupportedPlatform: () => true,
    );

    expect((await service.start(const RecordConfig())).isSuccess, isTrue);
    service.dispose();
    await platform.stopCalled.future;

    expect(service.isRecording, isFalse);
    expect(platform.calls, ['start', 'stop']);
    final restart = await service.start(const RecordConfig());
    expect(restart.isFailure, isTrue);
    expect(restart.requireFailure.code, AppFailureCode.unavailable);
  });
}

class _FakePlatform extends DesktopScreenRecorderPlatform {
  final List<String> calls = [];
  final Completer<void>? startGate;
  final Completer<void> startCalled = Completer<void>();
  final Completer<void> stopCalled = Completer<void>();
  final bool throwOnStart;

  _FakePlatform({
    this.startGate,
    this.throwOnStart = false,
  });

  @override
  Future<AppResult<void>> startRecording(RecordConfig config) async {
    calls.add('start');
    if (!startCalled.isCompleted) {
      startCalled.complete();
    }
    final gate = startGate;
    if (gate != null) {
      await gate.future;
    }
    if (throwOnStart) {
      throw StateError('Native bridge failed.');
    }
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
    if (!stopCalled.isCompleted) {
      stopCalled.complete();
    }
    return const AppResult.success(
      RecordResult(
        filePath: '/tmp/recording.mp4',
        duration: Duration(seconds: 1),
        format: RecordFormat.mp4,
        resolution: Size(1280, 720),
        fileSizeBytes: 1000,
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
