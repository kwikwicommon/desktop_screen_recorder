import 'dart:typed_data';

import 'package:flutter/material.dart';

import 'native_contract.dart';

enum RecordMode { region, fullscreen, window }

enum RecordFormat { mp4, gif }

enum CameraOverlayShape { circle, rectangle }

enum RecordingOverlayPosition { topLeft, topRight, bottomLeft, bottomRight }

enum RecordQuality {
  low,
  medium,
  high,
  ultra;

  String get labelKey {
    switch (this) {
      case low:
        return 'record.quality.low';
      case medium:
        return 'record.quality.medium';
      case high:
        return 'record.quality.high';
      case ultra:
        return 'record.quality.ultra';
    }
  }

  int get maxHeight {
    switch (this) {
      case low:
        return 720;
      case medium:
        return 1080;
      case high:
        return 1440;
      case ultra:
        return 0;
    }
  }

  int get videoBitrate {
    switch (this) {
      case low:
        return 4000000;
      case medium:
        return 8000000;
      case high:
        return 16000000;
      case ultra:
        return 25000000;
    }
  }
}

class GifTranscodeConfig {
  final int maxWidth;
  final int frameRate;
  final bool optimizePalette;

  const GifTranscodeConfig({
    this.maxWidth = 960,
    this.frameRate = 12,
    this.optimizePalette = true,
  });

  GifTranscodeConfig copyWith({
    int? maxWidth,
    int? frameRate,
    bool? optimizePalette,
  }) {
    return GifTranscodeConfig(
      maxWidth: maxWidth ?? this.maxWidth,
      frameRate: frameRate ?? this.frameRate,
      optimizePalette: optimizePalette ?? this.optimizePalette,
    );
  }

  Map<String, Object?> toPlatformMap() {
    return {
      'maxWidth': maxWidth,
      'frameRate': frameRate,
      'optimizePalette': optimizePalette,
    };
  }
}

class CameraOverlayConfig {
  final bool enabled;
  final CameraOverlayShape shape;
  final RecordingOverlayPosition position;
  final int size;

  const CameraOverlayConfig({
    this.enabled = false,
    this.shape = CameraOverlayShape.circle,
    this.position = RecordingOverlayPosition.bottomRight,
    this.size = 220,
  });

  CameraOverlayConfig copyWith({
    bool? enabled,
    CameraOverlayShape? shape,
    RecordingOverlayPosition? position,
    int? size,
  }) {
    return CameraOverlayConfig(
      enabled: enabled ?? this.enabled,
      shape: shape ?? this.shape,
      position: position ?? this.position,
      size: size ?? this.size,
    );
  }

  Map<String, Object?> toPlatformMap() {
    return {
      'enabled': enabled,
      'shape': shape.name,
      'position': position.name,
      'size': size,
    };
  }
}

class RecordingEffectiveCapabilities {
  final bool systemAudio;
  final bool microphone;
  final bool cursor;
  final bool clickHighlight;
  final bool keystrokes;
  final bool cameraOverlay;

  const RecordingEffectiveCapabilities({
    this.systemAudio = false,
    this.microphone = false,
    this.cursor = false,
    this.clickHighlight = false,
    this.keystrokes = false,
    this.cameraOverlay = false,
  });

  factory RecordingEffectiveCapabilities.fromPlatformMap(Object? raw) {
    if (raw is! Map) return const RecordingEffectiveCapabilities();
    return RecordingEffectiveCapabilities(
      systemAudio: raw['systemAudio'] == true,
      microphone: raw['microphone'] == true,
      cursor: raw['cursor'] == true,
      clickHighlight: raw['clickHighlight'] == true,
      keystrokes: raw['keystrokes'] == true,
      cameraOverlay: raw['cameraOverlay'] == true,
    );
  }

  factory RecordingEffectiveCapabilities.fromConfig(RecordConfig config) {
    return RecordingEffectiveCapabilities(
      systemAudio: config.captureAudio && config.format != RecordFormat.gif,
      microphone: config.captureMicrophone && config.format != RecordFormat.gif,
      cursor: config.showCursor,
      clickHighlight: config.highlightClicks,
      keystrokes: config.showKeystrokes,
      cameraOverlay: config.cameraOverlay.enabled,
    );
  }

  Map<String, Object?> toJson() {
    return {
      'systemAudio': systemAudio,
      'microphone': microphone,
      'cursor': cursor,
      'clickHighlight': clickHighlight,
      'keystrokes': keystrokes,
      'cameraOverlay': cameraOverlay,
    };
  }
}

class RecordConfig {
  final RecordMode mode;
  final RecordFormat format;
  final RecordQuality quality;
  final int frameRate;
  final bool captureAudio;
  final bool captureMicrophone;
  final bool showCursor;
  final bool highlightClicks;
  final bool showKeystrokes;
  final Rect? region;
  final int? displayId;
  final int? windowId;
  final String? windowTitle;
  final GifTranscodeConfig gif;
  final CameraOverlayConfig cameraOverlay;

  const RecordConfig({
    this.mode = RecordMode.fullscreen,
    this.format = RecordFormat.mp4,
    this.quality = RecordQuality.high,
    this.frameRate = 30,
    this.captureAudio = false,
    this.captureMicrophone = false,
    this.showCursor = true,
    this.highlightClicks = false,
    this.showKeystrokes = false,
    this.region,
    this.displayId,
    this.windowId,
    this.windowTitle,
    this.gif = const GifTranscodeConfig(),
    this.cameraOverlay = const CameraOverlayConfig(),
  });

  RecordConfig copyWith({
    RecordMode? mode,
    RecordFormat? format,
    RecordQuality? quality,
    int? frameRate,
    bool? captureAudio,
    bool? captureMicrophone,
    bool? showCursor,
    bool? highlightClicks,
    bool? showKeystrokes,
    Rect? region,
    int? displayId,
    int? windowId,
    String? windowTitle,
    GifTranscodeConfig? gif,
    CameraOverlayConfig? cameraOverlay,
    bool clearRegion = false,
    bool clearDisplay = false,
    bool clearWindow = false,
  }) {
    return RecordConfig(
      mode: mode ?? this.mode,
      format: format ?? this.format,
      quality: quality ?? this.quality,
      frameRate: frameRate ?? this.frameRate,
      captureAudio: captureAudio ?? this.captureAudio,
      captureMicrophone: captureMicrophone ?? this.captureMicrophone,
      showCursor: showCursor ?? this.showCursor,
      highlightClicks: highlightClicks ?? this.highlightClicks,
      showKeystrokes: showKeystrokes ?? this.showKeystrokes,
      region: clearRegion ? null : (region ?? this.region),
      displayId: clearDisplay ? null : (displayId ?? this.displayId),
      windowId: clearWindow ? null : (windowId ?? this.windowId),
      windowTitle: clearWindow ? null : (windowTitle ?? this.windowTitle),
      gif: gif ?? this.gif,
      cameraOverlay: cameraOverlay ?? this.cameraOverlay,
    );
  }

  Map<String, Object?> toPlatformMap() {
    return {
      'mode': mode.name,
      'format': format.name,
      'quality': quality.name,
      'qualityMaxHeight': quality.maxHeight,
      'videoBitrate': quality.videoBitrate,
      'frameRate': frameRate,
      'captureAudio': captureAudio,
      'captureMicrophone': captureMicrophone,
      'showCursor': showCursor,
      'highlightClicks': highlightClicks,
      'showKeystrokes': showKeystrokes,
      'region': region == null
          ? null
          : {
              'left': region!.left,
              'top': region!.top,
              'width': region!.width,
              'height': region!.height,
            },
      'displayId': displayId,
      'windowId': windowId,
      'windowTitle': windowTitle,
      'gif': gif.toPlatformMap(),
      'cameraOverlay': cameraOverlay.toPlatformMap(),
    };
  }
}

class RecordResult {
  final String filePath;
  final Duration duration;
  final RecordFormat format;
  final Size resolution;
  final int fileSizeBytes;
  final Uint8List? thumbnail;
  final RecordingEffectiveCapabilities capabilities;
  final Map<String, Object?> requested;
  final Map<String, Object?> actual;
  final List<String> recoveryEvents;
  final bool completedEarly;
  final String completionReason;
  final String completionSuggestion;

  const RecordResult({
    required this.filePath,
    required this.duration,
    required this.format,
    required this.resolution,
    required this.fileSizeBytes,
    this.thumbnail,
    this.capabilities = const RecordingEffectiveCapabilities(),
    this.requested = const {},
    this.actual = const {},
    this.recoveryEvents = const [],
    this.completedEarly = false,
    this.completionReason = '',
    this.completionSuggestion = '',
  });

  factory RecordResult.fromPlatformMap(Map<Object?, Object?> map) {
    final formatName = map['format'] as String? ?? RecordFormat.mp4.name;
    final format = RecordFormat.values.firstWhere(
      (value) => value.name == formatName,
      orElse: () => RecordFormat.mp4,
    );
    return RecordResult(
      filePath: map['filePath'] as String? ?? '',
      duration: Duration(
        milliseconds: _readNonNegativeInt(
          map['durationMs'],
          'durationMs',
          fallback: 0,
        ),
      ),
      format: format,
      resolution: Size(
        _readNonNegativeDouble(map['width'], 'width', fallback: 0),
        _readNonNegativeDouble(map['height'], 'height', fallback: 0),
      ),
      fileSizeBytes: _readNonNegativeInt(
        map['fileSizeBytes'],
        'fileSizeBytes',
        fallback: 0,
      ),
      thumbnail: readNullableBytes(map['thumbnail']),
      capabilities: map['effectiveCapabilities'] == null
          ? RecordingEffectiveCapabilities.fromPlatformMap(map['actual'])
          : RecordingEffectiveCapabilities.fromPlatformMap(
              map['effectiveCapabilities'],
            ),
      requested: objectMap(map['requested']),
      actual: objectMap(map['actual']),
      recoveryEvents: (map['recoveryEvents'] as List<dynamic>? ?? const [])
          .whereType<String>()
          .toList(),
      completedEarly: map['completedEarly'] as bool? ?? false,
      completionReason: map['completionReason'] as String? ?? '',
      completionSuggestion: map['completionSuggestion'] as String? ?? '',
    );
  }

  RecordResult copyWith({
    String? filePath,
    Duration? duration,
    RecordFormat? format,
    Size? resolution,
    int? fileSizeBytes,
    Uint8List? thumbnail,
    RecordingEffectiveCapabilities? capabilities,
    Map<String, Object?>? requested,
    Map<String, Object?>? actual,
    List<String>? recoveryEvents,
    bool? completedEarly,
    String? completionReason,
    String? completionSuggestion,
  }) {
    return RecordResult(
      filePath: filePath ?? this.filePath,
      duration: duration ?? this.duration,
      format: format ?? this.format,
      resolution: resolution ?? this.resolution,
      fileSizeBytes: fileSizeBytes ?? this.fileSizeBytes,
      thumbnail: thumbnail ?? this.thumbnail,
      capabilities: capabilities ?? this.capabilities,
      requested: requested ?? this.requested,
      actual: actual ?? this.actual,
      recoveryEvents: recoveryEvents ?? this.recoveryEvents,
      completedEarly: completedEarly ?? this.completedEarly,
      completionReason: completionReason ?? this.completionReason,
      completionSuggestion: completionSuggestion ?? this.completionSuggestion,
    );
  }
}

int _readRequiredInt(Object? value, String fieldName) {
  if (value is! num) {
    throw FormatException('Expected numeric $fieldName.');
  }
  final number = value.toDouble();
  if (!number.isFinite) {
    throw FormatException('Expected finite $fieldName.');
  }
  return value.toInt();
}

int _readNonNegativeInt(
  Object? value,
  String fieldName, {
  required int fallback,
}) {
  if (value == null) return fallback;
  final number = _readRequiredInt(value, fieldName);
  if (number < 0) {
    throw FormatException('Expected non-negative $fieldName.');
  }
  return number;
}

double _readRequiredDouble(Object? value, String fieldName) {
  if (value is! num) {
    throw FormatException('Expected numeric $fieldName.');
  }
  final number = value.toDouble();
  if (!number.isFinite) {
    throw FormatException('Expected finite $fieldName.');
  }
  return number;
}

double _readNonNegativeDouble(
  Object? value,
  String fieldName, {
  required double fallback,
}) {
  if (value == null) return fallback;
  final number = _readRequiredDouble(value, fieldName);
  if (number < 0) {
    throw FormatException('Expected non-negative $fieldName.');
  }
  return number;
}

class AudioDevice {
  final String id;
  final String name;
  final bool isInput;
  final bool isAvailable;
  final String? unavailableReason;

  const AudioDevice({
    required this.id,
    required this.name,
    required this.isInput,
    this.isAvailable = true,
    this.unavailableReason,
  });

  factory AudioDevice.fromPlatformMap(Map<Object?, Object?> map) {
    return AudioDevice(
      id: map['id'] as String? ?? '',
      name: map['name'] as String? ?? '',
      isInput: map['isInput'] as bool? ?? true,
      isAvailable: map['isAvailable'] as bool? ?? true,
      unavailableReason: map['unavailableReason'] as String?,
    );
  }

  factory AudioDevice.fromNativeDevice(NativeAudioDevice device) {
    return AudioDevice(
      id: device.id,
      name: device.name,
      isInput: device.isInput,
      isAvailable: device.isAvailable,
      unavailableReason: device.unavailableReason.isEmpty
          ? null
          : device.unavailableReason,
    );
  }
}
