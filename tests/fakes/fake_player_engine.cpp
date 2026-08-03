#include "fakes/fake_player_engine.h"

#include <utility>

namespace mediahub::test {

void FakePlayerEngine::open(core::MediaItem item) {
    FakeEngineCommand command{FakeEngineCommandKind::Open};
    command.media = std::move(item);
    record(std::move(command));
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

void FakePlayerEngine::setEventListener(core::PlayerEventListener* const listener) {
    const std::lock_guard lock(mutex_);
    listener_ = listener;
}

std::vector<FakeEngineCommand> FakePlayerEngine::commands() const {
    const std::lock_guard lock(mutex_);
    return commands_;
}

void FakePlayerEngine::emitStateChanged(const core::PlaybackState state) {
    {
        const std::lock_guard lock(mutex_);
        state_ = state;
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

void FakePlayerEngine::record(FakeEngineCommand command) {
    const std::lock_guard lock(mutex_);
    commands_.push_back(std::move(command));
}

core::PlayerEventListener* FakePlayerEngine::listener() const {
    const std::lock_guard lock(mutex_);
    return listener_;
}

}  // namespace mediahub::test
