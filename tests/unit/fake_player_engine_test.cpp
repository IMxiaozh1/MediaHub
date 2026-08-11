#include "fakes/fake_player_engine.h"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "mediahub/core/playback_state_machine.h"

namespace mediahub::test {
namespace {

using namespace std::chrono_literals;

enum class ObservedEventKind {
  OpenStarted,
  State,
  Position,
  Duration,
  Buffering,
  Waveform,
  End,
  Error,
  VideoSurfaceReleased,
};

// 回调在 FakePlayerEngine 的 emit 调用线程同步记录，不进行跨线程操作。
class RecordingListener final : public core::PlayerEventListener {
 public:
  void onOpenStarted(const core::OpenRequestId requestId) noexcept override {
    events.push_back(ObservedEventKind::OpenStarted);
    lastOpenRequestId = requestId;
  }

  void onStateChanged(const core::PlaybackState state) noexcept override {
    events.push_back(ObservedEventKind::State);
    lastState = state;
  }

  void onPositionChanged(core::PlaybackPosition position) noexcept override {
    events.push_back(ObservedEventKind::Position);
    lastPosition = std::move(position);
  }

  void onDurationChanged(const std::optional<std::chrono::milliseconds>
                             duration) noexcept override {
    events.push_back(ObservedEventKind::Duration);
    lastDuration = duration;
  }

  void onBufferingChanged(const int percentage) noexcept override {
    events.push_back(ObservedEventKind::Buffering);
    lastBufferingPercentage = percentage;
  }

  void onAudioWaveformChanged(core::AudioWaveform waveform) noexcept override {
    events.push_back(ObservedEventKind::Waveform);
    lastWaveform = std::move(waveform);
  }

  void onEndReached() noexcept override {
    events.push_back(ObservedEventKind::End);
  }

  void onError(core::PlaybackError error) noexcept override {
    events.push_back(ObservedEventKind::Error);
    lastError = std::move(error);
  }

  void onVideoSurfaceReleased(void* const nativeHandle) noexcept override {
    events.push_back(ObservedEventKind::VideoSurfaceReleased);
    lastReleasedVideoSurface = nativeHandle;
  }

  std::vector<ObservedEventKind> events;
  core::PlaybackState lastState{core::PlaybackState::Idle};
  core::PlaybackPosition lastPosition;
  std::optional<std::chrono::milliseconds> lastDuration;
  int lastBufferingPercentage{0};
  core::AudioWaveform lastWaveform;
  core::PlaybackError lastError;
  core::OpenRequestId lastOpenRequestId{0};
  void* lastReleasedVideoSurface{nullptr};
};

// 把同一测试线程上的状态事件交给状态机，并保留每次校验结果。
class StateMachineListener final : public core::PlayerEventListener {
 public:
  explicit StateMachineListener(core::PlaybackStateMachine& machine)
      : machine_(machine) {}

  void onOpenStarted(core::OpenRequestId) noexcept override {}
  void onStateChanged(const core::PlaybackState state) noexcept override {
    results.push_back(machine_.transitionTo(state));
  }

  void onPositionChanged(core::PlaybackPosition) noexcept override {}
  void onDurationChanged(
      std::optional<std::chrono::milliseconds>) noexcept override {}
  void onBufferingChanged(int) noexcept override {}
  void onAudioWaveformChanged(core::AudioWaveform) noexcept override {}
  void onEndReached() noexcept override {}
  void onError(core::PlaybackError) noexcept override {}
  void onVideoSurfaceReleased(void*) noexcept override {}

  std::vector<core::PlaybackTransitionResult> results;

 private:
  core::PlaybackStateMachine& machine_;
};

static_assert(!std::is_abstract_v<FakePlayerEngine>);

TEST(FakePlayerEngineTest, RecordsEveryControlRequestWithoutCompletingIt) {
  FakePlayerEngine engine;
  const core::MediaItem item{"C:/Media/song.mp3",
                             core::MediaSourceKind::LocalFile, "song.mp3"};
  int surfaceToken = 0;
  void* const nativeHandle = &surfaceToken;

  const auto openRequestId = engine.open(item, nativeHandle);
  engine.play();
  engine.pause();
  engine.stop();
  engine.seek(1250ms);
  engine.setVolume(75);
  engine.setMuted(true);
  engine.setPlaybackRate(1.5);
  engine.setVideoSurface(nativeHandle);

  const auto commands = engine.commands();
  ASSERT_EQ(commands.size(), 9U);
  EXPECT_EQ(commands[0].kind, FakeEngineCommandKind::Open);
  ASSERT_TRUE(commands[0].media.has_value());
  EXPECT_EQ(*commands[0].media, item);
  EXPECT_EQ(openRequestId, core::OpenRequestId{1});
  EXPECT_EQ(commands[0].openRequestId, openRequestId);
  EXPECT_EQ(commands[0].nativeHandle, nativeHandle);
  EXPECT_EQ(commands[1].kind, FakeEngineCommandKind::Play);
  EXPECT_EQ(commands[2].kind, FakeEngineCommandKind::Pause);
  EXPECT_EQ(commands[3].kind, FakeEngineCommandKind::Stop);
  EXPECT_EQ(commands[4].kind, FakeEngineCommandKind::Seek);
  EXPECT_EQ(commands[4].position, 1250ms);
  EXPECT_EQ(commands[5].kind, FakeEngineCommandKind::SetVolume);
  EXPECT_EQ(commands[5].volume, 75);
  EXPECT_EQ(commands[6].kind, FakeEngineCommandKind::SetMuted);
  EXPECT_TRUE(commands[6].flag);
  EXPECT_EQ(commands[7].kind, FakeEngineCommandKind::SetPlaybackRate);
  EXPECT_DOUBLE_EQ(commands[7].playbackRate, 1.5);
  EXPECT_EQ(commands[8].kind, FakeEngineCommandKind::SetVideoSurface);
  EXPECT_EQ(commands[8].nativeHandle, nativeHandle);
  EXPECT_EQ(engine.state(), core::PlaybackState::Idle);
}

TEST(FakePlayerEngineTest, EmitsEventsInExactCallerControlledOrder) {
  FakePlayerEngine engine;
  RecordingListener listener;
  engine.setEventListener(&listener);

  const core::PlaybackPosition position{1500ms, 8000ms, true};
  const core::PlaybackError error{core::PlaybackErrorKind::UnsupportedFormat,
                                  "decoder rejected input",
                                  "无法播放该媒体格式。"};
  core::AudioWaveform waveform;
  waveform.samples[3] = 0.75F;
  waveform.intensity = 0.65F;
  engine.emitStateChanged(core::PlaybackState::Opening);
  engine.emitPositionChanged(position);
  engine.emitDurationChanged(8000ms);
  engine.emitBufferingChanged(37);
  engine.emitAudioWaveformChanged(waveform);
  engine.emitEndReached();
  engine.emitError(error);

  const std::vector expectedEvents{
      ObservedEventKind::State,    ObservedEventKind::Position,
      ObservedEventKind::Duration, ObservedEventKind::Buffering,
      ObservedEventKind::Waveform, ObservedEventKind::End,
      ObservedEventKind::Error};
  EXPECT_EQ(listener.events, expectedEvents);
  EXPECT_EQ(listener.lastState, core::PlaybackState::Opening);
  EXPECT_EQ(listener.lastPosition, position);
  EXPECT_EQ(listener.lastDuration, 8000ms);
  EXPECT_EQ(listener.lastBufferingPercentage, 37);
  EXPECT_EQ(listener.lastWaveform, waveform);
  EXPECT_EQ(listener.lastError, error);
  EXPECT_EQ(engine.state(), core::PlaybackState::Opening);
  EXPECT_EQ(engine.position(), position);
  EXPECT_EQ(engine.duration(), 8000ms);
  EXPECT_TRUE(engine.isSeekable());
}

TEST(FakePlayerEngineTest, SupportsUnknownDurationAndListenerRemoval) {
  FakePlayerEngine engine;
  RecordingListener listener;
  engine.setEventListener(&listener);

  EXPECT_EQ(engine.networkStreamActivity(), std::nullopt);
  const core::NetworkStreamActivity activity{1024, 900, 10, 20, 8, 18};
  engine.setNetworkStreamActivity(activity);
  EXPECT_EQ(engine.networkStreamActivity(), activity);

  engine.emitPositionChanged(
      core::PlaybackPosition{250ms, std::nullopt, false});
  EXPECT_EQ(engine.duration(), std::nullopt);
  EXPECT_FALSE(engine.isSeekable());
  ASSERT_EQ(listener.events.size(), 1U);

  engine.setEventListener(nullptr);
  engine.emitStateChanged(core::PlaybackState::Playing);
  engine.emitEndReached();

  EXPECT_EQ(listener.events.size(), 1U);
  EXPECT_EQ(engine.state(), core::PlaybackState::Playing);
}

TEST(FakePlayerEngineTest, DrivesStateMachineInExactEmissionOrder) {
  FakePlayerEngine engine;
  core::PlaybackStateMachine machine;
  StateMachineListener listener(machine);
  engine.setEventListener(&listener);

  engine.emitStateChanged(core::PlaybackState::Opening);
  engine.emitStateChanged(core::PlaybackState::Playing);
  engine.emitStateChanged(core::PlaybackState::Idle);
  engine.emitStateChanged(core::PlaybackState::Paused);

  const std::vector expectedResults{
      core::PlaybackTransitionResult::Changed,
      core::PlaybackTransitionResult::Changed,
      core::PlaybackTransitionResult::Rejected,
      core::PlaybackTransitionResult::Changed,
  };
  EXPECT_EQ(listener.results, expectedResults);
  EXPECT_EQ(machine.state(), core::PlaybackState::Paused);
}

}  // namespace
}  // namespace mediahub::test
