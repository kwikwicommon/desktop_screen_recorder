import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('currentDuration excludes paused time', () {
    var now = DateTime(2026, 1, 1, 12);
    final tracker = RecordingStateTracker(now: () => now);

    tracker.start();
    now = now.add(const Duration(seconds: 10));
    expect(tracker.currentDuration, const Duration(seconds: 10));

    expect(tracker.pause(), isTrue);
    now = now.add(const Duration(seconds: 5));
    expect(tracker.currentDuration, const Duration(seconds: 10));

    expect(tracker.resume(), isTrue);
    now = now.add(const Duration(seconds: 7));
    expect(tracker.currentDuration, const Duration(seconds: 17));
  });

  test('currentDuration never reports negative elapsed time', () {
    var now = DateTime(2026, 1, 1, 12);
    final tracker = RecordingStateTracker(now: () => now);

    tracker.start();
    now = now.subtract(const Duration(seconds: 5));

    expect(tracker.currentDuration, Duration.zero);
  });
}
