#include "mediahub/engine_vlc/vlc_player_engine.h"

#include <vlc/vlc.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
constexpr auto kShutdownTimeout = 2s;
constexpr auto kShutdownPollInterval = 10ms;
constexpr float kWarmUnityPlaybackRate = 1.001F;
constexpr unsigned kWaveformSampleRate = 48'000;
constexpr char kVideoFileCachingOption[] = ":file-caching=30";

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

float playbackRateForLibVlc(const double rate) {
  const float boundedRate = static_cast<float>(std::clamp(rate, 0.25, 4.0));
  return std::abs(boundedRate - 1.0F) < 0.0005F ? kWarmUnityPlaybackRate
                                                : boundedRate;
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

}  // namespace

class VlcPlayerEngine::Impl {
 public:
  explicit Impl(const VlcPlayerEngineOptions options)
      : instance_(createInstance(options)),
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
  }

  ~Impl() { shutdown(); }

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

  void pause() {
    if (media_) {
      libvlc_media_player_set_pause(player_.get(), 1);
      if (isAudioOnlyMedia_) {
        libvlc_media_player_set_pause(analysisPlayer_.get(), 1);
      }
    }
  }

  void stop() {
    if (media_) {
      libvlc_media_player_stop(player_.get());
      stopAudioAnalysis();
      resetWaveform();
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
    if (isAudioOnlyMedia_) {
      libvlc_media_player_set_time(analysisPlayer_.get(), position.count());
      resetWaveform();
    }
    updatePosition(position, true);
  }

  void setVolume(const int volume) {
    const int boundedVolume = std::clamp(volume, 0, 100);
    static_cast<void>(libvlc_audio_set_volume(player_.get(), boundedVolume));
  }

  void setMuted(const bool isMuted) {
    libvlc_audio_set_mute(player_.get(), isMuted ? 1 : 0);
  }

  void setPlaybackRate(const double rate) {
    if (!media_ || !std::isfinite(rate) || rate <= 0.0) {
      return;
    }

    if (!isAudioOnlyMedia_) {
      static_cast<void>(libvlc_media_player_set_rate(
          player_.get(), playbackRateForLibVlc(rate)));
      return;
    }

    const libvlc_time_t currentTime =
        libvlc_media_player_get_time(player_.get());
    const libvlc_state_t currentState =
        libvlc_media_player_get_state(player_.get());
    const int result = libvlc_media_player_set_rate(
        player_.get(), playbackRateForLibVlc(rate));
    static_cast<void>(libvlc_media_player_set_rate(
        analysisPlayer_.get(), playbackRateForLibVlc(rate)));
    if (result == 0 && currentTime >= 0 &&
        (currentState == libvlc_Playing || currentState == libvlc_Paused)) {
      // VLC 3 改倍率后会清空旧倍率的排队音频并补静音。
      // 纯音频可在当前位置重建时钟；视频这样做会跳到附近关键帧。
      libvlc_media_player_set_time(player_.get(), currentTime);
      libvlc_media_player_set_time(analysisPlayer_.get(), currentTime);
      resetWaveform();
    }
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
      if (*playbackEvent == VlcPlaybackEvent::Buffering &&
          !isBufferingInProgress(event.u.media_player_buffering.new_cache,
                                 state())) {
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
    {
      const std::lock_guard lock(mutex_);
      displayName =
          currentDisplayName_.empty() ? "所选媒体" : currentDisplayName_;
    }
    if (updateFailedState) {
      updateState(core::PlaybackState::Failed);
    }
    dispatch(
        [error =
             core::PlaybackError{kind, detail,
                                 "无法播放媒体“" + displayName +
                                     "”，文件内容可能损坏或格式不受支持。"}](
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

  void stopCurrentMedia() {
    if (media_ &&
        !isTerminalState(libvlc_media_player_get_state(player_.get()))) {
      libvlc_media_player_stop(player_.get());
    }
    stopAudioAnalysis();
  }

  void clearCurrentMedia(std::string displayName) {
    stopCurrentMedia();
    libvlc_media_player_set_media(player_.get(), nullptr);
    libvlc_media_player_set_media(analysisPlayer_.get(), nullptr);
    media_.reset();
    analysisMedia_.reset();
    {
      const std::lock_guard lock(mutex_);
      isAudioOnlyMedia_ = false;
      currentDisplayName_ = std::move(displayName);
      position_ = {};
      lastPositionNotification_ = {};
      lastWaveformNotification_ = {};
    }
    resetWaveform();
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
      listener_ = nullptr;
    }

    // 释放顺序固定：先断开事件，再停止并等待，最后依次释放播放器、媒体和实例。
    detachEvents();
    stopAudioAnalysis();
    {
      std::unique_lock lock(mutex_);
      callbacksDrained_.wait(lock, [this] {
        return activeCallbacks_ == 0 && activeLibVlcCallbacks_ == 0 &&
               activeAudioCallbacks_ == 0;
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

  VlcInstancePtr instance_;
  VlcInstancePtr analysisInstance_;
  VlcMediaPtr media_;
  VlcMediaPtr analysisMedia_;
  VlcPlayerPtr player_;
  VlcPlayerPtr analysisPlayer_;
  libvlc_event_manager_t* eventManager_{nullptr};
  std::size_t attachedEventCount_{0};
  bool isAudioOnlyMedia_{false};

  mutable std::mutex mutex_;
  std::mutex waveformAnalysisMutex_;
  std::condition_variable callbacksDrained_;
  core::PlayerEventListener* listener_{nullptr};
  std::size_t activeCallbacks_{0};
  std::size_t activeLibVlcCallbacks_{0};
  std::size_t activeAudioCallbacks_{0};
  bool isShuttingDown_{false};
  core::PlaybackState state_{core::PlaybackState::Idle};
  core::PlaybackPosition position_;
  std::string currentDisplayName_;
  std::chrono::steady_clock::time_point lastPositionNotification_;
  std::chrono::steady_clock::time_point lastWaveformNotification_;
  float bassLowPassState_{0.0F};
  float bassAverage_{0.0F};
};

VlcPlayerEngine::VlcPlayerEngine(const VlcPlayerEngineOptions options)
    : impl_(std::make_unique<Impl>(options)) {}

VlcPlayerEngine::~VlcPlayerEngine() = default;

void VlcPlayerEngine::open(core::MediaItem item) {
  impl_->open(std::move(item));
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

void VlcPlayerEngine::setEventListener(
    core::PlayerEventListener* const listener) {
  impl_->setEventListener(listener);
}

}  // namespace mediahub::engine_vlc
