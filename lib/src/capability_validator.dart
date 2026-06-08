import 'package:desktop_screen_recorder_platform_interface/desktop_screen_recorder_platform_interface.dart'
    show DesktopScreenRecorderPlatform;

import 'app_error.dart';
import 'messages.dart';
import 'models.dart';
import 'native_contract.dart';

class RecordingCapabilityValidator {
  final DesktopScreenRecorderPlatform platform;
  final RecordErrorMessages messages;

  const RecordingCapabilityValidator({
    required this.platform,
    this.messages = defaultRecordErrorMessages,
  });

  Future<AppResult<void>> validate(RecordConfig config) async {
    try {
      final snapshotResult = await platform.queryCapabilities();
      if (snapshotResult.isFailure) {
        return AppResult.failure(snapshotResult.requireFailure);
      }

      final snapshot = snapshotResult.requireValue;
      for (final requirement in _requirementsForConfig(config)) {
        final availability = snapshot.features[requirement.feature];
        final failure = _validateFeatureAvailability(requirement, availability);
        if (failure != null) {
          return AppResult.failure(failure);
        }
      }
      return const AppResult.success();
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

  AppFailure? _validateFeatureAvailability(
    _RecordingFeatureRequirement requirement,
    DesktopFeatureAvailability? availability,
  ) {
    if (availability == null) {
      return AppFailure(
        code: AppFailureCode.unsupported,
        message: '${requirement.label} requires '
            '${messages.featureName(requirement.feature)}, but the native '
            'bridge did not report it.',
        recoverySuggestion: messages.checkPlatformChannelContract,
      );
    }

    switch (availability.status) {
      case DesktopFeatureAvailabilityStatus.implemented:
      case DesktopFeatureAvailabilityStatus.available:
        return null;
      case DesktopFeatureAvailabilityStatus.degraded:
        if (requirement.allowDegraded) return null;
        return AppFailure(
          code: AppFailureCode.notConfigured,
          message: availability.message.trim().isEmpty
              ? '${requirement.label} is degraded.'
              : availability.message,
          recoverySuggestion: messages.updateConfigAndCheck,
        );
      case DesktopFeatureAvailabilityStatus.requiresPermission:
      case DesktopFeatureAvailabilityStatus.permissionRequired:
        return AppFailure(
          code: AppFailureCode.permissionDenied,
          message: availability.message.trim().isEmpty
              ? '${requirement.label} requires permission.'
              : availability.message,
          recoverySuggestion: messages.grantRequiredPermission,
        );
      case DesktopFeatureAvailabilityStatus.requiresDependency:
      case DesktopFeatureAvailabilityStatus.notConfigured:
        return AppFailure(
          code: AppFailureCode.notConfigured,
          message: availability.message.trim().isEmpty
              ? '${requirement.label} requires configuration.'
              : availability.message,
          recoverySuggestion: messages.updateConfigAndCheck,
        );
      case DesktopFeatureAvailabilityStatus.notImplemented:
      case DesktopFeatureAvailabilityStatus.unsupported:
        return AppFailure(
          code: AppFailureCode.unsupported,
          message: availability.message.trim().isEmpty
              ? '${requirement.label} is not supported.'
              : availability.message,
          recoverySuggestion: messages.useSupportedPlatformVersion,
        );
    }
  }

  List<_RecordingFeatureRequirement> _requirementsForConfig(
    RecordConfig config,
  ) {
    return [
      switch (config.mode) {
        RecordMode.fullscreen => const _RecordingFeatureRequirement(
            feature: DesktopFeatureKey.recordDisplay,
            label: 'Display recording',
          ),
        RecordMode.region => const _RecordingFeatureRequirement(
            feature: DesktopFeatureKey.recordRegion,
            label: 'Region recording',
          ),
        RecordMode.window => const _RecordingFeatureRequirement(
            feature: DesktopFeatureKey.recordWindow,
            label: 'Window recording',
            allowDegraded: false,
          ),
      },
      if (config.captureAudio)
        const _RecordingFeatureRequirement(
          feature: DesktopFeatureKey.recordSystemAudio,
          label: 'System audio recording',
          allowDegraded: false,
        ),
      if (config.captureMicrophone)
        const _RecordingFeatureRequirement(
          feature: DesktopFeatureKey.recordMicrophone,
          label: 'Microphone recording',
          allowDegraded: false,
        ),
      if (config.cameraOverlay.enabled)
        const _RecordingFeatureRequirement(
          feature: DesktopFeatureKey.recordCameraOverlay,
          label: 'Camera overlay recording',
        ),
    ];
  }
}

class _RecordingFeatureRequirement {
  final DesktopFeatureKey feature;
  final String label;
  final bool allowDegraded;

  const _RecordingFeatureRequirement({
    required this.feature,
    required this.label,
    this.allowDegraded = true,
  });
}
