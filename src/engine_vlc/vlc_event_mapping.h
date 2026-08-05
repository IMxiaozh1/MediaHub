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

[[nodiscard]] core::PlaybackState mapPlaybackState(
    VlcPlaybackEvent event) noexcept;

// 100% 是缓冲完成通知；播放或暂停后的缓冲事件不能降级已经稳定的业务状态。
[[nodiscard]] bool isBufferingInProgress(
    float cachePercentage, core::PlaybackState currentState) noexcept;

[[nodiscard]] int bufferingPercentage(float cachePercentage) noexcept;

}  // namespace mediahub::engine_vlc
