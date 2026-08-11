#include "mediahub/engine_vlc/vlc_player_engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
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
  void onOpenStarted(const core::OpenRequestId requestId) noexcept override {
    const std::lock_guard lock(mutex_);
    openRequestIds_.push_back(requestId);
    changed_.notify_all();
  }

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

  void onBufferingChanged(const int percentage) noexcept override {
    const std::lock_guard lock(mutex_);
    bufferingPercentage_ = percentage;
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

  void onVideoSurfaceReleased(void* const nativeHandle) noexcept override {
    const std::lock_guard lock(mutex_);
    releasedVideoSurfaces_.push_back(nativeHandle);
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

  [[nodiscard]] bool waitForReleasedVideoSurface(
      void* const nativeHandle,
      const std::chrono::milliseconds timeout = 1s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] {
      return std::find(releasedVideoSurfaces_.begin(),
                       releasedVideoSurfaces_.end(),
                       nativeHandle) != releasedVideoSurfaces_.end();
    });
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
  int bufferingPercentage_{0};
  bool hasDurationEvent_{false};
  core::AudioWaveform waveform_;
  std::size_t waveformCount_{0};
  std::size_t nonSilentWaveformCount_{0};
  std::size_t endCount_{0};
  std::vector<core::PlaybackError> errors_;
  std::vector<core::OpenRequestId> openRequestIds_;
  std::vector<void*> releasedVideoSurfaces_;
};

class VideoSurfaceRecorder final {
 public:
  void record(void* const nativeHandle) {
    const std::lock_guard lock(mutex_);
    handles_.push_back(nativeHandle);
    changed_.notify_all();
  }

  [[nodiscard]] bool waitFor(
      void* const nativeHandle,
      const std::chrono::milliseconds timeout = 1s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [this, nativeHandle] {
      return std::find(handles_.begin(), handles_.end(), nativeHandle) !=
             handles_.end();
    });
  }

  [[nodiscard]] bool waitForCount(
      void* const nativeHandle, const std::size_t expectedCount,
      const std::chrono::milliseconds timeout = 1s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [this, nativeHandle, expectedCount] {
      return static_cast<std::size_t>(
                 std::count(handles_.begin(), handles_.end(), nativeHandle)) >=
             expectedCount;
    });
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::vector<void*> handles_;
};

class RetirementGate final {
 public:
  void waitBeforeStop() {
    std::unique_lock lock(mutex_);
    ++reachedStopCount_;
    ++activeWaiterCount_;
    changed_.notify_all();
    changed_.wait(lock, [this] { return canStop_; });
    --activeWaiterCount_;
    changed_.notify_all();
  }

  [[nodiscard]] bool waitUntilReached(
      const std::size_t expectedCount = 1,
      const std::chrono::milliseconds timeout = 1s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [this, expectedCount] {
      return reachedStopCount_ >= expectedCount;
    });
  }

  void allowStop() {
    const std::lock_guard lock(mutex_);
    canStop_ = true;
    changed_.notify_all();
  }

  [[nodiscard]] bool waitUntilIdle(
      const std::chrono::milliseconds timeout = 1s) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [this] { return activeWaiterCount_ == 0; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::size_t reachedStopCount_{0};
  std::size_t activeWaiterCount_{0};
  bool canStop_{false};
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
  EXPECT_EQ(bufferingPercentage(-1.0F), 0);
  EXPECT_EQ(bufferingPercentage(36.6F), 37);
  EXPECT_EQ(bufferingPercentage(100.4F), 100);
  EXPECT_EQ(bufferingPercentage(std::numeric_limits<float>::quiet_NaN()), 0);
  EXPECT_EQ(bufferingPercentage(std::numeric_limits<float>::infinity()), 0);
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

  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 1));
  EXPECT_EQ(engine.state(), core::PlaybackState::Opening);
  EXPECT_EQ(listener.stateCount(core::PlaybackState::Opening), 1U);
  EXPECT_EQ(listener.errorCount(), 0U);
}

TEST(VlcPlayerEngineTest, QueuesNetworkControlsWithoutBlockingCaller) {
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);

  const auto startedAt = std::chrono::steady_clock::now();
  engine.open(core::MediaItem{"http://127.0.0.1:1/slow.ts",
                              core::MediaSourceKind::NetworkStream, "slow.ts"});
  engine.play();
  engine.stop();

  EXPECT_LT(std::chrono::steady_clock::now() - startedAt, 250ms);
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Stopped, 1));

  engine.open(core::MediaItem{"https://example.invalid/retry.m3u8",
                              core::MediaSourceKind::NetworkStream,
                              "retry.m3u8"});
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 2));
}

TEST(VlcPlayerEngineTest, RepeatedNetworkSwitchesReuseInstanceAndSurface) {
  RecordingListener listener;
  VideoSurfaceRecorder surfaceRecorder;
  std::atomic<int> instanceCreatedCount{0};
  auto options = testOptions();
  options.videoSurfaceObserver = [&surfaceRecorder](void* const nativeHandle) {
    surfaceRecorder.record(nativeHandle);
  };
  options.instanceCreatedObserver = [&instanceCreatedCount] {
    ++instanceCreatedCount;
  };
  VlcPlayerEngine engine(std::move(options));
  engine.setEventListener(&listener);
  auto* const embeddedSurface = reinterpret_cast<void*>(0x1234);

  engine.setVideoSurface(embeddedSurface);
  ASSERT_TRUE(surfaceRecorder.waitFor(embeddedSurface));
  engine.open(core::MediaItem{"http://127.0.0.1:1/first.ts",
                              core::MediaSourceKind::NetworkStream,
                              "first.ts"});
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 1));

  engine.open(core::MediaItem{"http://127.0.0.1:1/second.ts",
                              core::MediaSourceKind::NetworkStream,
                              "second.ts"});
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 2));
  engine.open(core::MediaItem{"http://127.0.0.1:1/third.ts",
                              core::MediaSourceKind::NetworkStream,
                              "third.ts"});
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 3));

  EXPECT_TRUE(surfaceRecorder.waitForCount(embeddedSurface, 3));
  EXPECT_FALSE(surfaceRecorder.waitFor(nullptr, 500ms));
  EXPECT_EQ(instanceCreatedCount.load(), 2);
}

TEST(VlcPlayerEngineTest,
     WaitsForRetiredPlayerVoutAndSkipsSupersededNetworkOpen) {
  RecordingListener listener;
  RetirementGate retirementGate;
  auto options = testOptions();
  options.beforeRetiredPlayerStop = [&retirementGate] {
    retirementGate.waitBeforeStop();
  };
  VlcPlayerEngine engine(std::move(options));
  engine.setEventListener(&listener);

  engine.open(core::MediaItem{"http://127.0.0.1:1/first.ts",
                              core::MediaSourceKind::NetworkStream,
                              "first.ts"});
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 1));

  engine.open(core::MediaItem{"http://127.0.0.1:1/second.ts",
                              core::MediaSourceKind::NetworkStream,
                              "second.ts"});
  ASSERT_TRUE(retirementGate.waitUntilReached());
  EXPECT_FALSE(
      listener.waitForStateCount(core::PlaybackState::Opening, 2, 500ms));

  engine.open(core::MediaItem{"http://127.0.0.1:1/third.ts",
                              core::MediaSourceKind::NetworkStream,
                              "third.ts"});
  retirementGate.allowStop();
  EXPECT_TRUE(retirementGate.waitUntilIdle());
  EXPECT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 2));
  EXPECT_FALSE(
      listener.waitForStateCount(core::PlaybackState::Opening, 3, 500ms));
}

TEST(VlcPlayerEngineTest,
     OpensOnDistinctSurfaceWhilePreviousPlayerRetirementIsBlocked) {
  RecordingListener listener;
  RetirementGate retirementGate;
  VideoSurfaceRecorder surfaceRecorder;
  auto options = testOptions();
  options.beforeRetiredPlayerStop = [&retirementGate] {
    retirementGate.waitBeforeStop();
  };
  options.videoSurfaceObserver = [&surfaceRecorder](void *const nativeHandle) {
    surfaceRecorder.record(nativeHandle);
  };
  VlcPlayerEngine engine(std::move(options));
  engine.setEventListener(&listener);
  auto *const firstSurface = reinterpret_cast<void *>(0x1234);
  auto *const secondSurface = reinterpret_cast<void *>(0x5678);

  engine.open(core::MediaItem{"http://127.0.0.1:1/first.ts",
                              core::MediaSourceKind::NetworkStream,
                              "first.ts"},
              firstSurface);
  ASSERT_TRUE(surfaceRecorder.waitFor(firstSurface));
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 1));

  engine.open(core::MediaItem{"http://127.0.0.1:1/second.ts",
                              core::MediaSourceKind::NetworkStream,
                              "second.ts"},
              secondSurface);
  ASSERT_TRUE(retirementGate.waitUntilReached());
  EXPECT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 2,
                                         500ms));
  EXPECT_TRUE(surfaceRecorder.waitFor(secondSurface));
  EXPECT_FALSE(surfaceRecorder.waitFor(nullptr, 100ms));
  engine.stop();
  EXPECT_TRUE(listener.waitForStateCount(core::PlaybackState::Stopped, 1,
                                         500ms));
  EXPECT_TRUE(retirementGate.waitUntilReached(2));
  retirementGate.allowStop();
  EXPECT_TRUE(retirementGate.waitUntilIdle());
  EXPECT_TRUE(listener.waitForReleasedVideoSurface(firstSurface));
  EXPECT_FALSE(listener.waitForReleasedVideoSurface(secondSurface, 100ms));
}

TEST(VlcPlayerEngineTest, ReleasesPreviousSurfaceAfterSynchronousLocalSwitch) {
  GeneratedWav media(2s);
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);
  auto* const firstSurface = reinterpret_cast<void*>(0x1234);
  auto* const secondSurface = reinterpret_cast<void*>(0x5678);
  const core::MediaItem item{media.source(),
                             core::MediaSourceKind::LocalFile,
                             "generated.wav"};

  engine.open(item, firstSurface);
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 1));
  engine.open(item, secondSurface);

  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 2));
  EXPECT_TRUE(listener.waitForReleasedVideoSurface(firstSurface));
  EXPECT_FALSE(listener.waitForReleasedVideoSurface(secondSurface, 100ms));
}

TEST(VlcPlayerEngineTest,
     BoundsBlockedRetirementsAndKeepsOneStopSlotAvailable) {
  RecordingListener listener;
  RetirementGate retirementGate;
  auto options = testOptions();
  options.beforeRetiredPlayerStop = [&retirementGate] {
    retirementGate.waitBeforeStop();
  };
  VlcPlayerEngine engine(std::move(options));
  engine.setEventListener(&listener);
  const std::array<void*, 5> surfaces{
      reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000),
      reinterpret_cast<void*>(0x3000), reinterpret_cast<void*>(0x4000),
      reinterpret_cast<void*>(0x5000)};

  engine.open(core::MediaItem{"http://127.0.0.1:1/first.ts",
                              core::MediaSourceKind::NetworkStream,
                              "first.ts"},
              surfaces[0]);
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 1));
  for (std::size_t index = 1; index < 4; ++index) {
    engine.open(core::MediaItem{
                    "http://127.0.0.1:1/next.ts",
                    core::MediaSourceKind::NetworkStream, "next.ts"},
                surfaces[index]);
    ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening,
                                           index + 1));
    ASSERT_TRUE(retirementGate.waitUntilReached(index));
  }

  engine.open(core::MediaItem{"http://127.0.0.1:1/rejected.ts",
                              core::MediaSourceKind::NetworkStream,
                              "rejected.ts"},
              surfaces[4]);
  ASSERT_TRUE(listener.waitForError());
  EXPECT_EQ(listener.lastError().kind, core::PlaybackErrorKind::EngineBusy);
  EXPECT_EQ(listener.lastError().userMessage,
            "多个旧直播仍在退出，请稍候后重试。");
  EXPECT_FALSE(
      listener.waitForStateCount(core::PlaybackState::Opening, 5, 100ms));
  EXPECT_TRUE(listener.waitForReleasedVideoSurface(surfaces[4]));

  engine.stop();
  EXPECT_TRUE(listener.waitForStateCount(core::PlaybackState::Stopped, 1,
                                         500ms));
  EXPECT_TRUE(retirementGate.waitUntilReached(4));
  retirementGate.allowStop();
  EXPECT_TRUE(retirementGate.waitUntilIdle());
}

TEST(VlcPlayerEngineTest, SwitchesFromNetworkDescriptorToLocalAudio) {
  GeneratedWav media(2s);
  RecordingListener listener;
  std::atomic<int> instanceCreatedCount{0};
  auto options = testOptions();
  options.instanceCreatedObserver = [&instanceCreatedCount] {
    ++instanceCreatedCount;
  };
  VlcPlayerEngine engine(std::move(options));
  engine.setEventListener(&listener);

  engine.setVolume(37);
  engine.setMuted(false);
  engine.open(core::MediaItem{"https://example.invalid/live.flv",
                              core::MediaSourceKind::NetworkStream,
                              "live.flv"});
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 1));

  engine.open(core::makeMediaItem(media.source()));
  engine.play();

  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 2));
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Playing, 1));
  ASSERT_TRUE(listener.waitForNonSilentWaveform());
  EXPECT_TRUE(listener.waitForPositionAtLeast(100ms));
  EXPECT_EQ(instanceCreatedCount.load(), 3);
}

TEST(VlcPlayerEngineTest, RejectsInvalidNetworkDescriptorWithoutLeakingUrl) {
  RecordingListener listener;
  VlcPlayerEngine engine(testOptions());
  engine.setEventListener(&listener);
  const std::string invalidAddress =
      "custom://user:secret@example.invalid/live?token=private";

  engine.open(core::MediaItem{
      invalidAddress, core::MediaSourceKind::NetworkStream, "测试直播"});

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

TEST(VlcPlayerEngineTest,
     StopsEndedLocalPlayerBeforeApplyingReplayVideoSurface) {
  GeneratedWav media(300ms);
  RecordingListener listener;
  std::atomic<bool> hasStoppedEndedPlayer{false};
  std::atomic<bool> replaySurfaceAppliedAfterStop{false};
  auto* const firstSurface = reinterpret_cast<void*>(0x1234);
  auto* const replaySurface = reinterpret_cast<void*>(0x5678);
  auto options = testOptions();
  options.beforeRetiredPlayerStop = [&hasStoppedEndedPlayer] {
    hasStoppedEndedPlayer.store(true);
  };
  options.videoSurfaceObserver =
      [replaySurface, &hasStoppedEndedPlayer,
       &replaySurfaceAppliedAfterStop](void* const nativeHandle) {
        if (nativeHandle == replaySurface) {
          replaySurfaceAppliedAfterStop.store(hasStoppedEndedPlayer.load());
        }
      };
  VlcPlayerEngine engine(std::move(options));
  engine.setEventListener(&listener);
  const auto item = core::makeMediaItem(media.source());

  engine.open(item, firstSurface);
  ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Opening, 1));
  engine.play();
  ASSERT_TRUE(listener.waitForEnd());
  ASSERT_EQ(engine.state(), core::PlaybackState::Ended);

  const auto nextOpening =
      listener.stateCount(core::PlaybackState::Opening) + 1;
  engine.open(item, replaySurface);
  ASSERT_TRUE(
      listener.waitForStateCount(core::PlaybackState::Opening, nextOpening));
  EXPECT_TRUE(hasStoppedEndedPlayer.load());
  EXPECT_TRUE(replaySurfaceAppliedAfterStop.load());
  EXPECT_TRUE(listener.waitForReleasedVideoSurface(firstSurface));
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
