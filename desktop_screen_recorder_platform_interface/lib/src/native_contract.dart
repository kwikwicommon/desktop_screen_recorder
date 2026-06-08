import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/material.dart';

class DesktopNativeMethod {
  static const startRecording = 'record.start';
  static const pauseRecording = 'record.pause';
  static const resumeRecording = 'record.resume';
  static const stopRecording = 'record.stop';
  static const listAudioInputs = 'record.listAudioInputs';
  static const listAudioOutputs = 'record.listAudioOutputs';
  static const queryPermissions = 'capability.queryPermissions';
  static const queryCapabilities = 'capability.queryCapabilities';
  static const queryFeatureAvailability = 'capability.queryFeatureAvailability';
  static const requestPermission = 'capability.requestPermission';
}

enum DesktopPermissionType {
  screenRecording,
  microphone,
  camera,
  accessibility,
  fileSystem,
  systemAudio,
}

enum DesktopPermissionState {
  granted,
  denied,
  notDetermined,
  restricted,
  requiresRestart,
  unsupported,
}

enum DesktopFeatureKey {
  displayEnumeration,
  windowEnumeration,
  captureDisplay,
  captureWindow,
  captureRegion,
  recordDisplay,
  recordRegion,
  recordWindow,
  recordSystemAudio,
  recordMicrophone,
  recordCameraOverlay,
  imageClipboard,
  scrollAutomation,
  ocr,
}

enum DesktopFeatureAvailabilityStatus {
  implemented,
  available,
  requiresPermission,
  requiresDependency,
  degraded,
  notImplemented,
  permissionRequired,
  unsupported,
  notConfigured,
}

enum NativeFailureRecoverability {
  retryable,
  userActionRequired,
  notRecoverable,
  unknown,
}

class NativeFailureEnvelope {
  final String status;
  final String domain;
  final String nativeCode;
  final NativeFailureRecoverability recoverability;
  final String artifactPath;
  final bool requiresRestart;
  final Object? details;

  const NativeFailureEnvelope({
    required this.status,
    this.domain = '',
    this.nativeCode = '',
    this.recoverability = NativeFailureRecoverability.unknown,
    this.artifactPath = '',
    this.requiresRestart = false,
    this.details,
  });

  factory NativeFailureEnvelope.fromMap({
    required String status,
    required Map<dynamic, dynamic> map,
  }) {
    return NativeFailureEnvelope(
      status: status,
      domain: _optionalString(map['domain']),
      nativeCode: _optionalString(map['nativeCode']),
      recoverability: nativeFailureRecoverabilityFromName(
        _optionalString(map['recoverability']),
      ),
      artifactPath: _optionalString(map['artifactPath']),
      requiresRestart: map['requiresRestart'] == true,
      details: map['details'],
    );
  }

  Map<String, Object?> toMap() {
    return {
      'status': status,
      'domain': domain,
      'nativeCode': nativeCode,
      'recoverability': recoverability.name,
      'artifactPath': artifactPath,
      'requiresRestart': requiresRestart,
      'details': details,
    };
  }
}

class DesktopPermissionInfo {
  final DesktopPermissionType type;
  final DesktopPermissionState state;
  final String message;
  final bool requiresRestart;

  const DesktopPermissionInfo({
    required this.type,
    required this.state,
    this.message = '',
    this.requiresRestart = false,
  });

  factory DesktopPermissionInfo.fromMap(Map<dynamic, dynamic> map) {
    return DesktopPermissionInfo(
      type: _readDesktopPermissionType(map['type'], 'type'),
      state: _readDesktopPermissionState(map['state'], 'state'),
      message: _optionalString(map['message']),
      requiresRestart: map['requiresRestart'] == true,
    );
  }

  Map<String, Object?> toMap() {
    return {
      'type': type.name,
      'state': state.name,
      'message': message,
      'requiresRestart': requiresRestart,
    };
  }
}

class DesktopFeatureAvailability {
  final DesktopFeatureKey feature;
  final DesktopFeatureAvailabilityStatus status;
  final String message;
  final DesktopPermissionType? permissionType;

  const DesktopFeatureAvailability({
    required this.feature,
    required this.status,
    this.message = '',
    this.permissionType,
  });

  factory DesktopFeatureAvailability.fromMap(Map<dynamic, dynamic> map) {
    final permissionTypeName = _nullableString(map['permissionType']);
    return DesktopFeatureAvailability(
      feature: _readDesktopFeatureKey(map['feature'], 'feature'),
      status: _readDesktopFeatureAvailabilityStatus(map['status'], 'status'),
      message: _optionalString(map['message']),
      permissionType: permissionTypeName == null
          ? null
          : _readDesktopPermissionType(permissionTypeName, 'permissionType'),
    );
  }

  Map<String, Object?> toMap() {
    return {
      'feature': feature.name,
      'status': status.name,
      'message': message,
      'permissionType': permissionType?.name,
    };
  }
}

class DesktopCapabilitySnapshot {
  final Map<DesktopPermissionType, DesktopPermissionInfo> permissions;
  final Map<DesktopFeatureKey, DesktopFeatureAvailability> features;

  const DesktopCapabilitySnapshot({
    required this.permissions,
    required this.features,
  });

  factory DesktopCapabilitySnapshot.fromMap(Map<dynamic, dynamic> map) {
    final permissions = <DesktopPermissionType, DesktopPermissionInfo>{};
    final features = <DesktopFeatureKey, DesktopFeatureAvailability>{};

    final permissionList = _readMapList(map['permissions'], 'permissions');
    for (final entry in permissionList) {
      final item = DesktopPermissionInfo.fromMap(entry);
      permissions[item.type] = item;
    }

    final featureList = _readMapList(map['features'], 'features');
    for (final entry in featureList) {
      final item = DesktopFeatureAvailability.fromMap(entry);
      features[item.feature] = item;
    }

    return DesktopCapabilitySnapshot(
      permissions: permissions,
      features: features,
    );
  }
}

class NativePhysicalRect {
  final int left;
  final int top;
  final int width;
  final int height;

  const NativePhysicalRect({
    required this.left,
    required this.top,
    required this.width,
    required this.height,
  });

  factory NativePhysicalRect.fromRect(Rect rect) {
    final normalized = Rect.fromLTRB(
      math.min(rect.left, rect.right),
      math.min(rect.top, rect.bottom),
      math.max(rect.left, rect.right),
      math.max(rect.top, rect.bottom),
    );
    final left = normalized.left.floor();
    final top = normalized.top.floor();
    final right = normalized.right.ceil();
    final bottom = normalized.bottom.ceil();
    return NativePhysicalRect(
      left: left,
      top: top,
      width: math.max(0, right - left),
      height: math.max(0, bottom - top),
    );
  }

  factory NativePhysicalRect.fromMap(Map<dynamic, dynamic> map) {
    return NativePhysicalRect(
      left: _readRequiredInt(map['left'], 'left'),
      top: _readRequiredInt(map['top'], 'top'),
      width: _readPositiveInt(map['width'], 'width'),
      height: _readPositiveInt(map['height'], 'height'),
    );
  }

  Rect get asRect => Rect.fromLTWH(
    left.toDouble(),
    top.toDouble(),
    width.toDouble(),
    height.toDouble(),
  );

  Map<String, Object?> toMap() {
    return {'left': left, 'top': top, 'width': width, 'height': height};
  }
}

class NativeAudioDevice {
  final String id;
  final String name;
  final bool isInput;
  final bool isAvailable;
  final String unavailableReason;

  const NativeAudioDevice({
    required this.id,
    required this.name,
    required this.isInput,
    this.isAvailable = true,
    this.unavailableReason = '',
  });

  factory NativeAudioDevice.fromMap(Map<dynamic, dynamic> map) {
    return NativeAudioDevice(
      id: _optionalString(map['id']),
      name: _optionalString(map['name']),
      isInput: map['isInput'] as bool? ?? false,
      isAvailable: map['isAvailable'] as bool? ?? true,
      unavailableReason: _optionalString(map['unavailableReason']),
    );
  }
}

class NativeRecordRequest {
  final String mode;
  final String format;
  final String quality;
  final int qualityMaxHeight;
  final int videoBitrate;
  final int frameRate;
  final bool captureAudio;
  final bool captureMicrophone;
  final bool showCursor;
  final bool highlightClicks;
  final bool showKeystrokes;
  final int? displayId;
  final int? windowId;
  final String? windowTitle;
  final NativePhysicalRect? region;
  final Map<String, Object?>? gif;
  final Map<String, Object?>? cameraOverlay;

  const NativeRecordRequest({
    required this.mode,
    required this.format,
    required this.quality,
    required this.qualityMaxHeight,
    required this.videoBitrate,
    required this.frameRate,
    required this.captureAudio,
    required this.captureMicrophone,
    required this.showCursor,
    required this.highlightClicks,
    required this.showKeystrokes,
    this.displayId,
    this.windowId,
    this.windowTitle,
    this.region,
    this.gif,
    this.cameraOverlay,
  });

  Map<String, Object?> toMap() {
    return {
      'mode': mode,
      'format': format,
      'quality': quality,
      'qualityMaxHeight': qualityMaxHeight,
      'videoBitrate': videoBitrate,
      'frameRate': frameRate,
      'captureAudio': captureAudio,
      'captureMicrophone': captureMicrophone,
      'showCursor': showCursor,
      'highlightClicks': highlightClicks,
      'showKeystrokes': showKeystrokes,
      'displayId': displayId,
      'windowId': windowId,
      'windowTitle': windowTitle,
      'region': region?.toMap(),
      'gif': gif,
      'cameraOverlay': cameraOverlay,
    };
  }
}

class NativeRecordArtifact {
  final String filePath;
  final Duration duration;
  final String format;
  final Size resolution;
  final int fileSizeBytes;
  final Uint8List? thumbnail;
  final Map<String, Object?> effectiveCapabilities;
  final Map<String, Object?> requested;
  final Map<String, Object?> actual;
  final List<String> recoveryEvents;
  final bool completedEarly;
  final String completionReason;
  final String completionSuggestion;

  const NativeRecordArtifact({
    required this.filePath,
    required this.duration,
    required this.format,
    required this.resolution,
    required this.fileSizeBytes,
    this.thumbnail,
    this.effectiveCapabilities = const {},
    this.requested = const {},
    this.actual = const {},
    this.recoveryEvents = const [],
    this.completedEarly = false,
    this.completionReason = '',
    this.completionSuggestion = '',
  });

  factory NativeRecordArtifact.fromMap(Map<dynamic, dynamic> map) {
    return NativeRecordArtifact(
      filePath: _optionalString(map['filePath']),
      duration: Duration(
        milliseconds: _readNonNegativeInt(
          map['durationMs'],
          'durationMs',
          fallback: 0,
        ),
      ),
      format: _optionalString(map['format'], fallback: 'mp4'),
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
      effectiveCapabilities: objectMap(map['effectiveCapabilities']),
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
}

Map<String, Object?> objectMap(Object? value) {
  if (value is! Map) return const {};
  return value.map((key, value) => MapEntry(key.toString(), value));
}

Uint8List? readNullableBytes(dynamic value) {
  if (value == null) return null;
  if (value is Uint8List) return value;
  if (value is ByteData) {
    return value.buffer.asUint8List(value.offsetInBytes, value.lengthInBytes);
  }
  if (value is ByteBuffer) return value.asUint8List();
  if (value is List) {
    final bytes = <int>[];
    for (final item in value) {
      if (item is! num) return null;
      final byte = item.toInt();
      if (byte < 0 || byte > 255) return null;
      bytes.add(byte);
    }
    return Uint8List.fromList(bytes);
  }
  return null;
}

DesktopPermissionType desktopPermissionTypeFromName(String? name) {
  return DesktopPermissionType.values.firstWhere(
    (value) => value.name == name,
    orElse: () => DesktopPermissionType.screenRecording,
  );
}

DesktopPermissionState desktopPermissionStateFromName(String? name) {
  return DesktopPermissionState.values.firstWhere(
    (value) => value.name == name,
    orElse: () => DesktopPermissionState.unsupported,
  );
}

DesktopFeatureKey desktopFeatureKeyFromName(String? name) {
  return DesktopFeatureKey.values.firstWhere(
    (value) => value.name == name,
    orElse: () => DesktopFeatureKey.recordDisplay,
  );
}

DesktopFeatureAvailabilityStatus desktopFeatureAvailabilityStatusFromName(
  String? name,
) {
  switch (name) {
    case 'permissionRequired':
    case 'requiresPermission':
      return DesktopFeatureAvailabilityStatus.requiresPermission;
    case 'notConfigured':
    case 'requiresDependency':
      return DesktopFeatureAvailabilityStatus.requiresDependency;
  }
  return DesktopFeatureAvailabilityStatus.values.firstWhere(
    (value) => value.name == name,
    orElse: () => DesktopFeatureAvailabilityStatus.unsupported,
  );
}

NativeFailureRecoverability nativeFailureRecoverabilityFromName(String? name) {
  return NativeFailureRecoverability.values.firstWhere(
    (value) => value.name == name,
    orElse: () => NativeFailureRecoverability.unknown,
  );
}

String _optionalString(Object? value, {String fallback = ''}) {
  return value is String ? value : fallback;
}

String? _nullableString(Object? value) {
  return value is String ? value : null;
}

String _requireString(Object? value, String fieldName) {
  if (value is String && value.isNotEmpty) return value;
  throw FormatException('Expected non-empty string $fieldName.');
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

int _readPositiveInt(Object? value, String fieldName) {
  final number = _readRequiredInt(value, fieldName);
  if (number <= 0) {
    throw FormatException('Expected positive $fieldName.');
  }
  return number;
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

List<Map<dynamic, dynamic>> _readMapList(Object? value, String fieldName) {
  if (value == null) return const <Map<dynamic, dynamic>>[];
  if (value is! List) {
    throw FormatException('Expected list $fieldName.');
  }
  return value
      .map((entry) {
        if (entry is Map) return entry.cast<dynamic, dynamic>();
        throw FormatException('Expected map entries in $fieldName.');
      })
      .toList(growable: false);
}

DesktopPermissionType _readDesktopPermissionType(
  Object? value,
  String fieldName,
) {
  final name = _requireString(value, fieldName);
  for (final entry in DesktopPermissionType.values) {
    if (entry.name == name) return entry;
  }
  throw FormatException('Unknown $fieldName: $name.');
}

DesktopPermissionState _readDesktopPermissionState(
  Object? value,
  String fieldName,
) {
  final name = _requireString(value, fieldName);
  for (final entry in DesktopPermissionState.values) {
    if (entry.name == name) return entry;
  }
  throw FormatException('Unknown $fieldName: $name.');
}

DesktopFeatureKey _readDesktopFeatureKey(Object? value, String fieldName) {
  final name = _requireString(value, fieldName);
  for (final entry in DesktopFeatureKey.values) {
    if (entry.name == name) return entry;
  }
  throw FormatException('Unknown $fieldName: $name.');
}

DesktopFeatureAvailabilityStatus _readDesktopFeatureAvailabilityStatus(
  Object? value,
  String fieldName,
) {
  final name = _requireString(value, fieldName);
  switch (name) {
    case 'permissionRequired':
      return DesktopFeatureAvailabilityStatus.requiresPermission;
    case 'notConfigured':
      return DesktopFeatureAvailabilityStatus.notConfigured;
  }
  for (final entry in DesktopFeatureAvailabilityStatus.values) {
    if (entry.name == name) return entry;
  }
  throw FormatException('Unknown $fieldName: $name.');
}
