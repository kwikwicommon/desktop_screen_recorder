import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('cleanup snapshot round trips through injected storage', () async {
    final storage = MemoryCleanupStateStorage();
    const snapshot = DesktopCleanupSnapshot(
      macCreateDesktop: true,
      hasDesktopIconState: true,
      ownerJobId: 'job-1',
      ownerProcessId: 42,
    );

    await storage.save('snapshot', snapshot.toJson());
    final loaded = DesktopCleanupSnapshot.fromJson(
      await storage.load('snapshot'),
    );

    expect(loaded.macCreateDesktop, isTrue);
    expect(loaded.hasDesktopIconState, isTrue);
    expect(loaded.ownerJobId, 'job-1');

    await storage.delete('snapshot');
    expect(await storage.load('snapshot'), isNull);
  });

  test('memory cleanup storage isolates nested values', () async {
    final storage = MemoryCleanupStateStorage();
    final saved = {
      'entries': [
        {'ownerJobId': 'job-1'},
      ],
    };

    await storage.save('audit', saved);
    saved['entries'] = <Map<String, String>>[];

    final loaded = await storage.load('audit');
    final entries = loaded?['entries'] as List<dynamic>;
    final first = Map<String, dynamic>.from(entries.first as Map);
    first['ownerJobId'] = 'changed';
    final reloaded = await storage.load('audit');
    final reloadedEntries = reloaded?['entries'] as List<dynamic>;
    final reloadedFirst = Map<String, dynamic>.from(
      reloadedEntries.first as Map,
    );

    expect(reloadedFirst['ownerJobId'], 'job-1');
  });
}
