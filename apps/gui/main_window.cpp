#include "main_window.h"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

namespace mediahub::gui {

MainWindow::MainWindow(QWidget* const parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("MediaHub"));
    resize(960, 600);
    setMinimumSize(760, 480);

    auto* const fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    openAction_ = fileMenu->addAction(QStringLiteral("打开媒体文件(&O)..."));
    openAction_->setObjectName(QStringLiteral("openFileAction"));
    openAction_->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    auto* const exitAction = fileMenu->addAction(QStringLiteral("退出(&X)"));
    exitAction->setShortcut(QKeySequence::Quit);

    auto* const centralWidget = new QWidget(this);
    centralWidget->setObjectName(QStringLiteral("centralSurface"));
    auto* const rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(48, 42, 48, 42);
    rootLayout->setSpacing(24);

    auto* const eyebrow = new QLabel(QStringLiteral("LOCAL MEDIA / 01"), centralWidget);
    eyebrow->setObjectName(QStringLiteral("eyebrowLabel"));
    auto* const title = new QLabel(QStringLiteral("让本地声音重新流动"), centralWidget);
    title->setObjectName(QStringLiteral("titleLabel"));
    auto* const subtitle = new QLabel(
        QStringLiteral("打开一段本地音频，MediaHub 会在这里接管播放。"), centralWidget);
    subtitle->setObjectName(QStringLiteral("subtitleLabel"));
    rootLayout->addWidget(eyebrow);
    rootLayout->addWidget(title);
    rootLayout->addWidget(subtitle);

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
    rootLayout->addWidget(mediaCard);

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
    controls->addWidget(openButton_);
    controls->addStretch(1);
    controls->addWidget(playButton_);
    controls->addWidget(pauseButton_);
    controls->addWidget(stopButton_);
    rootLayout->addLayout(controls);
    rootLayout->addStretch(1);

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
    )"));

    connect(openAction_, &QAction::triggered, this, &MainWindow::chooseLocalFile);
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::chooseLocalFile);
    connect(playButton_, &QPushButton::clicked, this, &MainWindow::playRequested);
    connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::pauseRequested);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopRequested);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);

    applyViewState(PlayerViewState{QStringLiteral("未选择媒体"),
                                   QStringLiteral("未打开媒体")});
}

void MainWindow::applyViewState(const PlayerViewState& viewState) {
    openAction_->setEnabled(viewState.canOpen);
    openButton_->setEnabled(viewState.canOpen);
    playButton_->setEnabled(viewState.canPlay);
    pauseButton_->setEnabled(viewState.canPause);
    stopButton_->setEnabled(viewState.canStop);
    mediaNameLabel_->setText(viewState.mediaName);
    statusLabel_->setText(viewState.statusText);
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

}  // namespace mediahub::gui
