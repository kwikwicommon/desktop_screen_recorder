import 'dart:async';
import 'dart:io';

import 'app_error.dart';
import 'cleanup_state_storage.dart';
import 'messages.dart';

class DesktopCleanupSnapshot {
  final bool? macCreateDesktop;
  final int? windowsHideIcons;
  final bool? macDoNotDisturb;
  final bool hasDesktopIconState;
  final bool hasDoNotDisturbState;
  final String ownerJobId;
  final int? ownerProcessId;
  final DateTime? lockAcquiredAt;
  final DateTime? lastAuditAt;

  const DesktopCleanupSnapshot({
    this.macCreateDesktop,
    this.windowsHideIcons,
    this.macDoNotDisturb,
    this.hasDesktopIconState = false,
    this.hasDoNotDisturbState = false,
    this.ownerJobId = '',
    this.ownerProcessId,
    this.lockAcquiredAt,
    this.lastAuditAt,
  });

  bool get isEmpty => !hasDesktopIconState && !hasDoNotDisturbState;

  DesktopCleanupSnapshot copyWith({
    bool? macCreateDesktop,
    int? windowsHideIcons,
    bool? macDoNotDisturb,
    bool? hasDesktopIconState,
    bool? hasDoNotDisturbState,
    String? ownerJobId,
    int? ownerProcessId,
    DateTime? lockAcquiredAt,
    DateTime? lastAuditAt,
    bool clearDesktopIconState = false,
    bool clearDoNotDisturbState = false,
    bool clearOwnership = false,
  }) {
    return DesktopCleanupSnapshot(
      macCreateDesktop: clearDesktopIconState
          ? null
          : (macCreateDesktop ?? this.macCreateDesktop),
      windowsHideIcons: clearDesktopIconState
          ? null
          : (windowsHideIcons ?? this.windowsHideIcons),
      macDoNotDisturb: clearDoNotDisturbState
          ? null
          : (macDoNotDisturb ?? this.macDoNotDisturb),
      hasDesktopIconState: clearDesktopIconState
          ? false
          : (hasDesktopIconState ?? this.hasDesktopIconState),
      hasDoNotDisturbState: clearDoNotDisturbState
          ? false
          : (hasDoNotDisturbState ?? this.hasDoNotDisturbState),
      ownerJobId: clearOwnership ? '' : (ownerJobId ?? this.ownerJobId),
      ownerProcessId:
          clearOwnership ? null : (ownerProcessId ?? this.ownerProcessId),
      lockAcquiredAt:
          clearOwnership ? null : (lockAcquiredAt ?? this.lockAcquiredAt),
      lastAuditAt: lastAuditAt ?? this.lastAuditAt,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'macCreateDesktop': macCreateDesktop,
      'windowsHideIcons': windowsHideIcons,
      'macDoNotDisturb': macDoNotDisturb,
      'hasDesktopIconState': hasDesktopIconState,
      'hasDoNotDisturbState': hasDoNotDisturbState,
      'ownerJobId': ownerJobId,
      'ownerProcessId': ownerProcessId,
      'lockAcquiredAt': lockAcquiredAt?.toIso8601String(),
      'lastAuditAt': lastAuditAt?.toIso8601String(),
    };
  }

  static DesktopCleanupSnapshot fromJson(Object? value) {
    if (value is! Map) return const DesktopCleanupSnapshot();
    return DesktopCleanupSnapshot(
      macCreateDesktop: value['macCreateDesktop'] as bool?,
      windowsHideIcons: value['windowsHideIcons'] as int?,
      macDoNotDisturb: value['macDoNotDisturb'] as bool?,
      hasDesktopIconState: value['hasDesktopIconState'] == true,
      hasDoNotDisturbState: value['hasDoNotDisturbState'] == true,
      ownerJobId: value['ownerJobId'] as String? ?? '',
      ownerProcessId: value['ownerProcessId'] as int?,
      lockAcquiredAt: DateTime.tryParse(
        value['lockAcquiredAt'] as String? ?? '',
      ),
      lastAuditAt: DateTime.tryParse(
        value['lastAuditAt'] as String? ?? '',
      ),
    );
  }
}

class DesktopCleanupController {
  final DesktopCleanupService _service;
  final String jobId;
  final bool hideDesktopIcons;
  final bool enableDoNotDisturb;

  bool _begun = false;
  bool _restored = false;
  bool _iconsApplied = false;
  bool _dndApplied = false;

  DesktopCleanupController._({
    required DesktopCleanupService service,
    required this.jobId,
    required this.hideDesktopIcons,
    required this.enableDoNotDisturb,
  }) : _service = service;

  bool get hasActions => hideDesktopIcons || enableDoNotDisturb;

  Future<AppResult<List<AppFailure>>> begin() async {
    if (_begun) return const AppResult.success(<AppFailure>[]);
    _begun = true;
    if (!hasActions) return const AppResult.success(<AppFailure>[]);

    final diagnostics = <AppFailure>[];
    final ownership = await _service.claimJobOwnership(jobId);
    if (ownership.isFailure) {
      return AppResult.success([ownership.requireFailure]);
    }
    if (hideDesktopIcons) {
      final result = await _service.hideDesktopIconsResult(ownerJobId: jobId);
      if (result.isSuccess) {
        _iconsApplied = true;
      } else {
        diagnostics.add(result.requireFailure);
      }
    }

    if (enableDoNotDisturb) {
      final result = await _service.enableDoNotDisturbResult(
        ownerJobId: jobId,
      );
      if (result.isSuccess) {
        _dndApplied = true;
      } else {
        diagnostics.add(result.requireFailure);
      }
    }

    if (diagnostics.isNotEmpty) {
      final rollback = await restore();
      if (rollback.isSuccess) {
        diagnostics.addAll(rollback.requireValue);
      } else {
        diagnostics.add(rollback.requireFailure);
      }
    }

    return AppResult.success(diagnostics);
  }

  Future<AppResult<List<AppFailure>>> restore() async {
    if (_restored) return const AppResult.success(<AppFailure>[]);
    final diagnostics = <AppFailure>[];

    if (_dndApplied) {
      final result = await _service.disableDoNotDisturbResult(
        ownerJobId: jobId,
      );
      if (result.isFailure) {
        diagnostics.add(result.requireFailure);
      } else {
        _dndApplied = false;
      }
    }
    if (_iconsApplied) {
      final result = await _service.showDesktopIconsResult(ownerJobId: jobId);
      if (result.isFailure) {
        diagnostics.add(result.requireFailure);
      } else {
        _iconsApplied = false;
      }
    }

    _restored = !_iconsApplied && !_dndApplied;
    if (_restored) {
      await _service.releaseJobOwnership(jobId);
    }
    return AppResult.success(diagnostics);
  }
}

class DesktopCleanupService {
  static const String _snapshotKey = 'desktop_cleanup_snapshot';
  static const String _auditKey = 'desktop_cleanup_audit';
  static final DesktopCleanupService instance = DesktopCleanupService(
    storage: MemoryCleanupStateStorage(),
  );

  final CleanupStateStorage storage;
  final RecordErrorMessages messages;

  bool _iconsHidden = false;
  bool _dndEnabled = false;
  String _activeJobId = '';
  AppFailure? _lastFailure;
  DesktopCleanupSnapshot _memorySnapshot = const DesktopCleanupSnapshot();

  DesktopCleanupService({
    required this.storage,
    this.messages = defaultRecordErrorMessages,
  });

  bool get isMacOS => Platform.isMacOS;

  bool get isWindows => Platform.isWindows;

  bool get iconsHidden => _iconsHidden;

  bool get dndEnabled => _dndEnabled;

  AppFailure? get lastFailure => _lastFailure;

  DesktopCleanupController createController({
    String jobId = '',
    bool hideDesktopIcons = false,
    bool enableDoNotDisturb = false,
  }) {
    return DesktopCleanupController._(
      service: this,
      jobId: jobId,
      hideDesktopIcons: hideDesktopIcons,
      enableDoNotDisturb: enableDoNotDisturb,
    );
  }

  Future<AppResult<void>> hideDesktopIconsResult({
    String ownerJobId = '',
  }) async {
    if (!isMacOS && !isWindows) return _unsupportedPlatformFailure();

    try {
      final ownership = await _ensureOwnership(ownerJobId);
      if (ownership.isFailure) return ownership;
      var snapshot = await _loadSnapshot();
      if (!snapshot.hasDesktopIconState) {
        snapshot = await _captureDesktopIconState(snapshot);
        snapshot = _attachOwnership(snapshot, ownerJobId);
        await _saveSnapshot(snapshot);
      }

      final result = isMacOS
          ? await _setMacDesktopIconsVisible(false)
          : await _setWindowsDesktopIconsHidden(1);
      if (result.isFailure) return result;

      _iconsHidden = true;
      _lastFailure = null;
      return const AppResult<void>.success();
    } catch (error, stackTrace) {
      return _failure(
        AppFailureCode.platform,
        messages.desktopIconsHideFailed,
        error,
        stackTrace,
      );
    }
  }

  Future<AppResult<void>> showDesktopIconsResult({
    String ownerJobId = '',
  }) async {
    if (!isMacOS && !isWindows) return _unsupportedPlatformFailure();

    try {
      final ownership = await _ensureOwnership(ownerJobId);
      if (ownership.isFailure) return ownership;
      final snapshot = await _loadSnapshot();
      final result = await _restoreDesktopIcons(snapshot);
      if (result.isFailure) return result;

      await _saveSnapshot(snapshot.copyWith(clearDesktopIconState: true));
      _iconsHidden = false;
      _lastFailure = null;
      return const AppResult<void>.success();
    } catch (error, stackTrace) {
      return _failure(
        AppFailureCode.platform,
        messages.desktopIconsRestoreFailed,
        error,
        stackTrace,
      );
    }
  }

  Future<AppResult<void>> enableDoNotDisturbResult({
    String ownerJobId = '',
  }) async {
    if (!isMacOS) return _unsupportedPlatformFailure();

    try {
      final ownership = await _ensureOwnership(ownerJobId);
      if (ownership.isFailure) return ownership;
      var snapshot = await _loadSnapshot();
      if (!snapshot.hasDoNotDisturbState) {
        snapshot = snapshot.copyWith(
          macDoNotDisturb: await _readMacDoNotDisturb(),
          hasDoNotDisturbState: true,
        );
        snapshot = _attachOwnership(snapshot, ownerJobId);
        await _saveSnapshot(snapshot);
      }

      final result = await _setMacDoNotDisturb(true);
      if (result.isFailure) return result;

      _dndEnabled = true;
      _lastFailure = null;
      return const AppResult<void>.success();
    } catch (error, stackTrace) {
      return _failure(
        AppFailureCode.platform,
        messages.doNotDisturbEnableFailed,
        error,
        stackTrace,
      );
    }
  }

  Future<AppResult<void>> disableDoNotDisturbResult({
    String ownerJobId = '',
  }) async {
    if (!isMacOS) return _unsupportedPlatformFailure();

    try {
      final ownership = await _ensureOwnership(ownerJobId);
      if (ownership.isFailure) return ownership;
      final snapshot = await _loadSnapshot();
      final result = await _restoreDoNotDisturb(snapshot);
      if (result.isFailure) return result;

      await _saveSnapshot(snapshot.copyWith(clearDoNotDisturbState: true));
      _dndEnabled = false;
      _lastFailure = null;
      return const AppResult<void>.success();
    } catch (error, stackTrace) {
      return _failure(
        AppFailureCode.platform,
        messages.doNotDisturbDisableFailed,
        error,
        stackTrace,
      );
    }
  }

  Future<AppResult<void>> restoreAllResult() async {
    final snapshot = await _loadSnapshot();
    final failures = <AppFailure>[];
    if (_iconsHidden || snapshot.hasDesktopIconState) {
      final result = await showDesktopIconsResult();
      if (result.isFailure) failures.add(result.requireFailure);
    }
    if (_dndEnabled || snapshot.hasDoNotDisturbState) {
      final result = await disableDoNotDisturbResult();
      if (result.isFailure) failures.add(result.requireFailure);
    }
    if (failures.isEmpty) {
      await _clearSnapshot();
      _iconsHidden = false;
      _dndEnabled = false;
      _activeJobId = '';
      _lastFailure = null;
      return const AppResult.success();
    }
    return AppResult.failure(
      AppFailure(
        code: failures.first.code,
        message: failures.map((failure) => failure.message).join(' '),
        recoverySuggestion: failures
            .map((failure) => failure.recoverySuggestion)
            .join(' '),
        cause: failures,
      ),
    );
  }

  Future<AppResult<void>> recoverPendingState() async {
    final snapshot = await _loadSnapshot();
    if (snapshot.isEmpty) return const AppResult<void>.success();
    await _auditAbandonedSnapshot(snapshot);

    if (snapshot.hasDesktopIconState) {
      final result = await _restoreDesktopIcons(snapshot);
      if (result.isFailure) return result;
    }
    if (snapshot.hasDoNotDisturbState && isMacOS) {
      final result = await _restoreDoNotDisturb(snapshot);
      if (result.isFailure) return result;
    }

    await _clearSnapshot();
    _iconsHidden = false;
    _dndEnabled = false;
    _activeJobId = '';
    _lastFailure = null;
    return const AppResult<void>.success();
  }

  void dispose() {
    unawaited(restoreAllResult());
  }

  Future<AppResult<void>> claimJobOwnership(String jobId) async {
    if (jobId.trim().isEmpty) return const AppResult<void>.success();
    if (_activeJobId.isNotEmpty && _activeJobId != jobId) {
      return _failure(
        AppFailureCode.unavailable,
        messages.desktopCleanupCommandFailed,
        {'activeJobId': _activeJobId, 'requestedJobId': jobId},
        null,
      );
    }

    final snapshot = await _loadSnapshot();
    if (!snapshot.isEmpty &&
        snapshot.ownerProcessId != null &&
        snapshot.ownerProcessId != pid) {
      await _auditAbandonedSnapshot(snapshot);
      return _failure(
        AppFailureCode.unavailable,
        messages.desktopCleanupCommandFailed,
        {
          'ownerJobId': snapshot.ownerJobId,
          'ownerProcessId': snapshot.ownerProcessId,
          'currentProcessId': pid,
          'requestedJobId': jobId,
        },
        null,
      );
    }
    if (!snapshot.isEmpty &&
        snapshot.ownerJobId.isNotEmpty &&
        snapshot.ownerJobId != jobId) {
      await _auditAbandonedSnapshot(snapshot);
      return _failure(
        AppFailureCode.unavailable,
        messages.desktopCleanupCommandFailed,
        {
          'ownerJobId': snapshot.ownerJobId,
          'ownerProcessId': snapshot.ownerProcessId,
          'requestedJobId': jobId,
        },
        null,
      );
    }

    _activeJobId = jobId;
    return const AppResult<void>.success();
  }

  Future<void> releaseJobOwnership(String jobId) async {
    if (_activeJobId == jobId) {
      _activeJobId = '';
    }
    final snapshot = await _loadSnapshot();
    if (snapshot.isEmpty && snapshot.ownerJobId == jobId) {
      await _saveSnapshot(snapshot.copyWith(clearOwnership: true));
    }
  }

  Future<AppResult<void>> _ensureOwnership(String requestedJobId) async {
    final jobId = requestedJobId.trim().isEmpty ? _activeJobId : requestedJobId;
    if (jobId.trim().isEmpty) return const AppResult<void>.success();
    if (_activeJobId.isNotEmpty && _activeJobId != jobId) {
      return _failure(
        AppFailureCode.unavailable,
        messages.desktopCleanupCommandFailed,
        {'activeJobId': _activeJobId, 'requestedJobId': jobId},
        null,
      );
    }
    return claimJobOwnership(jobId);
  }

  DesktopCleanupSnapshot _attachOwnership(
    DesktopCleanupSnapshot snapshot,
    String requestedJobId,
  ) {
    final jobId = requestedJobId.trim().isEmpty ? _activeJobId : requestedJobId;
    if (jobId.trim().isEmpty) return snapshot;
    return snapshot.copyWith(
      ownerJobId: jobId,
      ownerProcessId: pid,
      lockAcquiredAt: snapshot.lockAcquiredAt ?? DateTime.now(),
    );
  }

  Future<void> _auditAbandonedSnapshot(DesktopCleanupSnapshot snapshot) async {
    final audited = snapshot.copyWith(lastAuditAt: DateTime.now());
    final existing = await _loadAuditLog();
    existing.add({
      'ownerJobId': snapshot.ownerJobId,
      'ownerProcessId': snapshot.ownerProcessId,
      'currentProcessId': pid,
      'lockAcquiredAt': snapshot.lockAcquiredAt?.toIso8601String(),
      'auditedAt': audited.lastAuditAt?.toIso8601String(),
      'hasDesktopIconState': snapshot.hasDesktopIconState,
      'hasDoNotDisturbState': snapshot.hasDoNotDisturbState,
    });
    final first = existing.length > 20 ? existing.length - 20 : 0;
    try {
      await storage.save(_auditKey, {
        'entries': existing.skip(first).toList(),
      });
      await _saveSnapshot(audited);
    } catch (_) {}
  }

  Future<List<Map<String, Object?>>> _loadAuditLog() async {
    try {
      final value = await storage.load(_auditKey);
      final entries = value?['entries'];
      if (entries is! List) return <Map<String, Object?>>[];
      return entries.whereType<Map>().map((entry) {
        return entry.map(
          (key, value) => MapEntry(key.toString(), value as Object?),
        );
      }).toList();
    } catch (_) {
      return <Map<String, Object?>>[];
    }
  }

  Future<DesktopCleanupSnapshot> _captureDesktopIconState(
    DesktopCleanupSnapshot snapshot,
  ) async {
    if (isMacOS) {
      return snapshot.copyWith(
        macCreateDesktop: await _readMacCreateDesktop(),
        hasDesktopIconState: true,
      );
    }

    return snapshot.copyWith(
      windowsHideIcons: await _readWindowsHideIcons(),
      hasDesktopIconState: true,
    );
  }

  Future<AppResult<void>> _restoreDesktopIcons(
    DesktopCleanupSnapshot snapshot,
  ) {
    if (isMacOS) {
      return snapshot.hasDesktopIconState
          ? _restoreMacDesktopIcons(snapshot.macCreateDesktop)
          : _setMacDesktopIconsVisible(true);
    }

    if (isWindows) {
      return snapshot.hasDesktopIconState
          ? _restoreWindowsDesktopIcons(snapshot.windowsHideIcons)
          : _setWindowsDesktopIconsHidden(0);
    }

    return Future.value(_unsupportedPlatformFailure());
  }

  Future<AppResult<void>> _restoreDoNotDisturb(
    DesktopCleanupSnapshot snapshot,
  ) {
    if (!isMacOS) return Future.value(_unsupportedPlatformFailure());
    if (!snapshot.hasDoNotDisturbState) {
      return _setMacDoNotDisturb(false);
    }
    final previous = snapshot.macDoNotDisturb;
    if (previous == null) {
      return _deleteMacDoNotDisturb();
    }
    return _setMacDoNotDisturb(previous);
  }

  Future<bool?> _readMacCreateDesktop() async {
    final result = await Process.run('defaults', [
      'read',
      'com.apple.finder',
      'CreateDesktop',
    ]);
    if (result.exitCode != 0) return null;
    return _parseBool(result.stdout.toString());
  }

  Future<int?> _readWindowsHideIcons() async {
    final result = await Process.run('powershell', [
      '-NoProfile',
      '-Command',
      r'''
$Path = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced"
$Value = (Get-ItemProperty -Path $Path -Name "HideIcons" -ErrorAction SilentlyContinue).HideIcons
if ($null -eq $Value) { "" } else { $Value }
''',
    ]);
    if (result.exitCode != 0) return null;
    return int.tryParse(result.stdout.toString().trim());
  }

  Future<bool?> _readMacDoNotDisturb() async {
    final result = await Process.run('defaults', [
      '-currentHost',
      'read',
      'com.apple.notificationcenterui',
      'doNotDisturb',
    ]);
    if (result.exitCode != 0) return null;
    return _parseBool(result.stdout.toString());
  }

  Future<AppResult<void>> _setMacDesktopIconsVisible(bool visible) async {
    final result = await Process.run('defaults', [
      'write',
      'com.apple.finder',
      'CreateDesktop',
      '-bool',
      visible ? 'true' : 'false',
    ]);
    if (result.exitCode != 0) return _processFailure(result.stderr);
    return _restartFinder();
  }

  Future<AppResult<void>> _restoreMacDesktopIcons(bool? previous) async {
    if (previous == null) {
      final result = await Process.run('defaults', [
        'delete',
        'com.apple.finder',
        'CreateDesktop',
      ]);
      if (result.exitCode != 0) return _processFailure(result.stderr);
      return _restartFinder();
    }
    return _setMacDesktopIconsVisible(previous);
  }

  Future<AppResult<void>> _setWindowsDesktopIconsHidden(int hidden) async {
    return _runWindowsDesktopScript('''
\$Path = "HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"
New-Item -Path \$Path -Force | Out-Null
Set-ItemProperty -Path \$Path -Name "HideIcons" -Value $hidden
Stop-Process -Name explorer -Force
Start-Process explorer
''');
  }

  Future<AppResult<void>> _restoreWindowsDesktopIcons(int? previous) async {
    if (previous == null) {
      return _runWindowsDesktopScript(r'''
$Path = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced"
Remove-ItemProperty -Path $Path -Name "HideIcons" -ErrorAction SilentlyContinue
Stop-Process -Name explorer -Force
Start-Process explorer
''');
    }
    return _setWindowsDesktopIconsHidden(previous);
  }

  Future<AppResult<void>> _setMacDoNotDisturb(bool enabled) async {
    final result = await Process.run('defaults', [
      '-currentHost',
      'write',
      'com.apple.notificationcenterui',
      'doNotDisturb',
      '-boolean',
      enabled ? 'true' : 'false',
    ]);
    if (result.exitCode != 0) return _processFailure(result.stderr);
    return _restartNotificationCenter();
  }

  Future<AppResult<void>> _deleteMacDoNotDisturb() async {
    final result = await Process.run('defaults', [
      '-currentHost',
      'delete',
      'com.apple.notificationcenterui',
      'doNotDisturb',
    ]);
    if (result.exitCode != 0) return _processFailure(result.stderr);
    return _restartNotificationCenter();
  }

  Future<AppResult<void>> _restartFinder() async {
    final result = await Process.run('killall', ['Finder']);
    if (result.exitCode != 0) return _processFailure(result.stderr);
    return const AppResult<void>.success();
  }

  Future<AppResult<void>> _restartNotificationCenter() async {
    final result = await Process.run('killall', ['NotificationCenter']);
    if (result.exitCode != 0) return _processFailure(result.stderr);
    return const AppResult<void>.success();
  }

  Future<AppResult<void>> _runWindowsDesktopScript(String script) async {
    final result = await Process.run('powershell', [
      '-NoProfile',
      '-Command',
      script,
    ]);
    if (result.exitCode != 0) return _processFailure(result.stderr);
    return const AppResult<void>.success();
  }

  Future<DesktopCleanupSnapshot> _loadSnapshot() async {
    try {
      final value = await storage.load(_snapshotKey);
      _memorySnapshot = DesktopCleanupSnapshot.fromJson(value);
      return _memorySnapshot;
    } catch (_) {
      return _memorySnapshot;
    }
  }

  Future<void> _saveSnapshot(DesktopCleanupSnapshot snapshot) async {
    _memorySnapshot = snapshot;
    try {
      if (snapshot.isEmpty) {
        await storage.delete(_snapshotKey);
      } else {
        await storage.save(_snapshotKey, snapshot.toJson());
      }
    } catch (_) {}
  }

  Future<void> _clearSnapshot() async {
    _memorySnapshot = const DesktopCleanupSnapshot();
    try {
      await storage.delete(_snapshotKey);
    } catch (_) {}
  }

  bool? _parseBool(String raw) {
    final value = raw.trim().toLowerCase();
    if (value == '1' || value == 'true' || value == 'yes') return true;
    if (value == '0' || value == 'false' || value == 'no') return false;
    return null;
  }

  AppResult<void> _processFailure(Object? stderr) {
    return _failure(
      AppFailureCode.platform,
      stderr?.toString().trim().isNotEmpty == true
          ? stderr.toString()
          : messages.desktopCleanupCommandFailed,
      null,
      null,
    );
  }

  AppResult<void> _unsupportedPlatformFailure() {
    return _failure(
      AppFailureCode.unsupportedPlatform,
      messages.desktopCleanupUnsupported,
      null,
      null,
    );
  }

  AppResult<void> _failure(
    AppFailureCode code,
    String message,
    Object? cause,
    StackTrace? stackTrace,
  ) {
    final failure = AppFailure(
      code: code,
      message: message,
      recoverySuggestion: messages.continueWithoutDesktopCleanup,
      cause: cause,
      stackTrace: stackTrace,
    );
    _lastFailure = failure;
    return AppResult.failure(failure);
  }
}
