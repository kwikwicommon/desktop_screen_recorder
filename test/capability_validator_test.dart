import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('validator rejects unavailable required feature', () async {
    final validator = RecordingCapabilityValidator(
      platform: _FakePlatform(
        const DesktopCapabilitySnapshot(
          permissions: {},
          features: {
            DesktopFeatureKey.recordWindow: DesktopFeatureAvailability(
              feature: DesktopFeatureKey.recordWindow,
              status: DesktopFeatureAvailabilityStatus.permissionRequired,
              message: 'Window recording requires permission.',
            ),
          },
        ),
      ),
    );

    final result = await validator.validate(
      const RecordConfig(mode: RecordMode.window, windowId: 1),
    );

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.permissionDenied);
  });

  test('validator converts capability probe exceptions to failures', () async {
    final validator = RecordingCapabilityValidator(
      platform: _FakePlatform(
        const DesktopCapabilitySnapshot(permissions: {}, features: {}),
        throwOnQuery: true,
      ),
    );

    final result = await validator.validate(const RecordConfig());

    expect(result.isFailure, isTrue);
    expect(result.requireFailure.code, AppFailureCode.nativeFailure);
  });
}

class _FakePlatform extends DesktopScreenRecorderPlatform {
  final DesktopCapabilitySnapshot snapshot;
  final bool throwOnQuery;

  _FakePlatform(
    this.snapshot, {
    this.throwOnQuery = false,
  });

  @override
  Future<AppResult<DesktopCapabilitySnapshot>> queryCapabilities() async {
    if (throwOnQuery) {
      throw StateError('Capability bridge failed.');
    }
    return AppResult.success(snapshot);
  }

  @override
  Future<AppResult<DesktopPermissionInfo>> requestPermission(
    DesktopPermissionType permissionType,
  ) async {
    return AppResult.success(
      DesktopPermissionInfo(
        type: permissionType,
        state: DesktopPermissionState.granted,
      ),
    );
  }
}
