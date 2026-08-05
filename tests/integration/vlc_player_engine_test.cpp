#include "mediahub/engine_vlc/vlc_player_engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "support/generated_media.h"
#include "vlc_event_mapping.h"

namespace mediahub::engine_vlc {
namespace {

using namespace std::chrono_literals;

using test::GeneratedInvalidMedia;
using test::GeneratedWav;

// libVLC 回调线程只写受互斥量保护的测试快照，并通过条件变量唤醒测试线程。
class RecordingListener final : public core::PlayerEventListener {
 public:
  void onStateChanged(const core::PlaybackState state) noexcept override {
    const std::lock_guard lock(mutex_);
    states_.push_back(state);
    stateThreads_.emplace_back(state, std::this_thread::get_id());
    changed_.notify_all();
  }

  void onPositionChanged(core::PlaybackPosition position) noexcept override {
    const std::lock_guard lock(mutex_);
    position_ = std::move(position);
    changed_.notify_all();
  }

  void onDurationChanged(const std::optional<std::chrono::milliseconds>
                             duration) noexcept override {
    const std::lock_guard lock(mutex_);
    duration_ = duration;
    hasDurationEvent_ = true;
    changed_.notify_all();
  }

  void onAudioWaveformChanged(core::AudioWaveform waveform) noexcept override {
    const std::lock_guard lock(mutex_);
    waveform_ = std::move(waveform);
    ++waveformCount_;
    if (std::any_of(waveform_.samples.begin(), waveform_.samples.end(),
                    [](const float sample) {
                      return sample < -0.05F || sample > 0.05F;
                    })) {
      ++nonSilentWaveformCount_;
    }
    changed_.notify_all();
  }

  void onEndReached() noexcept override {
    const std::lock_guard lock(mutex_);
    ++endCount_;
    changed_.notify_all();
  }

  void onError(core::PlaybackError error) noexcept override {
    const std::lock_guard lock(mutex_);
    errors_.push_back(std::move(error));
    changed_.notify_all();
  }

  [[nodiscard]] std::size_t stateCount(const core::PlaybackState state) const {
    const std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(
        std::count(states_.begin(), states_.end(), state));
  }

  [[nodiscard]] bool waitForStateCount(
      const core::PlaybackState state, const std::size_t expected,
      const std::chrono::milliseconds timeout = 5s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] {
      return static_cast<std::size_t>(
                 std::count(states_.begin(), states_.end(), state)) >= expected;
    });
  }

  [[nodiscard]] bool waitForPositionAtLeast(
      const std::chrono::milliseconds expected,
      const std::chrono::milliseconds timeout = 5s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [&] { return position_.current >= expected; });
  }

  [[nodiscard]] bool waitForKnownDuration(
      const std::chrono::milliseconds timeout = 5s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] {
      return hasDurationEvent_ && duration_.has_value();
    });
  }

  [[nodiscard]] bool waitForNonSilentWaveform(
      const std::chrono::milliseconds timeout = 5s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] {
      return std::any_of(
          waveform_.samples.begin(), waveform_.samples.end(),
          [](const float sample) { return sample < -0.05F || sample > 0.05F; });
    });
  }

  [[nodiscard]] bool waitForNonSilentWaveformCount(
      const std::size_t expected,
      const std::chrono::milliseconds timeout = 5s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(
        lock, timeout, [&] { return nonSilentWaveformCount_ >= expected; });
  }

  [[nodiscard]] bool waitForPositiveWaveformIntensity(
      const std::chrono::milliseconds timeout = 5s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [&] { return waveform_.intensity > 0.05F; });
  }

  [[nodiscard]] core::AudioWaveform lastWaveform() const {
    const std::lock_guard lock(mutex_);
    return waveform_;
  }

  [[nodiscard]] bool waitForEnd(const std::chrono::milliseconds timeout = 5s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] { return endCount_ > 0; });
  }

  [[nodiscard]] bool waitForError(
      const std::chrono::milliseconds timeout = 1s) {
    return waitForErrorCount(1, timeout);
  }

  [[nodiscard]] bool waitForErrorCount(
      const std::size_t expected,
      const std::chrono::milliseconds timeout = 1s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [&] { return errors_.size() >= expected; });
  }

  [[nodiscard]] core::PlaybackError lastError() const {
    const std::lock_guard lock(mutex_);
    return errors_.back();
  }

  [[nodiscard]] std::size_t errorCount() const {
    const std::lock_guard lock(mutex_);
    return errors_.size();
  }

  [[nodiscard]] std::size_t endCount() const {
    const std::lock_guard lock(mutex_);
    return endCount_;
  }

  [[nodiscard]] std::thread::id stateThread(
      const core::PlaybackState state) const {
    const std::lock_guard lock(mutex_);
    for (const auto& [observedState, thread] : stateThreads_) {
      if (observedState == state) {
        return thread;
      }
    }
    return {};
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<core::PlaybackState> states_;
  std::vector<std::pair<core::PlaybackState, std::thread::id>> stateThreads_;
  core::PlaybackPosition position_;
  std::optional<std::chrono::milliseconds> duration_;
  bool hasDurationEvent_{false};
  core::AudioWaveform waveform_;
  std::size_t waveformCount_{0};
  std::size_t nonSilentWaveformCount_{0};
  std::size_t endCount_{0};
  std::vector<core::PlaybackError> errors_;
};

VlcPlayerEngineOptions testOptions() {
  return VlcPlayerEngineOptions{true, true};
}

TEST(VlcEventMappingTest, MapsEveryPlaybackEventToCoreState) {
  EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::NothingSpecial),
            core::PlaybackState::Idle);
  EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Opening),
            core::PlaybackState::Opening);
  EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Buffering),
            core::PlaybackState::Buffering);
  EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Playing),
            core::PlaybackState::Playing);
  EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Paused),
            core::PlaybackState::Paused);
  EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Stopped),
            core::PlaybackState::Stopped);
  EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::EndReached),
            core::PlaybackState::Ended);
  EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::EncounteredError),
            core::PlaybackState::Failed);
  EXPECT_TRUE(isBufferingInProgress(0.0F, core::PlaybackState::Opening));
  EXPECT_TRUE(isBufferingInProgress(99.9F, core::PlaybackState::Buffering));
  EXPECT_FALSE(isBufferingInProgress(100.0F, core::PlaybackState::Opening));
  EXPECT_FALSE(isBufferingInProgress(101.0F, core::PlaybackState::Opening));
  EXPECT_FALSE(isBufferingInProgress(25.0F, core::PlaybackState::Playing));
  EXPECT_FALSE(isBufferingInProgress(25.0F, core::PlaybackState::Paused));
}

TEST(VlcPlayerEngineTest, InitializesWithStableEmptySnapshots) {
  VlcPlayerEngine engine(testOptions());
  EXPECT_EQ(engine.state(), core::PlaybackState::Idle);
  EXPECT_EQ(engine.position(), core::PlaybackPosition{});
  EXPECT_EQ(engine.duration(), std::nullopt);
  EXPECT_FALSE(engine.isSeekable());

  engine.setVolume(0);
  engine.setVolume(100);
  engine.setMuted(true);
  engine.setMuted(false);
  engine.setVideoSurface(nullptr);
}

TEST(VlcPlayerEngineTest, ReportsMissingLocalFileWithoutLeakingFullPath) {
  GeneratedWav existingFile(100ms);
  const auto missingSource = existingFile.source() + ".missing";
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);

  engine.open(core::MediaItem{missingSource, core::MediaSourceKind::LocalFile,
                              "missing.wav"});

  ASSERT_TRUE(listener.waitForError());
  const auto error = listener.lastError();
  EXPECT_EQ(engine.state(), core::PlaybackState::Failed);
  EXPECT_EQ(error.kind, core::PlaybackErrorKind::SourceNotFound);
  EXPECT_NE(error.userMessage.find("missing.wav"), std::string::npos);
  EXPECT_EQ(error.engineDetail.find(missingSource), std::string::npos);
  EXPECT_EQ(error.userMessage.find(missingSource), std::string::npos);
}

TEST(VlcPlayerEngineTest, CreatesNetworkDescriptorWithoutAccessingFileSystem) {
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);

  engine.open(core::makeMediaItem(
      "https://user:secret@example.invalid/live.m3u8?token=private"));

  EXPECT_EQ(engine.state(), core::PlaybackState::Opening);
  EXPECT_EQ(listener.stateCount(core::PlaybackState::Opening), 1U);
  EXPECT_EQ(listener.errorCount(), 0U);
}

TEST(VlcPlayerEngineTest, RejectsInvalidNetworkDescriptorWithoutLeakingUrl) {
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);
  const std::string invalidAddress =
      "custom://user:secret@example.invalid/live?token=private";

  engine.open(core::MediaItem{invalidAddress,
                              core::MediaSourceKind::NetworkStream,
                              "测试直播"});

  ASSERT_TRUE(listener.waitForError());
  EXPECT_EQ(engine.state(), core::PlaybackState::Failed);
  EXPECT_EQ(listener.lastError().kind,
            core::PlaybackErrorKind::UnsupportedFormat);
  EXPECT_EQ(listener.lastError().engineDetail.find(invalidAddress),
            std::string::npos);
  EXPECT_EQ(listener.lastError().userMessage.find(invalidAddress),
            std::string::npos);
}

TEST(VlcPlayerEngineTest, FailedOpenDoesNotLeavePreviousMediaLoaded) {
  GeneratedWav media(100ms);
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);

  engine.open(core::makeMediaItem(media.source()));
  engine.open(core::MediaItem{media.source() + ".missing",
                              core::MediaSourceKind::LocalFile, "missing.wav"});
  ASSERT_TRUE(listener.waitForErrorCount(1));

  engine.play();
  ASSERT_TRUE(listener.waitForErrorCount(2));
  EXPECT_EQ(listener.lastError().kind,
            core::PlaybackErrorKind::EngineNotInitialized);
}

TEST(VlcPlayerEngineTest, PlaysQtStyleLocalPathWithForwardSlashes) {
  GeneratedWav media(3s);
  std::string qtStyleSource = media.source();
  std::replace(qtStyleSource.begin(), qtStyleSource.end(), '\\', '/');
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);

  engine.open(core::makeMediaItem(qtStyleSource));
  engine.play();

  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Playing, 1));
  ASSERT_TRUE(listener.waitForNonSilentWaveform());
  ASSERT_TRUE(listener.waitForPositiveWaveformIntensity());
  ASSERT_TRUE(listener.waitForNonSilentWaveformCount(10));
  EXPECT_TRUE(listener.waitForPositionAtLeast(100ms));
  EXPECT_EQ(engine.state(), core::PlaybackState::Playing);
  engine.setEventListener(nullptr);
}

TEST(VlcPlayerEngineTest, ReportsCorruptAndDisguisedMediaAsUnsupportedFormat) {
  const std::vector<std::pair<std::wstring, std::string>> invalidFiles{
      {L"损坏 视频.mp4", std::string({char{0}, char{static_cast<char>(0xFF)},
                                      char{static_cast<char>(0x7F)}, 'b', 'r',
                                      'o', 'k', 'e', 'n'})},
      {L"文本 伪装.mp4",
       "\xEF\xBB\xBFthis is plain text and not an mp4 media file"},
  };

  for (const auto& [fileName, contents] : invalidFiles) {
    GeneratedInvalidMedia media(fileName, contents);
    RecordingListener listener;
    VlcPlayerEngine engine(testOptions());
    engine.setEventListener(&listener);
    engine.open(core::makeMediaItem(media.source()));

    ASSERT_TRUE(listener.waitForError(5s));
    const auto error = listener.lastError();
    EXPECT_EQ(error.kind, core::PlaybackErrorKind::UnsupportedFormat);
    EXPECT_NE(error.userMessage.find("损坏或格式不受支持"), std::string::npos);
    EXPECT_EQ(error.engineDetail.find(media.source()), std::string::npos);
    EXPECT_EQ(error.userMessage.find(media.source()), std::string::npos);
    EXPECT_EQ(listener.endCount(), 0U);
  }
}

TEST(VlcPlayerEngineTest, PlaysPausesSeeksResumesAndStopsGeneratedWav) {
  GeneratedWav media(3s);
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);
  engine.open(core::makeMediaItem(media.source()));

  const auto firstPlaying =
      listener.stateCount(core::PlaybackState::Playing) + 1;
  engine.play();
  ASSERT_TRUE(
      listener.waitForStateCount(core::PlaybackState::Playing, firstPlaying));
  EXPECT_NE(listener.stateThread(core::PlaybackState::Playing),
            std::this_thread::get_id());
  ASSERT_TRUE(listener.waitForKnownDuration());
  ASSERT_TRUE(listener.waitForPositionAtLeast(100ms));
  EXPECT_EQ(engine.state(), core::PlaybackState::Playing);
  engine.setVolume(64);
  engine.setMuted(true);
  engine.setMuted(false);
  engine.setPlaybackRate(1.5);
  ASSERT_TRUE(listener.waitForPositionAtLeast(400ms));
  EXPECT_EQ(engine.state(), core::PlaybackState::Playing);
  engine.setPlaybackRate(1.0);
  ASSERT_TRUE(listener.waitForPositionAtLeast(700ms));
  EXPECT_EQ(engine.state(), core::PlaybackState::Playing);

  const auto firstPause = listener.stateCount(core::PlaybackState::Paused) + 1;
  engine.pause();
  ASSERT_TRUE(
      listener.waitForStateCount(core::PlaybackState::Paused, firstPause));

  engine.seek(1s);
  ASSERT_TRUE(listener.waitForPositionAtLeast(900ms));
  EXPECT_EQ(engine.state(), core::PlaybackState::Paused);

  engine.seek(2s);
  ASSERT_TRUE(listener.waitForPositionAtLeast(1900ms));
  EXPECT_EQ(engine.state(), core::PlaybackState::Paused);

  const auto secondPlaying =
      listener.stateCount(core::PlaybackState::Playing) + 1;
  engine.play();
  ASSERT_TRUE(
      listener.waitForStateCount(core::PlaybackState::Playing, secondPlaying));

  const auto stopped = listener.stateCount(core::PlaybackState::Stopped) + 1;
  engine.stop();
  ASSERT_TRUE(
      listener.waitForStateCount(core::PlaybackState::Stopped, stopped));
  EXPECT_EQ(engine.position().current, 0ms);
  engine.setEventListener(nullptr);
}

TEST(VlcPlayerEngineTest, ReportsNaturalEndForGeneratedWav) {
  GeneratedWav media(300ms);
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);
  engine.open(core::makeMediaItem(media.source()));
  engine.play();

  ASSERT_TRUE(listener.waitForEnd());
  EXPECT_EQ(engine.state(), core::PlaybackState::Ended);
  EXPECT_GE(engine.position().current, 250ms);
  engine.setEventListener(nullptr);
}

TEST(VlcPlayerEngineTest, SeeksAndReplaysGeneratedWavAfterNaturalEnd) {
  GeneratedWav media(1s);
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);
  engine.open(core::makeMediaItem(media.source()));
  engine.play();

  ASSERT_TRUE(listener.waitForEnd());
  ASSERT_EQ(engine.state(), core::PlaybackState::Ended);
  const auto nextStopped =
      listener.stateCount(core::PlaybackState::Stopped) + 1;
  engine.stop();
  ASSERT_TRUE(
      listener.waitForStateCount(core::PlaybackState::Stopped, nextStopped));
  const auto nextPlaying =
      listener.stateCount(core::PlaybackState::Playing) + 1;
  engine.play();
  ASSERT_TRUE(
      listener.waitForStateCount(core::PlaybackState::Playing, nextPlaying));
  engine.seek(100ms);
  EXPECT_TRUE(listener.waitForPositionAtLeast(200ms));
  engine.setEventListener(nullptr);
}

TEST(VlcPlayerEngineTest, ReopensAndReplaysGeneratedWavAfterNaturalEnd) {
  GeneratedWav media(1s);
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);
  engine.open(core::makeMediaItem(media.source()));
  engine.play();

  ASSERT_TRUE(listener.waitForEnd());
  ASSERT_EQ(engine.state(), core::PlaybackState::Ended);
  const auto nextOpening =
      listener.stateCount(core::PlaybackState::Opening) + 1;
  engine.open(core::makeMediaItem(media.source()));
  ASSERT_TRUE(
      listener.waitForStateCount(core::PlaybackState::Opening, nextOpening));
  const auto nextPlaying =
      listener.stateCount(core::PlaybackState::Playing) + 1;
  engine.play();
  ASSERT_TRUE(
      listener.waitForStateCount(core::PlaybackState::Playing, nextPlaying));
  ASSERT_TRUE(listener.waitForKnownDuration());
  engine.seek(100ms);
  EXPECT_TRUE(listener.waitForPositionAtLeast(200ms));
  engine.setEventListener(nullptr);
}

TEST(VlcPlayerEngineTest, RepeatedlyDestroysActivePlayersWithinBoundedTime) {
  GeneratedWav media(1s);
  for (int iteration = 0; iteration < 5; ++iteration) {
    RecordingListener listener;
    const auto startedAt = std::chrono::steady_clock::now();
    {
      VlcPlayerEngine engine(testOptions());
      engine.setEventListener(&listener);
      engine.open(core::makeMediaItem(media.source()));
      engine.play();
      ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Playing, 1));
    }
    EXPECT_LT(std::chrono::steady_clock::now() - startedAt, 5s);
  }
}

}  // namespace
}  // namespace mediahub::engine_vlc
