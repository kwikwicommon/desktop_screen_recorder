import 'dart:async';
import 'dart:io';
import 'dart:ui' show Rect;

import 'package:desktop_screen_recorder_platform_interface/desktop_screen_recorder_platform_interface.dart'
    show DesktopScreenRecorderPlatform;

import 'app_error.dart';
import 'capability_validator.dart';
import 'messages.dart';
import 'models.dart';
import 'native_contract.dart';
import 'record_service.dart';
import 'recording_state_tracker.dart';

class RecordServiceImpl implements RecordService {
  final DesktopScreenRecorderPlatform platform;
  final RecordingCapabilityValidator capabilityValidator;
  final RecordErrorMessages messages;
  final bool Function() isSupportedPlatform;
  final RecordingStateTracker _stateTracker;

  AppFailure? _lastFailure;
  _RecordingOperation? _activeOperation;
  Completer<void>? _activeOperationDone;
  bool _isDisposed = false;

  RecordServiceImpl({
    DesktopScreenRecorderPlatform? platform,
    RecordingCapabilityValidator? capabilityValidator,
    RecordingStateTracker? stateTracker,
    this.messages = defaultRecordErrorMessages,
    bool Function()? isSupportedPlatform,
  }) : platform = platform ?? DesktopScreenRecorderPlatform.instance,
       capabilityValidator =
           capabilityValidator ??
           RecordingCapabilityValidator(
             platform: platform ?? DesktopScreenRecorderPlatform.instance,
             messages: messages,
           ),
       isSupportedPlatform =
           isSupportedPlatform ??
           (() => Platform.isMacOS || Platform.isWindows),
       _stateTracker = stateTracker ?? RecordingStateTracker();

  @override
  bool get isRecording => _stateTracker.isRecording;

  @override
  bool get isPaused => _stateTracker.isPaused;

  @override
  Duration get currentDuration => _stateTracker.currentDuration;

  @override
  String? get lastError => _lastFailure?.message;

  @override
  AppFailure? get lastFailure => _lastFailure;

  @override
  Future<AppResult<void>> start(RecordConfig config) async {
    final disposed = _ensureNotDisposed<void>();
    if (disposed != null) return disposed;

    if (!isSupportedPlatform()) {
      return _failResult(
        AppFailureCode.unsupportedPlatform,
        messages.unsupportedPlatform,
        recoverySuggestion: messages.useSupportedDesktopBuild,
      );
    }

    final configFailure = _validateStartConfig(config);
    if (configFailure != null) return configFailure;

    final busy = _beginOperation<void>(
      _RecordingOperation.start,
      busyCode: AppFailureCode.invalidInput,
      busyMessage: messages.alreadyRecording,
    );
    if (busy != null) return busy;

    try {
      if (_stateTracker.isRecording) {
        return _failResult(
          AppFailureCode.invalidInput,
          messages.alreadyRecording,
        );
      }

      final supported = await capabilityValidator.validate(config);
      if (_isDisposed) return _disposedResult();
      if (supported.isFailure) {
        _lastFailure = supported.requireFailure;
        return supported;
      }

      final nativeStart = await platform.startRecording(config);
      if (_isDisposed) {
        if (nativeStart.isSuccess) {
          unawaited(_stopNativeOnDispose());
        }
        return _disposedResult();
      }
      if (nativeStart.isFailure) {
        _lastFailure = nativeStart.requireFailure;
        return AppResult.failure(nativeStart.requireFailure);
      }

      _stateTracker.start();
      _lastFailure = null;
      return const AppResult.success();
    } catch (error, stackTrace) {
      return _failResult(
        AppFailureCode.nativeFailure,
        messages.nativeBridgeInvocationFailed,
        recoverySuggestion: messages.inspectPlatformChannelHandler,
        cause: error,
        stackTrace: stackTrace,
      );
    } finally {
      _endOperation(_RecordingOperation.start);
    }
  }

  AppResult<void>? _validateStartConfig(RecordConfig config) {
    if (config.frameRate <= 0) {
      return _failResult(
        AppFailureCode.invalidInput,
        messages.invalidFrameRate,
        recoverySuggestion: messages.updateConfigAndCheck,
      );
    }

    if (config.mode == RecordMode.region && !_isValidRegion(config.region)) {
      return _failResult(
        AppFailureCode.invalidInput,
        messages.regionRequired,
        recoverySuggestion: messages.updateConfigAndCheck,
      );
    }

    if (config.mode == RecordMode.window && (config.windowId ?? 0) <= 0) {
      return _failResult(
        AppFailureCode.invalidInput,
        messages.windowRequired,
        recoverySuggestion: messages.updateConfigAndCheck,
      );
    }

    if (config.format == RecordFormat.gif &&
        (config.captureAudio || config.captureMicrophone)) {
      return _failResult(
        AppFailureCode.invalidInput,
        messages.gifAudioUnsupported,
        recoverySuggestion: messages.updateConfigAndCheck,
      );
    }

    if (config.format == RecordFormat.gif &&
        (config.gif.maxWidth <= 0 || config.gif.frameRate <= 0)) {
      return _failResult(
        AppFailureCode.invalidInput,
        messages.invalidGifConfig,
        recoverySuggestion: messages.updateConfigAndCheck,
      );
    }

    if (config.cameraOverlay.enabled && config.cameraOverlay.size <= 0) {
      return _failResult(
        AppFailureCode.invalidInput,
        messages.invalidCameraOverlay,
        recoverySuggestion: messages.updateConfigAndCheck,
      );
    }

    return null;
  }

  @override
  Future<AppResult<void>> startRecording(RecordConfig config) {
    return start(config);
  }

  @override
  Future<AppResult<void>> pause() async {
    final disposed = _ensureNotDisposed<void>();
    if (disposed != null) return disposed;

    final busy = _beginOperation<void>(_RecordingOperation.pause);
    if (busy != null) return busy;

    try {
      if (!_stateTracker.isRecording || _stateTracker.isPaused) {
        return _failResult(
          AppFailureCode.invalidInput,
          messages.noActiveRecording,
        );
      }

      final nativePause = await platform.pauseRecording();
      if (_isDisposed) return _disposedResult();
      if (nativePause.isFailure) {
        _lastFailure = nativePause.requireFailure;
        return AppResult.failure(nativePause.requireFailure);
      }

      if (!_stateTracker.pause()) {
        return _failResult(
          AppFailureCode.nativeFailure,
          messages.nativePauseFailed,
        );
      }
      _lastFailure = null;
      return const AppResult.success();
    } catch (error, stackTrace) {
      return _failResult(
        AppFailureCode.nativeFailure,
        messages.nativeBridgeInvocationFailed,
        recoverySuggestion: messages.inspectPlatformChannelHandler,
        cause: error,
        stackTrace: stackTrace,
      );
    } finally {
      _endOperation(_RecordingOperation.pause);
    }
  }

  @override
  Future<AppResult<void>> pauseRecording() {
    return pause();
  }

  @override
  Future<AppResult<void>> resume() async {
    final disposed = _ensureNotDisposed<void>();
    if (disposed != null) return disposed;

    final busy = _beginOperation<void>(_RecordingOperation.resume);
    if (busy != null) return busy;

    try {
      if (!_stateTracker.isRecording || !_stateTracker.isPaused) {
        return _failResult(
          AppFailureCode.invalidInput,
          messages.recordingNotPaused,
        );
      }

      final nativeResume = await platform.resumeRecording();
      if (_isDisposed) return _disposedResult();
      if (nativeResume.isFailure) {
        _lastFailure = nativeResume.requireFailure;
        return AppResult.failure(nativeResume.requireFailure);
      }

      if (!_stateTracker.resume()) {
        return _failResult(
          AppFailureCode.nativeFailure,
          messages.nativeResumeFailed,
        );
      }
      _lastFailure = null;
      return const AppResult.success();
    } catch (error, stackTrace) {
      return _failResult(
        AppFailureCode.nativeFailure,
        messages.nativeBridgeInvocationFailed,
        recoverySuggestion: messages.inspectPlatformChannelHandler,
        cause: error,
        stackTrace: stackTrace,
      );
    } finally {
      _endOperation(_RecordingOperation.resume);
    }
  }

  @override
  Future<AppResult<void>> resumeRecording() {
    return resume();
  }

  @override
  Future<AppResult<RecordResult>> stop() async {
    final disposed = _ensureNotDisposed<RecordResult>();
    if (disposed != null) return disposed;

    final busy = _beginOperation<RecordResult>(_RecordingOperation.stop);
    if (busy != null) return busy;

    try {
      if (!_stateTracker.isRecording) {
        return _failResult(
          AppFailureCode.invalidInput,
          messages.noActiveRecording,
        );
      }

      final nativeStop = await platform.stopRecording();
      if (_isDisposed) return _disposedResult();
      if (nativeStop.isFailure) {
        _stateTracker.reset();
        _lastFailure = nativeStop.requireFailure;
        return AppResult.failure(nativeStop.requireFailure);
      }

      final result = nativeStop.requireValue;
      _stateTracker.reset();
      _lastFailure = null;
      return AppResult.success(result);
    } catch (error, stackTrace) {
      return _failResult(
        AppFailureCode.nativeFailure,
        messages.nativeBridgeInvocationFailed,
        recoverySuggestion: messages.inspectPlatformChannelHandler,
        cause: error,
        stackTrace: stackTrace,
      );
    } finally {
      _endOperation(_RecordingOperation.stop);
    }
  }

  @override
  Future<AppResult<RecordResult>> stopRecording() {
    return stop();
  }

  @override
  Future<AppResult<List<AudioDevice>>> getAudioInputDevices() async {
    return _getAudioDevices(platform.listAudioInputs);
  }

  @override
  Future<AppResult<List<AudioDevice>>> getAudioOutputDevices() async {
    return _getAudioDevices(platform.listAudioOutputs);
  }

  Future<AppResult<List<AudioDevice>>> _getAudioDevices(
    Future<AppResult<List<AudioDevice>>> Function() loader,
  ) async {
    final disposed = _ensureNotDisposed<List<AudioDevice>>();
    if (disposed != null) return disposed;

    if (!isSupportedPlatform()) {
      return _failResult(
        AppFailureCode.unsupportedPlatform,
        messages.audioDeviceEnumerationUnsupported,
        recoverySuggestion: messages.useSupportedDesktopBuild,
      );
    }

    try {
      final devices = await loader();
      if (_isDisposed) return _disposedResult();
      if (devices.isFailure) {
        _lastFailure = devices.requireFailure;
        return AppResult.failure(devices.requireFailure);
      }
      _lastFailure = null;
      return devices;
    } catch (error, stackTrace) {
      return _failResult(
        AppFailureCode.nativeFailure,
        messages.nativeBridgeInvocationFailed,
        recoverySuggestion: messages.inspectPlatformChannelHandler,
        cause: error,
        stackTrace: stackTrace,
      );
    }
  }

  @override
  Future<AppResult<DesktopCapabilitySnapshot>> queryCapabilities() async {
    final disposed = _ensureNotDisposed<DesktopCapabilitySnapshot>();
    if (disposed != null) return disposed;

    if (!isSupportedPlatform()) {
      return _failResult(
        AppFailureCode.unsupportedPlatform,
        messages.unsupportedPlatform,
        recoverySuggestion: messages.useSupportedDesktopBuild,
      );
    }

    try {
      final capabilities = await platform.queryCapabilities();
      if (_isDisposed) return _disposedResult();
      return _trackResult(capabilities);
    } catch (error, stackTrace) {
      return _failResult(
        AppFailureCode.nativeFailure,
        messages.nativeBridgeInvocationFailed,
        recoverySuggestion: messages.inspectPlatformChannelHandler,
        cause: error,
        stackTrace: stackTrace,
      );
    }
  }

  @override
  Future<AppResult<DesktopPermissionInfo>> requestPermission(
    DesktopPermissionType type,
  ) async {
    final disposed = _ensureNotDisposed<DesktopPermissionInfo>();
    if (disposed != null) return disposed;

    if (!isSupportedPlatform()) {
      return _failResult(
        AppFailureCode.unsupportedPlatform,
        messages.unsupportedPlatform,
        recoverySuggestion: messages.useSupportedDesktopBuild,
      );
    }

    try {
      final permission = await platform.requestPermission(type);
      if (_isDisposed) return _disposedResult();
      return _trackResult(permission);
    } catch (error, stackTrace) {
      return _failResult(
        AppFailureCode.nativeFailure,
        messages.nativeBridgeInvocationFailed,
        recoverySuggestion: messages.inspectPlatformChannelHandler,
        cause: error,
        stackTrace: stackTrace,
      );
    }
  }

  @override
  void dispose() {
    final activeOperation = _activeOperation;
    final activeOperationDone = _activeOperationDone?.future;
    final shouldStopNative =
        _stateTracker.isRecording &&
        activeOperation != _RecordingOperation.stop;
    _isDisposed = true;
    if (shouldStopNative) {
      unawaited(_stopNativeOnDispose(activeOperationDone));
    }
    _stateTracker.reset();
  }

  AppResult<T> _failResult<T>(
    AppFailureCode code,
    String message, {
    String? recoverySuggestion,
    Object? cause,
    StackTrace? stackTrace,
  }) {
    _lastFailure = AppFailure(
      code: code,
      message: message,
      recoverySuggestion: recoverySuggestion ?? '',
      cause: cause,
      stackTrace: stackTrace,
    );
    return AppResult.failure(_lastFailure!);
  }

  AppResult<T>? _beginOperation<T>(
    _RecordingOperation operation, {
    AppFailureCode busyCode = AppFailureCode.unavailable,
    String? busyMessage,
  }) {
    if (_activeOperation == null) {
      _activeOperation = operation;
      _activeOperationDone = Completer<void>();
      return null;
    }
    return _failResult<T>(
      busyCode,
      busyMessage ?? messages.recordingOperationInProgress,
    );
  }

  AppResult<T>? _ensureNotDisposed<T>() {
    if (!_isDisposed) return null;
    return _disposedResult<T>();
  }

  AppResult<T> _disposedResult<T>() {
    return _failResult<T>(
      AppFailureCode.unavailable,
      'Recording service has been disposed.',
    );
  }

  Future<void> _stopNativeOnDispose([Future<void>? activeOperationDone]) async {
    try {
      await activeOperationDone;
      final stopped = await platform.stopRecording();
      if (stopped.isFailure) {
        _lastFailure = stopped.requireFailure;
      }
    } catch (error, stackTrace) {
      _lastFailure = AppFailure(
        code: AppFailureCode.nativeFailure,
        message: messages.nativeBridgeInvocationFailed,
        recoverySuggestion: messages.inspectPlatformChannelHandler,
        cause: error,
        stackTrace: stackTrace,
      );
    }
  }

  void _endOperation(_RecordingOperation operation) {
    if (_activeOperation == operation) {
      _activeOperationDone?.complete();
      _activeOperationDone = null;
      _activeOperation = null;
    }
  }

  AppResult<T> _trackResult<T>(AppResult<T> result) {
    if (result.isFailure) {
      _lastFailure = result.requireFailure;
    } else {
      _lastFailure = null;
    }
    return result;
  }

  bool _isValidRegion(Rect? region) {
    if (region == null) return false;
    return region.left.isFinite &&
        region.top.isFinite &&
        region.width.isFinite &&
        region.height.isFinite &&
        region.width > 0 &&
        region.height > 0;
  }
}

enum _RecordingOperation { start, pause, resume, stop }
