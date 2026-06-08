# desktop_screen_recorder federated plugin 设计说明

## 改造目标

本次改造的目标是把 `desktop_screen_recorder` 标准化为 Flutter federated plugin 架构，而不是重写录屏引擎。app-facing package 继续作为宿主应用唯一入口，平台能力通过 platform interface 和 macOS、Windows 实现包隔离。

## 为什么不采用开源 screen_recorder

开源 `screen_recorder` 主要面向 Flutter widget 录制，并以 GIF 导出为核心场景。它不能满足 GoodCleanShot 的系统级桌面录屏需求。

GoodCleanShot 需要：

- 全屏录制。
- 区域录制。
- 窗口录制。
- MP4 输出。
- GIF 输出。
- 系统音频。
- 麦克风。
- 光标。
- 点击高亮。
- 按键显示。
- 摄像头画中画。
- 权限检查。
- 错误恢复和降级提示。

因此本项目继续保留自研 `desktop_screen_recorder`，并通过 federated plugin 结构提升发布、测试、替换和后续 Linux 扩展的可维护性。

## 包职责

- `desktop_screen_recorder`：用户入口 API、facade、GetX controller、状态管理、能力校验和桌面清理可选模块。
- `desktop_screen_recorder_platform_interface`：平台抽象、公共模型、错误模型和 MethodChannel contract。
- `desktop_screen_recorder_macos`：macOS 原生注册、ScreenCaptureKit、AVAssetWriter、权限和音频设备枚举。
- `desktop_screen_recorder_windows`：Windows 原生注册、DXGI Desktop Duplication、Media Foundation、WASAPI 和能力探测。

Linux 实现包命名预留为 `desktop_screen_recorder_linux`，本阶段不实现。

