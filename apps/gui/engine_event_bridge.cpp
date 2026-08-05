#include "engine_event_bridge.h"

#include <utility>

namespace mediahub::gui {

EngineEventBridge::EngineEventBridge(QObject* const parent) : QObject(parent) {
  qRegisterMetaType<core::PlaybackState>("mediahub::core::PlaybackState");
  qRegisterMetaType<core::PlaybackPosition>("mediahub::core::PlaybackPosition");
  qRegisterMetaType<core::PlaybackError>("mediahub::core::PlaybackError");
  qRegisterMetaType<core::AudioWaveform>("mediahub::core::AudioWaveform");
  qRegisterMetaType<OptionalDuration>("mediahub::gui::OptionalDuration");
}

void EngineEventBridge::deactivate() noexcept {
  isActive_.store(false, std::memory_order_release);
}

void EngineEventBridge::onStateChanged(
    const core::PlaybackState state) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit stateChanged(state);
  }
}

void EngineEventBridge::onPositionChanged(
    core::PlaybackPosition position) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit positionChanged(std::move(position));
  }
}

void EngineEventBridge::onDurationChanged(OptionalDuration duration) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit durationChanged(std::move(duration));
  }
}

void EngineEventBridge::onBufferingChanged(const int percentage) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit bufferingChanged(percentage);
  }
}

void EngineEventBridge::onAudioWaveformChanged(
    core::AudioWaveform waveform) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit audioWaveformChanged(std::move(waveform));
  }
}

void EngineEventBridge::onEndReached() noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit endReached();
  }
}

void EngineEventBridge::onError(core::PlaybackError error) noexcept {
  if (isActive_.load(std::memory_order_acquire)) {
    emit errorOccurred(std::move(error));
  }
}

}  // namespace mediahub::gui
