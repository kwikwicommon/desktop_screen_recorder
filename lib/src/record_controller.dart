import 'dart:async';

import 'package:flutter/material.dart';

import 'models.dart';

enum RecordStatus { idle, preparing, recording, paused, stopping }

class RecordState {
  final RecordStatus status;
  final RecordConfig config;
  final Duration duration;
  final RecordResult? lastResult;
  final String? errorMessage;

  const RecordState({
    this.status = RecordStatus.idle,
    this.config = const RecordConfig(),
    this.duration = Duration.zero,
    this.lastResult,
    this.errorMessage,
  });

  bool get isRecording =>
      status == RecordStatus.recording || status == RecordStatus.paused;

  RecordState copyWith({
    RecordStatus? status,
    RecordConfig? config,
    Duration? duration,
    RecordResult? lastResult,
    String? errorMessage,
    bool clearError = false,
    bool clearResult = false,
  }) {
    return RecordState(
      status: status ?? this.status,
      config: config ?? this.config,
      duration: duration ?? this.duration,
      lastResult: clearResult ? null : (lastResult ?? this.lastResult),
      errorMessage: clearError ? null : (errorMessage ?? this.errorMessage),
    );
  }
}

class RecordController extends ChangeNotifier {
  RecordController() {
    for (final value in _values) {
      value.addListener(notifyListeners);
    }
  }

  late final List<ChangeNotifier> _values = [
    status,
    config,
    duration,
    lastResult,
    errorMessage,
  ];

  final status = ValueNotifier(RecordStatus.idle);
  final config = ValueNotifier(const RecordConfig());
  final duration = ValueNotifier(Duration.zero);
  final lastResult = ValueNotifier<RecordResult?>(null);
  final errorMessage = ValueNotifier<String?>(null);

  Timer? _durationTimer;

  bool get isRecording =>
      status.value == RecordStatus.recording ||
      status.value == RecordStatus.paused;

  @override
  void dispose() {
    _durationTimer?.cancel();

    for (final value in _values) {
      value.dispose();
    }
    super.dispose();
  }

  void updateConfig(RecordConfig newConfig) {
    config.value = _normalizeConfig(newConfig);
  }

  void setMode(RecordMode mode) {
    final current = config.value;
    switch (mode) {
      case RecordMode.region:
        config.value = current.copyWith(mode: mode, clearWindow: true);
        break;
      case RecordMode.fullscreen:
        config.value = current.copyWith(
          mode: mode,
          clearRegion: true,
          clearDisplay: true,
          clearWindow: true,
        );
        break;
      case RecordMode.window:
        config.value = current.copyWith(
          mode: mode,
          clearRegion: true,
          clearDisplay: true,
        );
        break;
    }
  }

  void setFormat(RecordFormat format) {
    config.value = _normalizeConfig(config.value.copyWith(format: format));
  }

  void setFrameRate(int fps) {
    if (fps <= 0) return;
    config.value = config.value.copyWith(frameRate: fps);
  }

  void setQuality(RecordQuality quality) {
    config.value = config.value.copyWith(quality: quality);
  }

  void toggleAudioCapture() {
    if (config.value.format == RecordFormat.gif) return;
    config.value = config.value.copyWith(
      captureAudio: !config.value.captureAudio,
    );
  }

  void toggleMicrophoneCapture() {
    if (config.value.format == RecordFormat.gif) return;
    config.value = config.value.copyWith(
      captureMicrophone: !config.value.captureMicrophone,
    );
  }

  void toggleShowCursor() {
    config.value = config.value.copyWith(showCursor: !config.value.showCursor);
  }

  void toggleHighlightClicks() {
    config.value = config.value.copyWith(
      highlightClicks: !config.value.highlightClicks,
    );
  }

  void toggleShowKeystrokes() {
    config.value = config.value.copyWith(
      showKeystrokes: !config.value.showKeystrokes,
    );
  }

  void setRegion(Rect region) {
    if (!_isUsableRegion(region)) return;
    config.value = config.value.copyWith(
      mode: RecordMode.region,
      region: region,
      clearWindow: true,
    );
  }

  void clearRegion() {
    config.value = config.value.copyWith(clearRegion: true, clearDisplay: true);
  }

  void setWindowTarget({required int windowId, required String title}) {
    if (windowId <= 0) return;
    config.value = config.value.copyWith(
      windowId: windowId,
      windowTitle: title,
      mode: RecordMode.window,
      clearRegion: true,
      clearDisplay: true,
    );
  }

  void clearWindowTarget() {
    config.value = config.value.copyWith(clearWindow: true);
  }

  void updateGifConfig(GifTranscodeConfig gif) {
    final current = config.value.gif;
    config.value = config.value.copyWith(
      gif: GifTranscodeConfig(
        maxWidth: gif.maxWidth > 0 ? gif.maxWidth : current.maxWidth,
        frameRate: gif.frameRate > 0 ? gif.frameRate : current.frameRate,
        optimizePalette: gif.optimizePalette,
      ),
    );
  }

  void updateCameraOverlay(CameraOverlayConfig cameraOverlay) {
    config.value = config.value.copyWith(
      cameraOverlay: cameraOverlay.copyWith(
        size: cameraOverlay.size > 0
            ? cameraOverlay.size
            : config.value.cameraOverlay.size,
      ),
    );
  }

  void prepareRecording() {
    _durationTimer?.cancel();
    status.value = RecordStatus.preparing;
    duration.value = Duration.zero;
    errorMessage.value = null;
    lastResult.value = null;
  }

  void startRecording() {
    status.value = RecordStatus.recording;
    duration.value = Duration.zero;
    errorMessage.value = null;
    lastResult.value = null;
    _startDurationTimer();
  }

  void pauseRecording() {
    _durationTimer?.cancel();
    status.value = RecordStatus.paused;
  }

  void resumeRecording() {
    status.value = RecordStatus.recording;
    _startDurationTimer();
  }

  void markStopping() {
    _durationTimer?.cancel();
    status.value = RecordStatus.stopping;
  }

  void beginStopping() {
    markStopping();
  }

  void stopRecording(RecordResult? result) {
    _durationTimer?.cancel();
    status.value = RecordStatus.idle;
    duration.value = result?.duration ?? duration.value;
    lastResult.value = result;
  }

  void setError(String message) {
    status.value = RecordStatus.idle;
    errorMessage.value = message;
    _durationTimer?.cancel();
  }

  void _startDurationTimer() {
    _durationTimer?.cancel();
    _durationTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      duration.value = duration.value + const Duration(seconds: 1);
    });
  }

  RecordConfig _normalizeConfig(RecordConfig value) {
    var next = value;
    if (next.frameRate <= 0) {
      next = next.copyWith(frameRate: const RecordConfig().frameRate);
    }
    if (next.format == RecordFormat.gif &&
        (next.captureAudio || next.captureMicrophone)) {
      next = next.copyWith(captureAudio: false, captureMicrophone: false);
    }
    switch (next.mode) {
      case RecordMode.region:
        next = next.copyWith(clearWindow: true);
        break;
      case RecordMode.fullscreen:
        next = next.copyWith(
          clearRegion: true,
          clearDisplay: true,
          clearWindow: true,
        );
        break;
      case RecordMode.window:
        next = next.copyWith(clearRegion: true, clearDisplay: true);
        break;
    }
    return next;
  }

  bool _isUsableRegion(Rect region) {
    return region.left.isFinite &&
        region.top.isFinite &&
        region.width.isFinite &&
        region.height.isFinite &&
        region.width > 0 &&
        region.height > 0;
  }
}
