import 'native_contract.dart';

typedef DesktopPlatformResolver = bool Function();

class RecordErrorMessages {
  final String unsupportedPlatform;
  final String alreadyRecording;
  final String recordingOperationInProgress;
  final String windowRequired;
  final String regionRequired;
  final String invalidFrameRate;
  final String invalidGifConfig;
  final String invalidCameraOverlay;
  final String gifAudioUnsupported;
  final String noActiveRecording;
  final String recordingNotPaused;
  final String nativeStartFailed;
  final String nativePauseFailed;
  final String nativeResumeFailed;
  final String noRecordingResult;
  final String audioDeviceEnumerationUnsupported;
  final String nativePayloadParseFailed;
  final String nativeOperationFailed;
  final String nativeBridgeNotRegistered;
  final String nativeBridgeInvocationFailed;
  final String checkPlatformChannelContract;
  final String useSupportedDesktopBuild;
  final String inspectPlatformChannelHandler;
  final String checkRecordingPermission;
  final String updateConfigAndCheck;
  final String grantRequiredPermission;
  final String useSupportedPlatformVersion;
  final String continueWithoutDesktopCleanup;
  final String desktopCleanupUnsupported;
  final String desktopCleanupCommandFailed;
  final String desktopIconsHideFailed;
  final String desktopIconsRestoreFailed;
  final String doNotDisturbEnableFailed;
  final String doNotDisturbDisableFailed;

  const RecordErrorMessages({
    this.unsupportedPlatform =
        'Desktop recording is only supported on macOS and Windows.',
    this.alreadyRecording = 'A recording is already in progress.',
    this.recordingOperationInProgress =
        'A recording operation is already in progress.',
    this.windowRequired = 'A target window is required for window recording.',
    this.regionRequired =
        'A valid target region is required for region recording.',
    this.invalidFrameRate = 'Recording frame rate must be greater than zero.',
    this.invalidGifConfig =
        'GIF recording requires a positive max width and frame rate.',
    this.invalidCameraOverlay =
        'Camera overlay size must be greater than zero.',
    this.gifAudioUnsupported =
        'GIF recording does not support system audio or microphone capture.',
    this.noActiveRecording = 'No recording is active.',
    this.recordingNotPaused = 'No paused recording is active.',
    this.nativeStartFailed = 'Native recorder did not start.',
    this.nativePauseFailed = 'Native recorder did not pause.',
    this.nativeResumeFailed = 'Native recorder did not resume.',
    this.noRecordingResult = 'Native recorder did not return a result.',
    this.audioDeviceEnumerationUnsupported =
        'Audio device enumeration is not supported on this platform.',
    this.nativePayloadParseFailed = 'Native payload could not be parsed.',
    this.nativeOperationFailed = 'Native operation failed.',
    this.nativeBridgeNotRegistered = 'Native bridge is not registered.',
    this.nativeBridgeInvocationFailed = 'Native bridge invocation failed.',
    this.checkPlatformChannelContract =
        'Check the platform channel contract and native plugin registration.',
    this.useSupportedDesktopBuild = 'Use a supported desktop build.',
    this.inspectPlatformChannelHandler =
        'Inspect the platform channel handler.',
    this.checkRecordingPermission =
        'Check screen recording and audio permissions.',
    this.updateConfigAndCheck = 'Update the recording configuration and retry.',
    this.grantRequiredPermission = 'Grant the required permission and retry.',
    this.useSupportedPlatformVersion =
        'Use a platform version that supports this recording feature.',
    this.continueWithoutDesktopCleanup =
        'Continue without desktop cleanup or restore it manually.',
    this.desktopCleanupUnsupported =
        'Desktop cleanup is only supported on macOS and Windows.',
    this.desktopCleanupCommandFailed = 'Desktop cleanup command failed.',
    this.desktopIconsHideFailed = 'Desktop icons could not be hidden.',
    this.desktopIconsRestoreFailed = 'Desktop icons could not be restored.',
    this.doNotDisturbEnableFailed = 'Do Not Disturb could not be enabled.',
    this.doNotDisturbDisableFailed = 'Do Not Disturb could not be disabled.',
  });

  String featureName(DesktopFeatureKey feature) {
    switch (feature) {
      case DesktopFeatureKey.displayEnumeration:
        return 'display enumeration';
      case DesktopFeatureKey.windowEnumeration:
        return 'window enumeration';
      case DesktopFeatureKey.captureDisplay:
        return 'display capture';
      case DesktopFeatureKey.captureWindow:
        return 'window capture';
      case DesktopFeatureKey.captureRegion:
        return 'region capture';
      case DesktopFeatureKey.recordDisplay:
        return 'display recording';
      case DesktopFeatureKey.recordRegion:
        return 'region recording';
      case DesktopFeatureKey.recordWindow:
        return 'window recording';
      case DesktopFeatureKey.recordSystemAudio:
        return 'system audio recording';
      case DesktopFeatureKey.recordMicrophone:
        return 'microphone recording';
      case DesktopFeatureKey.recordCameraOverlay:
        return 'camera overlay recording';
      case DesktopFeatureKey.imageClipboard:
        return 'image clipboard';
      case DesktopFeatureKey.scrollAutomation:
        return 'scroll automation';
      case DesktopFeatureKey.ocr:
        return 'OCR';
    }
  }
}

const defaultRecordErrorMessages = RecordErrorMessages();
