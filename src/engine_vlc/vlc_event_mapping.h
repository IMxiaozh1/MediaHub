#pragma once

#include "mediahub/core/media_types.h"

namespace mediahub::engine_vlc {

// 与 libVLC 头文件隔离的事件分类，使映射规则可以作为纯逻辑测试。
enum class VlcPlaybackEvent {
    NothingSpecial,
    Opening,
    Buffering,
    Playing,
    Paused,
    Stopped,
    EndReached,
    EncounteredError,
};

[[nodiscard]] core::PlaybackState mapPlaybackState(VlcPlaybackEvent event) noexcept;

// 100% 是缓冲完成通知，不能把已经进入 Playing 的界面降级回 Buffering。
[[nodiscard]] bool isBufferingInProgress(float cachePercentage,
                                         bool isPlayerAlreadyPlaying) noexcept;

}  // namespace mediahub::engine_vlc
