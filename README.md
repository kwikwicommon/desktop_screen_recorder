# desktop_screen_recorder

`desktop_screen_recorder` 是 GoodCleanShot 的 app-facing federated plugin。它只暴露统一 Dart API、状态管理、能力校验和桌面清理控制；平台能力通过 `desktop_screen_recorder_platform_interface` 抽象，并由 macOS、Windows 实现包注册。

包结构：

- `desktop_screen_recorder`：宿主应用唯一依赖，提供 `DesktopScreenRecorder.instance`、`RecordService`、`RecordController` 和 cleanup API。
- `desktop_screen_recorder_platform_interface`：公共模型、错误模型、平台抽象和统一 MethodChannel contract。
- `desktop_screen_recorder_macos`：ScreenCaptureKit、AVAssetWriter、macOS 权限和音频设备枚举。
- `desktop_screen_recorder_windows`：DXGI Desktop Duplication、Media Foundation、WASAPI、WIC GIF 和能力探测。

## 安装

```yaml
dependencies:
  desktop_screen_recorder:
    path: packages/desktop_screen_recorder
```

父包会通过 federated plugin 配置自动选择：

- `desktop_screen_recorder_macos`
- `desktop_screen_recorder_windows`

宿主应用不需要显式依赖平台实现包。

## 基本用法

```dart
import 'package:desktop_screen_recorder/desktop_screen_recorder.dart';

final recorder = DesktopScreenRecorder.instance;

final startResult = await recorder.start(
  const RecordConfig(
    mode: RecordMode.fullscreen,
    format: RecordFormat.mp4,
    captureAudio: true,
    showCursor: true,
  ),
);

if (startResult.isFailure) {
  throw startResult.requireFailure;
}

await recorder.pause();
await recorder.resume();

final stopResult = await recorder.stop();
if (stopResult.isSuccess) {
  final artifact = stopResult.requireValue;
  print(artifact.filePath);
}
```

## GetX 集成

```dart
Get.lazyPut<RecordService>(
  () => DesktopScreenRecorder.instance,
  fenix: true,
);

Get.put(RecordController(), permanent: true);
```

## 可注入错误消息

父包不依赖宿主应用 i18n。宿主如需本地化，可创建自定义 `RecordErrorMessages` 并注入 `RecordServiceImpl`。

## 桌面清理存储

父包不依赖 Hive。宿主应用可通过 `CleanupStateStorage` 接入自己的持久化实现：

```dart
DesktopScreenRecorder.configureCleanupStorage(MyCleanupStorage());
```

## 原生协议

平台子包使用统一 Channel：`desktop_screen_recorder/native`。

Method 名称：

- `record.start`
- `record.pause`
- `record.resume`
- `record.stop`
- `record.listAudioInputs`
- `record.listAudioOutputs`
- `capability.queryPermissions`
- `capability.queryCapabilities`（兼容别名）
- `capability.requestPermission`

返回信封格式：

```dart
{
  'status': 'success',
  'payload': value,
}
```

## macOS 权限和能力

- 录屏使用 ScreenCaptureKit，最低能力边界为 macOS 12.3。
- 系统音频录制依赖 macOS 13 及以上能力。
- 麦克风和摄像头画中画需要对应隐私权限。
- macOS 15 上麦克风与系统音频组合能力更完整，低版本会通过 capability snapshot 返回 `unsupported`、`permissionRequired` 或 `degraded`。

## Windows 能力边界

- 全屏录制基于 DXGI Desktop Duplication。
- 区域录制在捕获帧后裁剪。
- 窗口录制会跟随目标窗口，窗口关闭时返回 `completedEarly`。
- MP4 输出使用 Media Foundation。
- GIF 输出使用 WIC 编码路径。
- 系统音频与麦克风使用 WASAPI；设备不可用时通过失败或降级事件反馈。

## MP4、GIF 和音频限制

- MP4 支持系统音频、麦克风、光标、点击高亮、按键显示和摄像头画中画，具体可用性以 `queryCapabilities()` 返回为准。
- GIF 不支持系统音频或麦克风；Dart 层会在调用原生前拦截 GIF + audio 配置。
- `RecordMode.window` 必须提供有效 `windowId`。
- 所有可恢复失败应尽量包含 `recoverySuggestion`，供宿主应用展示修复建议。

## 为什么不使用 screen_recorder

开源 `screen_recorder` 面向 Flutter widget 录制和 GIF 导出，不能覆盖 GoodCleanShot 所需的系统级桌面录屏能力。GoodCleanShot 需要全屏、区域、窗口录制、MP4、GIF、系统音频、麦克风、光标、点击高亮、按键显示、摄像头画中画、权限检查和错误恢复，因此继续保留自研桌面录屏实现。本次改造目标是标准化 federated plugin 架构，不重写录屏引擎。

失败返回：

```dart
{
  'status': 'nativeFailure',
  'failure': {
    'message': 'Operation failed',
    'recoverySuggestion': 'Retry recording',
  },
}
```
