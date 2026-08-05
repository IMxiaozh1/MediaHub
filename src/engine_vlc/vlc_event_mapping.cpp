#include "vlc_event_mapping.h"

#include <algorithm>
#include <cmath>

namespace mediahub::engine_vlc {

core::PlaybackState mapPlaybackState(const VlcPlaybackEvent event) noexcept {
  switch (event) {
    case VlcPlaybackEvent::NothingSpecial:
      return core::PlaybackState::Idle;
    case VlcPlaybackEvent::Opening:
      return core::PlaybackState::Opening;
    case VlcPlaybackEvent::Buffering:
      return core::PlaybackState::Buffering;
    case VlcPlaybackEvent::Playing:
      return core::PlaybackState::Playing;
    case VlcPlaybackEvent::Paused:
      return core::PlaybackState::Paused;
    case VlcPlaybackEvent::Stopped:
      return core::PlaybackState::Stopped;
    case VlcPlaybackEvent::EndReached:
      return core::PlaybackState::Ended;
    case VlcPlaybackEvent::EncounteredError:
      return core::PlaybackState::Failed;
  }

  return core::PlaybackState::Failed;
}

bool isBufferingInProgress(const float cachePercentage,
                           const core::PlaybackState currentState) noexcept {
  const bool hasStablePlaybackState =
      currentState == core::PlaybackState::Playing ||
      currentState == core::PlaybackState::Paused;
  return cachePercentage < 100.0F && !hasStablePlaybackState;
}

int bufferingPercentage(const float cachePercentage) noexcept {
  if (!std::isfinite(cachePercentage)) {
    return 0;
  }
  return std::clamp(static_cast<int>(std::lround(cachePercentage)), 0, 100);
}

}  // namespace mediahub::engine_vlc
