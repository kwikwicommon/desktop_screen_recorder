import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('recording timer stops on pause and disposal', (tester) async {
    final controller = RecordController();
    var notifications = 0;
    controller.addListener(() => notifications++);
    controller.startRecording();
    await tester.pump(const Duration(seconds: 2));
    expect(controller.duration.value, const Duration(seconds: 2));
    expect(notifications, greaterThan(0));

    controller.pauseRecording();
    await tester.pump(const Duration(seconds: 2));
    expect(controller.duration.value, const Duration(seconds: 2));
    controller.resumeRecording();
    await tester.pump(const Duration(seconds: 1));
    expect(controller.duration.value, const Duration(seconds: 3));

    controller.dispose();
    final finalNotifications = notifications;
    await tester.pump(const Duration(seconds: 2));
    expect(notifications, finalNotifications);
    expect(tester.takeException(), isNull);
  });
}
