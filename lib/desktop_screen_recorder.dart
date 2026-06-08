library desktop_screen_recorder;

import 'package:desktop_screen_recorder_platform_interface/desktop_screen_recorder_platform_interface.dart'
    show
        AppFailure,
        AppResult,
        AudioDevice,
        DesktopCapabilitySnapshot,
        DesktopPermissionInfo,
        DesktopPermissionType,
        DesktopScreenRecorderPlatform,
        RecordConfig,
        RecordErrorMessages,
        RecordResult,
        defaultRecordErrorMessages;

import 'src/cleanup_state_storage.dart';
import 'src/desktop_cleanup_service.dart';
import 'src/record_service.dart';
import 'src/record_service_impl.dart';

export 'package:desktop_screen_recorder_platform_interface/desktop_screen_recorder_platform_interface.dart';
export 'src/capability_validator.dart';
export 'src/cleanup_state_storage.dart';
export 'src/desktop_cleanup_service.dart';
export 'src/record_controller.dart';
export 'src/record_service.dart';
export 'src/record_service_impl.dart';
export 'src/recording_state_tracker.dart';

typedef RecordServiceFactory = RecordService Function();

class DesktopScreenRecorder implements RecordService {
  static RecordServiceFactory _factory = _defaultFactory;
  static DesktopCleanupService _cleanupService = DesktopCleanupService.instance;
  static DesktopScreenRecorder? _instance;

  static DesktopScreenRecorder get instance {
    return _instance ??= DesktopScreenRecorder._internal();
  }

  final RecordService _service;

  DesktopScreenRecorder._internal() : _service = _factory();

  static void registerWith(RecordServiceFactory factory) {
    _instance?.dispose();
    _instance = null;
    _factory = factory;
  }

  static void configureCleanupStorage(CleanupStateStorage storage) {
    _cleanupService = DesktopCleanupService(storage: storage);
  }

  static RecordService _defaultFactory() {
    return RecordServiceImpl(
      platform: DesktopScreenRecorderPlatform.instance,
      messages: defaultRecordErrorMessages,
    );
  }

  DesktopCleanupController createCleanupController({
    String jobId = '',
    bool hideDesktopIcons = false,
    bool enableDoNotDisturb = false,
  }) {
    return _cleanupService.createController(
      jobId: jobId,
      hideDesktopIcons: hideDesktopIcons,
      enableDoNotDisturb: enableDoNotDisturb,
    );
  }

  @override
  bool get isRecording => _service.isRecording;

  @override
  bool get isPaused => _service.isPaused;

  @override
  String? get lastError => _service.lastError;

  @override
  AppFailure? get lastFailure => _service.lastFailure;

  @override
  Duration get currentDuration => _service.currentDuration;

  @override
  Future<AppResult<void>> start(RecordConfig config) => _service.start(config);

  @override
  Future<AppResult<void>> pause() => _service.pause();

  @override
  Future<AppResult<void>> resume() => _service.resume();

  @override
  Future<AppResult<RecordResult>> stop() => _service.stop();

  @override
  Future<AppResult<void>> startRecording(RecordConfig config) =>
      _service.startRecording(config);

  @override
  Future<AppResult<void>> pauseRecording() => _service.pauseRecording();

  @override
  Future<AppResult<void>> resumeRecording() => _service.resumeRecording();

  @override
  Future<AppResult<RecordResult>> stopRecording() => _service.stopRecording();

  @override
  Future<AppResult<List<AudioDevice>>> getAudioInputDevices() =>
      _service.getAudioInputDevices();

  @override
  Future<AppResult<List<AudioDevice>>> getAudioOutputDevices() =>
      _service.getAudioOutputDevices();

  @override
  Future<AppResult<DesktopCapabilitySnapshot>> queryCapabilities() =>
      _service.queryCapabilities();

  @override
  Future<AppResult<DesktopPermissionInfo>> requestPermission(
    DesktopPermissionType type,
  ) =>
      _service.requestPermission(type);

  @override
  void dispose() {
    _service.dispose();
  }
}
