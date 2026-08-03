#include "engine_event_bridge.h"
#include "fakes/fake_player_engine.h"
#include "main_window.h"
#include "player_presenter.h"
#include "video_output_widget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QListView>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QTest>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <thread>

namespace mediahub::gui {
namespace {

using namespace std::chrono_literals;

struct GuiHarness {
    test::FakePlayerEngine engine;
    EngineEventBridge eventBridge;
    MainWindow window;
    PlayerPresenter presenter;

    GuiHarness() : presenter(engine, eventBridge, window) {}
};

template <typename Widget>
Widget* requiredChild(MainWindow& window, const char* const objectName) {
    auto* const child = window.findChild<Widget*>(QString::fromLatin1(objectName));
    Q_ASSERT(child != nullptr);
    return child;
}

QString statusText(GuiHarness& harness) {
    return requiredChild<QLabel>(harness.window, "playbackStatusLabel")->text();
}

QRect geometryInsideWindow(QWidget& widget, MainWindow& window) {
    return QRect(widget.mapTo(&window, QPoint(0, 0)), widget.size());
}

void openAndReachPlaying(GuiHarness& harness) {
    harness.presenter.openLocalFile(QStringLiteral("C:/媒体 库/测试 音频.wav"));
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()), 2);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));
}

bool hasSurfaceCommand(const GuiHarness& harness, const bool expectsNullHandle) {
    const auto commands = harness.engine.commands();
    return std::any_of(commands.begin(), commands.end(), [expectsNullHandle](const auto& command) {
        return command.kind == test::FakeEngineCommandKind::SetVideoSurface &&
               (command.nativeHandle == nullptr) == expectsNullHandle;
    });
}

int commandCount(const GuiHarness& harness, const test::FakeEngineCommandKind kind) {
    const auto commands = harness.engine.commands();
    return static_cast<int>(std::count_if(
        commands.begin(), commands.end(), [kind](const auto& command) {
            return command.kind == kind;
        }));
}

}  // namespace

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void hasFormalInitialLayout();
    void publishesNativeSurfaceOnlyAfterWindowIsShown();
    void opensUtf8LocalFileAndStartsAfterOpeningEvent();
    void routesPauseResumeAndStopThroughPresenter();
    void rendersBufferingAsNonInteractiveWait();
    void allowsReplayAfterNaturalEnd();
    void displaysEngineErrorWithoutBlockingDialog();
    void appliesWorkerThreadStateOnGuiThread();
    void forwardsEveryBridgeEventAsQueuedValue();
    void rendersPositionDurationAndSeekability();
    void keepsSeekPreviewStableAndSeeksOnlyOnRelease();
    void routesVolumeAndMuteWithoutChangingPlaybackState();
    void appliesBurstPositionEventsOnGuiThread();
    void addsMultipleFilesAndActivatesRequestedItem();
    void acceptsDroppedLocalFilesInOrder();
    void advancesNaturalEndAccordingToPlaybackMode();
    void removesCurrentItemsWithoutLeavingInvalidSelection();
    void switchesBetweenVideoSurfaceAndAudioPlaceholder();
    void resizesVideoSurfaceAndTogglesFullScreen();
    void stopsForwardingBeforeWindowCloses();
};

void MainWindowTest::hasFormalInitialLayout() {
    GuiHarness harness;

    QCOMPARE(harness.window.windowTitle(), QStringLiteral("MediaHub"));
    QCOMPARE(harness.window.size(), QSize(960, 720));
    QVERIFY(!harness.window.isVisible());
    QVERIFY(requiredChild<QAction>(harness.window, "openFileAction")->isEnabled());
    QVERIFY(requiredChild<QPushButton>(harness.window, "openFileButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "playButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "pauseButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "stopButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "fullScreenButton")->isEnabled());
    auto* const progressSlider = requiredChild<QSlider>(harness.window, "progressSlider");
    QCOMPARE(progressSlider->minimum(), 0);
    QCOMPARE(progressSlider->maximum(), 1000);
    QCOMPARE(progressSlider->value(), 0);
    QVERIFY(!progressSlider->isEnabled());
    QCOMPARE(requiredChild<QLabel>(harness.window, "positionLabel")->text(),
             QStringLiteral("00:00 / --:--"));
    auto* const volumeSlider = requiredChild<QSlider>(harness.window, "volumeSlider");
    QCOMPARE(volumeSlider->minimum(), 0);
    QCOMPARE(volumeSlider->maximum(), 100);
    QCOMPARE(volumeSlider->value(), 100);
    QVERIFY(volumeSlider->isEnabled());
    QCOMPARE(requiredChild<QPushButton>(harness.window, "muteButton")->text(),
             QStringLiteral("静音"));
    auto* const playlistView = requiredChild<QListView>(harness.window, "playlistView");
    QVERIFY(playlistView->model() != nullptr);
    QCOMPARE(playlistView->model()->rowCount(), 0);
    QCOMPARE(requiredChild<QComboBox>(harness.window, "playbackModeCombo")->currentIndex(),
             0);
    QVERIFY(!requiredChild<QPushButton>(harness.window, "previousButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "nextButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window,
                                        "removePlaylistButton")->isEnabled());
    QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));

    auto* const videoOutput = requiredChild<VideoOutputWidget>(
        harness.window, "videoOutputWidget");
    QVERIFY(!videoOutput->isVideoActive());
    QVERIFY(!videoOutput->hasHeightForWidth());
    QCOMPARE(videoOutput->sizeHint(), QSize(720, 405));
    QCOMPARE(videoOutput->placeholderText(),
             QStringLiteral("打开媒体后，画面会出现在这里"));
    QVERIFY(!hasSurfaceCommand(harness, false));
}

void MainWindowTest::publishesNativeSurfaceOnlyAfterWindowIsShown() {
    GuiHarness harness;
    QVERIFY(!hasSurfaceCommand(harness, false));

    harness.window.show();
    QTRY_VERIFY(hasSurfaceCommand(harness, false));
}

void MainWindowTest::opensUtf8LocalFileAndStartsAfterOpeningEvent() {
    GuiHarness harness;
    const QString filePath = QStringLiteral("C:/媒体 库/测试 音频.wav");

    harness.presenter.openLocalFile(filePath);
    const auto openCommands = harness.engine.commands();
    QCOMPARE(static_cast<int>(openCommands.size()), 1);
    QVERIFY(openCommands.front().kind == test::FakeEngineCommandKind::Open);
    QVERIFY(openCommands.front().media.has_value());
    QCOMPARE(openCommands.front().media->source, filePath.toUtf8().toStdString());
    QCOMPARE(openCommands.front().media->displayName,
             QStringLiteral("测试 音频.wav").toUtf8().toStdString());

    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()), 2);
    QVERIFY(harness.engine.commands().back().kind == test::FakeEngineCommandKind::Play);
    QCOMPARE(statusText(harness), QStringLiteral("正在打开..."));
    QVERIFY(requiredChild<QPushButton>(harness.window, "stopButton")->isEnabled());
}

void MainWindowTest::routesPauseResumeAndStopThroughPresenter() {
    GuiHarness harness;
    openAndReachPlaying(harness);

    auto* const pauseButton = requiredChild<QPushButton>(harness.window, "pauseButton");
    auto* const playButton = requiredChild<QPushButton>(harness.window, "playButton");
    auto* const stopButton = requiredChild<QPushButton>(harness.window, "stopButton");
    QVERIFY(pauseButton->isEnabled());
    QTest::mouseClick(pauseButton, Qt::LeftButton);
    QVERIFY(harness.engine.commands().back().kind == test::FakeEngineCommandKind::Pause);

    harness.engine.emitStateChanged(core::PlaybackState::Paused);
    QTRY_VERIFY(playButton->isEnabled());
    QVERIFY(!pauseButton->isEnabled());
    QTest::mouseClick(playButton, Qt::LeftButton);
    QVERIFY(harness.engine.commands().back().kind == test::FakeEngineCommandKind::Play);

    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QTRY_VERIFY(stopButton->isEnabled());
    QTest::mouseClick(stopButton, Qt::LeftButton);
    QVERIFY(harness.engine.commands().back().kind == test::FakeEngineCommandKind::Stop);

    harness.engine.emitStateChanged(core::PlaybackState::Stopped);
    QTRY_COMPARE(statusText(harness), QStringLiteral("已停止"));
    QVERIFY(playButton->isEnabled());
    QVERIFY(!pauseButton->isEnabled());
    QVERIFY(!stopButton->isEnabled());
}

void MainWindowTest::rendersBufferingAsNonInteractiveWait() {
    GuiHarness harness;
    harness.presenter.openLocalFile(QStringLiteral("C:/buffering.wav"));
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()), 2);

    harness.engine.emitStateChanged(core::PlaybackState::Buffering);
    QTRY_COMPARE(statusText(harness), QStringLiteral("正在缓冲..."));
    QVERIFY(!requiredChild<QPushButton>(harness.window, "playButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "pauseButton")->isEnabled());
    QVERIFY(requiredChild<QPushButton>(harness.window, "stopButton")->isEnabled());
}

void MainWindowTest::allowsReplayAfterNaturalEnd() {
    GuiHarness harness;
    openAndReachPlaying(harness);

    harness.engine.emitEndReached();
    QTRY_COMPARE(statusText(harness), QStringLiteral("播放结束"));
    QVERIFY(requiredChild<QPushButton>(harness.window, "playButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "pauseButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "stopButton")->isEnabled());
}

void MainWindowTest::displaysEngineErrorWithoutBlockingDialog() {
    GuiHarness harness;
    auto* const errorLabel = requiredChild<QLabel>(harness.window, "playbackErrorLabel");

    harness.engine.emitError(core::PlaybackError{
        core::PlaybackErrorKind::SourceNotFound,
        "Missing test media",
        "找不到测试媒体。",
    });

    QTRY_COMPARE(statusText(harness), QStringLiteral("播放失败"));
    QCOMPARE(errorLabel->text(), QStringLiteral("找不到测试媒体。"));
    QVERIFY(!errorLabel->isHidden());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "playButton")->isEnabled());

    harness.presenter.openLocalFile(QStringLiteral("C:/新的媒体.wav"));
    QVERIFY(errorLabel->isHidden());
}

void MainWindowTest::appliesWorkerThreadStateOnGuiThread() {
    GuiHarness harness;
    harness.presenter.openLocalFile(QStringLiteral("C:/thread-test.wav"));
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()), 2);

    QThread* handledThread = nullptr;
    connect(&harness.presenter,
            &PlayerPresenter::stateApplied,
            &harness.window,
            [&handledThread](const core::PlaybackState state) {
                if (state == core::PlaybackState::Playing) {
                    handledThread = QThread::currentThread();
                }
            });

    std::thread worker([&harness] {
        harness.engine.emitStateChanged(core::PlaybackState::Playing);
    });
    worker.join();

    QTRY_VERIFY(handledThread != nullptr);
    QCOMPARE(handledThread, QCoreApplication::instance()->thread());
    QCOMPARE(statusText(harness), QStringLiteral("正在播放"));
}

void MainWindowTest::forwardsEveryBridgeEventAsQueuedValue() {
    EngineEventBridge bridge;
    QObject receiver;
    int receivedCount = 0;
    QThread* handledThread = nullptr;
    core::PlaybackPosition receivedPosition;
    OptionalDuration receivedDuration;
    core::PlaybackError receivedError;

    connect(&bridge, &EngineEventBridge::stateChanged, &receiver,
            [&receivedCount, &handledThread](core::PlaybackState) {
                ++receivedCount;
                handledThread = QThread::currentThread();
            },
            Qt::QueuedConnection);
    connect(&bridge, &EngineEventBridge::positionChanged, &receiver,
            [&receivedCount, &receivedPosition](core::PlaybackPosition position) {
                ++receivedCount;
                receivedPosition = position;
            },
            Qt::QueuedConnection);
    connect(&bridge, &EngineEventBridge::durationChanged, &receiver,
            [&receivedCount, &receivedDuration](OptionalDuration duration) {
                ++receivedCount;
                receivedDuration = duration;
            },
            Qt::QueuedConnection);
    connect(&bridge, &EngineEventBridge::endReached, &receiver,
            [&receivedCount] { ++receivedCount; }, Qt::QueuedConnection);
    connect(&bridge, &EngineEventBridge::errorOccurred, &receiver,
            [&receivedCount, &receivedError](core::PlaybackError error) {
                ++receivedCount;
                receivedError = std::move(error);
            },
            Qt::QueuedConnection);

    std::thread worker([&bridge] {
        bridge.onStateChanged(core::PlaybackState::Playing);
        bridge.onPositionChanged(core::PlaybackPosition{750ms, 3s, true});
        bridge.onDurationChanged(3s);
        bridge.onEndReached();
        bridge.onError(core::PlaybackError{
            core::PlaybackErrorKind::Unknown,
            "Worker event",
            "工作线程错误",
        });
    });
    worker.join();

    QTRY_COMPARE(receivedCount, 5);
    QCOMPARE(handledThread, QCoreApplication::instance()->thread());
    QCOMPARE(receivedPosition.current, 750ms);
    QVERIFY(receivedPosition.total == std::optional<std::chrono::milliseconds>(3s));
    QVERIFY(receivedDuration == std::optional<std::chrono::milliseconds>(3s));
    QCOMPARE(receivedError.userMessage, std::string("工作线程错误"));
}

void MainWindowTest::rendersPositionDurationAndSeekability() {
    GuiHarness harness;
    openAndReachPlaying(harness);
    auto* const progressSlider = requiredChild<QSlider>(harness.window, "progressSlider");
    auto* const positionLabel = requiredChild<QLabel>(harness.window, "positionLabel");

    harness.engine.emitPositionChanged(core::PlaybackPosition{65s, 125s, true});
    QTRY_COMPARE(positionLabel->text(), QStringLiteral("01:05 / 02:05"));
    QCOMPARE(progressSlider->value(), 520);
    QVERIFY(progressSlider->isEnabled());
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 0);

    harness.engine.emitPositionChanged(core::PlaybackPosition{3661s, 7322s, true});
    QTRY_COMPARE(positionLabel->text(), QStringLiteral("1:01:01 / 2:02:02"));
    QCOMPARE(progressSlider->value(), 500);

    harness.engine.emitPositionChanged(core::PlaybackPosition{2s, std::nullopt, false});
    QTRY_COMPARE(positionLabel->text(), QStringLiteral("00:02 / --:--"));
    QCOMPARE(progressSlider->value(), 0);
    QVERIFY(!progressSlider->isEnabled());
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 0);

    harness.presenter.openLocalFile(QStringLiteral("C:/audio/next.wav"));
    QCOMPARE(positionLabel->text(), QStringLiteral("00:00 / --:--"));
    QCOMPARE(progressSlider->value(), 0);
    QVERIFY(!progressSlider->isEnabled());
}

void MainWindowTest::keepsSeekPreviewStableAndSeeksOnlyOnRelease() {
    GuiHarness harness;
    openAndReachPlaying(harness);
    auto* const progressSlider = requiredChild<QSlider>(harness.window, "progressSlider");
    auto* const positionLabel = requiredChild<QLabel>(harness.window, "positionLabel");
    harness.engine.emitPositionChanged(core::PlaybackPosition{30s, 120s, true});
    QTRY_VERIFY(progressSlider->isEnabled());
    QCOMPARE(progressSlider->value(), 250);

    const int commandsBeforeDrag = static_cast<int>(harness.engine.commands().size());
    progressSlider->setSliderDown(true);
    progressSlider->setSliderPosition(750);
    QTRY_COMPARE(positionLabel->text(), QStringLiteral("01:30 / 02:00"));
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 0);

    harness.engine.emitPositionChanged(core::PlaybackPosition{40s, 120s, true});
    QCoreApplication::processEvents();
    QCOMPARE(positionLabel->text(), QStringLiteral("01:30 / 02:00"));

    progressSlider->setSliderDown(false);
    QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()), commandsBeforeDrag + 1);
    const auto commands = harness.engine.commands();
    QVERIFY(commands.back().kind == test::FakeEngineCommandKind::Seek);
    QCOMPARE(commands.back().position, 90s);
    QCOMPARE(statusText(harness), QStringLiteral("正在播放"));
}

void MainWindowTest::routesVolumeAndMuteWithoutChangingPlaybackState() {
    GuiHarness harness;
    openAndReachPlaying(harness);
    auto* const volumeSlider = requiredChild<QSlider>(harness.window, "volumeSlider");
    auto* const volumeLabel = requiredChild<QLabel>(harness.window, "volumeLabel");
    auto* const muteButton = requiredChild<QPushButton>(harness.window, "muteButton");

    volumeSlider->setValue(64);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetVolume), 1);
    QCOMPARE(harness.engine.commands().back().volume, 64);
    QCOMPARE(volumeLabel->text(), QStringLiteral("音量 64%"));

    QTest::mouseClick(muteButton, Qt::LeftButton);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted), 1);
    QVERIFY(harness.engine.commands().back().flag);
    QCOMPARE(muteButton->text(), QStringLiteral("取消静音"));
    QCOMPARE(volumeLabel->text(), QStringLiteral("已静音 · 64%"));

    QTest::mouseClick(muteButton, Qt::LeftButton);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted), 2);
    QVERIFY(!harness.engine.commands().back().flag);
    QCOMPARE(muteButton->text(), QStringLiteral("静音"));
    QCOMPARE(statusText(harness), QStringLiteral("正在播放"));
}

void MainWindowTest::appliesBurstPositionEventsOnGuiThread() {
    GuiHarness harness;
    openAndReachPlaying(harness);
    auto* const progressSlider = requiredChild<QSlider>(harness.window, "progressSlider");
    auto* const positionLabel = requiredChild<QLabel>(harness.window, "positionLabel");

    std::thread worker([&harness] {
        for (int index = 1; index <= 400; ++index) {
            harness.engine.emitPositionChanged(
                core::PlaybackPosition{std::chrono::milliseconds(index * 10), 10s, true});
        }
    });
    worker.join();

    QTRY_COMPARE(positionLabel->text(), QStringLiteral("00:04 / 00:10"));
    QCOMPARE(progressSlider->value(), 400);
    QCOMPARE(statusText(harness), QStringLiteral("正在播放"));
}

void MainWindowTest::addsMultipleFilesAndActivatesRequestedItem() {
    GuiHarness harness;
    const QStringList paths{QStringLiteral("C:/媒体/第一首.mp3"),
                            QStringLiteral("C:/媒体/第二段.mp4"),
                            QStringLiteral("C:/媒体/第三首.wav")};
    harness.presenter.addLocalFiles(paths);

    auto* const playlistView = requiredChild<QListView>(harness.window, "playlistView");
    auto* const model = playlistView->model();
    QCOMPARE(model->rowCount(), 3);
    QVERIFY(model->data(model->index(0, 0)).toString().contains(QStringLiteral("第一首.mp3")));
    QVERIFY(model->data(model->index(1, 0)).toString().contains(QStringLiteral("第二段.mp4")));
    QVERIFY(model->data(model->index(2, 0)).toString().contains(QStringLiteral("第三首.wav")));
    QVERIFY(harness.engine.commands().back().media.has_value());
    QCOMPARE(harness.engine.commands().back().media->source, paths.front().toUtf8().toStdString());
    QCOMPARE(playlistView->currentIndex().row(), 0);
    QVERIFY(requiredChild<QPushButton>(harness.window, "nextButton")->isEnabled());

    const QModelIndex third = model->index(2, 0);
    QVERIFY(QMetaObject::invokeMethod(playlistView, "doubleClicked", Qt::DirectConnection,
                                      Q_ARG(QModelIndex, third)));
    QVERIFY(harness.engine.commands().back().media.has_value());
    QCOMPARE(harness.engine.commands().back().media->source, paths.back().toUtf8().toStdString());
    QCOMPARE(playlistView->currentIndex().row(), 2);
    QVERIFY(requiredChild<QPushButton>(harness.window, "previousButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "nextButton")->isEnabled());
}

void MainWindowTest::acceptsDroppedLocalFilesInOrder() {
    GuiHarness harness;
    QMimeData mimeData;
    mimeData.setUrls({QUrl::fromLocalFile(QStringLiteral("C:/拖放/甲.mp3")),
                      QUrl::fromLocalFile(QStringLiteral("C:/拖放/乙.mp4"))});
    QDragEnterEvent dragEnter(QPoint(10, 10), Qt::CopyAction, &mimeData,
                              Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&harness.window, &dragEnter);
    QVERIFY(dragEnter.isAccepted());

    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mimeData,
                    Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&harness.window, &drop);
    QVERIFY(drop.isAccepted());

    auto* const model = requiredChild<QListView>(harness.window, "playlistView")->model();
    QCOMPARE(model->rowCount(), 2);
    QVERIFY(model->data(model->index(0, 0)).toString().contains(QStringLiteral("甲.mp3")));
    QVERIFY(model->data(model->index(1, 0)).toString().contains(QStringLiteral("乙.mp4")));
}

void MainWindowTest::advancesNaturalEndAccordingToPlaybackMode() {
    const QStringList paths{QStringLiteral("C:/list/one.mp3"),
                            QStringLiteral("C:/list/two.mp3")};
    for (int modeIndex = 0; modeIndex < 3; ++modeIndex) {
        GuiHarness harness;
        harness.presenter.addLocalFiles(paths);
        harness.engine.emitStateChanged(core::PlaybackState::Opening);
        harness.engine.emitStateChanged(core::PlaybackState::Playing);
        auto* const playlistView = requiredChild<QListView>(harness.window, "playlistView");
        const QModelIndex second = playlistView->model()->index(1, 0);
        QVERIFY(QMetaObject::invokeMethod(playlistView, "doubleClicked", Qt::DirectConnection,
                                          Q_ARG(QModelIndex, second)));
        requiredChild<QComboBox>(harness.window, "playbackModeCombo")->setCurrentIndex(modeIndex);
        const int opensBeforeEnd = commandCount(harness, test::FakeEngineCommandKind::Open);

        harness.engine.emitEndReached();
        if (modeIndex == 0) {
            QTRY_COMPARE(statusText(harness), QStringLiteral("播放结束"));
            QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), opensBeforeEnd);
            continue;
        }

        QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
                     opensBeforeEnd + 1);
        const auto command = harness.engine.commands().back();
        QVERIFY(command.media.has_value());
        const QString expected = modeIndex == 1 ? paths.front() : paths.back();
        QCOMPARE(command.media->source, expected.toUtf8().toStdString());
    }
}

void MainWindowTest::removesCurrentItemsWithoutLeavingInvalidSelection() {
    GuiHarness harness;
    harness.presenter.addLocalFiles(
        {QStringLiteral("C:/remove/one.mp3"), QStringLiteral("C:/remove/two.mp3")});
    auto* const playlistView = requiredChild<QListView>(harness.window, "playlistView");
    auto* const removeButton = requiredChild<QPushButton>(harness.window,
                                                          "removePlaylistButton");

    QTest::mouseClick(removeButton, Qt::LeftButton);
    QCOMPARE(playlistView->model()->rowCount(), 1);
    QCOMPARE(playlistView->currentIndex().row(), 0);
    QVERIFY(harness.engine.commands().back().media.has_value());
    QCOMPARE(harness.engine.commands().back().media->displayName, std::string("two.mp3"));

    QTest::mouseClick(removeButton, Qt::LeftButton);
    QCOMPARE(playlistView->model()->rowCount(), 0);
    QVERIFY(!removeButton->isEnabled());
    QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));
    QVERIFY(harness.engine.commands().back().kind == test::FakeEngineCommandKind::Stop);

    // 移除最后一项后，内核可能迟到发送停止事件；空列表仍保持空闲界面。
    harness.engine.emitStateChanged(core::PlaybackState::Stopped);
    QCoreApplication::processEvents();
    QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));
}

void MainWindowTest::switchesBetweenVideoSurfaceAndAudioPlaceholder() {
    GuiHarness harness;
    auto* const videoOutput = requiredChild<VideoOutputWidget>(
        harness.window, "videoOutputWidget");

    harness.presenter.openLocalFile(QStringLiteral("C:/video/sample.mp4"));
    QVERIFY(!videoOutput->isVideoActive());
    QCOMPARE(videoOutput->placeholderText(), QStringLiteral("正在准备视频画面..."));

    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_VERIFY(videoOutput->isVideoActive());
    QCOMPARE(videoOutput->placeholderText(), QString());
    QVERIFY(videoOutput->testAttribute(Qt::WA_NoSystemBackground));
    QVERIFY(!videoOutput->testAttribute(Qt::WA_OpaquePaintEvent));

    harness.presenter.openLocalFile(QStringLiteral("C:/audio/sample.flac"));
    QVERIFY(!videoOutput->isVideoActive());
    QVERIFY(videoOutput->placeholderText().contains(QStringLiteral("音频播放模式")));
    QVERIFY(!videoOutput->testAttribute(Qt::WA_NoSystemBackground));
    QVERIFY(videoOutput->testAttribute(Qt::WA_OpaquePaintEvent));
}

void MainWindowTest::resizesVideoSurfaceAndTogglesFullScreen() {
    GuiHarness harness;
    harness.window.show();
    QCoreApplication::processEvents();
    auto* const videoOutput = requiredChild<VideoOutputWidget>(
        harness.window, "videoOutputWidget");
    const QSize initialSize = videoOutput->size();
    auto* const openButton = requiredChild<QPushButton>(harness.window, "openFileButton");
    auto* const fullScreenButton = requiredChild<QPushButton>(
        harness.window, "fullScreenButton");
    auto* const progressSlider = requiredChild<QSlider>(harness.window, "progressSlider");
    auto* const volumeSlider = requiredChild<QSlider>(harness.window, "volumeSlider");
    auto* const muteButton = requiredChild<QPushButton>(harness.window, "muteButton");
    auto* const previousButton = requiredChild<QPushButton>(
        harness.window, "previousButton");
    auto* const nextButton = requiredChild<QPushButton>(harness.window, "nextButton");
    auto* const removePlaylistButton = requiredChild<QPushButton>(
        harness.window, "removePlaylistButton");
    auto* const playlistView = requiredChild<QListView>(harness.window, "playlistView");
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*openButton, harness.window)));
    QVERIFY(harness.window.rect().contains(
        geometryInsideWindow(*fullScreenButton, harness.window)));
    QVERIFY(harness.window.rect().contains(
        geometryInsideWindow(*progressSlider, harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*volumeSlider, harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*muteButton, harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*previousButton,
                                                               harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*nextButton, harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*removePlaylistButton,
                                                               harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*playlistView, harness.window)));

    harness.window.resize(1200, 800);
    QTRY_VERIFY(videoOutput->size().width() > initialSize.width());
    QTRY_VERIFY(videoOutput->size().height() > initialSize.height());
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*openButton, harness.window)));
    QVERIFY(harness.window.rect().contains(
        geometryInsideWindow(*fullScreenButton, harness.window)));
    QVERIFY(harness.window.rect().contains(
        geometryInsideWindow(*progressSlider, harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*volumeSlider, harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*muteButton, harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*previousButton,
                                                               harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*nextButton, harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*removePlaylistButton,
                                                               harness.window)));
    QVERIFY(harness.window.rect().contains(geometryInsideWindow(*playlistView, harness.window)));

    harness.presenter.openLocalFile(QStringLiteral("C:/video/fullscreen.mkv"));
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_VERIFY(fullScreenButton->isEnabled());

    QTest::mouseClick(fullScreenButton, Qt::LeftButton);
    QTRY_VERIFY(harness.window.isFullScreen());
    QCOMPARE(fullScreenButton->text(), QStringLiteral("退出全屏"));
    QVERIFY(fullScreenButton->isHidden());

    requiredChild<QAction>(harness.window, "fullScreenAction")->trigger();
    QTRY_VERIFY(!harness.window.isFullScreen());
    QCOMPARE(fullScreenButton->text(), QStringLiteral("全屏"));
    QVERIFY(!fullScreenButton->isHidden());
}

void MainWindowTest::stopsForwardingBeforeWindowCloses() {
    GuiHarness harness;
    harness.window.show();
    QCoreApplication::processEvents();
    QVERIFY(harness.window.isVisible());

    harness.window.close();
    QCoreApplication::processEvents();
    QVERIFY(!harness.window.isVisible());
    const auto commands = harness.engine.commands();
    QVERIFY(commands.size() >= 3);
    QVERIFY(commands[commands.size() - 2].kind ==
            test::FakeEngineCommandKind::SetVideoSurface);
    QCOMPARE(commands[commands.size() - 2].nativeHandle, nullptr);
    QVERIFY(commands.back().kind == test::FakeEngineCommandKind::Stop);

    harness.eventBridge.onStateChanged(core::PlaybackState::Opening);
    QCoreApplication::processEvents();
    QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));
}

}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::MainWindowTest)

#include "main_window_test.moc"
