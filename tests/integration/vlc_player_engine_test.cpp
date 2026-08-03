#include "mediahub/engine_vlc/vlc_player_engine.h"
#include "vlc_event_mapping.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mediahub::engine_vlc {
namespace {

using namespace std::chrono_literals;

void writeLittleEndian16(std::ostream& output, const std::uint16_t value) {
    const char bytes[]{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
    };
    output.write(bytes, sizeof(bytes));
}

void writeLittleEndian32(std::ostream& output, const std::uint32_t value) {
    const char bytes[]{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU),
    };
    output.write(bytes, sizeof(bytes));
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

// 每个测试在系统临时目录生成静音 PCM/WAV，析构时只清理自己创建的唯一目录。
class GeneratedWav final {
public:
    explicit GeneratedWav(const std::chrono::milliseconds duration) {
        const auto uniquePart =
            std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
        directory_ = std::filesystem::temp_directory_path() /
                     std::filesystem::path(L"MediaHub 阶段4 空格") / uniquePart;
        std::filesystem::create_directories(directory_);
        path_ = directory_ / std::filesystem::path(L"测试 音频.wav");
        writeFile(duration);
    }

    ~GeneratedWav() {
        std::error_code error;
        const auto parent = directory_.parent_path();
        std::filesystem::remove_all(directory_, error);
        error.clear();
        std::filesystem::remove(parent, error);
    }

    GeneratedWav(const GeneratedWav&) = delete;
    GeneratedWav& operator=(const GeneratedWav&) = delete;

    [[nodiscard]] std::string source() const {
        return pathToUtf8(path_);
    }

private:
    void writeFile(const std::chrono::milliseconds duration) {
        constexpr std::uint32_t kSampleRate = 8'000;
        constexpr std::uint16_t kChannels = 1;
        constexpr std::uint16_t kBitsPerSample = 16;
        constexpr std::uint16_t kBlockAlign = kChannels * kBitsPerSample / 8;
        constexpr std::uint32_t kByteRate = kSampleRate * kBlockAlign;

        const auto sampleCount = static_cast<std::uint32_t>(
            duration.count() * kSampleRate / 1'000);
        const std::uint32_t dataSize = sampleCount * kBlockAlign;

        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write("RIFF", 4);
        writeLittleEndian32(output, 36U + dataSize);
        output.write("WAVE", 4);
        output.write("fmt ", 4);
        writeLittleEndian32(output, 16U);
        writeLittleEndian16(output, 1U);
        writeLittleEndian16(output, kChannels);
        writeLittleEndian32(output, kSampleRate);
        writeLittleEndian32(output, kByteRate);
        writeLittleEndian16(output, kBlockAlign);
        writeLittleEndian16(output, kBitsPerSample);
        output.write("data", 4);
        writeLittleEndian32(output, dataSize);

        const std::vector<char> silence(dataSize, 0);
        output.write(silence.data(), static_cast<std::streamsize>(silence.size()));
        ASSERT_TRUE(output.good());
    }

    std::filesystem::path directory_;
    std::filesystem::path path_;
};

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

    void onDurationChanged(
        const std::optional<std::chrono::milliseconds> duration) noexcept override {
        const std::lock_guard lock(mutex_);
        duration_ = duration;
        hasDurationEvent_ = true;
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
        return static_cast<std::size_t>(std::count(states_.begin(), states_.end(), state));
    }

    [[nodiscard]] bool waitForStateCount(const core::PlaybackState state,
                                         const std::size_t expected,
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
        return changed_.wait_for(lock, timeout, [&] { return position_.current >= expected; });
    }

    [[nodiscard]] bool waitForKnownDuration(
        const std::chrono::milliseconds timeout = 5s) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] {
            return hasDurationEvent_ && duration_.has_value();
        });
    }

    [[nodiscard]] bool waitForEnd(const std::chrono::milliseconds timeout = 5s) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return endCount_ > 0; });
    }

    [[nodiscard]] bool waitForError(const std::chrono::milliseconds timeout = 1s) {
        return waitForErrorCount(1, timeout);
    }

    [[nodiscard]] bool waitForErrorCount(
        const std::size_t expected,
        const std::chrono::milliseconds timeout = 1s) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return errors_.size() >= expected; });
    }

    [[nodiscard]] core::PlaybackError lastError() const {
        const std::lock_guard lock(mutex_);
        return errors_.back();
    }

    [[nodiscard]] std::thread::id stateThread(const core::PlaybackState state) const {
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
    std::size_t endCount_{0};
    std::vector<core::PlaybackError> errors_;
};

VlcPlayerEngineOptions testOptions() {
    return VlcPlayerEngineOptions{true, true};
}

TEST(VlcEventMappingTest, MapsEveryPlaybackEventToCoreState) {
    EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::NothingSpecial), core::PlaybackState::Idle);
    EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Opening), core::PlaybackState::Opening);
    EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Buffering), core::PlaybackState::Buffering);
    EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Playing), core::PlaybackState::Playing);
    EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Paused), core::PlaybackState::Paused);
    EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::Stopped), core::PlaybackState::Stopped);
    EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::EndReached), core::PlaybackState::Ended);
    EXPECT_EQ(mapPlaybackState(VlcPlaybackEvent::EncounteredError),
              core::PlaybackState::Failed);
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

TEST(VlcPlayerEngineTest, RejectsNetworkSourceWithoutOpeningIt) {
    RecordingListener listener;
    VlcPlayerEngine engine(testOptions());
    engine.setEventListener(&listener);

    engine.open(core::MediaItem{"https://example.invalid/live",
                                core::MediaSourceKind::NetworkStream, "测试直播"});

    ASSERT_TRUE(listener.waitForError());
    EXPECT_EQ(engine.state(), core::PlaybackState::Failed);
    EXPECT_EQ(listener.lastError().kind, core::PlaybackErrorKind::UnsupportedFormat);
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
    EXPECT_EQ(listener.lastError().kind, core::PlaybackErrorKind::EngineNotInitialized);
}

TEST(VlcPlayerEngineTest, PlaysPausesSeeksResumesAndStopsGeneratedWav) {
    GeneratedWav media(3s);
    RecordingListener listener;
    VlcPlayerEngine engine(testOptions());
    engine.setEventListener(&listener);
    engine.open(core::makeMediaItem(media.source()));

    const auto firstPlaying = listener.stateCount(core::PlaybackState::Playing) + 1;
    engine.play();
    ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Playing, firstPlaying));
    EXPECT_NE(listener.stateThread(core::PlaybackState::Playing), std::this_thread::get_id());
    ASSERT_TRUE(listener.waitForKnownDuration());
    ASSERT_TRUE(listener.waitForPositionAtLeast(100ms));

    const auto firstPause = listener.stateCount(core::PlaybackState::Paused) + 1;
    engine.pause();
    ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Paused, firstPause));

    engine.seek(1s);
    ASSERT_TRUE(listener.waitForPositionAtLeast(900ms));

    const auto secondPlaying = listener.stateCount(core::PlaybackState::Playing) + 1;
    engine.play();
    ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Playing, secondPlaying));

    const auto stopped = listener.stateCount(core::PlaybackState::Stopped) + 1;
    engine.stop();
    ASSERT_TRUE(listener.waitForStateCount(core::PlaybackState::Stopped, stopped));
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
