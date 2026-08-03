#include "main_window.h"

#include "video_output_widget.h"

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

namespace mediahub::gui {
namespace {

constexpr int kNormalHorizontalMargin = 36;
constexpr int kNormalVerticalMargin = 28;
constexpr int kNormalSpacing = 16;

}  // namespace

MainWindow::MainWindow(QWidget* const parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("MediaHub"));
    resize(960, 720);
    setMinimumSize(760, 640);

    auto* const fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    openAction_ = fileMenu->addAction(QStringLiteral("打开媒体文件(&O)..."));
    openAction_->setObjectName(QStringLiteral("openFileAction"));
    openAction_->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    auto* const exitAction = fileMenu->addAction(QStringLiteral("退出(&X)"));
    exitAction->setShortcut(QKeySequence::Quit);
    auto* const viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    fullScreenAction_ = viewMenu->addAction(QStringLiteral("进入全屏(&F)"));
    fullScreenAction_->setObjectName(QStringLiteral("fullScreenAction"));
    fullScreenAction_->setShortcut(QKeySequence(Qt::Key_F11));
    auto* const exitFullScreenAction = new QAction(this);
    exitFullScreenAction->setShortcut(QKeySequence(Qt::Key_Escape));
    addAction(exitFullScreenAction);

    auto* const centralWidget = new QWidget(this);
    centralWidget->setObjectName(QStringLiteral("centralSurface"));
    rootLayout_ = new QVBoxLayout(centralWidget);
    rootLayout_->setContentsMargins(kNormalHorizontalMargin,
                                    kNormalVerticalMargin,
                                    kNormalHorizontalMargin,
                                    kNormalVerticalMargin);
    rootLayout_->setSpacing(kNormalSpacing);

    auto* const eyebrow = new QLabel(QStringLiteral("LOCAL MEDIA / 01"), centralWidget);
    eyebrow->setObjectName(QStringLiteral("eyebrowLabel"));
    auto* const title = new QLabel(QStringLiteral("让本地声音重新流动"), centralWidget);
    title->setObjectName(QStringLiteral("titleLabel"));
    auto* const subtitle = new QLabel(
        QStringLiteral("打开本地音视频，声音与画面都留在你的设备上。"), centralWidget);
    subtitle->setObjectName(QStringLiteral("subtitleLabel"));
    rootLayout_->addWidget(eyebrow);
    rootLayout_->addWidget(title);
    rootLayout_->addWidget(subtitle);

    videoOutput_ = new VideoOutputWidget(centralWidget);
    rootLayout_->addWidget(videoOutput_, 1);

    auto* const mediaCard = new QFrame(centralWidget);
    mediaCard->setObjectName(QStringLiteral("mediaCard"));
    auto* const cardLayout = new QVBoxLayout(mediaCard);
    cardLayout->setContentsMargins(28, 24, 28, 24);
    cardLayout->setSpacing(14);

    auto* const mediaCaption = new QLabel(QStringLiteral("当前媒体"), mediaCard);
    mediaCaption->setObjectName(QStringLiteral("captionLabel"));
    mediaNameLabel_ = new QLabel(QStringLiteral("未选择媒体"), mediaCard);
    mediaNameLabel_->setObjectName(QStringLiteral("currentMediaLabel"));
    mediaNameLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mediaNameLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    statusLabel_ = new QLabel(QStringLiteral("未打开媒体"), mediaCard);
    statusLabel_->setObjectName(QStringLiteral("playbackStatusLabel"));
    statusLabel_->setAlignment(Qt::AlignCenter);

    auto* const mediaRow = new QHBoxLayout();
    mediaRow->setSpacing(18);
    mediaRow->addWidget(mediaNameLabel_, 1);
    mediaRow->addWidget(statusLabel_);
    cardLayout->addWidget(mediaCaption);
    cardLayout->addLayout(mediaRow);

    errorLabel_ = new QLabel(mediaCard);
    errorLabel_->setObjectName(QStringLiteral("playbackErrorLabel"));
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();
    cardLayout->addWidget(errorLabel_);
    rootLayout_->addWidget(mediaCard);

    auto* const transportPanel = new QFrame(centralWidget);
    transportPanel->setObjectName(QStringLiteral("transportPanel"));
    auto* const transportLayout = new QVBoxLayout(transportPanel);
    transportLayout->setContentsMargins(20, 14, 20, 14);
    transportLayout->setSpacing(10);

    auto* const timelineHeader = new QHBoxLayout();
    auto* const progressCaption = new QLabel(QStringLiteral("播放进度"), transportPanel);
    progressCaption->setObjectName(QStringLiteral("transportCaptionLabel"));
    positionLabel_ = new QLabel(QStringLiteral("00:00 / --:--"), transportPanel);
    positionLabel_->setObjectName(QStringLiteral("positionLabel"));
    timelineHeader->addWidget(progressCaption);
    timelineHeader->addStretch(1);
    timelineHeader->addWidget(positionLabel_);
    transportLayout->addLayout(timelineHeader);

    progressSlider_ = new QSlider(Qt::Horizontal, transportPanel);
    progressSlider_->setObjectName(QStringLiteral("progressSlider"));
    progressSlider_->setAccessibleName(QStringLiteral("播放进度"));
    progressSlider_->setRange(0, kProgressMaximum);
    progressSlider_->setPageStep(50);
    progressSlider_->setEnabled(false);
    transportLayout->addWidget(progressSlider_);

    auto* const volumeRow = new QHBoxLayout();
    volumeRow->setSpacing(12);
    volumeLabel_ = new QLabel(QStringLiteral("音量 100%"), transportPanel);
    volumeLabel_->setObjectName(QStringLiteral("volumeLabel"));
    volumeSlider_ = new QSlider(Qt::Horizontal, transportPanel);
    volumeSlider_->setObjectName(QStringLiteral("volumeSlider"));
    volumeSlider_->setAccessibleName(QStringLiteral("音量"));
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setSingleStep(1);
    volumeSlider_->setPageStep(10);
    volumeSlider_->setValue(100);
    muteButton_ = new QPushButton(QStringLiteral("静音"), transportPanel);
    muteButton_->setObjectName(QStringLiteral("muteButton"));
    volumeRow->addWidget(volumeLabel_);
    volumeRow->addWidget(volumeSlider_, 1);
    volumeRow->addWidget(muteButton_);
    transportLayout->addLayout(volumeRow);
    rootLayout_->addWidget(transportPanel);

    auto* const controls = new QHBoxLayout();
    controls->setSpacing(12);
    openButton_ = new QPushButton(QStringLiteral("打开文件"), centralWidget);
    openButton_->setObjectName(QStringLiteral("openFileButton"));
    playButton_ = new QPushButton(QStringLiteral("播放"), centralWidget);
    playButton_->setObjectName(QStringLiteral("playButton"));
    playButton_->setProperty("primary", true);
    pauseButton_ = new QPushButton(QStringLiteral("暂停"), centralWidget);
    pauseButton_->setObjectName(QStringLiteral("pauseButton"));
    stopButton_ = new QPushButton(QStringLiteral("停止"), centralWidget);
    stopButton_->setObjectName(QStringLiteral("stopButton"));
    fullScreenButton_ = new QPushButton(QStringLiteral("全屏"), centralWidget);
    fullScreenButton_->setObjectName(QStringLiteral("fullScreenButton"));
    controls->addWidget(openButton_);
    controls->addStretch(1);
    controls->addWidget(playButton_);
    controls->addWidget(pauseButton_);
    controls->addWidget(stopButton_);
    controls->addWidget(fullScreenButton_);
    rootLayout_->addLayout(controls);
    fullScreenChrome_ = {eyebrow,
                         title,
                         subtitle,
                         mediaCard,
                         transportPanel,
                         openButton_,
                         playButton_,
                         pauseButton_,
                         stopButton_,
                         fullScreenButton_};

    setCentralWidget(centralWidget);
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#centralSurface {
            background: #f4f0e6;
            color: #173c3a;
        }
        QMenuBar {
            background: #f4f0e6;
            color: #173c3a;
            padding: 4px 8px;
        }
        QMenuBar::item:selected, QMenu::item:selected {
            background: #dce7df;
        }
        QLabel#eyebrowLabel {
            color: #cc5a36;
            font-family: "Bahnschrift SemiCondensed";
            font-size: 13px;
            font-weight: 700;
        }
        QLabel#titleLabel {
            color: #102f2d;
            font-family: "Microsoft YaHei UI";
            font-size: 34px;
            font-weight: 700;
        }
        QLabel#subtitleLabel {
            color: #57706b;
            font-family: "Microsoft YaHei UI";
            font-size: 14px;
        }
        QFrame#mediaCard {
            background: #fffdf7;
            border: 1px solid #d9d4c8;
            border-left: 5px solid #1f7770;
            border-radius: 8px;
        }
        QLabel#captionLabel {
            color: #778984;
            font-size: 12px;
            font-weight: 700;
        }
        QLabel#currentMediaLabel {
            color: #173c3a;
            font-size: 20px;
            font-weight: 600;
        }
        QLabel#playbackStatusLabel {
            background: #dce7df;
            border-radius: 12px;
            color: #1f625d;
            font-size: 12px;
            font-weight: 700;
            min-width: 92px;
            padding: 6px 12px;
        }
        QLabel#playbackErrorLabel {
            background: #fae7df;
            border-radius: 5px;
            color: #983f28;
            padding: 9px 12px;
        }
        QFrame#transportPanel {
            background: #fffdf7;
            border: 1px solid #d9d4c8;
            border-radius: 8px;
        }
        QLabel#transportCaptionLabel, QLabel#positionLabel, QLabel#volumeLabel {
            color: #49645f;
            font-size: 12px;
            font-weight: 700;
        }
        QSlider::groove:horizontal {
            background: #d9e1dc;
            border-radius: 3px;
            height: 6px;
        }
        QSlider::sub-page:horizontal {
            background: #cc5a36;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #fffdf7;
            border: 2px solid #1f7770;
            border-radius: 7px;
            margin: -5px 0;
            width: 14px;
        }
        QSlider::groove:horizontal:disabled,
        QSlider::sub-page:horizontal:disabled {
            background: #e2dfd7;
        }
        QPushButton {
            background: #fffdf7;
            border: 1px solid #aebdb7;
            border-radius: 6px;
            color: #173c3a;
            font-size: 14px;
            font-weight: 600;
            min-width: 88px;
            padding: 10px 18px;
        }
        QPushButton:hover:enabled {
            background: #e6eee9;
            border-color: #1f7770;
        }
        QPushButton[primary="true"] {
            background: #1f7770;
            border-color: #1f7770;
            color: #ffffff;
        }
        QPushButton[primary="true"]:hover:enabled {
            background: #185f5a;
        }
        QPushButton:disabled {
            background: #e9e5dc;
            border-color: #d4cfc5;
            color: #a09d95;
        }
        QPushButton#muteButton {
            min-width: 76px;
            padding: 7px 12px;
        }
    )"));

    connect(openAction_, &QAction::triggered, this, &MainWindow::chooseLocalFile);
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::chooseLocalFile);
    connect(playButton_, &QPushButton::clicked, this, &MainWindow::playRequested);
    connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::pauseRequested);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopRequested);
    connect(progressSlider_, &QSlider::sliderPressed, this, &MainWindow::seekStarted);
    connect(progressSlider_, &QSlider::sliderMoved, this,
            &MainWindow::seekPreviewRequested);
    connect(progressSlider_, &QSlider::sliderReleased, this, [this] {
        emit seekRequested(progressSlider_->value());
    });
    connect(progressSlider_, &QSlider::valueChanged, this, [this](const int value) {
        if (!progressSlider_->isSliderDown()) {
            emit seekRequested(value);
        }
    });
    connect(volumeSlider_, &QSlider::valueChanged, this, &MainWindow::volumeRequested);
    connect(muteButton_, &QPushButton::clicked, this, &MainWindow::muteToggled);
    connect(fullScreenButton_, &QPushButton::clicked, this, &MainWindow::toggleFullScreen);
    connect(fullScreenAction_, &QAction::triggered, this, &MainWindow::toggleFullScreen);
    connect(exitFullScreenAction, &QAction::triggered, this, &MainWindow::exitFullScreen);
    connect(videoOutput_, &VideoOutputWidget::surfaceReady, this,
            &MainWindow::videoSurfaceReady);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);

    PlayerViewState initialState;
    initialState.mediaName = QStringLiteral("未选择媒体");
    initialState.statusText = QStringLiteral("未打开媒体");
    applyViewState(initialState);
}

void MainWindow::applyViewState(const PlayerViewState& viewState) {
    const QSignalBlocker progressBlocker(progressSlider_);
    const QSignalBlocker volumeBlocker(volumeSlider_);
    openAction_->setEnabled(viewState.canOpen);
    openButton_->setEnabled(viewState.canOpen);
    playButton_->setEnabled(viewState.canPlay);
    pauseButton_->setEnabled(viewState.canPause);
    stopButton_->setEnabled(viewState.canStop);
    progressSlider_->setEnabled(viewState.canSeek);
    progressSlider_->setValue(viewState.progressValue);
    volumeSlider_->setValue(viewState.volumeValue);
    fullScreenAction_->setEnabled(viewState.canToggleFullscreen);
    fullScreenButton_->setEnabled(viewState.canToggleFullscreen);
    mediaNameLabel_->setText(viewState.mediaName);
    statusLabel_->setText(viewState.statusText);
    positionLabel_->setText(viewState.positionText);
    volumeLabel_->setText(viewState.volumeText);
    muteButton_->setText(viewState.isMuted ? QStringLiteral("取消静音")
                                           : QStringLiteral("静音"));
    videoOutput_->setPresentation(viewState.isVideoSurfaceActive,
                                  viewState.videoPlaceholder);
    if (!viewState.canToggleFullscreen && isFullScreen()) {
        showNormal();
    }
}

void MainWindow::showPlaybackError(const QString& message) {
    errorLabel_->setText(message);
    errorLabel_->setVisible(!message.isEmpty());
}

void MainWindow::clearPlaybackError() {
    errorLabel_->clear();
    errorLabel_->hide();
}

void MainWindow::closeEvent(QCloseEvent* const event) {
    emit closing();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* const event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateFullScreenText();
    }
}

void MainWindow::chooseLocalFile() {
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开本地媒体"),
        {},
        QStringLiteral("媒体文件 (*.mp3 *.wav *.flac *.aac *.m4a *.ogg *.mp4 *.mkv "
                       "*.avi *.mov *.webm);;所有文件 (*.*)"));
    if (!filePath.isEmpty()) {
        emit localFileSelected(filePath);
    }
}

void MainWindow::toggleFullScreen() {
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
    updateFullScreenText();
}

void MainWindow::exitFullScreen() {
    if (isFullScreen()) {
        showNormal();
        updateFullScreenText();
    }
}

void MainWindow::updateFullScreenText() {
    if (fullScreenAction_ == nullptr || fullScreenButton_ == nullptr) {
        return;
    }

    const bool isNowFullScreen = isFullScreen();
    menuBar()->setVisible(!isNowFullScreen);
    for (auto* const widget : fullScreenChrome_) {
        widget->setVisible(!isNowFullScreen);
    }
    if (isNowFullScreen) {
        rootLayout_->setContentsMargins(0, 0, 0, 0);
        rootLayout_->setSpacing(0);
    } else {
        rootLayout_->setContentsMargins(kNormalHorizontalMargin,
                                        kNormalVerticalMargin,
                                        kNormalHorizontalMargin,
                                        kNormalVerticalMargin);
        rootLayout_->setSpacing(kNormalSpacing);
    }

    const QString actionText = isNowFullScreen ? QStringLiteral("退出全屏(&F)")
                                               : QStringLiteral("进入全屏(&F)");
    fullScreenAction_->setText(actionText);
    fullScreenButton_->setText(isNowFullScreen ? QStringLiteral("退出全屏")
                                               : QStringLiteral("全屏"));
}

}  // namespace mediahub::gui
