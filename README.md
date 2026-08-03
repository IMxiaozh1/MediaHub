# MediaHub

一个使用 C++20 和 Qt 5.14.2 开发的 Windows 桌面媒体播放器。

> **当前状态：阶段 4 已完成，阶段 5（GUI 播放控制与线程边界）尚未开始。
> libVLC 本地音频链路已通过哑输出集成测试，但当前 Qt 窗口尚未接入播放控件。**

## 这是什么

MediaHub 分三个版本推进：

| 版本 | 能力 | 状态 |
|---|---|---|
| v0.1 | 播放本地音频和视频文件 | 开发中 |
| v0.2 | 输入直播地址直接观看 | 未开始 |
| v0.3 | 自研 FFmpeg 播放内核，与 libVLC 内核并存对照 | 未开始 |

播放内核采用 libVLC，但藏在一个自定义的 `PlayerEngine` 纯虚接口之后。界面代码
完全不知道 libVLC 的存在，这使得 v0.3 替换内核时不需要改动界面和核心逻辑。

设计取舍的完整理由见
[docs/设计/播放内核选型.md](docs/设计/播放内核选型.md)。

## v0.1 计划做什么

- 打开本地音频和视频文件（菜单、拖放、命令行参数）
- 播放、暂停、继续、停止
- 视频画面输出
- 进度显示与拖动定位
- 音量调节与静音
- 播放列表，支持顺序、列表循环和单曲循环
- 播放失败的明确中文提示

## v0.1 计划不做什么

- 任何网络地址和流媒体协议
- 字幕、多音轨、倍速、截图、录制
- 媒体库、播放历史、封面刮削
- 换肤和自定义主题
- Windows 以外的平台

完整边界见 [docs/规划/项目目标.md](docs/规划/项目目标.md) 第 5 节。

## 安全边界

MediaHub 只**读取**用户的媒体文件。程序不修改、不移动、不删除、不重命名任何
文件，也不提供任何清理或整理功能。

MediaHub 不内置任何直播源列表，不提供源搜索能力，也不实现任何绕过访问控制的
手段。v0.2 只负责播放用户自己提供的地址，用户需自行确保所播放内容的来源合法。

## 构建

### 需要什么

| 组件 | 版本 | 说明 |
|---|---|---|
| Qt | 5.14.2 msvc2017_64 | 已在本机 `C:\Qt\Qt5.14.2` |
| MSVC | VS 2022 | 已在本机 |
| CMake | 3.20 以上 | 已在本机 |
| Ninja | 任意 | VS 2022 自带 |
| libVLC | 3.0.21 win64 SDK | 已在本机 `C:\SDK\vlc-3.0.21` |
| GoogleTest | 1.17.0 | 已固定在 `Third_Party/googletest` |

libVLC 不随仓库分发。获取和接入步骤见
[docs/交付/依赖接入与部署说明.md](docs/交付/依赖接入与部署说明.md)。

### 怎么构建

普通终端找不到 `cl.exe` 和 `ninja.exe`。使用 VS 开发者命令提示符，或在
Qt Creator 中配置构建套件。

```powershell
cmake -S . -B cmake-build-debug -G Ninja ^
      -DCMAKE_BUILD_TYPE=Debug ^
      -DCMAKE_PREFIX_PATH="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64" ^
      -DMEDIAHUB_VLC_ROOT="C:/SDK/vlc-3.0.21"

cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug --output-on-failure
```

只构建并测试不依赖 Qt、libVLC 的核心层：

```powershell
cmake -S . -B cmake-build-core-debug -G Ninja ^
      -DCMAKE_BUILD_TYPE=Debug ^
      -DMEDIAHUB_CORE_ONLY=ON

cmake --build cmake-build-core-debug
ctest --test-dir cmake-build-core-debug --output-on-failure
```

运行当前空窗口：

```powershell
.\cmake-build-debug\MediaHub.exe
```

当前 `MediaHub.exe` 只验证 Qt 5.14.2 窗口、目标依赖和生命周期。打开媒体、播放、
暂停、视频输出等功能从后续阶段逐步加入。

## 项目文档

文档是这个项目的主要产出之一，先于代码存在。入口：

- [docs/文档说明.md](docs/文档说明.md) — 文档导航
- [docs/交接文档.md](docs/交接文档.md) — **当前状态和下一步，开工前先读这份**
- [docs/测试/阶段4测试.md](docs/测试/阶段4测试.md) — 阶段 4 的实际构建与测试记录

按主题：

| 想了解 | 读哪份 |
|---|---|
| 项目做什么、不做什么 | [规划/项目目标.md](docs/规划/项目目标.md) |
| 第一版的具体行为和验收 | [规划/第一版MVP.md](docs/规划/第一版MVP.md) |
| 阶段拆分和总体架构 | [规划/项目完整大纲.md](docs/规划/项目完整大纲.md) |
| 为什么选 libVLC | [设计/播放内核选型.md](docs/设计/播放内核选型.md) |
| 模块、线程模型和错误策略 | [设计/第一版实现.md](docs/设计/第一版实现.md) |
| 目录和依赖规则 | [设计/项目结构.md](docs/设计/项目结构.md) |
| 编码规范 | [设计/代码规范.md](docs/设计/代码规范.md) |
| 怎么测、测不到什么 | [测试/测试策略与阶段模板.md](docs/测试/测试策略与阶段模板.md) |
| 音视频概念扫盲 | [学习/音视频基础.md](docs/学习/音视频基础.md) |
| 怎么学这个项目 | [学习/项目学习计划.md](docs/学习/项目学习计划.md) |

## 架构一句话

```text
apps/gui  ──依赖──>  mediahub_core（抽象接口）  <──实现──  mediahub_engine_vlc
   ↑                                                              ↓
只知道接口                                                     libVLC
```

界面不认识 libVLC，核心不认识 Qt。只有 `main.cpp` 同时认识两者，负责组装。

## 已知限制

以下限制在规划阶段就已确定：

- 仅支持 Windows x64。
- 具体格式的播放能力取决于 libVLC 与所携带的插件，项目不做能力承诺。
- 音画同步由播放内核负责，项目不提供同步质量指标。
- **视频画面、声音输出和音画同步依赖人工验收，没有自动化测试覆盖。**
- 发布包体积较大，主要来自 libVLC 插件目录。
- Qt 5.14.2 已是归档版本，长期维护应单独评估 Qt 6。

## 许可证

- 项目自身源码许可证**尚未决定**。
- 依赖 Qt 5.14.2（LGPLv3 / GPLv3）和 libVLC（LGPLv2.1+，部分插件为 GPLv2+）。
- 对外分发前必须完成许可证合规确认，详见
  [docs/交付/依赖接入与部署说明.md](docs/交付/依赖接入与部署说明.md) 第 6 节。
