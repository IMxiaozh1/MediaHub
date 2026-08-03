#include "engine_event_bridge.h"
#include "fakes/fake_player_engine.h"
#include "main_window.h"
#include "player_presenter.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QTest>
#include <QThread>

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

void openAndReachPlaying(GuiHarness& harness) {
    harness.presenter.openLocalFile(QStringLiteral("C:/媒体 库/测试 音频.wav"));
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()), 2);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));
}

}  // namespace

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void hasFormalInitialLayout();
    void opensUtf8LocalFileAndStartsAfterOpeningEvent();
    void routesPauseResumeAndStopThroughPresenter();
    void rendersBufferingAsNonInteractiveWait();
    void allowsReplayAfterNaturalEnd();
    void displaysEngineErrorWithoutBlockingDialog();
    void appliesWorkerThreadStateOnGuiThread();
    void forwardsEveryBridgeEventAsQueuedValue();
    void stopsForwardingBeforeWindowCloses();
};

void MainWindowTest::hasFormalInitialLayout() {
    GuiHarness harness;

    QCOMPARE(harness.window.windowTitle(), QStringLiteral("MediaHub"));
    QCOMPARE(harness.window.size(), QSize(960, 600));
    QVERIFY(!harness.window.isVisible());
    QVERIFY(requiredChild<QAction>(harness.window, "openFileAction")->isEnabled());
    QVERIFY(requiredChild<QPushButton>(harness.window, "openFileButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "playButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "pauseButton")->isEnabled());
    QVERIFY(!requiredChild<QPushButton>(harness.window, "stopButton")->isEnabled());
    QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));
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

void MainWindowTest::stopsForwardingBeforeWindowCloses() {
    GuiHarness harness;
    harness.window.show();
    QCoreApplication::processEvents();
    QVERIFY(harness.window.isVisible());

    harness.window.close();
    QCoreApplication::processEvents();
    QVERIFY(!harness.window.isVisible());
    QCOMPARE(static_cast<int>(harness.engine.commands().size()), 1);
    QVERIFY(harness.engine.commands().back().kind == test::FakeEngineCommandKind::Stop);

    harness.eventBridge.onStateChanged(core::PlaybackState::Opening);
    QCoreApplication::processEvents();
    QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));
}

}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::MainWindowTest)

#include "main_window_test.moc"
