#include "player_presenter.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "app_state_store.h"
#include "main_window.h"
#include "mediahub/core/media_types.h"

namespace mediahub::gui {
namespace {

constexpr std::array<double, 6> kPlaybackRates{0.5, 0.75, 1.0, 1.5, 2.0, 3.0};
constexpr auto kNetworkOpenTimeout = std::chrono::seconds(15);
constexpr int kMaxSessionNetworkUrls = 10;
constexpr int kMaxLivePlaylistUrlHistory = 20;
constexpr int kMaxFavoriteLiveSources = 5000;
constexpr int kMaxRecentLocalMedia = 20;
constexpr auto kMinimumResumePosition = std::chrono::seconds(10);
constexpr auto kResumeEndGuard = std::chrono::seconds(10);
constexpr auto kAppStatePersistDelay = std::chrono::seconds(5);

bool isPlaylistAddress(const QString &address) {
  const QUrl url(address, QUrl::StrictMode);
  const QString scheme = url.scheme().toLower();
  if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
    return false;
  }
  const QString path = url.path().toLower();
  return path.endsWith(QStringLiteral(".m3u")) ||
         path.endsWith(QStringLiteral(".m3u8"));
}

bool isAmbiguousHlsAddress(const QString &address) {
  return QUrl(address, QUrl::StrictMode)
      .path()
      .endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive);
}

std::optional<double> normalizedPlaybackRate(const double requestedRate) {
  const auto match =
      std::find_if(kPlaybackRates.begin(), kPlaybackRates.end(),
                   [requestedRate](const double supportedRate) {
                     return std::abs(requestedRate - supportedRate) < 0.001;
                   });
  if (match == kPlaybackRates.end()) {
    return std::nullopt;
  }
  return *match;
}

std::string stateName(const core::PlaybackState state) {
  switch (state) {
  case core::PlaybackState::Idle:
    return "idle";
  case core::PlaybackState::Opening:
    return "opening";
  case core::PlaybackState::Buffering:
    return "buffering";
  case core::PlaybackState::Playing:
    return "playing";
  case core::PlaybackState::Paused:
    return "paused";
  case core::PlaybackState::Stopped:
    return "stopped";
  case core::PlaybackState::Ended:
    return "ended";
  case core::PlaybackState::Failed:
    return "failed";
  }
  return "unknown";
}

std::string errorKindName(const core::PlaybackErrorKind kind) {
  switch (kind) {
  case core::PlaybackErrorKind::SourceNotFound:
    return "source_not_found";
  case core::PlaybackErrorKind::SourceUnreadable:
    return "source_unreadable";
  case core::PlaybackErrorKind::UnsupportedFormat:
    return "unsupported_format";
  case core::PlaybackErrorKind::AudioDeviceUnavailable:
    return "audio_device_unavailable";
  case core::PlaybackErrorKind::EngineNotInitialized:
    return "engine_not_initialized";
  case core::PlaybackErrorKind::Unknown:
    return "unknown";
  }
  return "unknown";
}

std::string networkUrlErrorName(const core::NetworkUrlValidationError error) {
  switch (error) {
  case core::NetworkUrlValidationError::None:
    return "none";
  case core::NetworkUrlValidationError::Empty:
    return "empty";
  case core::NetworkUrlValidationError::ContainsWhitespace:
    return "contains_whitespace";
  case core::NetworkUrlValidationError::MissingScheme:
    return "missing_scheme";
  case core::NetworkUrlValidationError::UnsupportedScheme:
    return "unsupported_scheme";
  case core::NetworkUrlValidationError::MissingTarget:
    return "missing_target";
  }
  return "unknown";
}

QString networkUrlErrorMessage(const core::NetworkUrlValidationError error) {
  switch (error) {
  case core::NetworkUrlValidationError::None:
    return {};
  case core::NetworkUrlValidationError::Empty:
    return QStringLiteral("请输入直播地址。");
  case core::NetworkUrlValidationError::ContainsWhitespace:
    return QStringLiteral("直播地址不能包含空格或换行。");
  case core::NetworkUrlValidationError::MissingScheme:
    return QStringLiteral(
        "请输入包含协议的完整地址，例如 https://example.com/live.m3u8。");
  case core::NetworkUrlValidationError::UnsupportedScheme:
    return QStringLiteral(
        "暂不支持该协议。可使用 HTTP、HTTPS、RTSP、RTMP、UDP、RTP 或 SRT。");
  case core::NetworkUrlValidationError::MissingTarget:
    return QStringLiteral("直播地址缺少有效的主机或接收目标。");
  }
  return QStringLiteral("直播地址格式无效。");
}

std::string utf8String(const QString &text) {
  const QByteArray encoded = text.toUtf8();
  return std::string(encoded.constData(),
                     static_cast<std::size_t>(encoded.size()));
}

QString fromUtf8(const std::string &text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

bool isAudioFile(const QString &filePath) {
  const QString suffix = QFileInfo(filePath).suffix().toLower();
  return suffix == QStringLiteral("mp3") || suffix == QStringLiteral("wav") ||
         suffix == QStringLiteral("flac") || suffix == QStringLiteral("aac") ||
         suffix == QStringLiteral("m4a") || suffix == QStringLiteral("ogg") ||
         suffix == QStringLiteral("opus") || suffix == QStringLiteral("wma");
}

QString localPathIdentity(const QString& filePath) {
  return QDir::fromNativeSeparators(QDir::cleanPath(filePath)).toCaseFolded();
}

std::optional<std::chrono::milliseconds> resumePosition(
    const LocalPlaybackRecord& record) {
  if (record.durationMilliseconds <= 0 ||
      record.positionMilliseconds < kMinimumResumePosition.count() ||
      record.durationMilliseconds - record.positionMilliseconds <
          kResumeEndGuard.count()) {
    return std::nullopt;
  }
  return std::chrono::milliseconds(record.positionMilliseconds);
}

QString formatTime(const std::chrono::milliseconds value) {
  const auto totalSeconds =
      std::max<std::chrono::milliseconds::rep>(value.count(), 0) / 1000;
  const auto hours = totalSeconds / 3600;
  const auto minutes = (totalSeconds % 3600) / 60;
  const auto seconds = totalSeconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<qlonglong>(hours))
        .arg(static_cast<qlonglong>(minutes), 2, 10, QLatin1Char('0'))
        .arg(static_cast<qlonglong>(seconds), 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2")
      .arg(static_cast<qlonglong>(minutes), 2, 10, QLatin1Char('0'))
      .arg(static_cast<qlonglong>(seconds), 2, 10, QLatin1Char('0'));
}

std::chrono::milliseconds
boundedPosition(const std::chrono::milliseconds current,
                const std::optional<std::chrono::milliseconds> total) {
  auto bounded = std::max(current, std::chrono::milliseconds::zero());
  if (total.has_value()) {
    bounded =
        std::min(bounded, std::max(*total, std::chrono::milliseconds::zero()));
  }
  return bounded;
}

QString formatPosition(const std::chrono::milliseconds current,
                       const std::optional<std::chrono::milliseconds> total) {
  return QStringLiteral("%1 / %2").arg(
      formatTime(boundedPosition(current, total)),
      total.has_value() ? formatTime(*total) : QStringLiteral("--:--"));
}

int progressValue(const std::chrono::milliseconds current,
                  const std::optional<std::chrono::milliseconds> total) {
  if (!total.has_value() || total->count() <= 0) {
    return 0;
  }
  const long double ratio =
      static_cast<long double>(boundedPosition(current, total).count()) /
      static_cast<long double>(total->count());
  return std::clamp(static_cast<int>(ratio * kProgressMaximum), 0,
                    kProgressMaximum);
}

std::chrono::milliseconds
positionFromProgress(const int value,
                     const std::optional<std::chrono::milliseconds> total) {
  if (!total.has_value() || total->count() <= 0) {
    return std::chrono::milliseconds::zero();
  }
  const int boundedValue = std::clamp(value, 0, kProgressMaximum);
  const long double target = static_cast<long double>(total->count()) *
                             boundedValue / kProgressMaximum;
  return std::chrono::milliseconds(
      static_cast<std::chrono::milliseconds::rep>(target));
}

bool isSeekAvailable(const core::PlaybackPosition &position,
                     const core::PlaybackState state) {
  if (!position.total.has_value() || position.total->count() <= 0) {
    return false;
  }
  if (state == core::PlaybackState::Ended) {
    return true;
  }
  return position.isSeekable && (state == core::PlaybackState::Playing ||
                                 state == core::PlaybackState::Paused);
}

QString livePlaylistErrorMessage(const LivePlaylistLoadError error) {
  switch (error) {
  case LivePlaylistLoadError::InvalidUrl:
    return QStringLiteral("请输入完整的 HTTP 或 HTTPS 清单 URL。");
  case LivePlaylistLoadError::NetworkFailure:
    return QStringLiteral("清单读取失败，请检查网络或服务状态后重试。");
  case LivePlaylistLoadError::Timeout:
    return QStringLiteral("清单读取超过 10 秒，已停止等待。");
  case LivePlaylistLoadError::TooManyRedirects:
    return QStringLiteral("清单重定向次数过多，已停止载入。");
  case LivePlaylistLoadError::UnsafeRedirect:
    return QStringLiteral("清单跳转到不安全地址，已拒绝载入。");
  case LivePlaylistLoadError::ResponseTooLarge:
    return QStringLiteral("清单内容超过 2 MiB，未替换当前直播列表。");
  case LivePlaylistLoadError::InvalidUtf8:
    return QStringLiteral("清单不是有效的 UTF-8 文本。");
  case LivePlaylistLoadError::InvalidFormat:
    return QStringLiteral("链接内容不是有效的 M3U/M3U8 清单。");
  case LivePlaylistLoadError::HlsMediaManifest:
    return QStringLiteral(
        "该链接是单路 HLS 媒体清单，请使用“打开网络地址”播放。");
  case LivePlaylistLoadError::TooManyEntries:
    return QStringLiteral("清单超过 5000 项，未替换当前直播列表。");
  case LivePlaylistLoadError::NoPlayableEntries:
    return QStringLiteral("清单中没有可显示的有效直播条目。");
  }
  return QStringLiteral("清单载入失败，当前直播列表保持不变。");
}

std::string livePlaylistErrorName(const LivePlaylistLoadError error) {
  switch (error) {
  case LivePlaylistLoadError::InvalidUrl:
    return "invalid_url";
  case LivePlaylistLoadError::NetworkFailure:
    return "network_failure";
  case LivePlaylistLoadError::Timeout:
    return "timeout";
  case LivePlaylistLoadError::TooManyRedirects:
    return "too_many_redirects";
  case LivePlaylistLoadError::UnsafeRedirect:
    return "unsafe_redirect";
  case LivePlaylistLoadError::ResponseTooLarge:
    return "response_too_large";
  case LivePlaylistLoadError::InvalidUtf8:
    return "invalid_utf8";
  case LivePlaylistLoadError::InvalidFormat:
    return "invalid_format";
  case LivePlaylistLoadError::HlsMediaManifest:
    return "hls_media_manifest";
  case LivePlaylistLoadError::TooManyEntries:
    return "too_many_entries";
  case LivePlaylistLoadError::NoPlayableEntries:
    return "no_playable_entries";
  }
  return "unknown";
}

} // namespace

PlayerPresenter::PlayerPresenter(core::PlayerEngine &engine,
                                 EngineEventBridge &eventBridge,
                                 MainWindow &window, QObject *const parent,
                                 logging::Logger *const logger,
                                 LyricsService *const lyricsService,
                                 LivePlaylistService *const livePlaylistService,
                                 AppStateStore *const appStateStore)
    : QObject(parent), engine_(engine), eventBridge_(eventBridge),
      window_(window), logger_(logger),
      ownedLyricsService_(lyricsService == nullptr
                              ? std::make_unique<OnlineLyricsService>()
                              : nullptr),
      lyricsService_(lyricsService == nullptr ? ownedLyricsService_.get()
                                              : lyricsService),
      ownedLivePlaylistService_(livePlaylistService == nullptr
                                    ? std::make_unique<LivePlaylistService>()
                                    : nullptr),
      livePlaylistService_(livePlaylistService == nullptr
                               ? ownedLivePlaylistService_.get()
                               : livePlaylistService),
      appStateStore_(appStateStore),
      localPlaylistModel_(localPlaylist_), livePlaylistModel_(livePlaylist_) {
  livePlaylistModel_.setMarkedPredicate(
      [this](const core::MediaItem& item) {
        return markedLiveSources_.contains(item.source);
      });
  livePlaylistModel_.setFavoritePredicate(
      [this](const core::MediaItem& item) {
        return favoriteLiveSources_.contains(item.source);
      });
  networkOpenTimeoutTimer_ = new QTimer(this);
  networkOpenTimeoutTimer_->setObjectName(
      QStringLiteral("networkOpenTimeoutTimer"));
  networkOpenTimeoutTimer_->setSingleShot(true);
  networkOpenTimeoutTimer_->setInterval(
      static_cast<int>(kNetworkOpenTimeout.count() * 1000));
  connect(networkOpenTimeoutTimer_, &QTimer::timeout, this,
          &PlayerPresenter::handleNetworkOpenTimeout);
  appStatePersistTimer_ = new QTimer(this);
  appStatePersistTimer_->setObjectName(
      QStringLiteral("appStatePersistTimer"));
  appStatePersistTimer_->setSingleShot(true);
  appStatePersistTimer_->setInterval(
      static_cast<int>(kAppStatePersistDelay.count() * 1000));
  connect(appStatePersistTimer_, &QTimer::timeout, this, [this] {
    persistAppState();
    updateRecentLocalMediaView();
  });
  restoreAppState();
  window_.setPlaylistModels(&localPlaylistModel_, &livePlaylistModel_);
  window_.setLivePlaylistUrl(lastLivePlaylistUrl_);
  window_.setLivePlaylistHistoryUrls(livePlaylistUrlHistory_);
  window_.setLiveSourceMemos(liveSourceMemos_);
  updateRecentLocalMediaView();
  connect(&window_, &MainWindow::localFilesSelected, this,
          &PlayerPresenter::addLocalFiles);
  connect(&window_, &MainWindow::networkUrlSelected, this,
          &PlayerPresenter::openNetworkUrl);
  connect(&window_, &MainWindow::livePlaylistLoadRequested, this,
          &PlayerPresenter::requestLivePlaylistLoad);
  connect(&window_, &MainWindow::livePlaylistLoadCancelRequested, this,
          &PlayerPresenter::cancelLivePlaylistLoad);
  connect(&window_, &MainWindow::livePlaylistHistoryChanged, this,
          &PlayerPresenter::updateLivePlaylistHistory);
  connect(&window_, &MainWindow::liveSourceMemosChanged, this,
          &PlayerPresenter::updateLiveSourceMemos);
  connect(&window_, &MainWindow::recentLocalMediaSelected, this,
          &PlayerPresenter::openRecentLocalMedia);
  connect(&window_, &MainWindow::recentLocalMediaClearRequested, this,
          &PlayerPresenter::clearRecentLocalMedia);
  connect(&window_, &MainWindow::playlistKindSelected, this,
          &PlayerPresenter::changePlaylistKind);
  connect(&window_, &MainWindow::playRequested, this,
          &PlayerPresenter::requestPlay);
  connect(&window_, &MainWindow::pauseRequested, this,
          &PlayerPresenter::requestPause);
  connect(&window_, &MainWindow::playbackToggleRequested, this,
          &PlayerPresenter::togglePlayback);
  connect(&window_, &MainWindow::stopRequested, this,
          &PlayerPresenter::requestStop);
  connect(&window_, &MainWindow::networkRefreshRequested, this,
          &PlayerPresenter::requestNetworkRefresh);
  connect(&window_, &MainWindow::seekStarted, this,
          &PlayerPresenter::beginSeek);
  connect(&window_, &MainWindow::seekPreviewRequested, this,
          &PlayerPresenter::previewSeek);
  connect(&window_, &MainWindow::seekRequested, this,
          &PlayerPresenter::commitSeek);
  connect(&window_, &MainWindow::volumeRequested, this,
          &PlayerPresenter::requestVolume);
  connect(&window_, &MainWindow::volumeStepRequested, this,
          &PlayerPresenter::requestVolumeStep);
  connect(&window_, &MainWindow::playbackRateRequested, this,
          &PlayerPresenter::requestPlaybackRate, Qt::QueuedConnection);
  connect(&window_, &MainWindow::temporaryFastPlaybackRequested, this,
          &PlayerPresenter::requestTemporaryFastPlayback, Qt::QueuedConnection);
  connect(&window_, &MainWindow::seekRelativeRequested, this,
          &PlayerPresenter::requestRelativeSeek);
  connect(&window_, &MainWindow::muteToggled, this,
          &PlayerPresenter::toggleMuted);
  connect(&window_, &MainWindow::muteStateRequested, this,
          &PlayerPresenter::requestMuted);
  connect(&window_, &MainWindow::lyricsToggled, this,
          &PlayerPresenter::toggleLyrics);
  connect(&window_, &MainWindow::previousRequested, this,
          &PlayerPresenter::requestPrevious);
  connect(&window_, &MainWindow::nextRequested, this,
          &PlayerPresenter::requestNext);
  connect(&window_, &MainWindow::playlistItemActivated, this,
          &PlayerPresenter::activatePlaylistItem);
  connect(&window_, &MainWindow::playlistItemsRemoveRequested, this,
          &PlayerPresenter::removePlaylistItems);
  connect(&window_, &MainWindow::livePlaylistMarkToggled, this,
          &PlayerPresenter::toggleLivePlaylistMark);
  connect(&window_, &MainWindow::livePlaylistFavoriteToggled, this,
          &PlayerPresenter::toggleLivePlaylistFavorite);
  connect(&window_, &MainWindow::playlistItemMoveRequested, this,
          &PlayerPresenter::movePlaylistItem);
  connect(&window_, &MainWindow::playlistItemRenameRequested, this,
          &PlayerPresenter::renamePlaylistItem);
  connect(&window_, &MainWindow::playbackModeRequested, this,
          &PlayerPresenter::changePlaybackMode);
  connect(&window_, &MainWindow::videoSurfaceReady, this,
          &PlayerPresenter::attachVideoSurface);
  connect(&window_, &MainWindow::closing, this, &PlayerPresenter::shutdown);

  // 强制排队可避免同步内核回调在原调用栈中重新进入控制接口。
  connect(&eventBridge_, &EngineEventBridge::stateChanged, this,
          &PlayerPresenter::handleStateChanged, Qt::QueuedConnection);
  connect(&eventBridge_, &EngineEventBridge::positionChanged, this,
          &PlayerPresenter::handlePositionChanged, Qt::QueuedConnection);
  connect(&eventBridge_, &EngineEventBridge::durationChanged, this,
          &PlayerPresenter::handleDurationChanged, Qt::QueuedConnection);
  connect(&eventBridge_, &EngineEventBridge::bufferingChanged, this,
          &PlayerPresenter::handleBufferingChanged, Qt::QueuedConnection);
  connect(&eventBridge_, &EngineEventBridge::audioWaveformChanged, this,
          &PlayerPresenter::handleAudioWaveformChanged, Qt::QueuedConnection);
  connect(&eventBridge_, &EngineEventBridge::endReached, this,
          &PlayerPresenter::handleEndReached, Qt::QueuedConnection);
  connect(&eventBridge_, &EngineEventBridge::errorOccurred, this,
          &PlayerPresenter::handleError, Qt::QueuedConnection);
  connect(lyricsService_, &LyricsService::resultReady, this,
          &PlayerPresenter::handleLyricsResult);
  connect(livePlaylistService_, &LivePlaylistService::loadSucceeded, this,
          &PlayerPresenter::handleLivePlaylistLoaded);
  connect(livePlaylistService_, &LivePlaylistService::loadFailed, this,
          &PlayerPresenter::handleLivePlaylistFailure);

  engine_.setEventListener(&eventBridge_);
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "created");
  }
  render();
}

PlayerPresenter::~PlayerPresenter() { shutdown(); }

void PlayerPresenter::openLocalFile(const QString &filePath) {
  addLocalFiles(QStringList{filePath});
}

void PlayerPresenter::addLocalFiles(const QStringList &filePaths) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || filePaths.isEmpty()) {
    return;
  }

  std::vector<core::MediaItem> items;
  items.reserve(static_cast<std::size_t>(filePaths.size()));
  for (const auto &filePath : filePaths) {
    if (filePath.isEmpty()) {
      continue;
    }
    const QFileInfo fileInfo(filePath);
    const QString displayName =
        fileInfo.fileName().isEmpty() ? filePath : fileInfo.fileName();
    items.push_back(
        core::makeMediaItem(utf8String(filePath), utf8String(displayName)));
  }
  if (items.empty()) {
    return;
  }

  const auto addedCount = items.size();
  const std::size_t firstNewIndex = localPlaylist_.size();
  localPlaylist_.add(std::move(items));
  static_cast<void>(localPlaylist_.select(firstNewIndex));
  activePlaylistKind_ = PlaylistKind::Local;
  localPlaylistModel_.refresh();
  persistAppState();
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "media_batch_added",
                 {{"count", std::to_string(addedCount)}});
  }
  openCurrentPlaylistItem(PlaylistKind::Local);
}

void PlayerPresenter::openNetworkUrl(const QString &url) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }

  const QString normalizedUrl = url.trimmed();
  const std::string source = utf8String(normalizedUrl);
  const auto validationError = core::validateNetworkUrl(source);
  if (validationError != core::NetworkUrlValidationError::None) {
    window_.showPlaybackError(networkUrlErrorMessage(validationError));
    if (logger_ != nullptr) {
      logger_->log(logging::LogLevel::Warning, "presenter",
                   "network_url_rejected",
                   {{"reason", networkUrlErrorName(validationError)}});
    }
    return;
  }

  rememberNetworkUrl(normalizedUrl);
  if (isPlaylistAddress(normalizedUrl)) {
    window_.setLivePlaylistUrl(normalizedUrl);
    startLivePlaylistLoad(normalizedUrl, isAmbiguousHlsAddress(normalizedUrl));
    return;
  }

  if (isLivePlaylistLoading_) {
    livePlaylistService_->cancel();
    isLivePlaylistLoading_ = false;
  }
  pendingPlaylistProbeUrl_.clear();
  openDirectNetworkUrl(normalizedUrl);
}

void PlayerPresenter::openDirectNetworkUrl(const QString &normalizedUrl) {
  const std::string source = utf8String(normalizedUrl);
  core::MediaItem item = core::makeMediaItem(source);
  const std::string displayName = item.displayName;
  const std::size_t newIndex = livePlaylist_.size();
  livePlaylist_.add(std::move(item));
  static_cast<void>(livePlaylist_.select(newIndex));
  activePlaylistKind_ = PlaylistKind::Live;
  livePlaylistModel_.refresh();
  livePlaylistStatusText_ =
      QStringLiteral("直播列表共 %1 项")
          .arg(static_cast<qulonglong>(livePlaylist_.size()));
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "network_media_added",
                 {{"media", displayName}});
  }
  openCurrentPlaylistItem(PlaylistKind::Live);
}

void PlayerPresenter::requestLivePlaylistLoad(const QString &playlistUrl) {
  startLivePlaylistLoad(playlistUrl, false);
}

void PlayerPresenter::cancelLivePlaylistLoad() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || !isLivePlaylistLoading_) {
    return;
  }
  livePlaylistService_->cancel();
  isLivePlaylistLoading_ = false;
  pendingPlaylistProbeUrl_.clear();
  activeLivePlaylistRequestUrl_.clear();
  livePlaylistStatusText_ = QStringLiteral("已取消载入直播清单");
  render();
}

void PlayerPresenter::startLivePlaylistLoad(
    const QString &playlistUrl, const bool fallsBackToDirectPlayback) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }
  const QString normalizedUrl = playlistUrl.trimmed();
  activePlaylistKind_ = PlaylistKind::Live;
  pendingPlaylistProbeUrl_ =
      fallsBackToDirectPlayback ? normalizedUrl : QString{};
  activeLivePlaylistRequestUrl_ = normalizedUrl;
  lastLivePlaylistUrl_ = normalizedUrl;
  persistAppState();
  isLivePlaylistLoading_ = true;
  livePlaylistStatusText_ = QStringLiteral("正在读取并解析直播清单...");
  render();
  livePlaylistService_->load(normalizedUrl);
}

void PlayerPresenter::handleLivePlaylistLoaded(LivePlaylistLoadResult result) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || !isLivePlaylistLoading_) {
    return;
  }
  const QString loadedPlaylistUrl = activeLivePlaylistRequestUrl_;
  pendingPlaylistProbeUrl_.clear();
  activeLivePlaylistRequestUrl_.clear();

  std::vector<core::MediaItem> items;
  items.reserve(result.library.channels.size());
  for (auto &channel : result.library.channels) {
    items.push_back(core::makeMediaItem(std::move(channel.streamUrl),
                                        std::move(channel.name)));
  }
  core::Playlist replacement;
  replacement.setMode(livePlaylist_.mode());
  replacement.add(std::move(items));
  livePlaylist_ = std::move(replacement);
  isLivePlaylistLoading_ = false;
  livePlaylistModel_.refresh();
  rememberLivePlaylistUrl(loadedPlaylistUrl);
  livePlaylistStatusText_ =
      QStringLiteral("已载入 %1 项 · 重复 %2 项 · 跳过 %3 项")
          .arg(static_cast<qulonglong>(livePlaylist_.size()))
          .arg(static_cast<qulonglong>(result.duplicateChannelCount))
          .arg(static_cast<qulonglong>(result.skippedChannelCount));
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "live_playlist_loaded",
                 {{"accepted", std::to_string(livePlaylist_.size())},
                  {"duplicates", std::to_string(result.duplicateChannelCount)},
                  {"skipped", std::to_string(result.skippedChannelCount)}});
  }
  render();
}

void PlayerPresenter::handleLivePlaylistFailure(
    const LivePlaylistLoadError error) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || !isLivePlaylistLoading_) {
    return;
  }
  isLivePlaylistLoading_ = false;
  activeLivePlaylistRequestUrl_.clear();
  if (error == LivePlaylistLoadError::HlsMediaManifest &&
      !pendingPlaylistProbeUrl_.isEmpty()) {
    const QString directUrl = std::exchange(pendingPlaylistProbeUrl_, {});
    openDirectNetworkUrl(directUrl);
    return;
  }
  pendingPlaylistProbeUrl_.clear();
  livePlaylistStatusText_ = livePlaylistErrorMessage(error);
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Warning, "presenter",
                 "live_playlist_load_failed",
                 {{"reason", livePlaylistErrorName(error)}});
  }
  render();
}

void PlayerPresenter::changePlaylistKind(const int kindIndex) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }
  activePlaylistKind_ =
      kindIndex == 1 ? PlaylistKind::Live : PlaylistKind::Local;
  render();
}

void PlayerPresenter::openCurrentPlaylistItem(const PlaylistKind playlistKind,
                                              const bool isNetworkRefresh) {
  const auto *const item = playlist(playlistKind).currentItem();
  if (item == nullptr) {
    return;
  }
  if (currentPlaybackItem_.has_value() &&
      currentPlaybackItem_->source != item->source) {
    updateCurrentLocalPlaybackProgress();
    if (appStatePersistTimer_->isActive()) {
      persistAppState();
    }
  }
  currentPlaybackItem_ = *item;
  currentPlaybackKind_ = playlistKind;
  openCurrentPlaybackItem(isNetworkRefresh);
}

void PlayerPresenter::openCurrentPlaybackItem(const bool isNetworkRefresh) {
  if (!currentPlaybackItem_.has_value()) {
    return;
  }
  const auto &item = *currentPlaybackItem_;

  mediaName_ = fromUtf8(item.displayName);
  currentSourcePath_ = fromUtf8(item.source);
  lyricsService_->cancel();
  isLyricsVisible_ = false;
  isLyricsLoading_ = false;
  hasLyricsResult_ = false;
  window_.clearLyrics();
  isAutoPlayPending_ = true;
  isSeeking_ = false;
  seekPreviewPosition_.reset();
  position_ = {};
  isNetworkMedia_ = item.kind == core::MediaSourceKind::NetworkStream;
  networkOpenTimeoutTimer_->stop();
  bufferingPercentage_ = 0;
  isNetworkOpenPending_ = isNetworkMedia_;
  isNetworkOpenCancelled_ = false;
  isNetworkOpenTimedOut_ = false;
  ignoresCancelledNetworkEvents_ = false;
  isNetworkRefreshPending_ = isNetworkRefresh && isNetworkMedia_;
  isNetworkDisconnected_ = false;
  isVideoMedia_ =
      isNetworkMedia_ ||
      !isAudioFile(QString::fromUtf8(item.source.data(),
                                     static_cast<int>(item.source.size())));
  isPreparingMedia_ = true;
  pendingRestartPosition_.reset();
  pendingResumePosition_ =
      isNetworkRefresh ? std::nullopt : resumePositionFor(item);
  isRestartPlayRequested_ = false;
  isResumeSeekRequested_ = false;
  lastAppliedPlaybackRate_ = 1.0;
  hasCurrentMediaStarted_ = false;
  window_.clearPlaybackError();
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "media_open_requested",
                 {{"media", item.displayName}});
  }
  render();
  if (isNetworkOpenPending_) {
    networkOpenTimeoutTimer_->start();
  }
  engine_.open(item);
}

void PlayerPresenter::shutdown() noexcept {
  if (isShuttingDown_) {
    return;
  }

  updateCurrentLocalPlaybackProgress();
  isShuttingDown_ = true;
  lastLivePlaylistUrl_ = window_.livePlaylistUrl();
  persistAppState();
  networkOpenTimeoutTimer_->stop();
  appStatePersistTimer_->stop();
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "shutdown");
  }
  isAutoPlayPending_ = false;
  isSeeking_ = false;
  pendingRestartPosition_.reset();
  pendingResumePosition_.reset();
  isRestartPlayRequested_ = false;
  isResumeSeekRequested_ = false;
  hasCurrentMediaStarted_ = false;
  seekPreviewPosition_.reset();
  livePlaylistService_->cancel();
  lyricsService_->cancel();
  isLyricsLoading_ = false;
  eventBridge_.deactivate();
  disconnect(&eventBridge_, nullptr, this, nullptr);
  try {
    engine_.setEventListener(nullptr);
  } catch (...) {
    // 关闭阶段继续执行剩余清理，不能让第三方内核异常越过 Qt 事件循环。
  }
  try {
    engine_.setVideoSurface(nullptr);
  } catch (...) {
    // 即使句柄解绑失败，也必须继续提交停止请求。
  }
  try {
    engine_.stop();
  } catch (...) {
    // 析构和关闭事件都不能向外传播异常。
  }
}

void PlayerPresenter::attachVideoSurface(void *const nativeHandle) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_) {
    engine_.setVideoSurface(nativeHandle);
  }
}

void PlayerPresenter::requestPlay() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }
  if (isNetworkMedia_ && (isNetworkOpenCancelled_ || isNetworkOpenTimedOut_)) {
    openCurrentPlaybackItem();
    return;
  }
  if (makeViewState().canPlay) {
    if (stateMachine_.state() == core::PlaybackState::Ended) {
      submitSeek(std::chrono::milliseconds::zero());
    } else {
      engine_.play();
    }
  }
}

void PlayerPresenter::requestPause() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ && makeViewState().canPause) {
    engine_.pause();
  }
}

void PlayerPresenter::togglePlayback() {
  Q_ASSERT(QThread::currentThread() == thread());
  const auto viewState = makeViewState();
  if (viewState.canPause) {
    requestPause();
  } else if (viewState.canPlay) {
    requestPlay();
  }
}

void PlayerPresenter::requestStop() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ && makeViewState().canStop) {
    const bool cancelsNetworkOperation =
        isNetworkMedia_ && isNetworkOpenPending_;
    isAutoPlayPending_ = false;
    isSeeking_ = false;
    pendingRestartPosition_.reset();
    pendingResumePosition_.reset();
    isRestartPlayRequested_ = false;
    isResumeSeekRequested_ = false;
    hasCurrentMediaStarted_ = false;
    seekPreviewPosition_.reset();
    isNetworkRefreshPending_ = false;
    isNetworkDisconnected_ = false;
    if (!isNetworkMedia_) {
      clearCurrentLocalResumePosition();
      persistAppState();
      updateRecentLocalMediaView();
    }
    if (cancelsNetworkOperation) {
      networkOpenTimeoutTimer_->stop();
      isNetworkOpenPending_ = false;
      isNetworkOpenCancelled_ = true;
      isNetworkOpenTimedOut_ = false;
      ignoresCancelledNetworkEvents_ = true;
      isPreparingMedia_ = false;
      bufferingPercentage_ = 0;
      window_.clearPlaybackError();
    }
    engine_.stop();
    if (cancelsNetworkOperation) {
      const auto result =
          stateMachine_.transitionTo(core::PlaybackState::Stopped);
      if (result != core::PlaybackTransitionResult::Rejected) {
        emit stateApplied(stateMachine_.state());
      }
      render();
      if (logger_ != nullptr) {
        logger_->log(logging::LogLevel::Info, "presenter",
                     "network_open_cancelled");
      }
    }
  }
}

void PlayerPresenter::requestNetworkRefresh() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || !isNetworkMedia_ ||
      !currentPlaybackItem_.has_value() ||
      currentPlaybackItem_->kind != core::MediaSourceKind::NetworkStream ||
      (currentPlaybackKind_ == PlaylistKind::Live &&
       markedLiveSources_.contains(currentPlaybackItem_->source))) {
    return;
  }

  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter",
                 "network_refresh_requested");
  }
  openCurrentPlaybackItem(true);
}

void PlayerPresenter::beginSeek() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || !makeViewState().canSeek) {
    return;
  }

  isSeeking_ = true;
  seekPreviewPosition_ = boundedPosition(position_.current, position_.total);
  render();
}

void PlayerPresenter::previewSeek(const int progress) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || !isSeeking_) {
    return;
  }

  seekPreviewPosition_ = positionFromProgress(progress, position_.total);
  render();
}

void PlayerPresenter::commitSeek(const int progress) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }

  if (!isSeekAvailable(position_, stateMachine_.state())) {
    isSeeking_ = false;
    seekPreviewPosition_.reset();
    render();
    return;
  }

  submitSeek(positionFromProgress(progress, position_.total));
}

void PlayerPresenter::submitSeek(const std::chrono::milliseconds target) {
  const bool shouldRestart =
      stateMachine_.state() == core::PlaybackState::Ended;
  isSeeking_ = false;
  seekPreviewPosition_.reset();
  pendingResumePosition_.reset();
  isResumeSeekRequested_ = false;
  position_.current = boundedPosition(target, position_.total);
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "seek_requested",
                 {{"position_ms", std::to_string(position_.current.count())}});
  }
  if (shouldRestart) {
    if (!currentPlaybackItem_.has_value()) {
      render();
      return;
    }
    const bool wasAlreadyPending = pendingRestartPosition_.has_value();
    pendingRestartPosition_ = position_.current;
    isRestartPlayRequested_ = true;
    if (!wasAlreadyPending) {
      isAutoPlayPending_ = true;
      isPreparingMedia_ = true;
      hasCurrentMediaStarted_ = false;
      window_.clearPlaybackError();
      if (logger_ != nullptr) {
        logger_->log(logging::LogLevel::Info, "presenter",
                     "media_restart_requested");
      }
      lastAppliedPlaybackRate_ = 1.0;
      render();
      engine_.open(*currentPlaybackItem_);
    } else {
      render();
    }
    return;
  }
  render();
  engine_.seek(position_.current);
}

void PlayerPresenter::requestRelativeSeek(const int seconds) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || seconds == 0 || !makeViewState().canSeek) {
    return;
  }

  submitSeek(position_.current + std::chrono::seconds(seconds));
}

void PlayerPresenter::requestVolume(const int volume) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }

  const int boundedVolume = std::clamp(volume, 0, 100);
  const bool volumeChanged = boundedVolume != volume_;
  if (!volumeChanged && !isMuted_) {
    return;
  }
  if (volumeChanged) {
    engine_.setVolume(boundedVolume);
    volume_ = boundedVolume;
  }
  if (isMuted_) {
    engine_.setMuted(false);
    isMuted_ = false;
  }
  render();
}

void PlayerPresenter::requestVolumeStep(const int delta) {
  Q_ASSERT(QThread::currentThread() == thread());
  requestVolume((isMuted_ ? 0 : volume_) + delta);
}

void PlayerPresenter::requestMuted(const bool isMuted) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || isMuted_ == isMuted) {
    return;
  }
  engine_.setMuted(isMuted);
  isMuted_ = isMuted;
  render();
}

void PlayerPresenter::requestPlaybackRate(const double rate) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }

  const auto normalizedRate = normalizedPlaybackRate(rate);
  if (!normalizedRate.has_value() ||
      std::abs(*normalizedRate - playbackRate_) < 0.001) {
    return;
  }
  playbackRate_ = *normalizedRate;
  if (!isTemporaryFastPlayback_ &&
      (stateMachine_.state() == core::PlaybackState::Playing ||
       stateMachine_.state() == core::PlaybackState::Paused)) {
    applyPlaybackRate(playbackRate_);
  }
  render();
}

void PlayerPresenter::requestTemporaryFastPlayback(const bool enabled) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || enabled == isTemporaryFastPlayback_) {
    return;
  }

  const bool canChangeRate =
      stateMachine_.state() == core::PlaybackState::Playing;
  if (enabled && !canChangeRate) {
    return;
  }
  isTemporaryFastPlayback_ = enabled;
  if (canChangeRate || !enabled) {
    applyPlaybackRate(enabled ? 2.0 : playbackRate_);
  }
  render();
}

void PlayerPresenter::applyPlaybackRate(const double rate) {
  if (std::abs(rate - lastAppliedPlaybackRate_) < 0.001) {
    return;
  }
  engine_.setPlaybackRate(rate);
  lastAppliedPlaybackRate_ = rate;
}

void PlayerPresenter::toggleMuted() {
  Q_ASSERT(QThread::currentThread() == thread());
  requestMuted(!isMuted_);
}

void PlayerPresenter::toggleLyrics() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || isVideoMedia_ || currentSourcePath_.isEmpty()) {
    return;
  }

  isLyricsVisible_ = !isLyricsVisible_;
  if (isLyricsVisible_ && !isLyricsLoading_ && !hasLyricsResult_) {
    isLyricsLoading_ = true;
    window_.showLyricsLoading();
    LyricsQuery query;
    query.filePath = currentSourcePath_;
    query.displayName = mediaName_;
    query.durationMilliseconds =
        position_.total.has_value() ? position_.total->count() : -1;
    lyricsService_->requestLyrics(query);
  }
  render();
}

void PlayerPresenter::requestPrevious() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ &&
      selectAdjacentPlaylistItem(activePlaylistKind_, false)) {
    playlistModel(activePlaylistKind_).refresh();
    openCurrentPlaylistItem(activePlaylistKind_);
  }
}

void PlayerPresenter::requestNext() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ &&
      selectAdjacentPlaylistItem(activePlaylistKind_, true)) {
    playlistModel(activePlaylistKind_).refresh();
    openCurrentPlaylistItem(activePlaylistKind_);
  }
}

void PlayerPresenter::activatePlaylistItem(const int row) {
  Q_ASSERT(QThread::currentThread() == thread());
  auto &activePlaylist = playlist(activePlaylistKind_);
  const bool isMarkedLiveItem =
      activePlaylistKind_ == PlaylistKind::Live && row >= 0 &&
      isLivePlaylistItemMarked(static_cast<std::size_t>(row));
  if (!isShuttingDown_ && !isMarkedLiveItem && row >= 0 &&
      activePlaylist.select(static_cast<std::size_t>(row))) {
    playlistModel(activePlaylistKind_).refresh();
    openCurrentPlaylistItem(activePlaylistKind_);
  }
}

void PlayerPresenter::removePlaylistItems(QList<int> rows) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || activePlaylistKind_ != PlaylistKind::Local ||
      rows.isEmpty()) {
    return;
  }

  auto& activePlaylist = localPlaylist_;
  std::vector<std::size_t> indexes;
  indexes.reserve(static_cast<std::size_t>(rows.size()));
  for (const int row : rows) {
    if (row >= 0 && static_cast<std::size_t>(row) < activePlaylist.size()) {
      indexes.push_back(static_cast<std::size_t>(row));
    }
  }
  std::sort(indexes.begin(), indexes.end());
  indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
  if (indexes.empty()) {
    return;
  }

  const bool wasCurrent = currentPlaybackKind_ == activePlaylistKind_ &&
                          activePlaylist.currentIndex().has_value() &&
                          std::binary_search(indexes.begin(), indexes.end(),
                                             *activePlaylist.currentIndex());
  for (auto index = indexes.rbegin(); index != indexes.rend(); ++index) {
    static_cast<void>(activePlaylist.remove(*index));
  }
  localPlaylistModel_.refresh();
  persistAppState();
  if (!wasCurrent) {
    render();
    return;
  }
  if (!activePlaylist.empty()) {
    openCurrentPlaylistItem(PlaylistKind::Local);
    return;
  }

  isAutoPlayPending_ = false;
  isSeeking_ = false;
  pendingRestartPosition_.reset();
  pendingResumePosition_.reset();
  isRestartPlayRequested_ = false;
  isResumeSeekRequested_ = false;
  seekPreviewPosition_.reset();
  position_ = {};
  mediaName_ = QStringLiteral("未选择媒体");
  currentPlaybackItem_.reset();
  currentPlaybackKind_.reset();
  networkOpenTimeoutTimer_->stop();
  bufferingPercentage_ = 0;
  isVideoMedia_ = false;
  isNetworkMedia_ = false;
  isNetworkOpenPending_ = false;
  isNetworkOpenCancelled_ = false;
  isNetworkOpenTimedOut_ = false;
  ignoresCancelledNetworkEvents_ = false;
  isNetworkRefreshPending_ = false;
  isNetworkDisconnected_ = false;
  isPreparingMedia_ = false;
  hasCurrentMediaStarted_ = false;
  engine_.stop();
  stateMachine_.reset();
  render();
}

void PlayerPresenter::toggleLivePlaylistMark(const int row) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || activePlaylistKind_ != PlaylistKind::Live || row < 0 ||
      static_cast<std::size_t>(row) >= livePlaylist_.size()) {
    return;
  }

  const auto& item = livePlaylist_.at(static_cast<std::size_t>(row));
  const auto markedItem = markedLiveSources_.find(item.source);
  const bool isMarking = markedItem == markedLiveSources_.end();
  if (isMarking) {
    markedLiveSources_.insert(item.source);
  } else {
    markedLiveSources_.erase(markedItem);
  }
  livePlaylistModel_.refreshItem(row);
  const bool marksCurrentPlayback =
      isMarking && currentPlaybackKind_ == PlaylistKind::Live &&
      currentPlaybackItem_.has_value() &&
      currentPlaybackItem_->source == item.source;
  if (marksCurrentPlayback) {
    requestStop();
    render();
  }
}

void PlayerPresenter::toggleLivePlaylistFavorite(const int row) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || activePlaylistKind_ != PlaylistKind::Live || row < 0 ||
      static_cast<std::size_t>(row) >= livePlaylist_.size()) {
    return;
  }

  const auto& item = livePlaylist_.at(static_cast<std::size_t>(row));
  const auto favoriteItem = favoriteLiveSources_.find(item.source);
  if (favoriteItem == favoriteLiveSources_.end()) {
    favoriteLiveSources_.insert(item.source);
  } else {
    favoriteLiveSources_.erase(favoriteItem);
  }
  livePlaylistModel_.refreshItem(row);
  persistAppState();
}

void PlayerPresenter::movePlaylistItem(const int row, const int targetRow) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || activePlaylistKind_ != PlaylistKind::Local ||
      row < 0 || targetRow < 0 ||
      !localPlaylist_.moveItem(static_cast<std::size_t>(row),
                               static_cast<std::size_t>(targetRow))) {
    return;
  }
  localPlaylistModel_.refresh();
  persistAppState();
  render();
}

void PlayerPresenter::renamePlaylistItem(const int row,
                                         const QString &displayName) {
  Q_ASSERT(QThread::currentThread() == thread());
  const QString normalizedName = displayName.trimmed();
  if (isShuttingDown_ || activePlaylistKind_ != PlaylistKind::Local ||
      row < 0 || normalizedName.isEmpty() ||
      !localPlaylist_.renameItem(static_cast<std::size_t>(row),
                                 normalizedName.toUtf8().toStdString())) {
    return;
  }
  if (currentPlaybackKind_ == PlaylistKind::Local &&
      localPlaylist_.currentIndex() == static_cast<std::size_t>(row)) {
    mediaName_ = normalizedName;
    if (currentPlaybackItem_.has_value()) {
      currentPlaybackItem_->displayName = normalizedName.toUtf8().toStdString();
    }
  }
  localPlaylistModel_.refresh();
  persistAppState();
  render();
}

void PlayerPresenter::changePlaybackMode(const int modeIndex) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }

  auto &activePlaylist = playlist(activePlaylistKind_);
  switch (modeIndex) {
  case 1:
    activePlaylist.setMode(core::PlaybackMode::LoopAll);
    break;
  case 2:
    activePlaylist.setMode(core::PlaybackMode::LoopOne);
    break;
  case 3:
    activePlaylist.setMode(core::PlaybackMode::Shuffle);
    break;
  default:
    activePlaylist.setMode(core::PlaybackMode::Sequential);
    break;
  }
  playlistModel(activePlaylistKind_).refresh();
  render();
}

void PlayerPresenter::handleStateChanged(const core::PlaybackState state) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }
  if (isNetworkMedia_ && ignoresCancelledNetworkEvents_) {
    return;
  }

  const bool wasStartedNetworkFailure = isNetworkMedia_ &&
                                        state == core::PlaybackState::Failed &&
                                        hasCurrentMediaStarted_;

  if (state == core::PlaybackState::Stopped &&
      stateMachine_.state() == core::PlaybackState::Ended &&
      !pendingRestartPosition_.has_value()) {
    // libVLC 的自然结束清理不是用户停止，不能覆盖可定位的结束界面。
    return;
  }

  if (state == core::PlaybackState::Opening ||
      state == core::PlaybackState::Failed) {
    isPreparingMedia_ = false;
  }
  if (state == core::PlaybackState::Stopped ||
      state == core::PlaybackState::Ended ||
      state == core::PlaybackState::Failed) {
    isSeeking_ = false;
    seekPreviewPosition_.reset();
  }
  if (state == core::PlaybackState::Failed) {
    pendingRestartPosition_.reset();
    pendingResumePosition_.reset();
    isRestartPlayRequested_ = false;
    isResumeSeekRequested_ = false;
  }

  const auto result = stateMachine_.transitionTo(state);
  if (result == core::PlaybackTransitionResult::Rejected) {
    return;
  }

  if (state == core::PlaybackState::Playing) {
    if (isNetworkMedia_) {
      networkOpenTimeoutTimer_->stop();
      isNetworkOpenPending_ = false;
      bufferingPercentage_ = 100;
      isNetworkRefreshPending_ = false;
      isNetworkDisconnected_ = false;
      // 网络音频输出可能在连接完成后才创建，进入 Playing 后同步一次界面状态。
      engine_.setVolume(volume_);
      engine_.setMuted(isMuted_);
    }
    const double effectiveRate = isTemporaryFastPlayback_ ? 2.0 : playbackRate_;
    applyPlaybackRate(effectiveRate);
    hasCurrentMediaStarted_ = true;
    if (!isNetworkMedia_) {
      rememberCurrentLocalMedia();
      tryApplyPendingResumePosition();
    }
  } else if (state == core::PlaybackState::Paused && !isNetworkMedia_) {
    updateCurrentLocalPlaybackProgress();
    persistAppState();
    updateRecentLocalMediaView();
  } else if (state == core::PlaybackState::Stopped ||
             state == core::PlaybackState::Failed) {
    if (isNetworkMedia_) {
      networkOpenTimeoutTimer_->stop();
      isNetworkOpenPending_ = false;
      isNetworkRefreshPending_ = false;
      if (state == core::PlaybackState::Failed) {
        isNetworkDisconnected_ = wasStartedNetworkFailure;
      }
    }
    hasCurrentMediaStarted_ = false;
  }

  render();

  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "state_changed",
                 {{"state", stateName(state)}});
  }
  emit stateApplied(stateMachine_.state());

  if (state == core::PlaybackState::Stopped &&
      pendingRestartPosition_.has_value() && !isRestartPlayRequested_) {
    isRestartPlayRequested_ = true;
    engine_.play();
    return;
  }
  if (state == core::PlaybackState::Playing &&
      pendingRestartPosition_.has_value() && isRestartPlayRequested_) {
    const auto target = *pendingRestartPosition_;
    pendingRestartPosition_.reset();
    isRestartPlayRequested_ = false;
    position_.current = boundedPosition(target, position_.total);
    render();
    engine_.seek(position_.current);
  }

  if (state == core::PlaybackState::Opening && isAutoPlayPending_) {
    isAutoPlayPending_ = false;
    engine_.play();
  }
}

void PlayerPresenter::handlePositionChanged(core::PlaybackPosition position) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ &&
      !(isNetworkMedia_ && ignoresCancelledNetworkEvents_)) {
    if (pendingRestartPosition_.has_value()) {
      const auto total =
          position.total.has_value() ? position.total : position_.total;
      position.current = boundedPosition(*pendingRestartPosition_, total);
      position.total = total;
    } else if (stateMachine_.state() == core::PlaybackState::Ended) {
      const auto total =
          position.total.has_value() ? position.total : position_.total;
      if (total.has_value()) {
        position.current = *total;
        position.total = total;
      }
    }
    if (pendingResumePosition_.has_value() && isResumeSeekRequested_) {
      const auto confirmationFloor =
          *pendingResumePosition_ - std::chrono::seconds(2);
      if (position.current >= confirmationFloor) {
        pendingResumePosition_.reset();
        isResumeSeekRequested_ = false;
      } else {
        position.current =
            boundedPosition(*pendingResumePosition_, position.total);
      }
    }
    position_ = std::move(position);
    tryApplyPendingResumePosition();
    updateCurrentLocalPlaybackProgress();
    if (!isSeeking_) {
      render();
    }
  }
}

void PlayerPresenter::handleDurationChanged(const OptionalDuration duration) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ &&
      !(isNetworkMedia_ && ignoresCancelledNetworkEvents_)) {
    position_.total = duration;
    if (pendingRestartPosition_.has_value()) {
      position_.current =
          boundedPosition(*pendingRestartPosition_, position_.total);
    } else if (stateMachine_.state() == core::PlaybackState::Ended &&
               duration.has_value()) {
      position_.current = *duration;
    }
    tryApplyPendingResumePosition();
    if (!isSeeking_) {
      render();
    }
  }
}

void PlayerPresenter::handleBufferingChanged(const int percentage) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || !isNetworkMedia_ || ignoresCancelledNetworkEvents_) {
    return;
  }
  bufferingPercentage_ = std::clamp(percentage, 0, 100);
  if (stateMachine_.state() == core::PlaybackState::Buffering) {
    render();
  }
}

void PlayerPresenter::handleAudioWaveformChanged(core::AudioWaveform waveform) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ && !isVideoMedia_) {
    window_.setAudioWaveform(std::move(waveform));
  }
}

void PlayerPresenter::handleLyricsResult(LyricsResult result) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }
  isLyricsLoading_ = false;
  hasLyricsResult_ = true;
  window_.setLyricsResult(result);
  render();
}

void PlayerPresenter::handleEndReached() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || (isNetworkMedia_ && ignoresCancelledNetworkEvents_)) {
    return;
  }

  if (isNetworkMedia_) {
    if (!hasCurrentMediaStarted_) {
      return;
    }
    hasCurrentMediaStarted_ = false;
    isNetworkOpenPending_ = false;
    isNetworkRefreshPending_ = false;
    isNetworkDisconnected_ = true;
    isPreparingMedia_ = false;
    const auto result = stateMachine_.transitionTo(core::PlaybackState::Ended);
    if (result != core::PlaybackTransitionResult::Rejected) {
      render();
      emit stateApplied(stateMachine_.state());
    }
    return;
  }
  if (!hasCurrentMediaStarted_) {
    return;
  }

  if (position_.total.has_value()) {
    position_.current = *position_.total;
  }
  hasCurrentMediaStarted_ = false;
  pendingRestartPosition_.reset();
  pendingResumePosition_.reset();
  isRestartPlayRequested_ = false;
  isResumeSeekRequested_ = false;
  clearCurrentLocalResumePosition();
  persistAppState();
  updateRecentLocalMediaView();
  if (currentPlaybackKind_.has_value() &&
      playlist(*currentPlaybackKind_).advanceAfterEnd()) {
    playlistModel(*currentPlaybackKind_).refresh();
    openCurrentPlaylistItem(*currentPlaybackKind_);
    return;
  }

  const auto result = stateMachine_.transitionTo(core::PlaybackState::Ended);
  if (result != core::PlaybackTransitionResult::Rejected) {
    isSeeking_ = false;
    seekPreviewPosition_.reset();
    render();
    emit stateApplied(stateMachine_.state());
  }
}

void PlayerPresenter::handleError(core::PlaybackError error) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || (isNetworkMedia_ && ignoresCancelledNetworkEvents_)) {
    return;
  }

  const bool wasStartedNetwork = isNetworkMedia_ && hasCurrentMediaStarted_;

  isAutoPlayPending_ = false;
  isSeeking_ = false;
  pendingRestartPosition_.reset();
  pendingResumePosition_.reset();
  isRestartPlayRequested_ = false;
  isResumeSeekRequested_ = false;
  seekPreviewPosition_.reset();
  isPreparingMedia_ = false;
  hasCurrentMediaStarted_ = false;
  if (isNetworkMedia_) {
    networkOpenTimeoutTimer_->stop();
    isNetworkOpenPending_ = false;
    isNetworkRefreshPending_ = false;
    isNetworkDisconnected_ = isNetworkDisconnected_ || wasStartedNetwork;
  }
  if (logger_ != nullptr) {
    logger_->log(
        logging::LogLevel::Error, "presenter", "media_error",
        {{"kind", errorKindName(error.kind)}, {"detail", error.engineDetail}});
  }
  const auto result = stateMachine_.transitionTo(core::PlaybackState::Failed);
  if (result != core::PlaybackTransitionResult::Rejected) {
    render();
    emit stateApplied(stateMachine_.state());
  }
  window_.showPlaybackError(fromUtf8(error.userMessage));
}

void PlayerPresenter::handleNetworkOpenTimeout() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || !isNetworkMedia_ || !isNetworkOpenPending_) {
    return;
  }

  isAutoPlayPending_ = false;
  isNetworkOpenPending_ = false;
  isNetworkOpenCancelled_ = false;
  isNetworkOpenTimedOut_ = true;
  ignoresCancelledNetworkEvents_ = true;
  isPreparingMedia_ = false;
  hasCurrentMediaStarted_ = false;
  isNetworkRefreshPending_ = false;
  isNetworkDisconnected_ = false;
  bufferingPercentage_ = 0;
  engine_.stop();
  const auto result = stateMachine_.transitionTo(core::PlaybackState::Failed);
  if (result != core::PlaybackTransitionResult::Rejected) {
    render();
    emit stateApplied(stateMachine_.state());
  }
  window_.showPlaybackError(
      QStringLiteral("连接网络媒体超时，请检查地址、网络或服务状态后重试。"));
  if (logger_ != nullptr) {
    logger_->log(
        logging::LogLevel::Warning, "presenter", "network_open_timeout",
        {{"timeout_ms", std::to_string(kNetworkOpenTimeout.count() * 1000)}});
  }
}

void PlayerPresenter::rememberNetworkUrl(const QString &url) {
  recentNetworkUrls_.removeAll(url);
  recentNetworkUrls_.prepend(url);
  while (recentNetworkUrls_.size() > kMaxSessionNetworkUrls) {
    recentNetworkUrls_.removeLast();
  }
  window_.setRecentNetworkUrls(recentNetworkUrls_);
}

void PlayerPresenter::rememberLivePlaylistUrl(const QString& url) {
  const QString normalizedUrl = url.trimmed();
  if (normalizedUrl.isEmpty()) {
    return;
  }
  QStringList history = livePlaylistUrlHistory_;
  history.removeAll(normalizedUrl);
  history.prepend(normalizedUrl);
  updateLivePlaylistHistory(history);
}

void PlayerPresenter::updateLivePlaylistHistory(const QStringList& urls) {
  Q_ASSERT(QThread::currentThread() == thread());
  QStringList normalized;
  normalized.reserve(std::min(urls.size(), kMaxLivePlaylistUrlHistory));
  for (const QString& value : urls) {
    const QString url = value.trimmed();
    if (url.isEmpty() || normalized.contains(url)) {
      continue;
    }
    normalized.append(url);
    if (normalized.size() == kMaxLivePlaylistUrlHistory) {
      break;
    }
  }
  livePlaylistUrlHistory_ = std::move(normalized);
  window_.setLivePlaylistHistoryUrls(livePlaylistUrlHistory_);
  persistAppState();
}

void PlayerPresenter::updateLiveSourceMemos(
    const QVector<LiveSourceMemo>& memos) {
  Q_ASSERT(QThread::currentThread() == thread());
  QVector<LiveSourceMemo> normalized;
  normalized.reserve(memos.size());
  for (const LiveSourceMemo& memo : memos) {
    const QString sourceUrl = memo.sourceUrl.trimmed();
    if (sourceUrl.isEmpty()) {
      continue;
    }
    normalized.append(LiveSourceMemo{sourceUrl, memo.note.trimmed()});
  }
  liveSourceMemos_ = std::move(normalized);
  window_.setLiveSourceMemos(liveSourceMemos_);
  persistAppState();
}

void PlayerPresenter::openRecentLocalMedia(const QString& filePath) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || filePath.trimmed().isEmpty()) {
    return;
  }
  const QString identity = localPathIdentity(filePath);
  const QFileInfo fileInfo(filePath);
  if (!fileInfo.exists() || !fileInfo.isFile()) {
    recentLocalMedia_.erase(
        std::remove_if(recentLocalMedia_.begin(), recentLocalMedia_.end(),
                       [&identity](const LocalPlaybackRecord& record) {
                         return localPathIdentity(fromUtf8(record.item.source)) ==
                                identity;
                       }),
        recentLocalMedia_.end());
    persistAppState();
    updateRecentLocalMediaView();
    window_.showPlaybackError(
        QStringLiteral("最近播放文件已不存在，记录已移除。"));
    return;
  }

  for (std::size_t index = 0; index < localPlaylist_.size(); ++index) {
    if (localPathIdentity(fromUtf8(localPlaylist_.at(index).source)) ==
        identity) {
      static_cast<void>(localPlaylist_.select(index));
      activePlaylistKind_ = PlaylistKind::Local;
      localPlaylistModel_.refresh();
      openCurrentPlaylistItem(PlaylistKind::Local);
      return;
    }
  }
  addLocalFiles(QStringList{filePath});
}

void PlayerPresenter::clearRecentLocalMedia() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || recentLocalMedia_.empty()) {
    return;
  }
  recentLocalMedia_.clear();
  pendingResumePosition_.reset();
  isResumeSeekRequested_ = false;
  persistAppState();
  updateRecentLocalMediaView();
}

void PlayerPresenter::rememberCurrentLocalMedia() {
  if (currentPlaybackKind_ != PlaylistKind::Local ||
      !currentPlaybackItem_.has_value()) {
    return;
  }
  const QString identity =
      localPathIdentity(fromUtf8(currentPlaybackItem_->source));
  LocalPlaybackRecord record{*currentPlaybackItem_, 0, -1};
  const auto existing = std::find_if(
      recentLocalMedia_.begin(), recentLocalMedia_.end(),
      [&identity](const LocalPlaybackRecord& value) {
        return localPathIdentity(fromUtf8(value.item.source)) == identity;
      });
  if (existing != recentLocalMedia_.end()) {
    record.positionMilliseconds = existing->positionMilliseconds;
    record.durationMilliseconds = existing->durationMilliseconds;
    recentLocalMedia_.erase(existing);
  }
  recentLocalMedia_.insert(recentLocalMedia_.begin(), std::move(record));
  if (recentLocalMedia_.size() > kMaxRecentLocalMedia) {
    recentLocalMedia_.resize(kMaxRecentLocalMedia);
  }
  persistAppState();
  updateRecentLocalMediaView();
}

void PlayerPresenter::clearCurrentLocalResumePosition() {
  if (currentPlaybackKind_ != PlaylistKind::Local ||
      !currentPlaybackItem_.has_value()) {
    return;
  }
  const QString identity =
      localPathIdentity(fromUtf8(currentPlaybackItem_->source));
  const auto record = std::find_if(
      recentLocalMedia_.begin(), recentLocalMedia_.end(),
      [&identity](const LocalPlaybackRecord& value) {
        return localPathIdentity(fromUtf8(value.item.source)) == identity;
      });
  if (record != recentLocalMedia_.end()) {
    record->positionMilliseconds = 0;
    if (position_.total.has_value()) {
      record->durationMilliseconds = position_.total->count();
    }
  }
}

void PlayerPresenter::updateCurrentLocalPlaybackProgress() {
  if (isNetworkMedia_ || currentPlaybackKind_ != PlaylistKind::Local ||
      !currentPlaybackItem_.has_value() ||
      pendingResumePosition_.has_value()) {
    return;
  }
  const QString identity =
      localPathIdentity(fromUtf8(currentPlaybackItem_->source));
  const auto record = std::find_if(
      recentLocalMedia_.begin(), recentLocalMedia_.end(),
      [&identity](const LocalPlaybackRecord& value) {
        return localPathIdentity(fromUtf8(value.item.source)) == identity;
      });
  if (record == recentLocalMedia_.end()) {
    return;
  }

  const qint64 duration =
      position_.total.has_value() ? position_.total->count() : -1;
  LocalPlaybackRecord candidate = *record;
  candidate.durationMilliseconds = duration;
  candidate.positionMilliseconds = position_.current.count();
  if (!resumePosition(candidate).has_value()) {
    candidate.positionMilliseconds = 0;
  }
  if (record->positionMilliseconds == candidate.positionMilliseconds &&
      record->durationMilliseconds == candidate.durationMilliseconds) {
    return;
  }
  record->positionMilliseconds = candidate.positionMilliseconds;
  record->durationMilliseconds = candidate.durationMilliseconds;
  scheduleAppStatePersist();
}

void PlayerPresenter::updateRecentLocalMediaView() {
  QVector<RecentLocalMediaItem> items;
  items.reserve(static_cast<int>(recentLocalMedia_.size()));
  for (const LocalPlaybackRecord& record : recentLocalMedia_) {
    QString label = fromUtf8(record.item.displayName);
    if (const auto position = resumePosition(record); position.has_value()) {
      label = QStringLiteral("%1  —  继续 %2").arg(label,
                                                   formatTime(*position));
    }
    items.append(RecentLocalMediaItem{fromUtf8(record.item.source), label});
  }
  window_.setRecentLocalMedia(items);
}

void PlayerPresenter::scheduleAppStatePersist() {
  if (appStateStore_ != nullptr && !isShuttingDown_ &&
      !appStatePersistTimer_->isActive()) {
    appStatePersistTimer_->start();
  }
}

void PlayerPresenter::tryApplyPendingResumePosition() {
  if (!pendingResumePosition_.has_value() || isResumeSeekRequested_ ||
      isNetworkMedia_ || currentPlaybackKind_ != PlaylistKind::Local ||
      stateMachine_.state() != core::PlaybackState::Playing ||
      !position_.isSeekable || !position_.total.has_value()) {
    return;
  }
  const auto target =
      boundedPosition(*pendingResumePosition_, position_.total);
  if (target < kMinimumResumePosition ||
      *position_.total - target < kResumeEndGuard) {
    pendingResumePosition_.reset();
    return;
  }
  isResumeSeekRequested_ = true;
  position_.current = target;
  engine_.seek(target);
}

std::optional<std::chrono::milliseconds> PlayerPresenter::resumePositionFor(
    const core::MediaItem& item) const {
  if (item.kind != core::MediaSourceKind::LocalFile) {
    return std::nullopt;
  }
  const QString identity = localPathIdentity(fromUtf8(item.source));
  const auto record = std::find_if(
      recentLocalMedia_.begin(), recentLocalMedia_.end(),
      [&identity](const LocalPlaybackRecord& value) {
        return localPathIdentity(fromUtf8(value.item.source)) == identity;
      });
  return record == recentLocalMedia_.end() ? std::nullopt
                                           : resumePosition(*record);
}

void PlayerPresenter::restoreAppState() {
  if (appStateStore_ == nullptr) {
    return;
  }
  try {
    AppStateSnapshot snapshot = appStateStore_->load();
    std::vector<core::MediaItem> localItems;
    localItems.reserve(snapshot.localPlaylist.size());
    for (auto& item : snapshot.localPlaylist) {
      if (item.kind == core::MediaSourceKind::LocalFile &&
          !item.source.empty() && !item.displayName.empty()) {
        localItems.push_back(std::move(item));
      }
    }
    localPlaylist_.add(std::move(localItems));
    lastLivePlaylistUrl_ = snapshot.lastLivePlaylistUrl.trimmed();
    for (const QString& value : snapshot.livePlaylistUrlHistory) {
      const QString url = value.trimmed();
      if (!url.isEmpty() && !livePlaylistUrlHistory_.contains(url)) {
        livePlaylistUrlHistory_.append(url);
      }
      if (livePlaylistUrlHistory_.size() == kMaxLivePlaylistUrlHistory) {
        break;
      }
    }
    for (const QString& value : snapshot.favoriteLiveSourceUrls) {
      const QString url = value.trimmed();
      if (!url.isEmpty()) {
        favoriteLiveSources_.insert(utf8String(url));
      }
      if (favoriteLiveSources_.size() == kMaxFavoriteLiveSources) {
        break;
      }
    }
    for (LocalPlaybackRecord& record : snapshot.recentLocalMedia) {
      if (record.item.kind != core::MediaSourceKind::LocalFile ||
          record.item.source.empty() || record.item.displayName.empty()) {
        continue;
      }
      const QString identity = localPathIdentity(fromUtf8(record.item.source));
      const bool isDuplicate = std::any_of(
          recentLocalMedia_.begin(), recentLocalMedia_.end(),
          [&identity](const LocalPlaybackRecord& existing) {
            return localPathIdentity(fromUtf8(existing.item.source)) ==
                   identity;
          });
      if (isDuplicate) {
        continue;
      }
      record.positionMilliseconds =
          std::max<qint64>(0, record.positionMilliseconds);
      if (!resumePosition(record).has_value()) {
        record.positionMilliseconds = 0;
      }
      recentLocalMedia_.push_back(std::move(record));
      if (recentLocalMedia_.size() == kMaxRecentLocalMedia) {
        break;
      }
    }
    for (const LiveSourceMemo& memo : snapshot.liveSourceMemos) {
      const QString sourceUrl = memo.sourceUrl.trimmed();
      if (!sourceUrl.isEmpty()) {
        liveSourceMemos_.append(
            LiveSourceMemo{sourceUrl, memo.note.trimmed()});
      }
    }
    if (logger_ != nullptr) {
      logger_->log(
          logging::LogLevel::Info, "presenter", "app_state_restored",
          {{"local_items", std::to_string(localPlaylist_.size())},
           {"live_history", std::to_string(livePlaylistUrlHistory_.size())},
           {"live_favorites", std::to_string(favoriteLiveSources_.size())},
           {"recent_local", std::to_string(recentLocalMedia_.size())},
           {"live_memos", std::to_string(liveSourceMemos_.size())}});
    }
  } catch (...) {
    localPlaylist_.clear();
    livePlaylistUrlHistory_.clear();
    favoriteLiveSources_.clear();
    recentLocalMedia_.clear();
    liveSourceMemos_.clear();
    lastLivePlaylistUrl_.clear();
    if (logger_ != nullptr) {
      logger_->log(logging::LogLevel::Warning, "presenter",
                   "app_state_restore_failed");
    }
  }
}

void PlayerPresenter::persistAppState() noexcept {
  if (appStatePersistTimer_ != nullptr) {
    appStatePersistTimer_->stop();
  }
  if (appStateStore_ == nullptr) {
    return;
  }
  try {
    AppStateSnapshot snapshot;
    snapshot.localPlaylist.reserve(localPlaylist_.size());
    for (std::size_t index = 0; index < localPlaylist_.size(); ++index) {
      snapshot.localPlaylist.push_back(localPlaylist_.at(index));
    }
    snapshot.lastLivePlaylistUrl = lastLivePlaylistUrl_;
    snapshot.livePlaylistUrlHistory = livePlaylistUrlHistory_;
    snapshot.recentLocalMedia = recentLocalMedia_;
    snapshot.favoriteLiveSourceUrls.reserve(
        static_cast<int>(favoriteLiveSources_.size()));
    for (const std::string& source : favoriteLiveSources_) {
      snapshot.favoriteLiveSourceUrls.append(fromUtf8(source));
    }
    std::sort(snapshot.favoriteLiveSourceUrls.begin(),
              snapshot.favoriteLiveSourceUrls.end());
    snapshot.liveSourceMemos = liveSourceMemos_;
    appStateStore_->save(snapshot);
  } catch (...) {
    if (logger_ != nullptr) {
      logger_->log(logging::LogLevel::Warning, "presenter",
                   "app_state_save_failed");
    }
  }
}

core::Playlist &
PlayerPresenter::playlist(const PlaylistKind playlistKind) noexcept {
  return playlistKind == PlaylistKind::Live ? livePlaylist_ : localPlaylist_;
}

const core::Playlist &
PlayerPresenter::playlist(const PlaylistKind playlistKind) const noexcept {
  return playlistKind == PlaylistKind::Live ? livePlaylist_ : localPlaylist_;
}

PlaylistModel &
PlayerPresenter::playlistModel(const PlaylistKind playlistKind) noexcept {
  return playlistKind == PlaylistKind::Live ? livePlaylistModel_
                                            : localPlaylistModel_;
}

bool PlayerPresenter::isLivePlaylistItemMarked(const std::size_t index) const {
  return index < livePlaylist_.size() &&
         markedLiveSources_.contains(livePlaylist_.at(index).source);
}

bool PlayerPresenter::selectAdjacentPlaylistItem(
    const PlaylistKind playlistKind, const bool selectsNext) {
  auto& targetPlaylist = playlist(playlistKind);
  if (playlistKind == PlaylistKind::Local) {
    return selectsNext ? targetPlaylist.selectNext()
                       : targetPlaylist.selectPrevious();
  }

  const auto originalIndex = targetPlaylist.currentIndex();
  for (std::size_t attempt = 0; attempt < targetPlaylist.size(); ++attempt) {
    const bool wasSelected = selectsNext ? targetPlaylist.selectNext()
                                         : targetPlaylist.selectPrevious();
    if (!wasSelected || !targetPlaylist.currentIndex().has_value()) {
      break;
    }
    const std::size_t selectedIndex = *targetPlaylist.currentIndex();
    if (originalIndex.has_value() && selectedIndex == *originalIndex) {
      break;
    }
    if (!isLivePlaylistItemMarked(selectedIndex)) {
      return true;
    }
  }
  if (originalIndex.has_value()) {
    static_cast<void>(targetPlaylist.select(*originalIndex));
  }
  return false;
}

void PlayerPresenter::render() {
  Q_ASSERT(QThread::currentThread() == thread());
  window_.applyViewState(makeViewState());
}

PlayerViewState PlayerPresenter::makeViewState() const {
  PlayerViewState viewState;
  const auto &activePlaylist = playlist(activePlaylistKind_);
  viewState.mediaName = mediaName_;
  viewState.videoPlaceholder = QStringLiteral("打开媒体后，画面会出现在这里");
  const auto displayedPosition =
      seekPreviewPosition_.value_or(position_.current);
  const std::optional<std::chrono::milliseconds> displayedTotal =
      isNetworkMedia_ ? std::nullopt : position_.total;
  viewState.positionText = formatPosition(displayedPosition, displayedTotal);
  viewState.progressValue = progressValue(displayedPosition, displayedTotal);
  viewState.positionMilliseconds =
      std::max<qint64>(static_cast<qint64>(displayedPosition.count()), 0);
  viewState.volumeValue = isMuted_ ? 0 : volume_;
  viewState.isMuted = isMuted_;
  viewState.volumeText = isMuted_ ? QStringLiteral("已静音 · 0%")
                                  : QStringLiteral("音量 %1%").arg(volume_);
  viewState.livePlaylistStatusText = livePlaylistStatusText_;
  viewState.isLivePlaylistActive = activePlaylistKind_ == PlaylistKind::Live;
  const auto* const activePlaylistItem = activePlaylist.currentItem();
  viewState.isCurrentPlaybackInActivePlaylist =
      currentPlaybackKind_.has_value() && currentPlaybackItem_.has_value() &&
      *currentPlaybackKind_ == activePlaylistKind_ &&
      activePlaylistItem != nullptr &&
      activePlaylistItem->source == currentPlaybackItem_->source;
  const auto playbackState = stateMachine_.state();
  const bool hasActiveLivePlayback =
      currentPlaybackKind_ == PlaylistKind::Live &&
      currentPlaybackItem_.has_value() &&
      !markedLiveSources_.contains(currentPlaybackItem_->source) &&
      (isNetworkOpenPending_ || playbackState == core::PlaybackState::Opening ||
       playbackState == core::PlaybackState::Buffering ||
       playbackState == core::PlaybackState::Playing ||
       playbackState == core::PlaybackState::Paused);
  if (hasActiveLivePlayback) {
    for (std::size_t index = 0; index < livePlaylist_.size(); ++index) {
      if (livePlaylist_.at(index).source == currentPlaybackItem_->source) {
        viewState.currentLivePlaybackIndex = static_cast<int>(index);
        break;
      }
    }
  }
  viewState.isLivePlaylistLoading = isLivePlaylistLoading_;
  viewState.isPlaylistEditable = activePlaylistKind_ == PlaylistKind::Local;
  viewState.currentPlaylistIndex =
      activePlaylist.currentIndex().has_value()
          ? static_cast<int>(*activePlaylist.currentIndex())
          : -1;
  viewState.playbackModeIndex = static_cast<int>(activePlaylist.mode());
  viewState.playbackRate = playbackRate_;
  viewState.isTemporaryFastPlayback = isTemporaryFastPlayback_;
  viewState.canGoPrevious = activePlaylist.previousIndex().has_value();
  viewState.canGoNext = activePlaylist.nextIndex().has_value();
  viewState.canRemovePlaylistItem =
      activePlaylistKind_ == PlaylistKind::Local &&
      activePlaylist.currentIndex().has_value();
  const bool isCurrentLivePlaybackMarked =
      currentPlaybackKind_ == PlaylistKind::Live &&
      currentPlaybackItem_.has_value() &&
      markedLiveSources_.contains(currentPlaybackItem_->source);
  viewState.canRefreshNetwork =
      isNetworkMedia_ && !isCurrentLivePlaybackMarked;

  switch (stateMachine_.state()) {
  case core::PlaybackState::Idle:
    if (isNetworkOpenPending_) {
      viewState.statusText = QStringLiteral("正在连接...");
    } else if (isNetworkOpenCancelled_) {
      viewState.statusText = QStringLiteral("已取消连接");
      viewState.canPlay = true;
    } else {
      viewState.statusText = QStringLiteral("未打开媒体");
    }
    break;
  case core::PlaybackState::Opening:
    viewState.statusText = QStringLiteral("正在打开...");
    viewState.canStop = true;
    break;
  case core::PlaybackState::Buffering:
    viewState.statusText =
        isNetworkMedia_
            ? QStringLiteral("正在缓冲 %1%").arg(bufferingPercentage_)
            : QStringLiteral("正在缓冲...");
    viewState.canStop = true;
    break;
  case core::PlaybackState::Playing:
    viewState.statusText = QStringLiteral("正在播放");
    viewState.canPause = true;
    viewState.canStop = true;
    break;
  case core::PlaybackState::Paused:
    viewState.statusText = QStringLiteral("已暂停");
    viewState.canPlay = true;
    viewState.canStop = true;
    break;
  case core::PlaybackState::Stopped:
    viewState.statusText = isNetworkOpenCancelled_
                               ? QStringLiteral("已取消连接")
                               : QStringLiteral("已停止");
    viewState.canPlay = true;
    break;
  case core::PlaybackState::Ended:
    viewState.statusText = QStringLiteral("播放结束");
    viewState.canPlay = true;
    break;
  case core::PlaybackState::Failed:
    viewState.statusText =
        isNetworkOpenTimedOut_
            ? QStringLiteral("连接超时")
            : (isNetworkMedia_ ? QStringLiteral("直播连接失败，请点击刷新")
                               : QStringLiteral("播放失败"));
    viewState.canPlay = isNetworkOpenTimedOut_;
    break;
  }

  if (isNetworkOpenPending_) {
    viewState.canStop = true;
    if (stateMachine_.state() != core::PlaybackState::Opening &&
        stateMachine_.state() != core::PlaybackState::Buffering) {
      viewState.statusText = QStringLiteral("正在连接...");
      viewState.canPlay = false;
      viewState.canPause = false;
    }
  }

  if (isNetworkRefreshPending_ && isNetworkOpenPending_) {
    viewState.statusText = QStringLiteral("正在刷新直播...");
    viewState.canPlay = false;
    viewState.canPause = false;
    viewState.canStop = true;
  } else if (isNetworkDisconnected_) {
    viewState.statusText = QStringLiteral("直播已断开，请点击刷新");
    viewState.canPlay = false;
    viewState.canPause = false;
    viewState.canStop = false;
  }

  if (isCurrentLivePlaybackMarked) {
    viewState.canPlay = false;
    viewState.canPause = false;
  }

  viewState.canSeek =
      !isNetworkMedia_ && isSeekAvailable(position_, stateMachine_.state());

  if (mediaName_ == QStringLiteral("未选择媒体")) {
    return viewState;
  }

  viewState.canToggleFullscreen =
      !isPreparingMedia_ && (playbackState == core::PlaybackState::Opening ||
                             playbackState == core::PlaybackState::Buffering ||
                             playbackState == core::PlaybackState::Playing ||
                             playbackState == core::PlaybackState::Paused);
  if (!isVideoMedia_) {
    viewState.canShowLyrics = true;
    viewState.isLyricsVisible = isLyricsVisible_;
    viewState.isAudioVisualizationActive =
        playbackState != core::PlaybackState::Failed;
    viewState.isAudioVisualizationPlaying =
        playbackState == core::PlaybackState::Playing;
    viewState.videoPlaceholder = playbackState == core::PlaybackState::Failed
                                     ? QStringLiteral("音频播放失败")
                                     : QString{};
    return viewState;
  }

  if (isPreparingMedia_) {
    viewState.videoPlaceholder = QStringLiteral("正在准备视频画面...");
    return viewState;
  }

  switch (stateMachine_.state()) {
  case core::PlaybackState::Opening:
  case core::PlaybackState::Buffering:
  case core::PlaybackState::Playing:
  case core::PlaybackState::Paused:
    viewState.isVideoSurfaceActive = true;
    viewState.videoPlaceholder.clear();
    break;
  case core::PlaybackState::Stopped:
    viewState.videoPlaceholder = QStringLiteral("视频已停止");
    break;
  case core::PlaybackState::Ended:
    viewState.videoPlaceholder = QStringLiteral("视频播放结束");
    break;
  case core::PlaybackState::Failed:
    viewState.videoPlaceholder = QStringLiteral("视频播放失败");
    break;
  case core::PlaybackState::Idle:
    viewState.videoPlaceholder = QStringLiteral("正在准备视频画面...");
    break;
  }

  return viewState;
}

} // namespace mediahub::gui
