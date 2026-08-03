#include "player_presenter.h"

#include "main_window.h"
#include "mediahub/core/media_types.h"

#include <QByteArray>
#include <QFileInfo>
#include <QThread>

#include <string>
#include <utility>

namespace mediahub::gui {
namespace {

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

}  // namespace

PlayerPresenter::PlayerPresenter(core::PlayerEngine& engine,
                                 EngineEventBridge& eventBridge,
                                 MainWindow& window,
                                 QObject* const parent)
    : QObject(parent), engine_(engine), eventBridge_(eventBridge), window_(window) {
    connect(&window_, &MainWindow::localFileSelected, this,
            &PlayerPresenter::openLocalFile);
    connect(&window_, &MainWindow::playRequested, this, &PlayerPresenter::requestPlay);
    connect(&window_, &MainWindow::pauseRequested, this, &PlayerPresenter::requestPause);
    connect(&window_, &MainWindow::stopRequested, this, &PlayerPresenter::requestStop);
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
    render();
}

PlayerPresenter::~PlayerPresenter() {
    shutdown();
}

void PlayerPresenter::openLocalFile(const QString& filePath) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (isShuttingDown_ || filePath.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(filePath);
    mediaName_ = fileInfo.fileName().isEmpty() ? filePath : fileInfo.fileName();
    isAutoPlayPending_ = true;
    isVideoMedia_ = !isAudioFile(filePath);
    isPreparingMedia_ = true;
    window_.clearPlaybackError();
    render();
    engine_.open(core::makeMediaItem(utf8String(filePath), utf8String(mediaName_)));
}

void PlayerPresenter::shutdown() noexcept {
    if (isShuttingDown_) {
        return;
    }

    isShuttingDown_ = true;
    isAutoPlayPending_ = false;
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
        engine_.stop();
    }
}

void PlayerPresenter::handleStateChanged(const core::PlaybackState state) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (isShuttingDown_) {
        return;
    }

    if (state == core::PlaybackState::Opening || state == core::PlaybackState::Failed) {
        isPreparingMedia_ = false;
    }

    const auto result = stateMachine_.transitionTo(state);
    if (result == core::PlaybackTransitionResult::Rejected) {
        return;
    }

    render();
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
    }
}

void PlayerPresenter::handleDurationChanged(const OptionalDuration duration) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!isShuttingDown_) {
        position_.total = duration;
    }
}

void PlayerPresenter::handleEndReached() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (isShuttingDown_) {
        return;
    }

    const auto result = stateMachine_.transitionTo(core::PlaybackState::Ended);
    if (result != core::PlaybackTransitionResult::Rejected) {
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
    isPreparingMedia_ = false;
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
