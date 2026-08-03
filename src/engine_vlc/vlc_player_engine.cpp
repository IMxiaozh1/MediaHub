#include "mediahub/engine_vlc/vlc_player_engine.h"

#include "vlc_event_mapping.h"
#include "vlc_handles.h"

#include <vlc/vlc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace mediahub::engine_vlc {
namespace {

using namespace std::chrono_literals;

constexpr auto kPositionEventInterval = 200ms;
constexpr auto kShutdownTimeout = 2s;
constexpr auto kShutdownPollInterval = 10ms;

constexpr std::array<libvlc_event_type_t, 11> kPlayerEvents{
    libvlc_MediaPlayerNothingSpecial,
    libvlc_MediaPlayerOpening,
    libvlc_MediaPlayerBuffering,
    libvlc_MediaPlayerPlaying,
    libvlc_MediaPlayerPaused,
    libvlc_MediaPlayerStopped,
    libvlc_MediaPlayerEndReached,
    libvlc_MediaPlayerEncounteredError,
    libvlc_MediaPlayerTimeChanged,
    libvlc_MediaPlayerSeekableChanged,
    libvlc_MediaPlayerLengthChanged,
};

std::vector<const char*> initializationArguments(const VlcPlayerEngineOptions options) {
    std::vector<const char*> arguments{
        "--ignore-config",
        "--no-video-title-show",
        "--quiet",
    };
    if (options.useDummyAudioOutput) {
        arguments.push_back("--aout=dummy");
    }
    if (options.useDummyVideoOutput) {
        arguments.push_back("--vout=dummy");
    }
    return arguments;
}

VlcInstancePtr createInstance(const VlcPlayerEngineOptions options) {
    const auto arguments = initializationArguments(options);
    VlcInstancePtr instance(
        libvlc_new(static_cast<int>(arguments.size()), arguments.data()));
    if (!instance) {
        throw std::runtime_error("无法初始化 libVLC 播放内核。");
    }
    return instance;
}

std::optional<VlcPlaybackEvent> playbackEventFromLibVlc(
    const libvlc_event_type_t eventType) noexcept {
    switch (eventType) {
    case libvlc_MediaPlayerNothingSpecial:
        return VlcPlaybackEvent::NothingSpecial;
    case libvlc_MediaPlayerOpening:
        return VlcPlaybackEvent::Opening;
    case libvlc_MediaPlayerBuffering:
        return VlcPlaybackEvent::Buffering;
    case libvlc_MediaPlayerPlaying:
        return VlcPlaybackEvent::Playing;
    case libvlc_MediaPlayerPaused:
        return VlcPlaybackEvent::Paused;
    case libvlc_MediaPlayerStopped:
        return VlcPlaybackEvent::Stopped;
    case libvlc_MediaPlayerEndReached:
        return VlcPlaybackEvent::EndReached;
    case libvlc_MediaPlayerEncounteredError:
        return VlcPlaybackEvent::EncounteredError;
    default:
        return std::nullopt;
    }
}

std::optional<std::chrono::milliseconds> durationFromLibVlc(
    const libvlc_time_t duration) noexcept {
    if (duration < 0) {
        return std::nullopt;
    }
    return std::chrono::milliseconds(duration);
}

bool isTerminalState(const libvlc_state_t state) noexcept {
    return state == libvlc_NothingSpecial || state == libvlc_Stopped ||
           state == libvlc_Ended || state == libvlc_Error;
}

std::string displayNameFor(const core::MediaItem& item) {
    if (!item.displayName.empty()) {
        return item.displayName;
    }

    const std::string_view source = item.source;
    const auto separator = source.find_last_of("/\\");
    const auto name = source.substr(separator == std::string_view::npos ? 0 : separator + 1);
    return name.empty() ? std::string("所选媒体") : std::string(name);
}

std::filesystem::path pathFromUtf8(const std::string_view source) {
    const auto* const begin = reinterpret_cast<const char8_t*>(source.data());
    return std::filesystem::path(std::u8string(begin, begin + source.size()));
}

bool hasObviouslyInvalidMp4Content(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    if (extension != ".mp4") {
        return false;
    }

    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError || size < 12) {
        return true;
    }

    std::array<char, 64> header{};
    std::ifstream input(path, std::ios::binary);
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    const auto bytesRead = static_cast<std::size_t>(input.gcount());
    const auto end = header.begin() + static_cast<std::ptrdiff_t>(bytesRead);
    const std::array<char, 4> signature{'f', 't', 'y', 'p'};
    if (std::search(header.begin(), end, signature.begin(), signature.end()) != end) {
        return false;
    }

    return std::all_of(header.begin(), end, [](const unsigned char value) {
        return std::isprint(value) != 0 || std::isspace(value) != 0;
    });
}

}  // namespace

class VlcPlayerEngine::Impl {
public:
    explicit Impl(const VlcPlayerEngineOptions options)
        : instance_(createInstance(options)),
          player_(libvlc_media_player_new(instance_.get())) {
        if (!player_) {
            throw std::runtime_error("无法创建 libVLC 播放器对象。");
        }
        attachEvents();
    }

    ~Impl() {
        shutdown();
    }

    void open(core::MediaItem item) {
        clearCurrentMedia(displayNameFor(item));
        if (item.kind == core::MediaSourceKind::NetworkStream) {
            reportError(core::PlaybackErrorKind::UnsupportedFormat,
                        "Network input is disabled in MediaHub v0.1",
                        "当前版本暂不支持播放网络地址。");
            return;
        }

        const auto path = pathFromUtf8(item.source);
        std::error_code pathError;
        const bool pathExists = std::filesystem::exists(path, pathError);
        if (pathError) {
            reportError(core::PlaybackErrorKind::SourceUnreadable,
                        "Local media path could not be inspected",
                        "无法读取媒体文件“" + displayNameFor(item) + "”。");
            return;
        }
        if (!pathExists) {
            reportError(core::PlaybackErrorKind::SourceNotFound,
                        "Local media path does not exist",
                        "找不到媒体文件“" + displayNameFor(item) + "”。");
            return;
        }
        const bool isRegularFile = std::filesystem::is_regular_file(path, pathError);
        if (pathError || !isRegularFile) {
            reportError(core::PlaybackErrorKind::SourceUnreadable,
                        "Local media path is not a readable regular file",
                        "无法读取媒体文件“" + displayNameFor(item) + "”。");
            return;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            reportError(core::PlaybackErrorKind::SourceUnreadable,
                        "Local media file could not be opened for reading",
                        "无法读取媒体文件“" + displayNameFor(item) + "”。");
            return;
        }
        input.close();

        if (hasObviouslyInvalidMp4Content(path)) {
            reportError(core::PlaybackErrorKind::UnsupportedFormat,
                        "MP4 container signature is missing or invalid",
                        "无法播放媒体“" + displayNameFor(item) +
                            "”，文件内容可能损坏或格式不受支持。");
            return;
        }

        VlcMediaPtr newMedia(libvlc_media_new_path(instance_.get(), item.source.c_str()));
        if (!newMedia) {
            reportError(core::PlaybackErrorKind::SourceUnreadable,
                        "libVLC could not create a media descriptor",
                        "无法打开媒体文件“" + displayNameFor(item) + "”。");
            return;
        }

        libvlc_media_player_set_media(player_.get(), newMedia.get());
        media_ = std::move(newMedia);
        {
            const std::lock_guard lock(mutex_);
            currentDisplayName_ = displayNameFor(item);
        }
        updateState(core::PlaybackState::Opening);
    }

    void play() {
        if (!media_) {
            reportError(core::PlaybackErrorKind::EngineNotInitialized,
                        "Play was requested without loaded media",
                        "请先打开一个媒体文件再开始播放。");
            return;
        }
        if (libvlc_media_player_play(player_.get()) != 0) {
            reportPlaybackFailure("libVLC rejected the play request",
                                  core::PlaybackErrorKind::Unknown);
        }
    }

    void pause() {
        if (media_) {
            libvlc_media_player_set_pause(player_.get(), 1);
        }
    }

    void stop() {
        if (media_) {
            libvlc_media_player_stop(player_.get());
        }
    }

    void seek(std::chrono::milliseconds position) {
        if (!media_) {
            return;
        }

        position = std::max(position, 0ms);
        {
            const std::lock_guard lock(mutex_);
            if (position_.total.has_value()) {
                position = std::min(position, *position_.total);
            }
        }
        libvlc_media_player_set_time(player_.get(), position.count());
        updatePosition(position, true);
    }

    void setVolume(const int volume) {
        const int boundedVolume = std::clamp(volume, 0, 100);
        static_cast<void>(libvlc_audio_set_volume(player_.get(), boundedVolume));
    }

    void setMuted(const bool isMuted) {
        libvlc_audio_set_mute(player_.get(), isMuted ? 1 : 0);
    }

    void setVideoSurface(void* const nativeHandle) {
        libvlc_media_player_set_hwnd(player_.get(), nativeHandle);
    }

    core::PlaybackState state() const {
        const std::lock_guard lock(mutex_);
        return state_;
    }

    core::PlaybackPosition position() const {
        const std::lock_guard lock(mutex_);
        return position_;
    }

    std::optional<std::chrono::milliseconds> duration() const {
        const std::lock_guard lock(mutex_);
        return position_.total;
    }

    bool isSeekable() const {
        const std::lock_guard lock(mutex_);
        return position_.isSeekable;
    }

    void setEventListener(core::PlayerEventListener* const listener) {
        std::unique_lock lock(mutex_);
        listener_ = nullptr;
        callbacksDrained_.wait(lock, [this] { return activeCallbacks_ == 0; });
        if (!isShuttingDown_) {
            listener_ = listener;
        }
    }

private:
    template <typename Callback>
    void dispatch(Callback&& callback) noexcept {
        core::PlayerEventListener* listener = nullptr;
        {
            const std::lock_guard lock(mutex_);
            if (isShuttingDown_ || listener_ == nullptr) {
                return;
            }
            listener = listener_;
            ++activeCallbacks_;
        }

        callback(*listener);

        {
            const std::lock_guard lock(mutex_);
            --activeCallbacks_;
            if (activeCallbacks_ == 0) {
                callbacksDrained_.notify_all();
            }
        }
    }

    void attachEvents() {
        eventManager_ = libvlc_media_player_event_manager(player_.get());
        for (const auto eventType : kPlayerEvents) {
            if (libvlc_event_attach(eventManager_, eventType, &Impl::libVlcCallback, this) != 0) {
                detachEvents();
                throw std::runtime_error("无法注册 libVLC 播放事件。");
            }
            ++attachedEventCount_;
        }
    }

    void detachEvents() noexcept {
        if (eventManager_ == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < attachedEventCount_; ++index) {
            libvlc_event_detach(eventManager_, kPlayerEvents[index], &Impl::libVlcCallback, this);
        }
        attachedEventCount_ = 0;
        eventManager_ = nullptr;
    }

    // 调用线程：libVLC 事件线程，禁止在此操作控件或调用停止与释放。
    static void libVlcCallback(const libvlc_event_t* const event, void* const opaque) noexcept {
        if (event != nullptr && opaque != nullptr) {
            auto& implementation = *static_cast<Impl*>(opaque);
            implementation.beginLibVlcCallback();
            implementation.handleEvent(*event);
            implementation.finishLibVlcCallback();
        }
    }

    void beginLibVlcCallback() noexcept {
        const std::lock_guard lock(mutex_);
        ++activeLibVlcCallbacks_;
    }

    void finishLibVlcCallback() noexcept {
        const std::lock_guard lock(mutex_);
        --activeLibVlcCallbacks_;
        if (activeLibVlcCallbacks_ == 0) {
            callbacksDrained_.notify_all();
        }
    }

    // 调用线程：libVLC 事件线程，只更新快照并向监听器发送值类型事件。
    void handleEvent(const libvlc_event_t& event) noexcept {
        if (const auto playbackEvent = playbackEventFromLibVlc(event.type)) {
            if (*playbackEvent == VlcPlaybackEvent::Buffering &&
                !isBufferingInProgress(
                    event.u.media_player_buffering.new_cache,
                    libvlc_media_player_get_state(player_.get()) == libvlc_Playing)) {
                return;
            }
            const auto nextState = mapPlaybackState(*playbackEvent);
            if (*playbackEvent == VlcPlaybackEvent::Stopped) {
                updatePosition(0ms, true);
            } else if (*playbackEvent == VlcPlaybackEvent::EndReached) {
                movePositionToEnd();
            }
            updateState(nextState);

            if (*playbackEvent == VlcPlaybackEvent::EndReached) {
                dispatch([](core::PlayerEventListener& listener) noexcept {
                    listener.onEndReached();
                });
            } else if (*playbackEvent == VlcPlaybackEvent::EncounteredError) {
                reportPlaybackFailure("libVLC reported MediaPlayerEncounteredError",
                                      core::PlaybackErrorKind::UnsupportedFormat,
                                      false);
            }
            return;
        }

        switch (event.type) {
        case libvlc_MediaPlayerTimeChanged:
            updatePosition(
                std::chrono::milliseconds(std::max<libvlc_time_t>(
                    event.u.media_player_time_changed.new_time, 0)),
                false);
            break;
        case libvlc_MediaPlayerSeekableChanged:
            updateSeekable(event.u.media_player_seekable_changed.new_seekable != 0);
            break;
        case libvlc_MediaPlayerLengthChanged:
            updateDuration(durationFromLibVlc(event.u.media_player_length_changed.new_length));
            break;
        default:
            break;
        }
    }

    void updateState(const core::PlaybackState state) noexcept {
        {
            const std::lock_guard lock(mutex_);
            state_ = state;
        }
        dispatch([state](core::PlayerEventListener& listener) noexcept {
            listener.onStateChanged(state);
        });
    }

    void updatePosition(const std::chrono::milliseconds current, const bool force) noexcept {
        core::PlaybackPosition snapshot;
        bool shouldNotify = false;
        {
            const std::lock_guard lock(mutex_);
            position_.current = current;
            const auto now = std::chrono::steady_clock::now();
            shouldNotify = force ||
                           lastPositionNotification_ ==
                               std::chrono::steady_clock::time_point{} ||
                           now - lastPositionNotification_ >= kPositionEventInterval;
            if (shouldNotify) {
                lastPositionNotification_ = now;
                snapshot = position_;
            }
        }
        if (shouldNotify) {
            dispatch([snapshot](core::PlayerEventListener& listener) noexcept {
                listener.onPositionChanged(snapshot);
            });
        }
    }

    void updateDuration(
        const std::optional<std::chrono::milliseconds> duration) noexcept {
        {
            const std::lock_guard lock(mutex_);
            position_.total = duration;
        }
        dispatch([duration](core::PlayerEventListener& listener) noexcept {
            listener.onDurationChanged(duration);
        });
    }

    void updateSeekable(const bool isSeekable) noexcept {
        core::PlaybackPosition snapshot;
        {
            const std::lock_guard lock(mutex_);
            position_.isSeekable = isSeekable;
            lastPositionNotification_ = std::chrono::steady_clock::now();
            snapshot = position_;
        }
        dispatch([snapshot](core::PlayerEventListener& listener) noexcept {
            listener.onPositionChanged(snapshot);
        });
    }

    void movePositionToEnd() noexcept {
        core::PlaybackPosition snapshot;
        {
            const std::lock_guard lock(mutex_);
            if (position_.total.has_value()) {
                position_.current = *position_.total;
            }
            lastPositionNotification_ = std::chrono::steady_clock::now();
            snapshot = position_;
        }
        dispatch([snapshot](core::PlayerEventListener& listener) noexcept {
            listener.onPositionChanged(snapshot);
        });
    }

    void reportPlaybackFailure(
        const std::string detail,
        const core::PlaybackErrorKind kind = core::PlaybackErrorKind::UnsupportedFormat,
        const bool updateFailedState = true) {
        std::string displayName;
        {
            const std::lock_guard lock(mutex_);
            displayName = currentDisplayName_.empty() ? "所选媒体" : currentDisplayName_;
        }
        if (updateFailedState) {
            updateState(core::PlaybackState::Failed);
        }
        dispatch([error = core::PlaybackError{
                      kind,
                      detail,
                      "无法播放媒体“" + displayName + "”，文件内容可能损坏或格式不受支持。"}](
                     core::PlayerEventListener& listener) mutable noexcept {
            listener.onError(std::move(error));
        });
    }

    void reportError(const core::PlaybackErrorKind kind,
                     std::string detail,
                     std::string userMessage) {
        updateState(core::PlaybackState::Failed);
        dispatch([error = core::PlaybackError{kind, std::move(detail), std::move(userMessage)}](
                     core::PlayerEventListener& listener) mutable noexcept {
            listener.onError(std::move(error));
        });
    }

    void stopCurrentMedia() {
        if (media_ && !isTerminalState(libvlc_media_player_get_state(player_.get()))) {
            libvlc_media_player_stop(player_.get());
        }
    }

    void clearCurrentMedia(std::string displayName) {
        stopCurrentMedia();
        libvlc_media_player_set_media(player_.get(), nullptr);
        media_.reset();
        const std::lock_guard lock(mutex_);
        currentDisplayName_ = std::move(displayName);
        position_ = {};
        lastPositionNotification_ = {};
    }

    void waitUntilPlayerStops() noexcept {
        const auto deadline = std::chrono::steady_clock::now() + kShutdownTimeout;
        while (!isTerminalState(libvlc_media_player_get_state(player_.get())) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(kShutdownPollInterval);
        }
    }

    void shutdown() noexcept {
        {
            const std::lock_guard lock(mutex_);
            isShuttingDown_ = true;
            listener_ = nullptr;
        }

        // 释放顺序固定：先断开事件，再停止并等待，最后依次释放播放器、媒体和实例。
        detachEvents();
        {
            std::unique_lock lock(mutex_);
            callbacksDrained_.wait(lock, [this] {
                return activeCallbacks_ == 0 && activeLibVlcCallbacks_ == 0;
            });
        }
        if (player_) {
            if (!isTerminalState(libvlc_media_player_get_state(player_.get()))) {
                libvlc_media_player_stop(player_.get());
            }
            waitUntilPlayerStops();
        }
        player_.reset();
        media_.reset();
        instance_.reset();
    }

    VlcInstancePtr instance_;
    VlcMediaPtr media_;
    VlcPlayerPtr player_;
    libvlc_event_manager_t* eventManager_{nullptr};
    std::size_t attachedEventCount_{0};

    mutable std::mutex mutex_;
    std::condition_variable callbacksDrained_;
    core::PlayerEventListener* listener_{nullptr};
    std::size_t activeCallbacks_{0};
    std::size_t activeLibVlcCallbacks_{0};
    bool isShuttingDown_{false};
    core::PlaybackState state_{core::PlaybackState::Idle};
    core::PlaybackPosition position_;
    std::string currentDisplayName_;
    std::chrono::steady_clock::time_point lastPositionNotification_;
};

VlcPlayerEngine::VlcPlayerEngine(const VlcPlayerEngineOptions options)
    : impl_(std::make_unique<Impl>(options)) {}

VlcPlayerEngine::~VlcPlayerEngine() = default;

void VlcPlayerEngine::open(core::MediaItem item) {
    impl_->open(std::move(item));
}

void VlcPlayerEngine::play() {
    impl_->play();
}

void VlcPlayerEngine::pause() {
    impl_->pause();
}

void VlcPlayerEngine::stop() {
    impl_->stop();
}

void VlcPlayerEngine::seek(const std::chrono::milliseconds position) {
    impl_->seek(position);
}

void VlcPlayerEngine::setVolume(const int volume) {
    impl_->setVolume(volume);
}

void VlcPlayerEngine::setMuted(const bool isMuted) {
    impl_->setMuted(isMuted);
}

void VlcPlayerEngine::setVideoSurface(void* const nativeHandle) {
    impl_->setVideoSurface(nativeHandle);
}

core::PlaybackState VlcPlayerEngine::state() const {
    return impl_->state();
}

core::PlaybackPosition VlcPlayerEngine::position() const {
    return impl_->position();
}

std::optional<std::chrono::milliseconds> VlcPlayerEngine::duration() const {
    return impl_->duration();
}

bool VlcPlayerEngine::isSeekable() const {
    return impl_->isSeekable();
}

void VlcPlayerEngine::setEventListener(core::PlayerEventListener* const listener) {
    impl_->setEventListener(listener);
}

}  // namespace mediahub::engine_vlc
