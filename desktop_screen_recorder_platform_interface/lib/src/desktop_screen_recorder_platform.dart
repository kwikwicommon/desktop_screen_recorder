import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'app_error.dart';
import 'method_channel_desktop_screen_recorder.dart';
import 'models.dart';
import 'native_contract.dart';

abstract class DesktopScreenRecorderPlatform extends PlatformInterface {
  DesktopScreenRecorderPlatform() : super(token: _token);

  static final Object _token = Object();

  static DesktopScreenRecorderPlatform _instance =
      MethodChannelDesktopScreenRecorder();

  static DesktopScreenRecorderPlatform get instance => _instance;

  static set instance(DesktopScreenRecorderPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<AppResult<void>> startRecording(RecordConfig config) {
    throw UnimplementedError('startRecording() has not been implemented.');
  }

  Future<AppResult<void>> pauseRecording() {
    throw UnimplementedError('pauseRecording() has not been implemented.');
  }

  Future<AppResult<void>> resumeRecording() {
    throw UnimplementedError('resumeRecording() has not been implemented.');
  }

  Future<AppResult<RecordResult>> stopRecording() {
    throw UnimplementedError('stopRecording() has not been implemented.');
  }

  Future<AppResult<List<AudioDevice>>> listAudioInputs() {
    throw UnimplementedError('listAudioInputs() has not been implemented.');
  }

  Future<AppResult<List<AudioDevice>>> listAudioOutputs() {
    throw UnimplementedError('listAudioOutputs() has not been implemented.');
  }

  Future<AppResult<DesktopCapabilitySnapshot>> queryCapabilities() {
    throw UnimplementedError('queryCapabilities() has not been implemented.');
  }

  Future<AppResult<DesktopPermissionInfo>> requestPermission(
    DesktopPermissionType type,
  ) {
    throw UnimplementedError('requestPermission() has not been implemented.');
  }
}

