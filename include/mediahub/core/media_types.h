#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace mediahub::core {

// 描述媒体来源；网络流先作为稳定的接口能力保留，v0.1 只接收本地文件。
enum class MediaSourceKind {
    LocalFile,
    NetworkStream,
};

// 可拷贝的媒体描述。source 和 displayName 均使用 UTF-8。
struct MediaItem {
    std::string source;
    MediaSourceKind kind{MediaSourceKind::LocalFile};
    std::string displayName;

    bool operator==(const MediaItem&) const = default;
};

// 播放状态是控制逻辑和界面展示的唯一状态来源。
enum class PlaybackState {
    Idle,
    Opening,
    Buffering,
    Playing,
    Paused,
    Stopped,
    Ended,
    Failed,
};

// total 为空表示总时长未知，例如尚未解析完成或未来的直播流。
struct PlaybackPosition {
    std::chrono::milliseconds current{0};
    std::optional<std::chrono::milliseconds> total;
    bool isSeekable{false};

    bool operator==(const PlaybackPosition&) const = default;
};

// 稳定的业务错误分类；具体内核错误不得越过这一层直接进入界面。
enum class PlaybackErrorKind {
    SourceNotFound,
    SourceUnreadable,
    UnsupportedFormat,
    AudioDeviceUnavailable,
    EngineNotInitialized,
    Unknown,
};

// engineDetail 只用于诊断日志，界面只应展示 userMessage。
struct PlaybackError {
    PlaybackErrorKind kind{PlaybackErrorKind::Unknown};
    std::string engineDetail;
    std::string userMessage;

    bool operator==(const PlaybackError&) const = default;
};

// 阶段 8 播放列表使用的三种切换规则，提前固定为核心值类型。
enum class PlaybackMode {
    Sequential,
    LoopAll,
    LoopOne,
};

// 按 URI 语法集中识别来源，避免界面和播放列表重复判断。
[[nodiscard]] MediaSourceKind classifyMediaSource(std::string_view source) noexcept;

// 创建媒体项；displayName 为空时从路径或 URL 的最后一段推导显示名称。
[[nodiscard]] MediaItem makeMediaItem(std::string source, std::string displayName = {});

}  // namespace mediahub::core
