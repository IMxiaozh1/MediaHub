#include "player_presenter.h"

#include "main_window.h"
#include "mediahub/core/media_types.h"

#include <QByteArray>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace mediahub::gui {
namespace {

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

std::string utf8String(const QString& text) {
    const QByteArray encoded = text.toUtf8();
    return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
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
    const auto totalSeconds = std::max<std::chrono::milliseconds::rep>(
                                  value.count(), 0) /
                              1000;
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
        bounded = std::min(bounded, std::max(*total, std::chrono::milliseconds::zero()));
    }
    return bounded;
}

QString formatPosition(const std::chrono::milliseconds current,
                       const std::optional<std::chrono::milliseconds> total) {
    return QStringLiteral("%1 / %2")
        .arg(formatTime(boundedPosition(current, total)),
             total.has_value() ? formatTime(*total) : QStringLiteral("--:--"));
}

int progressValue(const std::chrono::milliseconds current,
                  const std::optional<std::chrono::milliseconds> total) {
    if (!total.has_value() || total->count() <= 0) {
        return 0;
    }
    const long double ratio = static_cast<long double>(boundedPosition(current, total).count()) /
                              static_cast<long double>(total->count());
    return std::clamp(static_cast<int>(ratio * kProgressMaximum), 0, kProgressMaximum);
}

std::chrono::milliseconds positionFromProgress(
    const int value, const std::optional<std::chrono::milliseconds> total) {
    if (!total.has_value() || total->count() <= 0) {
        return std::chrono::milliseconds::zero();
    }
    const int boundedValue = std::clamp(value, 0, kProgressMaximum);
    const long double target = static_cast<long double>(total->count()) * boundedValue /
                               kProgressMaximum;
    return std::chrono::milliseconds(
        static_cast<std::chrono::milliseconds::rep>(target));
}

}  // namespace

PlayerPresenter::PlayerPresenter(core::PlayerEngine& engine,
                                 EngineEventBridge& eventBridge,
                                 MainWindow& window,
                                 QObject* const parent,
                                 logging::Logger* const logger)
    : QObject(parent),
      engine_(engine),
      eventBridge_(eventBridge),
      window_(window),
      logger_(logger),
      playlistModel_(playlist_) {
    window_.setPlaylistModel(&playlistModel_);
    connect(&window_, &MainWindow::localFilesSelected, this,
            &PlayerPresenter::addLocalFiles);
    connect(&window_, &MainWindow::playRequested, this, &PlayerPresenter::requestPlay);
    connect(&window_, &MainWindow::pauseRequested, this, &PlayerPresenter::requestPause);
    connect(&window_, &MainWindow::stopRequested, this, &PlayerPresenter::requestStop);
    connect(&window_, &MainWindow::seekStarted, this, &PlayerPresenter::beginSeek);
    connect(&window_, &MainWindow::seekPreviewRequested, this,
            &PlayerPresenter::previewSeek);
    connect(&window_, &MainWindow::seekRequested, this, &PlayerPresenter::commitSeek);
    connect(&window_, &MainWindow::volumeRequested, this,
            &PlayerPresenter::requestVolume);
    connect(&window_, &MainWindow::muteToggled, this, &PlayerPresenter::toggleMuted);
    connect(&window_, &MainWindow::previousRequested, this,
            &PlayerPresenter::requestPrevious);
    connect(&window_, &MainWindow::nextRequested, this, &PlayerPresenter::requestNext);
    connect(&window_, &MainWindow::playlistItemActivated, this,
            &PlayerPresenter::activatePlaylistItem);
    connect(&window_, &MainWindow::removePlaylistItemRequested, this,
            &PlayerPresenter::removePlaylistItem);
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
    connect(&eventBridge_, &EngineEventBridge::endReached, this,
            &PlayerPresenter::handleEndReached, Qt::QueuedConnection);
    connect(&eventBridge_, &EngineEventBridge::errorOccurred, this,
            &PlayerPresenter::handleError, Qt::QueuedConnection);

    engine_.setEventListener(&eventBridge_);
    if (logger_ != nullptr) {
        logger_->log(logging::LogLevel::Info, "presenter", "created");
    }
    render();
}

PlayerPresenter::~PlayerPresenter() {
    shutdown();
}

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
        const QString displayName = fileInfo.fileName().isEmpty() ? filePath
                                                                  : fileInfo.fileName();
        items.push_back(core::makeMediaItem(utf8String(filePath), utf8String(displayName)));
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
        logger_->log(logging::LogLevel::Info,
                     "presenter",
                     "media_batch_added",
                     {{"count", std::to_string(addedCount)}});
    }
    openCurrentPlaylistItem();
}

void PlayerPresenter::openCurrentPlaylistItem() {
    const auto* const item = playlist_.currentItem();
    if (item == nullptr) {
        return;
    }

    mediaName_ = fromUtf8(item->displayName);
    isAutoPlayPending_ = true;
    isSeeking_ = false;
    seekPreviewPosition_.reset();
    position_ = {};
    isVideoMedia_ = !isAudioFile(QString::fromUtf8(
        item->source.data(), static_cast<int>(item->source.size())));
    isPreparingMedia_ = true;
    window_.clearPlaybackError();
    if (logger_ != nullptr) {
        logger_->log(logging::LogLevel::Info,
                     "presenter",
                     "media_open_requested",
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
    seekPreviewPosition_.reset();
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
        engine_.play();
    }
}

void PlayerPresenter::requestPause() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!isShuttingDown_ && makeViewState().canPause) {
        engine_.pause();
    }
}

void PlayerPresenter::requestStop() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!isShuttingDown_ && makeViewState().canStop) {
        isAutoPlayPending_ = false;
        isSeeking_ = false;
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

    const bool isPlaybackSeekable =
        position_.isSeekable && position_.total.has_value() && position_.total->count() > 0 &&
        (stateMachine_.state() == core::PlaybackState::Playing ||
         stateMachine_.state() == core::PlaybackState::Paused);
    if (!isPlaybackSeekable) {
        isSeeking_ = false;
        seekPreviewPosition_.reset();
        render();
        return;
    }

    const auto target = positionFromProgress(progress, position_.total);
    isSeeking_ = false;
    seekPreviewPosition_.reset();
    position_.current = target;
    if (logger_ != nullptr) {
        logger_->log(logging::LogLevel::Info,
                     "presenter",
                     "seek_requested",
                     {{"position_ms", std::to_string(target.count())}});
    }
    render();
    engine_.seek(target);
}

void PlayerPresenter::requestVolume(const int volume) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (isShuttingDown_) {
        return;
    }

    const int boundedVolume = std::clamp(volume, 0, 100);
    engine_.setVolume(boundedVolume);
    volume_ = boundedVolume;
    render();
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
    if (!isShuttingDown_ && row >= 0 && playlist_.select(static_cast<std::size_t>(row))) {
        playlistModel_.refresh();
        openCurrentPlaylistItem();
    }
}

void PlayerPresenter::removePlaylistItem(const int row) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (isShuttingDown_ || row < 0) {
        return;
    }

    const bool wasCurrent = playlist_.currentIndex() == static_cast<std::size_t>(row);
    if (!playlist_.remove(static_cast<std::size_t>(row))) {
        return;
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
    seekPreviewPosition_.reset();
    position_ = {};
    mediaName_ = QStringLiteral("未选择媒体");
    isVideoMedia_ = false;
    isPreparingMedia_ = false;
    engine_.stop();
    stateMachine_.reset();
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

    if (state == core::PlaybackState::Opening || state == core::PlaybackState::Failed) {
        isPreparingMedia_ = false;
    }
    if (state == core::PlaybackState::Stopped || state == core::PlaybackState::Ended ||
        state == core::PlaybackState::Failed) {
        isSeeking_ = false;
        seekPreviewPosition_.reset();
    }

    const auto result = stateMachine_.transitionTo(state);
    if (result == core::PlaybackTransitionResult::Rejected) {
        return;
    }

    render();
    if (logger_ != nullptr) {
        logger_->log(logging::LogLevel::Info,
                     "presenter",
                     "state_changed",
                     {{"state", stateName(state)}});
    }
    emit stateApplied(stateMachine_.state());

    if (state == core::PlaybackState::Opening && isAutoPlayPending_) {
        isAutoPlayPending_ = false;
        engine_.play();
    }
}

void PlayerPresenter::handlePositionChanged(core::PlaybackPosition position) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!isShuttingDown_) {
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
        if (!isSeeking_) {
            render();
        }
    }
}

void PlayerPresenter::handleEndReached() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (isShuttingDown_) {
        return;
    }

    if (playlist_.advanceAfterEnd()) {
        playlistModel_.refresh();
        openCurrentPlaylistItem();
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
    if (isShuttingDown_) {
        return;
    }

    isAutoPlayPending_ = false;
    isSeeking_ = false;
    seekPreviewPosition_.reset();
    isPreparingMedia_ = false;
    if (logger_ != nullptr) {
        logger_->log(logging::LogLevel::Error,
                     "presenter",
                     "media_error",
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
    const auto displayedPosition = seekPreviewPosition_.value_or(position_.current);
    viewState.positionText = formatPosition(displayedPosition, position_.total);
    viewState.progressValue = progressValue(displayedPosition, position_.total);
    viewState.volumeValue = volume_;
    viewState.isMuted = isMuted_;
    viewState.volumeText = isMuted_
                               ? QStringLiteral("已静音 · %1%").arg(volume_)
                               : QStringLiteral("音量 %1%").arg(volume_);
    viewState.currentPlaylistIndex = playlist_.currentIndex().has_value()
                                         ? static_cast<int>(*playlist_.currentIndex())
                                         : -1;
    viewState.playbackModeIndex = static_cast<int>(playlist_.mode());
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

    viewState.canSeek = position_.isSeekable && position_.total.has_value() &&
                        position_.total->count() > 0 &&
                        (stateMachine_.state() == core::PlaybackState::Playing ||
                         stateMachine_.state() == core::PlaybackState::Paused);

    if (mediaName_ == QStringLiteral("未选择媒体")) {
        return viewState;
    }
    if (!isVideoMedia_) {
        viewState.videoPlaceholder = QStringLiteral("音频播放模式\n当前媒体没有视频画面");
        return viewState;
    }

    viewState.canToggleFullscreen = !isPreparingMedia_ &&
                                    stateMachine_.state() != core::PlaybackState::Failed &&
                                    stateMachine_.state() != core::PlaybackState::Stopped &&
                                    stateMachine_.state() != core::PlaybackState::Ended;
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
