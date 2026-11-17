import 'dart:io';

import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('desktop cleanup hide and restore icon flow', () async {
    if (Platform.environment['RUN_DESTRUCTIVE_DESKTOP_CLEANUP_TESTS'] != '1') {
      return;
    }

    final service = DesktopCleanupService(storage: MemoryCleanupStateStorage());
    final controller = service.createController(
      jobId: 'desktop-cleanup-integration',
      hideDesktopIcons: true,
    );

    final begin = await controller.begin();
    expect(begin.isSuccess, isTrue);

    final restore = await controller.restore();
    expect(restore.isSuccess, isTrue);
  });
}
