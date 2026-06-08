typedef DateTimeProvider = DateTime Function();

class RecordingStateTracker {
  final DateTimeProvider now;
  bool _isRecording = false;
  bool _isPaused = false;
  DateTime? _recordStartTime;
  Duration _pausedDuration = Duration.zero;
  DateTime? _pauseStartTime;

  RecordingStateTracker({DateTimeProvider? now})
      : now = now ?? DateTime.now;

  bool get isRecording => _isRecording;

  bool get isPaused => _isPaused;

  Duration get currentDuration {
    if (_recordStartTime == null) return Duration.zero;
    final currentTime = now();
    final elapsed = _nonNegative(currentTime.difference(_recordStartTime!));
    if (_isPaused && _pauseStartTime != null) {
      return _nonNegative(
        elapsed -
            _pausedDuration -
            _nonNegative(currentTime.difference(_pauseStartTime!)),
      );
    }
    return _nonNegative(elapsed - _pausedDuration);
  }

  void start() {
    _isRecording = true;
    _isPaused = false;
    _recordStartTime = now();
    _pausedDuration = Duration.zero;
    _pauseStartTime = null;
  }

  bool pause() {
    if (!_isRecording || _isPaused) return false;
    _isPaused = true;
    _pauseStartTime = now();
    return true;
  }

  bool resume() {
    if (!_isRecording || !_isPaused) return false;
    if (_pauseStartTime != null) {
      _pausedDuration += _nonNegative(now().difference(_pauseStartTime!));
    }
    _isPaused = false;
    _pauseStartTime = null;
    return true;
  }

  void reset() {
    _isRecording = false;
    _isPaused = false;
    _recordStartTime = null;
    _pausedDuration = Duration.zero;
    _pauseStartTime = null;
  }

  Duration _nonNegative(Duration value) {
    return value.isNegative ? Duration.zero : value;
  }
}
