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
        engine_.stop();
    } catch (...) {
        // 关闭阶段不能让第三方内核异常越过 Qt 事件循环。
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

    return viewState;
}

}  // namespace mediahub::gui
