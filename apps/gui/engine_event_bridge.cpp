#include "engine_event_bridge.h"

#include <utility>

namespace mediahub::gui {

EngineEventBridge::EngineEventBridge(QObject* const parent) : QObject(parent) {
  qRegisterMetaType<core::OpenRequestId>("mediahub::core::OpenRequestId");
  qRegisterMetaType<core::PlaybackState>("mediahub::core::PlaybackState");
  qRegisterMetaType<core::PlaybackPosition>("mediahub::core::PlaybackPosition");
  qRegisterMetaType<core::PlaybackError>("mediahub::core::PlaybackError");
  qRegisterMetaType<core::AudioWaveform>("mediahub::core::AudioWaveform");
  qRegisterMetaType<OptionalDuration>("mediahub::gui::OptionalDuration");
}

void EngineEventBridge::deactivate() noexcept {
  isActive_.store(false, std::memory_order_release);
}

void EngineEventBridge::onOpenStarted(
    const core::OpenRequestId requestId) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit openStarted(requestId);
  }
}

void EngineEventBridge::onStateChanged(
    const core::OpenRequestId requestId,
    const core::PlaybackState state) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit stateChanged(requestId, state);
  }
}

void EngineEventBridge::onPositionChanged(
    const core::OpenRequestId requestId,
    core::PlaybackPosition position) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit positionChanged(requestId, std::move(position));
  }
}

void EngineEventBridge::onDurationChanged(
    const core::OpenRequestId requestId, OptionalDuration duration) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit durationChanged(requestId, std::move(duration));
  }
}

void EngineEventBridge::onBufferingChanged(
    const core::OpenRequestId requestId, const int percentage) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit bufferingChanged(requestId, percentage);
  }
}

void EngineEventBridge::onAudioWaveformChanged(
    const core::OpenRequestId requestId,
    core::AudioWaveform waveform) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit audioWaveformChanged(requestId, std::move(waveform));
  }
}

void EngineEventBridge::onEndReached(
    const core::OpenRequestId requestId) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit endReached(requestId);
  }
}

void EngineEventBridge::onError(const core::OpenRequestId requestId,
                                core::PlaybackError error) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit errorOccurred(requestId, std::move(error));
  }
}

void EngineEventBridge::onVideoSurfaceReleased(
    void* const nativeHandle) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit videoSurfaceReleased(nativeHandle);
  }
}

}  // namespace mediahub::gui
