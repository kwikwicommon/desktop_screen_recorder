# desktop_screen_recorder_platform_interface

`desktop_screen_recorder_platform_interface` 是 `desktop_screen_recorder` 的平台接口包。

它包含：

- `DesktopScreenRecorderPlatform` 平台抽象和 token 验证。
- `MethodChannelDesktopScreenRecorder` 默认 MethodChannel contract。
- 录屏公共模型：`RecordConfig`、`RecordResult`、`AudioDevice` 等。
- 能力与权限模型：`DesktopCapabilitySnapshot`、`DesktopPermissionInfo`、`DesktopFeatureAvailability`。
- 错误模型：`AppResult`、`AppFailure`、`AppFailureCode`。

平台实现包应继承或复用 `MethodChannelDesktopScreenRecorder`，并在 `registerWith()` 中设置：

```dart
DesktopScreenRecorderPlatform.instance = DesktopScreenRecorderMacos();
```

宿主应用不应直接依赖本包，应继续通过：

```dart
import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';
```

