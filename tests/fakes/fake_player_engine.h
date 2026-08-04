#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include "mediahub/core/player_engine.h"

namespace mediahub::test {

enum class FakeEngineCommandKind {
  Open,
  Play,
  Pause,
  Stop,
  Seek,
  SetVolume,
  SetMuted,
  SetPlaybackRate,
  SetVideoSurface,
};

// 保存一次控制请求的输入；未被该命令使用的字段保持默认值。
struct FakeEngineCommand {
  FakeEngineCommandKind kind{FakeEngineCommandKind::Play};
  std::optional<core::MediaItem> media;
  std::chrono::milliseconds position{0};
  int volume{0};
  double playbackRate{1.0};
  bool flag{false};
  void* nativeHandle{nullptr};
};

// 测试专用内核。控制请求只被记录，事件仅在测试显式调用 emit 方法时同步发出。
class FakePlayerEngine final : public core::PlayerEngine {
 public:
  void open(core::MediaItem item) override;
  void play() override;
  void pause() override;
  void stop() override;
  void seek(std::chrono::milliseconds position) override;
  void setVolume(int volume) override;
  void setMuted(bool isMuted) override;
  void setPlaybackRate(double rate) override;
  void setVideoSurface(void* nativeHandle) override;

  [[nodiscard]] core::PlaybackState state() const override;
  [[nodiscard]] core::PlaybackPosition position() const override;
  [[nodiscard]] std::optional<std::chrono::milliseconds> duration()
      const override;
  [[nodiscard]] bool isSeekable() const override;
  void setEventListener(core::PlayerEventListener* listener) override;

  [[nodiscard]] std::vector<FakeEngineCommand> commands() const;

  // 调用线程：测试线程。回调在调用者线程中同步执行，方便精确控制事件顺序。
  void emitStateChanged(core::PlaybackState state);
  // 调用线程：测试线程。回调在调用者线程中同步执行。
  void emitPositionChanged(core::PlaybackPosition position);
  // 调用线程：测试线程。回调在调用者线程中同步执行。
  void emitDurationChanged(std::optional<std::chrono::milliseconds> duration);
  // 调用线程：测试线程。回调在调用者线程中同步执行。
  void emitAudioWaveformChanged(core::AudioWaveform waveform);
  // 调用线程：测试线程。回调在调用者线程中同步执行。
  void emitEndReached();
  // 调用线程：测试线程。回调在调用者线程中同步执行。
  void emitError(core::PlaybackError error);

 private:
  void record(FakeEngineCommand command);
  [[nodiscard]] core::PlayerEventListener* listener() const;

  mutable std::mutex mutex_;
  std::vector<FakeEngineCommand> commands_;
  core::PlaybackState state_{core::PlaybackState::Idle};
  core::PlaybackPosition position_;
  core::PlayerEventListener* listener_{nullptr};
};

}  // namespace mediahub::test
