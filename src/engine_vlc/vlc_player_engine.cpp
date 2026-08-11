#include "mediahub/engine_vlc/vlc_player_engine.h"

#include <vlc/vlc.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "vlc_event_mapping.h"
#include "vlc_handles.h"

namespace mediahub::engine_vlc {
namespace {

using namespace std::chrono_literals;

constexpr auto kPositionEventInterval = 200ms;
constexpr auto kWaveformEventInterval = 66ms;
constexpr auto kNetworkActivityPollInterval = 250ms;
constexpr auto kShutdownTimeout = 2s;
constexpr auto kShutdownPollInterval = 10ms;
constexpr std::size_t kMaxConcurrentRetirements = 4;
constexpr std::size_t kMaxOpenRetirements = kMaxConcurrentRetirements - 1;
constexpr float kWarmUnityPlaybackRate = 1.001F;
constexpr unsigned kWaveformSampleRate = 48'000;
constexpr char kVideoFileCachingOption[] = ":file-caching=30";
constexpr char kNetworkCachingOption[] = ":network-caching=1000";
constexpr char kLiveCachingOption[] = ":live-caching=1000";

constexpr std::array<libvlc_event_type_t, 11> kPlayerEvents{
    libvlc_MediaPlayerNothingSpecial, libvlc_MediaPlayerOpening,
    libvlc_MediaPlayerBuffering,      libvlc_MediaPlayerPlaying,
    libvlc_MediaPlayerPaused,         libvlc_MediaPlayerStopped,
    libvlc_MediaPlayerEndReached,     libvlc_MediaPlayerEncounteredError,
    libvlc_MediaPlayerTimeChanged,    libvlc_MediaPlayerSeekableChanged,
    libvlc_MediaPlayerLengthChanged,
};

std::vector<const char*> initializationArguments(
    const VlcPlayerEngineOptions options) {
  std::vector<const char*> arguments{
      "--ignore-config",
      "--no-video-title-show",
      "--quiet",
      "--audio-time-stretch",
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
  if (options.instanceCreatedObserver) {
    try {
      options.instanceCreatedObserver();
    } catch (...) {
      // 测试观察器不得影响真实播放内核初始化。
    }
  }
  return instance;
}

void applyVideoSurface(
    libvlc_media_player_t* const player, void* const nativeHandle,
    const std::function<void(void*)>& videoSurfaceObserver) noexcept {
  libvlc_media_player_set_hwnd(player, nativeHandle);
  if (videoSurfaceObserver) {
    try {
      videoSurfaceObserver(nativeHandle);
    } catch (...) {
      // 测试观察器不得影响真实播放控制路径。
    }
  }
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

std::uint64_t nonnegativeCounter(const int value) noexcept {
  return static_cast<std::uint64_t>(std::max(value, 0));
}

core::NetworkStreamActivity networkActivityFromLibVlc(
    const libvlc_media_stats_t& stats) noexcept {
  return {
      nonnegativeCounter(stats.i_read_bytes),
      nonnegativeCounter(stats.i_demux_read_bytes),
      nonnegativeCounter(stats.i_decoded_video),
      nonnegativeCounter(stats.i_decoded_audio),
      nonnegativeCounter(stats.i_displayed_pictures),
      nonnegativeCounter(stats.i_played_abuffers),
  };
}

core::AudioWaveform audioWaveformFromPcm(const std::int16_t* const samples,
                                         const std::size_t count) noexcept {
  core::AudioWaveform waveform;
  if (samples == nullptr || count == 0) {
    return waveform;
  }

  for (std::size_t index = 0; index < waveform.samples.size(); ++index) {
    const std::size_t begin = index * count / waveform.samples.size();
    const std::size_t calculatedEnd =
        (index + 1) * count / waveform.samples.size();
    const std::size_t end = std::min(count, std::max(begin + 1, calculatedEnd));
    std::int16_t signedPeak = samples[begin];
    int peakMagnitude = std::abs(static_cast<int>(signedPeak));
    for (std::size_t sampleIndex = begin + 1; sampleIndex < end;
         ++sampleIndex) {
      const int magnitude = std::abs(static_cast<int>(samples[sampleIndex]));
      if (magnitude > peakMagnitude) {
        signedPeak = samples[sampleIndex];
        peakMagnitude = magnitude;
      }
    }
    waveform.samples[index] =
        static_cast<float>(signedPeak) / static_cast<float>(1U << 15U);
  }
  return waveform;
}

float playbackRateForLibVlc(const double rate, const bool keepsUnityWarm) {
  const float boundedRate = static_cast<float>(std::clamp(rate, 0.25, 4.0));
  if (std::abs(boundedRate - 1.0F) >= 0.0005F) {
    return boundedRate;
  }
  return keepsUnityWarm ? kWarmUnityPlaybackRate : 1.0F;
}

std::string displayNameFor(const core::MediaItem& item) {
  if (!item.displayName.empty()) {
    return item.displayName;
  }

  const std::string_view source = item.source;
  const auto separator = source.find_last_of("/\\");
  const auto name =
      source.substr(separator == std::string_view::npos ? 0 : separator + 1);
  return name.empty() ? std::string("所选媒体") : std::string(name);
}

std::filesystem::path pathFromUtf8(const std::string_view source) {
  const auto* const begin = reinterpret_cast<const char8_t*>(source.data());
  return std::filesystem::path(std::u8string(begin, begin + source.size()));
}

std::string preferredPathToUtf8(std::filesystem::path path) {
  path.make_preferred();
  const auto utf8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

bool isAudioOnlyMedia(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const char value) {
                   return static_cast<char>(
                       std::tolower(static_cast<unsigned char>(value)));
                 });
  constexpr std::array<std::string_view, 8> kAudioExtensions{
      ".aac", ".flac", ".m4a", ".mp3", ".ogg", ".opus", ".wav", ".wma",
  };
  return std::find(kAudioExtensions.begin(), kAudioExtensions.end(),
                   extension) != kAudioExtensions.end();
}

bool hasObviouslyInvalidMp4Content(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const char value) {
                   return static_cast<char>(
                       std::tolower(static_cast<unsigned char>(value)));
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
  auto contentBegin = header.begin();
  const auto end = contentBegin + static_cast<std::ptrdiff_t>(bytesRead);
  if (bytesRead >= 3 && static_cast<unsigned char>(header[0]) == 0xEFU &&
      static_cast<unsigned char>(header[1]) == 0xBBU &&
      static_cast<unsigned char>(header[2]) == 0xBFU) {
    contentBegin += 3;
  }

  // 纯文本即使带 UTF-8 BOM，也不能因为扩展名是 .mp4 就交给 libVLC 播放。
  return contentBegin != end &&
         std::all_of(contentBegin, end, [](const unsigned char value) {
           return std::isprint(value) != 0 || std::isspace(value) != 0;
         });
}

// 监听器状态由共享对象持有，使脱离主控制线程的回收任务也能安全投递完成事件。
class EventDispatcher final {
 public:
  // 调用线程：应用控制线程。切换监听器前等待任意内核线程上的旧回调退出。
  void setListener(core::PlayerEventListener* const listener) noexcept {
    std::unique_lock lock(mutex_);
    listener_ = nullptr;
    callbacksDrained_.wait(lock, [this] { return activeCallbacks_ == 0; });
    if (isActive_) {
      listener_ = listener;
    }
  }

  // 调用线程：引擎析构线程。停用后等待已经取得监听器的回调退出。
  void deactivate() noexcept {
    std::unique_lock lock(mutex_);
    isActive_ = false;
    listener_ = nullptr;
    callbacksDrained_.wait(lock, [this] { return activeCallbacks_ == 0; });
  }

  template <typename Callback>
  // 调用线程：任意内核线程或旧播放器回收线程。监听器只在回调期间受保护。
  void dispatch(Callback&& callback) noexcept {
    core::PlayerEventListener* listener = nullptr;
    {
      const std::lock_guard lock(mutex_);
      if (!isActive_ || listener_ == nullptr) {
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

 private:
  std::mutex mutex_;
  std::condition_variable callbacksDrained_;
  core::PlayerEventListener* listener_{nullptr};
  std::size_t activeCallbacks_{0};
  bool isActive_{true};
};

// 回收线程可能永久停在第三方调用中；计数器为正常切换设置硬上限并预留一次 Stop。
class RetirementLimiter final {
 public:
  // 调用线程：唯一内核控制线程。成功后必须由回收线程在退出时归还名额。
  [[nodiscard]] bool tryAcquire(const std::size_t limit) noexcept {
    auto active = active_.load(std::memory_order_relaxed);
    while (active < limit) {
      if (active_.compare_exchange_weak(active, active + 1,
                                        std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
        return true;
      }
    }
    return false;
  }

  // 调用线程：旧播放器回收线程。只在第三方停止和释放全部完成后归还名额。
  void release() noexcept {
    active_.fetch_sub(1, std::memory_order_acq_rel);
  }

 private:
  std::atomic<std::size_t> active_{0};
};

}  // namespace

class VlcPlayerEngine::Impl {
 public:
  explicit Impl(const VlcPlayerEngineOptions options)
      : options_(options),
        instance_(createInstance(options)),
        analysisInstance_(createInstance(options)),
        player_(libvlc_media_player_new(instance_.get())),
        analysisPlayer_(libvlc_media_player_new(analysisInstance_.get())) {
    if (!player_ || !analysisPlayer_) {
      throw std::runtime_error("无法创建 libVLC 播放器或音频分析器对象。");
    }
    libvlc_audio_set_callbacks(analysisPlayer_.get(), &Impl::audioPlayCallback,
                               nullptr, nullptr, nullptr, nullptr, this);
    libvlc_audio_set_format(analysisPlayer_.get(), "S16N", kWaveformSampleRate,
                            1);
    attachEvents();
    try {
      commandWorker_ = std::thread(&Impl::runCommandWorker, this);
    } catch (...) {
      detachEvents();
      throw;
    }
  }

  ~Impl() { shutdown(); }

  // libVLC 3 在断流时的 stop/set_media 可能等待网络读取，所有控制命令
  // 串行放到工作线程，让 GUI 调用立即返回。
  core::OpenRequestId open(core::MediaItem item,
                           void* const nativeVideoHandle) {
    const auto requestId = latestOpenRequestId_.fetch_add(1) + 1;
    enqueueCommand([this, item = std::move(item), requestId,
                    nativeVideoHandle]() mutable {
      openNow(std::move(item), requestId, nativeVideoHandle);
    });
    return requestId;
  }

  void play() {
    enqueueCommand([this] { playNow(); });
  }

  void pause() {
    enqueueCommand([this] { pauseNow(); });
  }

  void stop() {
    enqueueCommand([this] { stopNow(); });
  }

  void seek(const std::chrono::milliseconds position) {
    enqueueCommand([this, position] { seekNow(position); });
  }

  void setVolume(const int volume) {
    enqueueCommand([this, volume] { setVolumeNow(volume); });
  }

  void setMuted(const bool isMuted) {
    enqueueCommand([this, isMuted] { setMutedNow(isMuted); });
  }

  void setPlaybackRate(const double rate) {
    enqueueCommand([this, rate] { setPlaybackRateNow(rate); });
  }

  void setVideoSurface(void* const nativeHandle) {
    enqueueCommand([this, nativeHandle] { setVideoSurfaceNow(nativeHandle); });
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

  std::optional<core::NetworkStreamActivity> networkStreamActivity() const {
    const std::lock_guard lock(mutex_);
    return networkStreamActivity_;
  }

  void setEventListener(core::PlayerEventListener* const listener) {
    {
      const std::lock_guard lock(mutex_);
      if (isShuttingDown_) {
        eventDispatcher_->setListener(nullptr);
        return;
      }
    }
    eventDispatcher_->setListener(listener);
  }

 private:
  // 调用线程：唯一内核控制线程。被取代且从未使用的请求句柄会立即归还 GUI。
  void openNow(core::MediaItem item, const core::OpenRequestId requestId,
               void* const nativeVideoHandle) {
    if (requestId != latestOpenRequestId_.load()) {
      if (nativeVideoHandle != nullptr &&
          nativeVideoHandle != playerVideoSurface_) {
        dispatch([nativeVideoHandle](
                     core::PlayerEventListener& listener) noexcept {
          listener.onVideoSurfaceReleased(nativeVideoHandle);
        });
      }
      return;
    }
    void* const previousVideoSurface = videoSurface_;
    if (nativeVideoHandle != nullptr) {
      videoSurface_ = nativeVideoHandle;
    }
    if (!clearCurrentMedia(displayNameFor(item), item.kind)) {
      videoSurface_ = previousVideoSurface;
      if (nativeVideoHandle != nullptr &&
          nativeVideoHandle != playerVideoSurface_) {
        dispatch([nativeVideoHandle](
                     core::PlayerEventListener& listener) noexcept {
          listener.onVideoSurfaceReleased(nativeVideoHandle);
        });
      }
      reportError(core::PlaybackErrorKind::EngineBusy,
                  "Retired player capacity exhausted",
                  "多个旧直播仍在退出，请稍候后重试。");
      return;
    }
    if (playerVideoSurface_ != videoSurface_) {
      void* const releasedSurface = playerVideoSurface_;
      applyVideoSurface(player_.get(), videoSurface_,
                        options_.videoSurfaceObserver);
      playerVideoSurface_ = videoSurface_;
      if (releasedSurface != nullptr) {
        dispatch([releasedSurface](
                     core::PlayerEventListener& listener) noexcept {
          listener.onVideoSurfaceReleased(releasedSurface);
        });
      }
    }
    // 旧直播停止期间可能收到更多选择请求，只打开最后一次选择的媒体。
    if (requestId != latestOpenRequestId_.load()) {
      return;
    }
    if (item.kind == core::MediaSourceKind::NetworkStream) {
      if (core::validateNetworkUrl(item.source) !=
          core::NetworkUrlValidationError::None) {
        reportError(core::PlaybackErrorKind::UnsupportedFormat,
                    "Network input failed URL validation",
                    "网络地址格式无效或协议暂不受支持。");
        return;
      }

      VlcMediaPtr newMedia(
          libvlc_media_new_location(instance_.get(), item.source.c_str()));
      if (!newMedia) {
        reportError(core::PlaybackErrorKind::SourceUnreadable,
                    "libVLC could not create a network media descriptor",
                    "无法打开网络媒体“" + displayNameFor(item) + "”。");
        return;
      }

      // 网络连接由 libVLC 播放线程执行；这里仅集中配置直播缓存并提交媒体描述。
      libvlc_media_add_option(newMedia.get(), kNetworkCachingOption);
      libvlc_media_add_option(newMedia.get(), kLiveCachingOption);
      libvlc_media_player_set_media(player_.get(), newMedia.get());
      libvlc_media_player_set_media(analysisPlayer_.get(), nullptr);
      static_cast<void>(libvlc_media_player_set_rate(player_.get(), 1.0F));
      media_ = std::move(newMedia);
      {
        const std::lock_guard lock(mutex_);
        isAudioOnlyMedia_ = false;
        isNetworkMedia_ = true;
        currentDisplayName_ = displayNameFor(item);
      }
      dispatch([requestId](core::PlayerEventListener& listener) noexcept {
        listener.onOpenStarted(requestId);
      });
      updateState(core::PlaybackState::Opening);
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
    const bool isRegularFile =
        std::filesystem::is_regular_file(path, pathError);
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

    // Qt 在 Windows 上返回正斜杠路径，而 libVLC 3
    // 的本地路径入口要求原生分隔符。
    const std::string nativeSource = preferredPathToUtf8(path);
    const bool audioOnlyMedia = isAudioOnlyMedia(path);
    VlcMediaPtr newMedia(
        libvlc_media_new_path(instance_.get(), nativeSource.c_str()));
    if (!newMedia) {
      reportError(core::PlaybackErrorKind::SourceUnreadable,
                  "libVLC could not create a media descriptor",
                  "无法打开媒体文件“" + displayNameFor(item) + "”。");
      return;
    }
    VlcMediaPtr newAnalysisMedia;
    if (audioOnlyMedia) {
      newAnalysisMedia.reset(
          libvlc_media_new_path(analysisInstance_.get(), nativeSource.c_str()));
      if (!newAnalysisMedia) {
        reportError(core::PlaybackErrorKind::SourceUnreadable,
                    "libVLC could not create an audio analysis descriptor",
                    "无法分析媒体文件“" + displayNameFor(item) + "”。");
        return;
      }
    }

    if (!audioOnlyMedia) {
      // 缩短本地视频的预解码时钟跨度，避免改倍率时清空约一秒的旧倍率队列。
      libvlc_media_add_option(newMedia.get(), kVideoFileCachingOption);
    }
    libvlc_media_player_set_media(player_.get(), newMedia.get());
    libvlc_media_player_set_media(analysisPlayer_.get(),
                                  newAnalysisMedia.get());
    // VLC 在精确 1.0 倍时绕过 scaletempo；最小偏移让滤镜从首帧起保持热状态。
    static_cast<void>(
        libvlc_media_player_set_rate(player_.get(), kWarmUnityPlaybackRate));
    if (audioOnlyMedia) {
      static_cast<void>(libvlc_media_player_set_rate(analysisPlayer_.get(),
                                                     kWarmUnityPlaybackRate));
    }
    media_ = std::move(newMedia);
    analysisMedia_ = std::move(newAnalysisMedia);
    {
      const std::lock_guard lock(mutex_);
      isAudioOnlyMedia_ = audioOnlyMedia;
      isNetworkMedia_ = false;
      currentDisplayName_ = displayNameFor(item);
    }
    dispatch([requestId](core::PlayerEventListener& listener) noexcept {
      listener.onOpenStarted(requestId);
    });
    updateState(core::PlaybackState::Opening);
  }

  void playNow() {
    if (!media_) {
      reportError(core::PlaybackErrorKind::EngineNotInitialized,
                  "Play was requested without loaded media",
                  "请先打开一个媒体文件再开始播放。");
      return;
    }
    if (libvlc_media_player_play(player_.get()) != 0) {
      reportPlaybackFailure("libVLC rejected the play request",
                            core::PlaybackErrorKind::Unknown);
      return;
    }
    if (isAudioOnlyMedia_) {
      const auto analysisState =
          libvlc_media_player_get_state(analysisPlayer_.get());
      if (isTerminalState(analysisState)) {
        const libvlc_time_t currentTime =
            libvlc_media_player_get_time(player_.get());
        if (currentTime >= 0) {
          libvlc_media_player_set_time(analysisPlayer_.get(), currentTime);
        }
      }
      static_cast<void>(libvlc_media_player_play(analysisPlayer_.get()));
    }
  }

  void pauseNow() {
    if (media_) {
      libvlc_media_player_set_pause(player_.get(), 1);
      if (isAudioOnlyMedia_) {
        libvlc_media_player_set_pause(analysisPlayer_.get(), 1);
      }
    }
  }

  void stopNow() {
    if (!media_) {
      return;
    }

    bool isNetworkMedia = false;
    {
      const std::lock_guard lock(mutex_);
      isNetworkMedia = isNetworkMedia_;
    }
    if (isNetworkMedia) {
      if (!replacePrimaryPlayer(true, true, false,
                                kMaxConcurrentRetirements)) {
        reportError(core::PlaybackErrorKind::EngineBusy,
                    "Retired player capacity exhausted during stop",
                    "多个旧直播仍在退出，暂时无法停止当前直播。");
        return;
      }
      {
        const std::lock_guard lock(mutex_);
        position_ = {};
        lastPositionNotification_ = {};
        networkStreamActivity_.reset();
      }
      updateState(core::PlaybackState::Stopped);
    } else {
      libvlc_media_player_stop(player_.get());
      stopAudioAnalysis();
    }
    resetWaveform();
  }

  void seekNow(std::chrono::milliseconds position) {
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
    if (isAudioOnlyMedia_) {
      libvlc_media_player_set_time(analysisPlayer_.get(), position.count());
      resetWaveform();
    }
    updatePosition(position, true);
  }

  void setVolumeNow(const int volume) {
    const int boundedVolume = std::clamp(volume, 0, 100);
    volume_ = boundedVolume;
    static_cast<void>(libvlc_audio_set_volume(player_.get(), boundedVolume));
  }

  void setMutedNow(const bool isMuted) {
    isMuted_ = isMuted;
    libvlc_audio_set_mute(player_.get(), isMuted ? 1 : 0);
  }

  void reapplyAudioStateAfterPlaying() noexcept {
    try {
      enqueueCommand([this] {
        static_cast<void>(libvlc_audio_set_volume(player_.get(), volume_));
        libvlc_audio_set_mute(player_.get(), isMuted_ ? 1 : 0);
      });
    } catch (...) {
    }
  }

  void setPlaybackRateNow(const double rate) {
    if (!media_ || !std::isfinite(rate) || rate <= 0.0) {
      return;
    }

    bool isAudioOnlyMedia = false;
    bool isNetworkMedia = false;
    {
      const std::lock_guard lock(mutex_);
      isAudioOnlyMedia = isAudioOnlyMedia_;
      isNetworkMedia = isNetworkMedia_;
    }
    if (!isAudioOnlyMedia) {
      static_cast<void>(libvlc_media_player_set_rate(
          player_.get(), playbackRateForLibVlc(rate, !isNetworkMedia)));
      return;
    }

    const libvlc_time_t currentTime =
        libvlc_media_player_get_time(player_.get());
    const libvlc_state_t currentState =
        libvlc_media_player_get_state(player_.get());
    const int result = libvlc_media_player_set_rate(
        player_.get(), playbackRateForLibVlc(rate, true));
    static_cast<void>(libvlc_media_player_set_rate(
        analysisPlayer_.get(), playbackRateForLibVlc(rate, true)));
    if (result == 0 && currentTime >= 0 &&
        (currentState == libvlc_Playing || currentState == libvlc_Paused)) {
      // VLC 3 改倍率后会清空旧倍率的排队音频并补静音。
      // 纯音频可在当前位置重建时钟；视频这样做会跳到附近关键帧。
      libvlc_media_player_set_time(player_.get(), currentTime);
      libvlc_media_player_set_time(analysisPlayer_.get(), currentTime);
      resetWaveform();
    }
  }

  void setVideoSurfaceNow(void* const nativeHandle) {
    videoSurface_ = nativeHandle;
    applyVideoSurface(player_.get(), nativeHandle,
                      options_.videoSurfaceObserver);
    playerVideoSurface_ = nativeHandle;
  }

  void enqueueCommand(std::function<void()> command) {
    {
      const std::lock_guard lock(commandMutex_);
      if (commandWorkerStopping_) {
        return;
      }
      commands_.push_back(std::move(command));
    }
    commandReady_.notify_one();
  }

  void runCommandWorker() noexcept {
    for (;;) {
      std::function<void()> command;
      bool shouldRefreshNetworkActivity = false;
      {
        std::unique_lock lock(commandMutex_);
        const bool commandReady = commandReady_.wait_for(
            lock, kNetworkActivityPollInterval,
            [this] { return commandWorkerStopping_ || !commands_.empty(); });
        shouldRefreshNetworkActivity = !commandReady;
        if (commands_.empty()) {
          if (commandWorkerStopping_) {
            return;
          }
        } else {
          command = std::move(commands_.front());
          commands_.pop_front();
        }
      }

      if (shouldRefreshNetworkActivity) {
        refreshNetworkStreamActivityNow();
        continue;
      }
      if (!command) {
        continue;
      }
      try {
        command();
      } catch (const std::exception& error) {
        try {
          reportError(core::PlaybackErrorKind::Unknown,
                      std::string("Player command failed: ") + error.what(),
                      "播放内核执行操作失败，请重试。");
        } catch (...) {
        }
      } catch (...) {
        try {
          reportError(core::PlaybackErrorKind::Unknown,
                      "Player command failed with an unknown exception",
                      "播放内核执行操作失败，请重试。");
        } catch (...) {
        }
      }
    }
  }

  void refreshNetworkStreamActivityNow() noexcept {
    bool isNetworkMedia = false;
    {
      const std::lock_guard lock(mutex_);
      isNetworkMedia = isNetworkMedia_;
    }
    if (!isNetworkMedia || !media_) {
      return;
    }

    libvlc_media_stats_t stats{};
    if (libvlc_media_get_stats(media_.get(), &stats) == 0) {
      return;
    }

    const auto activity = networkActivityFromLibVlc(stats);
    const std::lock_guard lock(mutex_);
    if (isNetworkMedia_) {
      networkStreamActivity_ = activity;
    }
  }

  void stopCommandWorker() noexcept {
    {
      const std::lock_guard lock(commandMutex_);
      commandWorkerStopping_ = true;
    }
    commandReady_.notify_one();
    if (commandWorker_.joinable()) {
      commandWorker_.join();
    }
  }

  template <typename Callback>
  void dispatch(Callback&& callback) noexcept {
    eventDispatcher_->dispatch(std::forward<Callback>(callback));
  }

  void attachEvents() {
    eventManager_ = libvlc_media_player_event_manager(player_.get());
    for (const auto eventType : kPlayerEvents) {
      if (libvlc_event_attach(eventManager_, eventType, &Impl::libVlcCallback,
                              this) != 0) {
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
      libvlc_event_detach(eventManager_, kPlayerEvents[index],
                          &Impl::libVlcCallback, this);
    }
    attachedEventCount_ = 0;
    eventManager_ = nullptr;
  }

  // 调用线程：libVLC 事件线程，禁止在此操作控件或调用停止与释放。
  static void libVlcCallback(const libvlc_event_t* const event,
                             void* const opaque) noexcept {
    if (event != nullptr && opaque != nullptr) {
      auto& implementation = *static_cast<Impl*>(opaque);
      implementation.beginLibVlcCallback();
      implementation.handleEvent(*event);
      implementation.finishLibVlcCallback();
    }
  }

  // 调用线程：辅助 libVLC 音频线程。只压缩 PCM 并投递固定大小的值快照。
  static void audioPlayCallback(void* const opaque, const void* const samples,
                                const unsigned count,
                                const std::int64_t pts) noexcept {
    static_cast<void>(pts);
    if (opaque == nullptr) {
      return;
    }
    auto& implementation = *static_cast<Impl*>(opaque);
    implementation.beginAudioCallback();
    implementation.handleAudioSamples(static_cast<const std::int16_t*>(samples),
                                      count);
    implementation.finishAudioCallback();
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

  void beginAudioCallback() noexcept {
    const std::lock_guard lock(mutex_);
    ++activeAudioCallbacks_;
  }

  void finishAudioCallback() noexcept {
    const std::lock_guard lock(mutex_);
    --activeAudioCallbacks_;
    if (activeAudioCallbacks_ == 0) {
      callbacksDrained_.notify_all();
    }
  }

  void handleAudioSamples(const std::int16_t* const samples,
                          const std::size_t count) noexcept {
    const auto now = std::chrono::steady_clock::now();
    {
      const std::lock_guard lock(mutex_);
      if (isShuttingDown_ || !isAudioOnlyMedia_) {
        return;
      }
    }

    const float intensity = audioEnergyIntensity(samples, count);
    {
      const std::lock_guard lock(mutex_);
      if (isShuttingDown_ || !isAudioOnlyMedia_ ||
          (lastWaveformNotification_ !=
               std::chrono::steady_clock::time_point{} &&
           now - lastWaveformNotification_ < kWaveformEventInterval)) {
        return;
      }
      lastWaveformNotification_ = now;
    }

    auto waveform = audioWaveformFromPcm(samples, count);
    waveform.intensity = intensity;
    dispatch([waveform](core::PlayerEventListener& listener) mutable noexcept {
      listener.onAudioWaveformChanged(std::move(waveform));
    });
  }

  float audioEnergyIntensity(const std::int16_t* const samples,
                             const std::size_t count) noexcept {
    if (samples == nullptr || count == 0) {
      return 0.0F;
    }

    const std::lock_guard lock(waveformAnalysisMutex_);
    double fullBandSquares = 0.0;
    double bassSquares = 0.0;
    float lowPass = bassLowPassState_;
    constexpr float kBassLowPassCoefficient = 0.0233F;
    for (std::size_t index = 0; index < count; ++index) {
      const float sample =
          static_cast<float>(samples[index]) / static_cast<float>(1U << 15U);
      lowPass += kBassLowPassCoefficient * (sample - lowPass);
      fullBandSquares += static_cast<double>(sample) * sample;
      bassSquares += static_cast<double>(lowPass) * lowPass;
    }
    bassLowPassState_ = lowPass;

    const float fullBandRms =
        static_cast<float>(std::sqrt(fullBandSquares / count));
    const float bassRms = static_cast<float>(std::sqrt(bassSquares / count));
    bassAverage_ += 0.018F * (bassRms - bassAverage_);
    const float bassTransient = std::max(0.0F, bassRms - bassAverage_);
    const float target =
        std::clamp(fullBandRms * 1.65F + bassRms * 1.8F + bassTransient * 7.5F,
                   0.0F, 1.0F);

    return target;
  }

  // 调用线程：libVLC 事件线程，只更新快照并向监听器发送值类型事件。
  void handleEvent(const libvlc_event_t& event) noexcept {
    if (const auto playbackEvent = playbackEventFromLibVlc(event.type)) {
      if (*playbackEvent == VlcPlaybackEvent::Buffering) {
        const float cachePercentage = event.u.media_player_buffering.new_cache;
        const int percentage = bufferingPercentage(cachePercentage);
        dispatch([percentage](core::PlayerEventListener& listener) noexcept {
          listener.onBufferingChanged(percentage);
        });
        if (!isBufferingInProgress(cachePercentage, state())) {
          return;
        }
      }
      const auto nextState = mapPlaybackState(*playbackEvent);
      if (*playbackEvent == VlcPlaybackEvent::Stopped) {
        updatePosition(0ms, true);
      } else if (*playbackEvent == VlcPlaybackEvent::EndReached) {
        movePositionToEnd();
      }
      updateState(nextState);
      if (*playbackEvent == VlcPlaybackEvent::Playing) {
        reapplyAudioStateAfterPlaying();
      }

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
        updatePosition(std::chrono::milliseconds(std::max<libvlc_time_t>(
                           event.u.media_player_time_changed.new_time, 0)),
                       false);
        break;
      case libvlc_MediaPlayerSeekableChanged:
        updateSeekable(event.u.media_player_seekable_changed.new_seekable != 0);
        break;
      case libvlc_MediaPlayerLengthChanged:
        updateDuration(
            durationFromLibVlc(event.u.media_player_length_changed.new_length));
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

  void updatePosition(const std::chrono::milliseconds current,
                      const bool force) noexcept {
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

  void reportPlaybackFailure(const std::string detail,
                             const core::PlaybackErrorKind kind =
                                 core::PlaybackErrorKind::UnsupportedFormat,
                             const bool updateFailedState = true) {
    std::string displayName;
    bool isNetworkMedia = false;
    {
      const std::lock_guard lock(mutex_);
      displayName =
          currentDisplayName_.empty() ? "所选媒体" : currentDisplayName_;
      isNetworkMedia = isNetworkMedia_;
    }
    if (updateFailedState) {
      updateState(core::PlaybackState::Failed);
    }
    const auto resolvedKind =
        isNetworkMedia ? core::PlaybackErrorKind::SourceUnreadable : kind;
    const std::string userMessage =
        isNetworkMedia ? "无法连接网络媒体“" + displayName +
                             "”，请检查地址、网络或服务状态。"
                       : "无法播放媒体“" + displayName +
                             "”，文件内容可能损坏或格式不受支持。";
    dispatch([error = core::PlaybackError{resolvedKind, detail, userMessage}](
                 core::PlayerEventListener& listener) mutable noexcept {
      listener.onError(std::move(error));
    });
  }

  void reportError(const core::PlaybackErrorKind kind, std::string detail,
                   std::string userMessage) {
    updateState(core::PlaybackState::Failed);
    dispatch([error = core::PlaybackError{kind, std::move(detail),
                                          std::move(userMessage)}](
                 core::PlayerEventListener& listener) mutable noexcept {
      listener.onError(std::move(error));
    });
  }

  void resetWaveform() noexcept {
    {
      const std::lock_guard lock(waveformAnalysisMutex_);
      bassLowPassState_ = 0.0F;
      bassAverage_ = 0.0F;
    }
    {
      const std::lock_guard lock(mutex_);
      lastWaveformNotification_ = {};
    }
    dispatch([](core::PlayerEventListener& listener) noexcept {
      listener.onAudioWaveformChanged({});
    });
  }

  void stopAudioAnalysis() noexcept {
    if (analysisPlayer_) {
      // Ended 只表示时间线结束，音频输出线程仍可能在执行最后一次
      // drain/cleanup。 始终 Stop，确保 set_media 或 release 前不会再进入 PCM
      // 回调。
      libvlc_media_player_stop(analysisPlayer_.get());
    }
    std::unique_lock lock(mutex_);
    callbacksDrained_.wait(lock, [this] { return activeAudioCallbacks_ == 0; });
  }

  // 调用线程：内核控制线程。不同句柄允许后台回收；同句柄重用时必须等旧 vout 退出。
  void retirePrimaryPlayer(VlcPlayerPtr player,
                           libvlc_instance_t* const owner,
                           void* const retiredSurface,
                           const bool waitsForRetirement,
                           std::function<void()> beforeStop) noexcept {
    if (!player || owner == nullptr) {
      if (!waitsForRetirement) {
        retirementLimiter_->release();
      }
      return;
    }

    const auto stopPlayer = [beforeStop = std::move(beforeStop)](
                                libvlc_media_player_t* const rawPlayer) {
      libvlc_audio_set_mute(rawPlayer, 1);
      static_cast<void>(libvlc_audio_set_volume(rawPlayer, 0));
      static_cast<void>(libvlc_audio_set_track(rawPlayer, -1));
      static_cast<void>(libvlc_video_set_track(rawPlayer, -1));
      if (beforeStop) {
        try {
          beforeStop();
        } catch (...) {
          // 测试观察器不得影响真实播放内核回收。
        }
      }
      libvlc_media_player_stop(rawPlayer);
    };
    if (waitsForRetirement) {
      stopPlayer(player.get());
      player.reset();
      return;
    }

    auto* const rawPlayer = player.release();
    libvlc_retain(owner);
    const auto eventDispatcher = eventDispatcher_;
    const auto retirementLimiter = retirementLimiter_;
    try {
      std::thread([rawPlayer, owner, retiredSurface, stopPlayer,
                   eventDispatcher, retirementLimiter] {
        // 独立句柄让新 vout 可以立即启动；这个线程可被 libVLC 长时间阻塞。
        stopPlayer(rawPlayer);
        libvlc_media_player_release(rawPlayer);
        libvlc_release(owner);
        if (retiredSurface != nullptr) {
          eventDispatcher->dispatch(
              [retiredSurface](core::PlayerEventListener& listener) noexcept {
                listener.onVideoSurfaceReleased(retiredSurface);
              });
        }
        retirementLimiter->release();
      }).detach();
    } catch (...) {
      // 创建线程失败时保留引用和名额；硬上限会阻止继续累积泄漏或阻塞 GUI。
    }
  }

  // 调用线程：唯一内核控制线程。完成播放器交接后可把旧播放器移交回收线程。
  [[nodiscard]] bool replacePrimaryPlayer(
      const bool preserveMedia, const bool reuseInstance,
      const bool waitsForRetirement,
      const std::size_t asyncRetirementLimit) {
    VlcInstancePtr replacementInstance;
    libvlc_instance_t* replacementOwner = instance_.get();
    if (!reuseInstance) {
      replacementInstance = createInstance(options_);
      replacementOwner = replacementInstance.get();
    }

    VlcPlayerPtr replacement(libvlc_media_player_new(replacementOwner));
    if (!replacement) {
      throw std::runtime_error("无法重建 libVLC 播放器。");
    }
    static_cast<void>(libvlc_audio_set_volume(replacement.get(), volume_));
    libvlc_audio_set_mute(replacement.get(), isMuted_ ? 1 : 0);
    if (preserveMedia && media_) {
      libvlc_media_player_set_media(replacement.get(), media_.get());
    }

    std::function<void()> beforeStop = options_.beforeRetiredPlayerStop;
    if (!waitsForRetirement &&
        !retirementLimiter_->tryAcquire(asyncRetirementLimit)) {
      return false;
    }

    detachEvents();
    auto retiredPlayer = std::move(player_);
    auto* retiredOwner = instance_.get();
    const auto retiredSurface = playerVideoSurface_;
    VlcInstancePtr retiredInstance;
    if (!reuseInstance) {
      retiredInstance = std::move(instance_);
      retiredOwner = retiredInstance.get();
      instance_ = std::move(replacementInstance);
    }
    player_ = std::move(replacement);
    applyVideoSurface(player_.get(), videoSurface_,
                      options_.videoSurfaceObserver);
    playerVideoSurface_ = videoSurface_;
    // 停止直播时新播放器继续持有同一句柄，旧播放器退出不代表窗口可销毁。
    void* const releasedSurface =
        retiredSurface == playerVideoSurface_ ? nullptr : retiredSurface;
    try {
      attachEvents();
    } catch (...) {
      retirePrimaryPlayer(std::move(retiredPlayer), retiredOwner,
                          releasedSurface, waitsForRetirement,
                          std::move(beforeStop));
      throw;
    }
    retirePrimaryPlayer(std::move(retiredPlayer), retiredOwner,
                        releasedSurface, waitsForRetirement,
                        std::move(beforeStop));
    return true;
  }

  void stopCurrentMedia() {
    if (media_ &&
        !isTerminalState(libvlc_media_player_get_state(player_.get()))) {
      libvlc_media_player_stop(player_.get());
    }
    stopAudioAnalysis();
  }

  [[nodiscard]] bool clearCurrentMedia(
      std::string displayName,
      const core::MediaSourceKind nextSourceKind) {
    bool wasNetworkMedia = false;
    {
      const std::lock_guard lock(mutex_);
      wasNetworkMedia = isNetworkMedia_;
    }
    if (wasNetworkMedia) {
      if (!replacePrimaryPlayer(
              false, nextSourceKind == core::MediaSourceKind::NetworkStream,
              playerVideoSurface_ == videoSurface_,
              kMaxOpenRetirements)) {
        return false;
      }
      stopAudioAnalysis();
    } else {
      stopCurrentMedia();
    }
    libvlc_media_player_set_media(player_.get(), nullptr);
    libvlc_media_player_set_media(analysisPlayer_.get(), nullptr);
    media_.reset();
    analysisMedia_.reset();
    {
      const std::lock_guard lock(mutex_);
      isAudioOnlyMedia_ = false;
      isNetworkMedia_ = false;
      currentDisplayName_ = std::move(displayName);
      position_ = {};
      networkStreamActivity_.reset();
      lastPositionNotification_ = {};
      lastWaveformNotification_ = {};
    }
    resetWaveform();
    return true;
  }

  void waitUntilPlayerStops(libvlc_media_player_t* const player) noexcept {
    if (player == nullptr) {
      return;
    }
    const auto deadline = std::chrono::steady_clock::now() + kShutdownTimeout;
    while (!isTerminalState(libvlc_media_player_get_state(player)) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(kShutdownPollInterval);
    }
  }

  void shutdown() noexcept {
    {
      const std::lock_guard lock(mutex_);
      isShuttingDown_ = true;
    }
    eventDispatcher_->deactivate();

    // 先等已提交的控制命令退出，再断开事件和释放 libVLC 对象。
    stopCommandWorker();
    detachEvents();
    stopAudioAnalysis();
    {
      std::unique_lock lock(mutex_);
      callbacksDrained_.wait(lock, [this] {
        return activeLibVlcCallbacks_ == 0 && activeAudioCallbacks_ == 0;
      });
    }
    if (player_) {
      if (!isTerminalState(libvlc_media_player_get_state(player_.get()))) {
        libvlc_media_player_stop(player_.get());
      }
      waitUntilPlayerStops(player_.get());
    }
    analysisPlayer_.reset();
    player_.reset();
    analysisMedia_.reset();
    media_.reset();
    analysisInstance_.reset();
    instance_.reset();
  }

  const VlcPlayerEngineOptions options_;
  VlcInstancePtr instance_;
  VlcInstancePtr analysisInstance_;
  VlcMediaPtr media_;
  VlcMediaPtr analysisMedia_;
  VlcPlayerPtr player_;
  VlcPlayerPtr analysisPlayer_;
  libvlc_event_manager_t* eventManager_{nullptr};
  std::size_t attachedEventCount_{0};
  bool isAudioOnlyMedia_{false};
  bool isNetworkMedia_{false};
  int volume_{100};
  bool isMuted_{false};
  void* videoSurface_{nullptr};
  void* playerVideoSurface_{nullptr};
  std::atomic<core::OpenRequestId> latestOpenRequestId_{0};
  std::shared_ptr<EventDispatcher> eventDispatcher_{
      std::make_shared<EventDispatcher>()};
  std::shared_ptr<RetirementLimiter> retirementLimiter_{
      std::make_shared<RetirementLimiter>()};

  std::mutex commandMutex_;
  std::condition_variable commandReady_;
  std::deque<std::function<void()>> commands_;
  std::thread commandWorker_;
  bool commandWorkerStopping_{false};

  mutable std::mutex mutex_;
  std::mutex waveformAnalysisMutex_;
  std::condition_variable callbacksDrained_;
  std::size_t activeLibVlcCallbacks_{0};
  std::size_t activeAudioCallbacks_{0};
  bool isShuttingDown_{false};
  core::PlaybackState state_{core::PlaybackState::Idle};
  core::PlaybackPosition position_;
  std::optional<core::NetworkStreamActivity> networkStreamActivity_;
  std::string currentDisplayName_;
  std::chrono::steady_clock::time_point lastPositionNotification_;
  std::chrono::steady_clock::time_point lastWaveformNotification_;
  float bassLowPassState_{0.0F};
  float bassAverage_{0.0F};
};

VlcPlayerEngine::VlcPlayerEngine(const VlcPlayerEngineOptions options)
    : impl_(std::make_unique<Impl>(options)) {}

VlcPlayerEngine::~VlcPlayerEngine() = default;

core::OpenRequestId VlcPlayerEngine::open(core::MediaItem item,
                                          void* const nativeVideoHandle) {
  return impl_->open(std::move(item), nativeVideoHandle);
}

void VlcPlayerEngine::play() { impl_->play(); }

void VlcPlayerEngine::pause() { impl_->pause(); }

void VlcPlayerEngine::stop() { impl_->stop(); }

void VlcPlayerEngine::seek(const std::chrono::milliseconds position) {
  impl_->seek(position);
}

void VlcPlayerEngine::setVolume(const int volume) { impl_->setVolume(volume); }

void VlcPlayerEngine::setMuted(const bool isMuted) { impl_->setMuted(isMuted); }

void VlcPlayerEngine::setPlaybackRate(const double rate) {
  impl_->setPlaybackRate(rate);
}

void VlcPlayerEngine::setVideoSurface(void* const nativeHandle) {
  impl_->setVideoSurface(nativeHandle);
}

core::PlaybackState VlcPlayerEngine::state() const { return impl_->state(); }

core::PlaybackPosition VlcPlayerEngine::position() const {
  return impl_->position();
}

std::optional<std::chrono::milliseconds> VlcPlayerEngine::duration() const {
  return impl_->duration();
}

bool VlcPlayerEngine::isSeekable() const { return impl_->isSeekable(); }

std::optional<core::NetworkStreamActivity>
VlcPlayerEngine::networkStreamActivity() const {
  return impl_->networkStreamActivity();
}

void VlcPlayerEngine::setEventListener(
    core::PlayerEventListener* const listener) {
  impl_->setEventListener(listener);
}

}  // namespace mediahub::engine_vlc
