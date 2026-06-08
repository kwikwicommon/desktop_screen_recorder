import 'app_error.dart';
import 'models.dart';
import 'native_contract.dart';

abstract class RecordService {
  Future<AppResult<void>> start(RecordConfig config);

  Future<AppResult<void>> pause();

  Future<AppResult<void>> resume();

  Future<AppResult<RecordResult>> stop();

  Future<AppResult<void>> startRecording(RecordConfig config) => start(config);

  Future<AppResult<void>> pauseRecording() => pause();

  Future<AppResult<void>> resumeRecording() => resume();

  Future<AppResult<RecordResult>> stopRecording() => stop();

  bool get isRecording;

  bool get isPaused;

  Duration get currentDuration;

  Future<AppResult<List<AudioDevice>>> getAudioInputDevices();

  Future<AppResult<List<AudioDevice>>> getAudioOutputDevices();

  Future<AppResult<DesktopCapabilitySnapshot>> queryCapabilities();

  Future<AppResult<DesktopPermissionInfo>> requestPermission(
    DesktopPermissionType type,
  );

  String? get lastError;

  AppFailure? get lastFailure;

  void dispose();
}
