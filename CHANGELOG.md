# 变更记录

本文件记录 MediaHub 的版本变更。格式参考 Keep a Changelog，版本号遵循语义化
版本。

## [未发布]

### 已完成

- 阶段 0：项目文档与边界确定（2026-08-03）
  - 确定项目名称 `MediaHub`，可执行文件 `MediaHub.exe`。
  - 确定播放内核方案：v0.1 使用 libVLC，保留 `PlayerEngine` 可替换抽象接口，
    v0.3 补充自研 FFmpeg 内核并与之并存。
  - 确定 v0.1 范围：仅本地音视频播放，不含任何网络能力。
  - 确定 12 个开发阶段的产出与通过条件。
  - 建立规划、设计、测试、学习、交付五类文档。
  - 确认本机开发环境：Qt 5.14.2 msvc2017_64、MSVC 14.44、Ninja、CMake 4.4。
- 阶段 1：环境准备与依赖接入（2026-08-03）
  - 从 VideoLAN 官方获取 VLC 3.0.21 win64 SDK，并通过官方 SHA-256 校验。
  - 通过 `MEDIAHUB_VLC_ROOT` 接入外部 SDK，缺失或错误路径在配置阶段明确失败。
  - 增加 `MediaHubVlcProbe.exe`，使用 RAII 创建和释放 libVLC 实例。
  - Debug 与 Release 均输出 `libVLC version: 3.0.21 Vetinari`，CTest 1/1 通过。
  - 初始化本地 Git 仓库和构建产物忽略规则，并配置 Gitee SSH 远程仓库。
- 阶段 2：最小工程骨架（2026-08-03）
  - 固定接入 GoogleTest 1.17.0，并保留完整上游源码和许可证。
  - 建立 `mediahub_core`、`mediahub_engine_vlc`、`mediahub_gui_support`、
    `mediahub_gui`、`mediahub_tests` 和 `mediahub_gui_tests` 基础目标。
  - 使用 Qt 5.14.2 Widgets 生成可启动和正常关闭的空窗口 `MediaHub.exe`。
  - 接入 GoogleTest、Qt Test、CTest 和构建后 Qt 运行库部署。
  - Debug、Release CTest 均为 3/3 通过，依赖隔离与无测试构建验证通过。
- 阶段 3：播放内核抽象与状态机（2026-08-03）
  - 定义媒体项、来源、播放状态、位置、错误和播放模式等纯 C++ 值类型。
  - 建立不暴露 Qt、libVLC、FFmpeg 或 Windows 类型的 `PlayerEngine` 与事件接口。
  - 建立 8 状态转换表，区分状态改变、重复事件和非法事件。
  - 建立测试专用 `FakePlayerEngine`，控制请求与异步事件时序可独立验证。
  - 增加 `MEDIAHUB_CORE_ONLY`，在不查找 Qt/libVLC 时构建核心库与单元测试。
  - Debug、Release CTest 均为 15/15 通过，核心独立 CTest 为 13/13 通过。
- 阶段 4：libVLC 内核实现与本地音频（2026-08-03）
  - 使用 PImpl 建立不暴露 libVLC 类型的 `VlcPlayerEngine`，实现全部抽象方法。
  - 用自定义删除器管理实例、媒体和播放器句柄，并显式执行安全释放顺序。
  - 映射状态、位置、时长、可定位、结束和失败事件，位置事件按 200 毫秒节流。
  - 测试模式通过构造选项启用 libVLC 哑音频和哑视频输出。
  - 集成测试运行时生成中文空格路径的无版权静音 PCM/WAV，不提交媒体文件。
  - Debug、Release CTest 均为 23/23 通过，无测试构建与核心独立回归通过。

## 版本计划

| 版本 | 主题 | 状态 |
|---|---|---|
| v0.1.0 | 本地音视频播放，libVLC 内核 | 规划中 |
| v0.2.0 | URL 直播播放、缓冲与断流处理 | 未开始 |
| v0.3.0 | 自研 FFmpeg 内核，与 libVLC 内核并存对照 | 未开始 |
