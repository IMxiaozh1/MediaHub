# Repository Guidelines

## 项目结构与模块组织

MediaHub 是一个 C++20/Qt5/CMake 桌面播放器项目：`apps/gui/main.cpp` 是程序
入口，`mediahub_core` 是不依赖任何界面和内核的静态核心库，
`mediahub_engine_vlc` 是基于 libVLC 的播放内核实现，`mediahub_gui_support`
封装 Qt 应用层，`mediahub_gui` 生成 `MediaHub.exe`，`mediahub_tests` 承载
GoogleTest 测试，`mediahub_gui_tests` 承载 Qt Test。`docs/` 保存规划、设计、
学习、测试和交付文档。

开始新的开发会话或切换阶段前，必须先阅读 `docs/交接文档.md`，核对当前阶段、
环境状态、未完成事项和下一步边界。每个阶段实现、验证或提交状态发生变化后，
同步更新该交接文档，避免依赖聊天记录传递上下文。

项目扩展时遵循 `docs/设计/项目结构.md`：公共核心头文件放入
`include/mediahub/core/`，实现放入 `src/core/`，内核实现放入
`src/engine_vlc/`，GUI 代码放入 `apps/gui/`，测试分别放入 `tests/unit/`、
`tests/integration/` 和 `tests/gui/`。

第三方源码统一放入 `Third_Party/<library>/`，由 CMake 从本地目录接入。保留
上游许可证和版本记录，不直接修改第三方源码；依赖升级必须使用独立提交。
libVLC 是例外：它作为外部 SDK 通过 `MEDIAHUB_VLC_ROOT` 变量接入，不进入仓库。

## 三条不可违反的架构约束

这三条由构建系统和代码审查共同强制，违反它们会让 v0.3 替换内核变得不可能：

1. **`mediahub_core` 不得依赖 Qt 和 libVLC。** 核心层的编译不应该需要这两者
   存在。
2. **`mediahub_gui_support` 不得链接 libVLC。** 界面代码里出现 libVLC 调用应
   直接编译失败。只有 `apps/gui/main.cpp` 知道具体内核的存在。
3. **内核事件回调中不得操作 Qt 控件，不得调用会等待内核线程的接口。** 事件必须
   经 `EngineEventBridge` 投递到 GUI 主线程。

## 构建、测试与本地开发

本机没有安装 CLion。使用 Qt Creator、Visual Studio 或 VS 开发者命令提示符。
普通终端找不到 `cl.exe` 和 `ninja.exe`。

日常开发默认维护 `cmake-build-debug/`，只有明确进行 Release 回归或交付时才
使用 `cmake-build-release/`。配置时需要同时提供 Qt 与 libVLC 路径：

```powershell
cmake -S . -B cmake-build-debug -G Ninja ^
      -DCMAKE_BUILD_TYPE=Debug ^
      -DCMAKE_PREFIX_PATH="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64" ^
      -DMEDIAHUB_VLC_ROOT="<本机 libVLC SDK 目录>"
```

- `cmake --build cmake-build-debug`：构建程序和测试。
- `.\cmake-build-debug\MediaHub.exe`：运行 Debug 程序。
- `ctest --test-dir cmake-build-debug --output-on-failure`：运行测试。

不要提交构建产物或个人 IDE 设置。不要把本机绝对路径写入项目 CMake。

## 编码风格与命名约定

统一使用 C++20 和 UTF-8；缩进为 4 个空格，禁止 Tab，单行建议不超过
100 个字符。左花括号与声明同行，头文件使用 `#pragma once`。优先采用
RAII、值语义、`enum class` 和显式单参数构造函数。C 库句柄必须用带自定义
删除器的 `std::unique_ptr` 包装，业务代码中不出现显式释放调用。

类型使用 `PascalCase`，函数、参数和局部变量使用 `lowerCamelCase`，私有
成员使用 `lowerCamelCase_`，常量使用 `kPascalCase`，命名空间和文件名
使用 `snake_case`。布尔值以 `is`、`has`、`can` 等开头。完整规则见
`docs/设计/代码规范.md`；项目目前尚未配置自动格式化工具。

项目自有代码的注释必须使用中文，并遵循 Google C++ Style Guide 的 Comments
章节。公共接口和非显然的类型必须说明用途、输入、输出及约束；实现注释重点
解释关键步骤和设计原因，不得逐行复述代码。**涉及线程的函数，注释必须写明
它会在哪个线程被调用**，例如"调用线程：libVLC 事件线程，禁止在此操作控件"。
`TODO` 必须带 Issue、负责人或其他可追踪标识。第三方源码保持上游原貌。

## 测试规范

核心逻辑必须能够脱离 Qt、libVLC 和音频设备测试。测试文件命名为
`<module>_test.cpp`，每个测试只验证一种行为。播放逻辑测试使用假内核；内核
集成测试使用哑音频与哑视频输出，不依赖真实声卡。

测试媒体在运行时生成，不得依赖测试顺序、固定盘符或个人路径。**不在仓库中
提交任何有版权的音视频素材。** 异步测试必须使用明确的等待条件和超时上限，
禁止用固定时长 `sleep` 碰运气。修复缺陷前，先添加可复现问题的测试。

音画输出效果、音画同步和真实格式播放无法可靠自动化，只做人工验收并在阶段
测试文档中明确标注为人工验证。不假装这部分有自动化覆盖。

## 提交与合并请求

提交信息采用 `<类型>: <简短说明>`，类型包括 `feat`、`fix`、`test`、
`docs`、`build` 和 `chore`，例如 `feat: 添加播放状态机`。每个提交只表达
一个目的；未经讨论不要新增第三方依赖。

每个开发阶段在实现完成、规定测试全部通过且阶段测试文档回填后，自动创建本地
Git 提交，不再逐次等待提交确认。测试失败、人工验收未完成或存在阻塞问题时不得
提交。自动提交不包含推送；只有收到明确推送要求后才执行 `git push`。

合并请求需说明目标、影响文件、设计理由及验证命令和结果。只有涉及可见界面
变化时才需要截图。

## 安全与配置

不要提交凭据、敏感路径、公司代码或业务数据。日志不得记录媒体文件的完整
路径。程序只读取用户媒体文件，不修改、移动、删除或重命名任何文件。
