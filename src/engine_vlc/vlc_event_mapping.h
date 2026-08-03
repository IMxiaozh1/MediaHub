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

}  // namespace mediahub::engine_vlc
