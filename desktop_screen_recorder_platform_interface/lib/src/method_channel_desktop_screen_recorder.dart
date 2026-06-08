import 'package:flutter/services.dart';

import 'app_error.dart';
import 'desktop_screen_recorder_platform.dart';
import 'messages.dart';
import 'models.dart';
import 'native_contract.dart';

class MethodChannelDesktopScreenRecorder extends DesktopScreenRecorderPlatform {
  static const channelName = 'desktop_screen_recorder/native';

  final MethodChannel channel;
  final RecordErrorMessages messages;

  MethodChannelDesktopScreenRecorder({
    MethodChannel? channel,
    this.messages = defaultRecordErrorMessages,
  }) : channel = channel ?? const MethodChannel(channelName);

  @override
  Future<AppResult<void>> startRecording(RecordConfig config) {
    return _invokeVoid(
      DesktopNativeMethod.startRecording,
      arguments: _nativeRequestFromConfig(config).toMap(),
    );
  }

  @override
  Future<AppResult<void>> pauseRecording() {
    return _invokeVoid(DesktopNativeMethod.pauseRecording);
  }

  @override
  Future<AppResult<void>> resumeRecording() {
    return _invokeVoid(DesktopNativeMethod.resumeRecording);
  }

  @override
  Future<AppResult<RecordResult>> stopRecording() {
    return _invokePayload(
      DesktopNativeMethod.stopRecording,
      parser: (payload) => RecordResult.fromPlatformMap(_asObjectMap(payload)),
    );
  }

  @override
  Future<AppResult<List<AudioDevice>>> listAudioInputs() {
    return _invokePayload(
      DesktopNativeMethod.listAudioInputs,
      parser: (payload) => _parseList(payload, AudioDevice.fromPlatformMap),
    );
  }

  @override
  Future<AppResult<List<AudioDevice>>> listAudioOutputs() {
    return _invokePayload(
      DesktopNativeMethod.listAudioOutputs,
      parser: (payload) => _parseList(payload, AudioDevice.fromPlatformMap),
    );
  }

  @override
  Future<AppResult<DesktopCapabilitySnapshot>> queryCapabilities() {
    return _invokePayload(
      DesktopNativeMethod.queryCapabilities,
      parser: (payload) => DesktopCapabilitySnapshot.fromMap(_asMap(payload)),
    );
  }

  @override
  Future<AppResult<DesktopPermissionInfo>> requestPermission(
    DesktopPermissionType type,
  ) {
    return _invokePayload(
      DesktopNativeMethod.requestPermission,
      arguments: {'type': type.name},
      parser: (payload) => DesktopPermissionInfo.fromMap(_asMap(payload)),
    );
  }

  Future<AppResult<void>> _invokeVoid(
    String method, {
    Map<String, Object?>? arguments,
  }) async {
    final envelope = await _invokeEnvelope(method, arguments: arguments);
    if (envelope.isFailure) return AppResult.failure(envelope.requireFailure);
    return const AppResult.success();
  }

  Future<AppResult<T>> _invokePayload<T>(
    String method, {
    Map<String, Object?>? arguments,
    required T Function(Object? payload) parser,
  }) async {
    final envelope = await _invokeEnvelope(method, arguments: arguments);
    if (envelope.isFailure) return AppResult.failure(envelope.requireFailure);

    try {
      return AppResult.success(parser(envelope.requireValue['payload']));
    } catch (error, stackTrace) {
      return AppResult.failure(
        AppFailure(
          code: AppFailureCode.serialization,
          message: messages.nativePayloadParseFailed,
          recoverySuggestion: messages.checkPlatformChannelContract,
          cause: error,
          stackTrace: stackTrace,
        ),
      );
    }
  }

  Future<AppResult<Map<dynamic, dynamic>>> _invokeEnvelope(
    String method, {
    Map<String, Object?>? arguments,
  }) async {
    try {
      final raw = await channel.invokeMethod<Object?>(method, arguments);
      final envelope = _tryMap(raw);
      if (envelope == null) {
        return AppResult.failure(_serializationFailure());
      }

      final statusValue = envelope['status'];
      if (statusValue is! String || statusValue.isEmpty) {
        return AppResult.failure(_serializationFailure());
      }
      final status = statusValue;
      if (status == 'success') {
        return AppResult.success(envelope);
      }

      final failureMap = _tryMap(envelope['failure']) ?? const {};
      final nativeFailure = NativeFailureEnvelope.fromMap(
        status: status,
        map: failureMap,
      );
      return AppResult.failure(
        AppFailure(
          code: failureCodeForEnvelope(nativeFailure),
          message: _stringOrDefault(
            failureMap['message'],
            messages.nativeOperationFailed,
          ),
          recoverySuggestion: _stringOrDefault(
            failureMap['recoverySuggestion'],
            '',
          ),
          nativeCode: nativeFailure.nativeCode,
          cause: nativeFailure,
        ),
      );
    } on MissingPluginException catch (error, stackTrace) {
      return AppResult.failure(
        AppFailure(
          code: AppFailureCode.unsupported,
          message: messages.nativeBridgeNotRegistered,
          recoverySuggestion: messages.useSupportedDesktopBuild,
          cause: error,
          stackTrace: stackTrace,
        ),
      );
    } on PlatformException catch (error, stackTrace) {
      return AppResult.failure(
        AppFailure(
          code: AppFailureCode.nativeFailure,
          message: error.message ?? error.code,
          recoverySuggestion: messages.inspectPlatformChannelHandler,
          nativeCode: error.code,
          cause: error.details,
          stackTrace: stackTrace,
        ),
      );
    } catch (error, stackTrace) {
      return AppResult.failure(
        AppFailure(
          code: AppFailureCode.nativeFailure,
          message: messages.nativeBridgeInvocationFailed,
          recoverySuggestion: messages.inspectPlatformChannelHandler,
          cause: error,
          stackTrace: stackTrace,
        ),
      );
    }
  }

  AppFailureCode failureCodeForEnvelope(NativeFailureEnvelope failure) {
    if (failure.requiresRestart) {
      return AppFailureCode.requiresRestart;
    }
    if (failure.recoverability ==
        NativeFailureRecoverability.userActionRequired) {
      switch (failure.domain) {
        case 'permission':
          return AppFailureCode.permissionDenied;
        case 'configuration':
          return AppFailureCode.notConfigured;
      }
    }

    switch (failure.status) {
      case 'cancelled':
        return AppFailureCode.cancelled;
      case 'invalidInput':
        return AppFailureCode.invalidInput;
      case 'permissionDenied':
        return AppFailureCode.permissionDenied;
      case 'unsupported':
        return AppFailureCode.unsupported;
      case 'ioFailure':
        return AppFailureCode.ioFailure;
      case 'notConfigured':
        return AppFailureCode.notConfigured;
      default:
        return AppFailureCode.nativeFailure;
    }
  }

  NativeRecordRequest _nativeRequestFromConfig(RecordConfig config) {
    return NativeRecordRequest(
      mode: config.mode.name,
      format: config.format.name,
      quality: config.quality.name,
      qualityMaxHeight: config.quality.maxHeight,
      videoBitrate: config.quality.videoBitrate,
      frameRate: config.frameRate,
      captureAudio: config.captureAudio,
      captureMicrophone: config.captureMicrophone,
      showCursor: config.showCursor,
      highlightClicks: config.highlightClicks,
      showKeystrokes: config.showKeystrokes,
      displayId: config.displayId,
      windowId: config.windowId,
      windowTitle: config.windowTitle,
      region: config.region == null
          ? null
          : NativePhysicalRect.fromRect(config.region!),
      gif: config.gif.toPlatformMap(),
      cameraOverlay: config.cameraOverlay.toPlatformMap(),
    );
  }

  List<T> _parseList<T>(
    Object? payload,
    T Function(Map<Object?, Object?> map) parser,
  ) {
    if (payload is! List) {
      throw const FormatException('Expected list payload.');
    }
    return payload
        .map((item) {
          if (item is! Map) {
            throw const FormatException('Expected map item in list payload.');
          }
          return parser(item.cast<Object?, Object?>());
        })
        .toList(growable: false);
  }

  Map<dynamic, dynamic> _asMap(Object? raw) {
    final map = _tryMap(raw);
    if (map != null) return map;
    throw const FormatException('Expected map payload.');
  }

  Map<Object?, Object?> _asObjectMap(Object? raw) {
    if (raw is Map) return raw.cast<Object?, Object?>();
    throw const FormatException('Expected map payload.');
  }

  Map<dynamic, dynamic>? _tryMap(Object? raw) {
    if (raw is Map) return raw.cast<dynamic, dynamic>();
    return null;
  }

  AppFailure _serializationFailure() {
    return AppFailure(
      code: AppFailureCode.serialization,
      message: messages.nativePayloadParseFailed,
      recoverySuggestion: messages.checkPlatformChannelContract,
    );
  }

  String _stringOrDefault(Object? value, String fallback) {
    if (value is String) return value;
    return fallback;
  }
}
