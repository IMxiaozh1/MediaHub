#include "fakes/fake_player_engine.h"

#include <utility>

namespace mediahub::test {

core::OpenRequestId FakePlayerEngine::open(core::MediaItem item,
                                           void* const nativeVideoHandle) {
  FakeEngineCommand command{FakeEngineCommandKind::Open};
  command.media = std::move(item);
  command.nativeHandle = nativeVideoHandle;
  {
    const std::lock_guard lock(mutex_);
    command.openRequestId = ++latestOpenRequestId_;
    commands_.push_back(command);
  }
  return command.openRequestId;
}

void FakePlayerEngine::play() {
  record(FakeEngineCommand{FakeEngineCommandKind::Play});
}

void FakePlayerEngine::pause() {
  record(FakeEngineCommand{FakeEngineCommandKind::Pause});
}

void FakePlayerEngine::stop() {
  record(FakeEngineCommand{FakeEngineCommandKind::Stop});
}

void FakePlayerEngine::seek(const std::chrono::milliseconds position) {
  FakeEngineCommand command{FakeEngineCommandKind::Seek};
  command.position = position;
  record(std::move(command));
}

void FakePlayerEngine::setVolume(const int volume) {
  FakeEngineCommand command{FakeEngineCommandKind::SetVolume};
  command.volume = volume;
  record(std::move(command));
}

void FakePlayerEngine::setMuted(const bool isMuted) {
  FakeEngineCommand command{FakeEngineCommandKind::SetMuted};
  command.flag = isMuted;
  record(std::move(command));
}

void FakePlayerEngine::setPlaybackRate(const double rate) {
  FakeEngineCommand command{FakeEngineCommandKind::SetPlaybackRate};
  command.playbackRate = rate;
  record(std::move(command));
}

void FakePlayerEngine::setVideoSurface(void* const nativeHandle) {
  FakeEngineCommand command{FakeEngineCommandKind::SetVideoSurface};
  command.nativeHandle = nativeHandle;
  record(std::move(command));
}

core::PlaybackState FakePlayerEngine::state() const {
  const std::lock_guard lock(mutex_);
  return state_;
}

core::PlaybackPosition FakePlayerEngine::position() const {
  const std::lock_guard lock(mutex_);
  return position_;
}

std::optional<std::chrono::milliseconds> FakePlayerEngine::duration() const {
  const std::lock_guard lock(mutex_);
  return position_.total;
}

bool FakePlayerEngine::isSeekable() const {
  const std::lock_guard lock(mutex_);
  return position_.isSeekable;
}

std::optional<core::NetworkStreamActivity>
FakePlayerEngine::networkStreamActivity() const {
  const std::lock_guard lock(mutex_);
  return networkStreamActivity_;
}

void FakePlayerEngine::setEventListener(
    core::PlayerEventListener* const listener) {
  const std::lock_guard lock(mutex_);
  listener_ = listener;
}

std::vector<FakeEngineCommand> FakePlayerEngine::commands() const {
  const std::lock_guard lock(mutex_);
  return commands_;
}

void FakePlayerEngine::setNetworkStreamActivity(
    std::optional<core::NetworkStreamActivity> activity) {
  const std::lock_guard lock(mutex_);
  networkStreamActivity_ = std::move(activity);
}

void FakePlayerEngine::emitOpenStarted(const core::OpenRequestId requestId) {
  {
    const std::lock_guard lock(mutex_);
    announcedOpenRequestId_ = requestId;
  }
  if (auto* const currentListener = listener()) {
    currentListener->onOpenStarted(requestId);
  }
}

void FakePlayerEngine::emitStateChanged(const core::PlaybackState state) {
  core::OpenRequestId openRequestId = 0;
  {
    const std::lock_guard lock(mutex_);
    state_ = state;
    if (state == core::PlaybackState::Opening &&
        announcedOpenRequestId_ != latestOpenRequestId_) {
      announcedOpenRequestId_ = latestOpenRequestId_;
      openRequestId = latestOpenRequestId_;
    }
  }
  if (openRequestId != 0) {
    if (auto* const currentListener = listener()) {
      currentListener->onOpenStarted(openRequestId);
    }
  }
  if (auto* const currentListener = listener()) {
    currentListener->onStateChanged(state);
  }
}

void FakePlayerEngine::emitPositionChanged(core::PlaybackPosition position) {
  {
    const std::lock_guard lock(mutex_);
    position_ = position;
  }
  if (auto* const currentListener = listener()) {
    currentListener->onPositionChanged(std::move(position));
  }
}

void FakePlayerEngine::emitDurationChanged(
    const std::optional<std::chrono::milliseconds> duration) {
  {
    const std::lock_guard lock(mutex_);
    position_.total = duration;
  }
  if (auto* const currentListener = listener()) {
    currentListener->onDurationChanged(duration);
  }
}

void FakePlayerEngine::emitBufferingChanged(const int percentage) {
  if (auto* const currentListener = listener()) {
    currentListener->onBufferingChanged(percentage);
  }
}

void FakePlayerEngine::emitAudioWaveformChanged(core::AudioWaveform waveform) {
  if (auto* const currentListener = listener()) {
    currentListener->onAudioWaveformChanged(std::move(waveform));
  }
}

void FakePlayerEngine::emitEndReached() {
  if (auto* const currentListener = listener()) {
    currentListener->onEndReached();
  }
}

void FakePlayerEngine::emitError(core::PlaybackError error) {
  if (auto* const currentListener = listener()) {
    currentListener->onError(std::move(error));
  }
}

void FakePlayerEngine::emitVideoSurfaceReleased(void* const nativeHandle) {
  if (auto* const currentListener = listener()) {
    currentListener->onVideoSurfaceReleased(nativeHandle);
  }
}

void FakePlayerEngine::record(FakeEngineCommand command) {
  const std::lock_guard lock(mutex_);
  commands_.push_back(std::move(command));
}

core::PlayerEventListener* FakePlayerEngine::listener() const {
  const std::lock_guard lock(mutex_);
  return listener_;
}

}  // namespace mediahub::test
