#include "player_presenter.h"

#include <QByteArray>
#include <QFileInfo>
#include <QThread>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "main_window.h"
#include "mediahub/core/media_types.h"

namespace mediahub::gui {
namespace {

constexpr std::array<double, 6> kPlaybackRates{0.5, 0.75, 1.0, 1.5, 2.0, 3.0};

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

std::string utf8String(const QString& text) {
  const QByteArray encoded = text.toUtf8();
  return std::string(encoded.constData(),
                     static_cast<std::size_t>(encoded.size()));
}

QString fromUtf8(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

bool isAudioFile(const QString& filePath) {
  const QString suffix = QFileInfo(filePath).suffix().toLower();
  return suffix == QStringLiteral("mp3") || suffix == QStringLiteral("wav") ||
         suffix == QStringLiteral("flac") || suffix == QStringLiteral("aac") ||
         suffix == QStringLiteral("m4a") || suffix == QStringLiteral("ogg") ||
         suffix == QStringLiteral("opus") || suffix == QStringLiteral("wma");
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

std::chrono::milliseconds boundedPosition(
    const std::chrono::milliseconds current,
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

std::chrono::milliseconds positionFromProgress(
    const int value, const std::optional<std::chrono::milliseconds> total) {
  if (!total.has_value() || total->count() <= 0) {
    return std::chrono::milliseconds::zero();
  }
  const int boundedValue = std::clamp(value, 0, kProgressMaximum);
  const long double target = static_cast<long double>(total->count()) *
                             boundedValue / kProgressMaximum;
  return std::chrono::milliseconds(
      static_cast<std::chrono::milliseconds::rep>(target));
}

bool isSeekAvailable(const core::PlaybackPosition& position,
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

}  // namespace

PlayerPresenter::PlayerPresenter(core::PlayerEngine& engine,
                                 EngineEventBridge& eventBridge,
                                 MainWindow& window, QObject* const parent,
                                 logging::Logger* const logger,
                                 LyricsService* const lyricsService)
    : QObject(parent),
      engine_(engine),
      eventBridge_(eventBridge),
      window_(window),
      logger_(logger),
      ownedLyricsService_(lyricsService == nullptr
                              ? std::make_unique<OnlineLyricsService>()
                              : nullptr),
      lyricsService_(lyricsService == nullptr ? ownedLyricsService_.get()
                                              : lyricsService),
      playlistModel_(playlist_) {
  window_.setPlaylistModel(&playlistModel_);
  connect(&window_, &MainWindow::localFilesSelected, this,
          &PlayerPresenter::addLocalFiles);
  connect(&window_, &MainWindow::networkUrlSelected, this,
          &PlayerPresenter::openNetworkUrl);
  connect(&window_, &MainWindow::playRequested, this,
          &PlayerPresenter::requestPlay);
  connect(&window_, &MainWindow::pauseRequested, this,
          &PlayerPresenter::requestPause);
  connect(&window_, &MainWindow::playbackToggleRequested, this,
          &PlayerPresenter::togglePlayback);
  connect(&window_, &MainWindow::stopRequested, this,
          &PlayerPresenter::requestStop);
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
  connect(&eventBridge_, &EngineEventBridge::audioWaveformChanged, this,
          &PlayerPresenter::handleAudioWaveformChanged, Qt::QueuedConnection);
  connect(&eventBridge_, &EngineEventBridge::endReached, this,
          &PlayerPresenter::handleEndReached, Qt::QueuedConnection);
  connect(&eventBridge_, &EngineEventBridge::errorOccurred, this,
          &PlayerPresenter::handleError, Qt::QueuedConnection);
  connect(lyricsService_, &LyricsService::resultReady, this,
          &PlayerPresenter::handleLyricsResult);

  engine_.setEventListener(&eventBridge_);
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "created");
  }
  render();
}

PlayerPresenter::~PlayerPresenter() { shutdown(); }

void PlayerPresenter::openLocalFile(const QString& filePath) {
  addLocalFiles(QStringList{filePath});
}

void PlayerPresenter::addLocalFiles(const QStringList& filePaths) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || filePaths.isEmpty()) {
    return;
  }

  std::vector<core::MediaItem> items;
  items.reserve(static_cast<std::size_t>(filePaths.size()));
  for (const auto& filePath : filePaths) {
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
  const std::size_t firstNewIndex = playlist_.size();
  playlist_.add(std::move(items));
  static_cast<void>(playlist_.select(firstNewIndex));
  playlistModel_.refresh();
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "media_batch_added",
                 {{"count", std::to_string(addedCount)}});
  }
  openCurrentPlaylistItem();
}

void PlayerPresenter::openNetworkUrl(const QString& url) {
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

  core::MediaItem item = core::makeMediaItem(source);
  const std::string displayName = item.displayName;
  const std::size_t newIndex = playlist_.size();
  playlist_.add(std::move(item));
  static_cast<void>(playlist_.select(newIndex));
  playlistModel_.refresh();
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter",
                 "network_media_added", {{"media", displayName}});
  }
  openCurrentPlaylistItem();
}

void PlayerPresenter::openCurrentPlaylistItem() {
  const auto* const item = playlist_.currentItem();
  if (item == nullptr) {
    return;
  }

  mediaName_ = fromUtf8(item->displayName);
  currentSourcePath_ = fromUtf8(item->source);
  lyricsService_->cancel();
  isLyricsVisible_ = false;
  isLyricsLoading_ = false;
  hasLyricsResult_ = false;
  window_.clearLyrics();
  isAutoPlayPending_ = true;
  isSeeking_ = false;
  seekPreviewPosition_.reset();
  position_ = {};
  isNetworkMedia_ = item->kind == core::MediaSourceKind::NetworkStream;
  isVideoMedia_ =
      isNetworkMedia_ ||
      !isAudioFile(QString::fromUtf8(item->source.data(),
                                     static_cast<int>(item->source.size())));
  isPreparingMedia_ = true;
  pendingRestartPosition_.reset();
  isRestartPlayRequested_ = false;
  lastAppliedPlaybackRate_ = 1.0;
  hasCurrentMediaStarted_ = false;
  window_.clearPlaybackError();
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "media_open_requested",
                 {{"media", item->displayName}});
  }
  render();
  engine_.open(*item);
}

void PlayerPresenter::shutdown() noexcept {
  if (isShuttingDown_) {
    return;
  }

  isShuttingDown_ = true;
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "shutdown");
  }
  isAutoPlayPending_ = false;
  isSeeking_ = false;
  pendingRestartPosition_.reset();
  isRestartPlayRequested_ = false;
  hasCurrentMediaStarted_ = false;
  seekPreviewPosition_.reset();
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

void PlayerPresenter::attachVideoSurface(void* const nativeHandle) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_) {
    engine_.setVideoSurface(nativeHandle);
  }
}

void PlayerPresenter::requestPlay() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ && makeViewState().canPlay) {
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
    isAutoPlayPending_ = false;
    isSeeking_ = false;
    pendingRestartPosition_.reset();
    isRestartPlayRequested_ = false;
    hasCurrentMediaStarted_ = false;
    seekPreviewPosition_.reset();
    engine_.stop();
  }
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
  position_.current = boundedPosition(target, position_.total);
  if (logger_ != nullptr) {
    logger_->log(logging::LogLevel::Info, "presenter", "seek_requested",
                 {{"position_ms", std::to_string(position_.current.count())}});
  }
  if (shouldRestart) {
    const auto* const item = playlist_.currentItem();
    if (item == nullptr) {
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
      engine_.open(*item);
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
  if (isShuttingDown_) {
    return;
  }

  const bool nextMuted = !isMuted_;
  engine_.setMuted(nextMuted);
  isMuted_ = nextMuted;
  render();
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
  if (!isShuttingDown_ && playlist_.selectPrevious()) {
    playlistModel_.refresh();
    openCurrentPlaylistItem();
  }
}

void PlayerPresenter::requestNext() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ && playlist_.selectNext()) {
    playlistModel_.refresh();
    openCurrentPlaylistItem();
  }
}

void PlayerPresenter::activatePlaylistItem(const int row) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_ && row >= 0 &&
      playlist_.select(static_cast<std::size_t>(row))) {
    playlistModel_.refresh();
    openCurrentPlaylistItem();
  }
}

void PlayerPresenter::removePlaylistItems(QList<int> rows) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || rows.isEmpty()) {
    return;
  }

  std::vector<std::size_t> indexes;
  indexes.reserve(static_cast<std::size_t>(rows.size()));
  for (const int row : rows) {
    if (row >= 0 && static_cast<std::size_t>(row) < playlist_.size()) {
      indexes.push_back(static_cast<std::size_t>(row));
    }
  }
  std::sort(indexes.begin(), indexes.end());
  indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
  if (indexes.empty()) {
    return;
  }

  const bool wasCurrent = playlist_.currentIndex().has_value() &&
                          std::binary_search(indexes.begin(), indexes.end(),
                                             *playlist_.currentIndex());
  for (auto index = indexes.rbegin(); index != indexes.rend(); ++index) {
    static_cast<void>(playlist_.remove(*index));
  }
  playlistModel_.refresh();
  if (!wasCurrent) {
    render();
    return;
  }
  if (!playlist_.empty()) {
    openCurrentPlaylistItem();
    return;
  }

  isAutoPlayPending_ = false;
  isSeeking_ = false;
  pendingRestartPosition_.reset();
  isRestartPlayRequested_ = false;
  seekPreviewPosition_.reset();
  position_ = {};
  mediaName_ = QStringLiteral("未选择媒体");
  isVideoMedia_ = false;
  isPreparingMedia_ = false;
  hasCurrentMediaStarted_ = false;
  engine_.stop();
  stateMachine_.reset();
  render();
}

void PlayerPresenter::movePlaylistItem(const int row, const int targetRow) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_ || row < 0 || targetRow < 0 ||
      !playlist_.moveItem(static_cast<std::size_t>(row),
                          static_cast<std::size_t>(targetRow))) {
    return;
  }
  playlistModel_.refresh();
  render();
}

void PlayerPresenter::renamePlaylistItem(const int row,
                                         const QString& displayName) {
  Q_ASSERT(QThread::currentThread() == thread());
  const QString normalizedName = displayName.trimmed();
  if (isShuttingDown_ || row < 0 || normalizedName.isEmpty() ||
      !playlist_.renameItem(static_cast<std::size_t>(row),
                            normalizedName.toUtf8().toStdString())) {
    return;
  }
  if (playlist_.currentIndex() == static_cast<std::size_t>(row)) {
    mediaName_ = normalizedName;
  }
  playlistModel_.refresh();
  render();
}

void PlayerPresenter::changePlaybackMode(const int modeIndex) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }

  switch (modeIndex) {
    case 1:
      playlist_.setMode(core::PlaybackMode::LoopAll);
      break;
    case 2:
      playlist_.setMode(core::PlaybackMode::LoopOne);
      break;
    case 3:
      playlist_.setMode(core::PlaybackMode::Shuffle);
      break;
    default:
      playlist_.setMode(core::PlaybackMode::Sequential);
      break;
  }
  playlistModel_.refresh();
  render();
}

void PlayerPresenter::handleStateChanged(const core::PlaybackState state) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (isShuttingDown_) {
    return;
  }

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
    isRestartPlayRequested_ = false;
  }

  const auto result = stateMachine_.transitionTo(state);
  if (result == core::PlaybackTransitionResult::Rejected) {
    return;
  }

  if (state == core::PlaybackState::Playing) {
    if (isNetworkMedia_) {
      // 网络音频输出可能在连接完成后才创建，进入 Playing 后同步一次界面状态。
      engine_.setVolume(volume_);
      engine_.setMuted(isMuted_);
    }
    const double effectiveRate = isTemporaryFastPlayback_ ? 2.0 : playbackRate_;
    applyPlaybackRate(effectiveRate);
    hasCurrentMediaStarted_ = true;
  } else if (state == core::PlaybackState::Stopped ||
             state == core::PlaybackState::Failed) {
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
  if (!isShuttingDown_) {
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
    position_ = std::move(position);
    if (!isSeeking_) {
      render();
    }
  }
}

void PlayerPresenter::handleDurationChanged(const OptionalDuration duration) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (!isShuttingDown_) {
    position_.total = duration;
    if (pendingRestartPosition_.has_value()) {
      position_.current =
          boundedPosition(*pendingRestartPosition_, position_.total);
    } else if (stateMachine_.state() == core::PlaybackState::Ended &&
               duration.has_value()) {
      position_.current = *duration;
    }
    if (!isSeeking_) {
      render();
    }
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
  if (isShuttingDown_ || !hasCurrentMediaStarted_) {
    return;
  }

  hasCurrentMediaStarted_ = false;
  pendingRestartPosition_.reset();
  isRestartPlayRequested_ = false;
  if (playlist_.advanceAfterEnd()) {
    playlistModel_.refresh();
    openCurrentPlaylistItem();
    return;
  }

  if (position_.total.has_value()) {
    position_.current = *position_.total;
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
  if (isShuttingDown_) {
    return;
  }

  isAutoPlayPending_ = false;
  isSeeking_ = false;
  pendingRestartPosition_.reset();
  isRestartPlayRequested_ = false;
  seekPreviewPosition_.reset();
  isPreparingMedia_ = false;
  hasCurrentMediaStarted_ = false;
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

void PlayerPresenter::render() {
  Q_ASSERT(QThread::currentThread() == thread());
  window_.applyViewState(makeViewState());
}

PlayerViewState PlayerPresenter::makeViewState() const {
  PlayerViewState viewState;
  viewState.mediaName = mediaName_;
  viewState.videoPlaceholder = QStringLiteral("打开媒体后，画面会出现在这里");
  const auto displayedPosition =
      seekPreviewPosition_.value_or(position_.current);
  viewState.positionText = formatPosition(displayedPosition, position_.total);
  viewState.progressValue = progressValue(displayedPosition, position_.total);
  viewState.positionMilliseconds =
      std::max<qint64>(static_cast<qint64>(displayedPosition.count()), 0);
  viewState.volumeValue = isMuted_ ? 0 : volume_;
  viewState.isMuted = isMuted_;
  viewState.volumeText = isMuted_ ? QStringLiteral("已静音 · 0%")
                                  : QStringLiteral("音量 %1%").arg(volume_);
  viewState.currentPlaylistIndex =
      playlist_.currentIndex().has_value()
          ? static_cast<int>(*playlist_.currentIndex())
          : -1;
  viewState.playbackModeIndex = static_cast<int>(playlist_.mode());
  viewState.playbackRate = playbackRate_;
  viewState.isTemporaryFastPlayback = isTemporaryFastPlayback_;
  viewState.canGoPrevious = playlist_.previousIndex().has_value();
  viewState.canGoNext = playlist_.nextIndex().has_value();
  viewState.canRemovePlaylistItem = playlist_.currentIndex().has_value();

  switch (stateMachine_.state()) {
    case core::PlaybackState::Idle:
      viewState.statusText = QStringLiteral("未打开媒体");
      break;
    case core::PlaybackState::Opening:
      viewState.statusText = QStringLiteral("正在打开...");
      viewState.canStop = true;
      break;
    case core::PlaybackState::Buffering:
      viewState.statusText = QStringLiteral("正在缓冲...");
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
      viewState.statusText = QStringLiteral("已停止");
      viewState.canPlay = true;
      break;
    case core::PlaybackState::Ended:
      viewState.statusText = QStringLiteral("播放结束");
      viewState.canPlay = true;
      break;
    case core::PlaybackState::Failed:
      viewState.statusText = QStringLiteral("播放失败");
      break;
  }

  viewState.canSeek = isSeekAvailable(position_, stateMachine_.state());

  if (mediaName_ == QStringLiteral("未选择媒体")) {
    return viewState;
  }

  const auto playbackState = stateMachine_.state();
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

}  // namespace mediahub::gui
