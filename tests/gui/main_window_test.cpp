#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QScrollBar>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include "engine_event_bridge.h"
#include "fakes/fake_player_engine.h"
#include "lyrics_service.h"
#include "lyrics_view.h"
#include "mediahub/core/playlist.h"
#include "player_presenter.h"
#include "playlist_model.h"
#include "shutdown_watchdog.h"
#include "video_output_widget.h"
#include "window_icon_manager.h"

namespace mediahub::gui {
namespace {

using namespace std::chrono_literals;

class FakeLyricsService final : public LyricsService {
public:
  void requestLyrics(const LyricsQuery &query) override {
    ++requestCount;
    lastQuery = query;
  }

  void cancel() noexcept override { ++cancelCount; }

  void complete(LyricsResult result) { emit resultReady(std::move(result)); }

  LyricsQuery lastQuery;
  int requestCount{0};
  int cancelCount{0};
};

class FakeLivePlaylistService final : public LivePlaylistService {
public:
  void load(const QString &playlistUrl) override {
    ++loadCount;
    lastPlaylistUrl = playlistUrl;
    isPending = true;
    if (QUrl(playlistUrl)
            .path()
            .endsWith(QStringLiteral(".m3u8"), Qt::CaseInsensitive)) {
      fail(LivePlaylistLoadError::HlsMediaManifest);
    }
  }

  void cancel() noexcept override {
    ++cancelCount;
    isPending = false;
  }

  void complete(LivePlaylistLoadResult result) {
    isPending = false;
    emit loadSucceeded(std::move(result));
  }

  void fail(const LivePlaylistLoadError error) {
    isPending = false;
    emit loadFailed(error);
  }

  QString lastPlaylistUrl;
  int loadCount{0};
  int cancelCount{0};
  bool isPending{false};
};

struct GuiHarness {
  std::ostringstream logOutput;
  logging::Logger logger{logOutput};
  test::FakePlayerEngine engine;
  EngineEventBridge eventBridge;
  MainWindow window;
  FakeLyricsService lyricsService;
  FakeLivePlaylistService livePlaylistService;
  PlayerPresenter presenter;

  GuiHarness()
      : presenter(engine, eventBridge, window, nullptr, &logger, &lyricsService,
                  &livePlaylistService) {}
};

template <typename Widget>
Widget *requiredChild(MainWindow &window, const char *const objectName) {
  auto *const child =
      window.findChild<Widget *>(QString::fromLatin1(objectName));
  Q_ASSERT(child != nullptr);
  return child;
}

QString statusText(GuiHarness &harness) {
  return requiredChild<QLabel>(harness.window, "playbackStatusLabel")->text();
}

QRect geometryInsideWindow(QWidget &widget, MainWindow &window) {
  return QRect(widget.mapTo(&window, QPoint(0, 0)), widget.size());
}

int verticalCenterInsideWindow(QWidget &widget, MainWindow &window) {
  return widget.mapTo(&window, widget.rect().center()).y();
}

void openAndReachPlaying(GuiHarness &harness) {
  harness.presenter.openLocalFile(QStringLiteral("C:/媒体 库/测试 音频.wav"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()), 2);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));
}

bool hasSurfaceCommand(const GuiHarness &harness,
                       const bool expectsNullHandle) {
  const auto commands = harness.engine.commands();
  return std::any_of(
      commands.begin(), commands.end(),
      [expectsNullHandle](const auto &command) {
        return command.kind == test::FakeEngineCommandKind::SetVideoSurface &&
               (command.nativeHandle == nullptr) == expectsNullHandle;
      });
}

int commandCount(const GuiHarness &harness,
                 const test::FakeEngineCommandKind kind) {
  const auto commands = harness.engine.commands();
  return static_cast<int>(std::count_if(
      commands.begin(), commands.end(),
      [kind](const auto &command) { return command.kind == kind; }));
}

bool requestPlaylistContextMenu(QListView &view, const int row) {
  const QModelIndex index = view.model()->index(row, 0);
  if (!index.isValid()) {
    return false;
  }
  view.scrollTo(index);
  QCoreApplication::processEvents();
  const QPoint position = view.visualRect(index).center();
  return QMetaObject::invokeMethod(&view, "customContextMenuRequested",
                                   Qt::DirectConnection,
                                   Q_ARG(QPoint, position));
}

} // namespace

class MainWindowTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void hasFormalInitialLayout();
  void switchesVisualModesWithoutRebuildingControls();
  void loadsReplaceableWindowIconsFromFixedSlots();
  void fallsBackForMissingOrDamagedWindowIcons();
  void parsesSynchronizedAndPlainLyrics();
  void matchesQualifiedTitleByDurationWhenArtistCreditDiffers();
  void selectsKugouNestedAudioVersionAndDecodesLyrics();
  void publishesNativeSurfaceOnlyAfterWindowIsShown();
  void opensUtf8LocalFileAndStartsAfterOpeningEvent();
  void opensValidatedNetworkUrlWithoutLoggingPrivateParts();
  void expandsPlaylistUrlsAndFallsBackForSingleHlsStreams();
  void separatesLocalAndLiveListsWithoutStoppingPlayback();
  void controlsAndMarksLiveSourcesFromRightClickMenu();
  void keepsLiveListPositionAndLocatesCurrentPlayback();
  void replacesRemoteLiveListAtomicallyAndKeepsItOnFailure();
  void keepsNetworkStreamsNonSeekableWhenEngineReportsLiveWindow();
  void rejectsInvalidNetworkUrlBeforeCallingEngine();
  void rendersNetworkBufferingAndStopsTimeoutAfterPlaying();
  void cancelsAndTimesOutNetworkOpeningWithoutAcceptingLateEvents();
  void doesNotAutomaticallyReconnectInterruptedNetwork();
  void refreshesCurrentNetworkByButtonAndF5();
  void remembersRecentNetworkUrlsForCurrentSession();
  void routesPauseResumeAndStopThroughPresenter();
  void rendersBufferingAsNonInteractiveWait();
  void allowsReplayAfterNaturalEnd();
  void keepsProgressAtEndAfterLatePositionEvents();
  void keepsNaturalEndInteractiveAfterLateStoppedEvent();
  void resumesAfterSeekingFromNaturalEnd();
  void displaysEngineErrorWithoutBlockingDialog();
  void keepsOtherPlaylistItemsUsableAfterFailure();
  void writesLifecycleLogsWithoutMediaPath();
  void appliesWorkerThreadStateOnGuiThread();
  void forwardsEveryBridgeEventAsQueuedValue();
  void rendersPositionDurationAndSeekability();
  void seeksOnceWhenProgressTrackIsClicked();
  void keepsSeekPreviewStableAndSeeksOnlyOnRelease();
  void keepsPausedSeekEnabledAcrossRepeatedDrags();
  void routesVolumeAndMuteWithoutChangingPlaybackState();
  void selectsPlaybackRateFromHoverMenuAndKeepsCurrentValue();
  void keepsHoverMenusStableWithoutMouseGrab();
  void doesNotOpenHoverMenuAfterImmediateLeave();
  void holdsRightKeyAtTwoTimesAndRestoresSelectedRate();
  void routesKeyboardPlaybackVolumeAndSeek();
  void routesCtrlArrowKeysToPlaylistNavigation();
  void usesConfiguredKeyboardSeekStepAndClampsBoundaries();
  void appliesBurstPositionEventsOnGuiThread();
  void addsMultipleFilesAndActivatesRequestedItem();
  void usesPlaylistContextMenuForSingleItemActions();
  void enablesPlaylistContextPlaybackActionsBySelectedItemState();
  void supportsCtrlShiftMultiSelectionAndOnlyAllowsRemoval();
  void acceptsDroppedLocalFilesInOrder();
  void exposesPlaylistModelRolesAndSelectionMarker();
  void advancesNaturalEndAccordingToPlaybackMode();
  void cyclesPlaybackModeOnButtonClickAndUpdatesHoverMenu();
  void ignoresLateEndEventAfterSwitchingToFailedItem();
  void removesCurrentItemsWithoutLeavingInvalidSelection();
  void ignoresRejectedStateEventsAndCommandsAfterShutdown();
  void mapsEveryErrorKindToStableLogValue();
  void switchesBetweenVideoSurfaceAndAudioVisualization();
  void rendersBottomUpwardAudioWaveformAndTogglesFullScreen();
  void togglesLyricsBesideVolumeAndTracksSynchronizedLine();
  void collapsesAndExpandsPlaylistWithMediaResize();
  void resizesVideoSurfaceAndTogglesFullScreen();
  void stopsForwardingBeforeWindowCloses();
  void runsShutdownFallbackOnlyAfterTimeout();
  void cancelsShutdownFallbackAfterNormalCleanup();

private:
  QTemporaryDir settingsDirectory_;
};

void MainWindowTest::initTestCase() {
  QVERIFY(settingsDirectory_.isValid());
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     settingsDirectory_.path());
}

void MainWindowTest::loadsReplaceableWindowIconsFromFixedSlots() {
  QCOMPARE(WindowIconManager::defaultIconDirectory(),
           QDir(QCoreApplication::applicationDirPath())
               .filePath(QStringLiteral("icons")));

  QTemporaryDir iconDirectory;
  QVERIFY(iconDirectory.isValid());
  QImage taskbarImage(19, 23, QImage::Format_ARGB32);
  taskbarImage.fill(QColor(198, 37, 52));
  QImage windowImage(17, 21, QImage::Format_RGB32);
  windowImage.fill(QColor(22, 77, 211));
  const QDir directory(iconDirectory.path());
  QVERIFY(taskbarImage.save(directory.filePath(QStringLiteral("taskbar.png")),
                            "PNG"));
  QVERIFY(windowImage.save(directory.filePath(QStringLiteral("window.jpg")),
                           "JPG", 100));

  const WindowIconImages loaded =
      WindowIconManager::loadImages(iconDirectory.path());
  QCOMPARE(loaded.taskbarImage.size(), taskbarImage.size());
  QCOMPARE(loaded.windowImage.size(), windowImage.size());
  QCOMPARE(loaded.taskbarImage.pixelColor(0, 0), QColor(198, 37, 52));
  const QColor loadedWindowColor = loaded.windowImage.pixelColor(0, 0);
  QVERIFY(loadedWindowColor.blue() > 190);
  QVERIFY(loadedWindowColor.red() < 50);
}

void MainWindowTest::fallsBackForMissingOrDamagedWindowIcons() {
  QTemporaryDir iconDirectory;
  QVERIFY(iconDirectory.isValid());
  const QDir directory(iconDirectory.path());
  const WindowIconImages fallback =
      WindowIconManager::loadImages(iconDirectory.path());
  QVERIFY(!fallback.taskbarImage.isNull());
  QVERIFY(!fallback.windowImage.isNull());

  QFile damagedTaskbar(directory.filePath(QStringLiteral("taskbar.png")));
  QVERIFY(damagedTaskbar.open(QIODevice::WriteOnly));
  QCOMPARE(damagedTaskbar.write("not an image"), qint64(12));
  damagedTaskbar.close();
  QImage externalWindow(20, 18, QImage::Format_RGB32);
  externalWindow.fill(QColor(24, 176, 89));
  QVERIFY(externalWindow.save(
      directory.filePath(QStringLiteral("window.jpg")), "JPG", 100));

  const WindowIconImages firstLoad =
      WindowIconManager::loadImages(iconDirectory.path());
  QVERIFY(firstLoad.taskbarImage == fallback.taskbarImage);
  QVERIFY(firstLoad.windowImage != fallback.windowImage);
  QVERIFY(firstLoad.windowImage.pixelColor(0, 0).green() > 140);

  QImage externalTaskbar(18, 20, QImage::Format_ARGB32);
  externalTaskbar.fill(QColor(231, 145, 24));
  QVERIFY(externalTaskbar.save(
      directory.filePath(QStringLiteral("taskbar.png")), "PNG"));
  QFile damagedWindow(directory.filePath(QStringLiteral("window.jpg")));
  QVERIFY(damagedWindow.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(damagedWindow.write("broken"), qint64(6));
  damagedWindow.close();

  const WindowIconImages secondLoad =
      WindowIconManager::loadImages(iconDirectory.path());
  QVERIFY(secondLoad.taskbarImage != fallback.taskbarImage);
  QCOMPARE(secondLoad.taskbarImage.pixelColor(0, 0), QColor(231, 145, 24));
  QVERIFY(secondLoad.windowImage == fallback.windowImage);
}

void MainWindowTest::hasFormalInitialLayout() {
  GuiHarness harness;

  QCOMPARE(harness.window.windowTitle(), QStringLiteral("MediaHub"));
  QVERIFY(!harness.window.windowIcon().isNull());
  QVERIFY(harness.window.windowIcon().actualSize(QSize(32, 32)).isValid());
  QCOMPARE(harness.window.size(), QSize(960, 720));
  QVERIFY(!harness.window.isVisible());
  QVERIFY(
      requiredChild<QAction>(harness.window, "openFileAction")->isEnabled());
  QVERIFY(
      requiredChild<QAction>(harness.window, "openNetworkAction")->isEnabled());
  QVERIFY(requiredChild<QPushButton>(harness.window, "openFileButton")
              ->isEnabled());
  auto *const playPauseButton =
      requiredChild<QToolButton>(harness.window, "playPauseButton");
  auto *const stopButton =
      requiredChild<QToolButton>(harness.window, "stopButton");
  auto *const fullScreenButton =
      requiredChild<QToolButton>(harness.window, "fullScreenButton");
  QVERIFY(!playPauseButton->isEnabled());
  QVERIFY(!stopButton->isEnabled());
  QVERIFY(!fullScreenButton->isEnabled());
  QVERIFY(!playPauseButton->icon().isNull());
  QVERIFY(playPauseButton->text().isEmpty());
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  QCOMPARE(progressSlider->minimum(), 0);
  QCOMPARE(progressSlider->maximum(), 1000);
  QCOMPARE(progressSlider->value(), 0);
  QVERIFY(!progressSlider->isEnabled());
  QCOMPARE(requiredChild<QLabel>(harness.window, "positionLabel")->text(),
           QStringLiteral("00:00 / --:--"));
  auto *const volumeSlider =
      requiredChild<QSlider>(harness.window, "volumeSlider");
  QCOMPARE(volumeSlider->minimum(), 0);
  QCOMPARE(volumeSlider->maximum(), 100);
  QCOMPARE(volumeSlider->value(), 100);
  QCOMPARE(volumeSlider->orientation(), Qt::Vertical);
  QVERIFY(volumeSlider->isEnabled());
  auto *const volumeButton =
      requiredChild<QToolButton>(harness.window, "volumeButton");
  QVERIFY(volumeButton->text().isEmpty());
  QVERIFY(!volumeButton->icon().isNull());
  QVERIFY(volumeButton->menu() == nullptr);
  auto *const lyricsButton =
      requiredChild<QToolButton>(harness.window, "lyricsButton");
  QCOMPARE(lyricsButton->text(), QStringLiteral("词"));
  QVERIFY(lyricsButton->icon().isNull());
  QVERIFY(!lyricsButton->isEnabled());
  QCOMPARE(lyricsButton->size(), QSize(36, 36));
  QVERIFY(!lyricsButton->isChecked());
  QVERIFY(requiredChild<QWidget>(harness.window, "lyricsTimingControls")
              ->isHidden());
  auto *const volumePopup =
      requiredChild<QWidget>(harness.window, "volumePopup");
  QVERIFY(volumePopup->isWindow());
  QVERIFY(volumePopup->isHidden());
  auto *const keyboardSeekStepButton =
      requiredChild<QToolButton>(harness.window, "keyboardSeekStepButton");
  QCOMPARE(keyboardSeekStepButton->text(), QStringLiteral("5 秒"));
  QVERIFY(keyboardSeekStepButton->menu() != nullptr);
  QCOMPARE(keyboardSeekStepButton->menu()->actions().size(), 4);
  QVERIFY(requiredChild<QAction>(harness.window, "keyboardSeekStepAction5") !=
          nullptr);
  QVERIFY(requiredChild<QAction>(harness.window, "keyboardSeekStepAction20") !=
          nullptr);
  auto *const playbackRateButton =
      requiredChild<QToolButton>(harness.window, "playbackRateButton");
  QCOMPARE(playbackRateButton->text(), QStringLiteral("1.0×"));
  QCOMPARE(playbackRateButton->menu()->actions().size(), 6);
  QVERIFY(requiredChild<QAction>(harness.window, "playbackRateAction75") !=
          nullptr);
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  QVERIFY(playlistView->model() != nullptr);
  QCOMPARE(playlistView->model()->rowCount(), 0);
  QCOMPARE(playlistView->selectionMode(), QAbstractItemView::ExtendedSelection);
  QCOMPARE(playlistView->contextMenuPolicy(), Qt::CustomContextMenu);
  auto *const playbackModeButton =
      requiredChild<QToolButton>(harness.window, "playbackModeButton");
  QCOMPARE(playbackModeButton->menu()->actions().size(), 4);
  QVERIFY(requiredChild<QAction>(harness.window, "playbackModeAction3") !=
          nullptr);
  QVERIFY(playbackModeButton->accessibleName().contains(
      QStringLiteral("顺序播放")));
  QVERIFY(!requiredChild<QToolButton>(harness.window, "previousButton")
               ->isEnabled());
  QVERIFY(
      !requiredChild<QToolButton>(harness.window, "nextButton")->isEnabled());
  QVERIFY(harness.window.findChild<QPushButton *>(
              QStringLiteral("removePlaylistButton")) == nullptr);
  QVERIFY(!requiredChild<QAction>(harness.window, "playlistRemoveAction")
               ->isEnabled());
  QCOMPARE(requiredChild<QPushButton>(harness.window, "openFileButton")
               ->parentWidget(),
           requiredChild<QWidget>(harness.window, "playlistPanel"));
  QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));

  auto *const videoOutput =
      requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget");
  QVERIFY(!videoOutput->isVideoActive());
  QVERIFY(!videoOutput->hasHeightForWidth());
  QCOMPARE(videoOutput->sizeHint(), QSize(720, 405));
  QCOMPARE(videoOutput->placeholderText(),
           QStringLiteral("打开媒体后，画面会出现在这里"));
  QVERIFY(!hasSurfaceCommand(harness, false));
  QVERIFY(requiredChild<QWidget>(harness.window, "playlistPanel")
              ->isVisibleTo(&harness.window));
  QVERIFY(!requiredChild<QToolButton>(harness.window, "playlistToggleButton")
               ->icon()
               .isNull());
}

void MainWindowTest::switchesVisualModesWithoutRebuildingControls() {
  GuiHarness harness;
  auto *const centralSurface =
      requiredChild<QWidget>(harness.window, "centralSurface");
  auto *const titleLabel = requiredChild<QLabel>(harness.window, "titleLabel");
  auto *const modeBadge =
      requiredChild<QLabel>(harness.window, "modeBadgeLabel");
  auto *const playPauseButton =
      requiredChild<QToolButton>(harness.window, "playPauseButton");
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");

  QCOMPARE(centralSurface->property("themeMode").toString(),
           QStringLiteral("video"));
  QCOMPARE(modeBadge->text(), QStringLiteral("VIDEO"));
  QVERIFY(titleLabel->text().contains(QStringLiteral("播放")));

  harness.presenter.openLocalFile(QStringLiteral("C:/music/theme-song.mp3"));
  QTRY_COMPARE(centralSurface->property("themeMode").toString(),
               QStringLiteral("audio"));
  QCOMPARE(modeBadge->text(), QStringLiteral("MUSIC"));
  QCOMPARE(requiredChild<QToolButton>(harness.window, "playPauseButton"),
           playPauseButton);
  QCOMPARE(requiredChild<QSlider>(harness.window, "progressSlider"),
           progressSlider);
  QCOMPARE(requiredChild<QListView>(harness.window, "playlistView"),
           playlistView);

  harness.presenter.openLocalFile(QStringLiteral("C:/video/theme-movie.mp4"));
  QTRY_COMPARE(centralSurface->property("themeMode").toString(),
               QStringLiteral("video"));
  QCOMPARE(modeBadge->text(), QStringLiteral("VIDEO"));

  auto *const playlistTabs =
      requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  playlistTabs->setCurrentIndex(1);
  QTRY_COMPARE(centralSurface->property("themeMode").toString(),
               QStringLiteral("live"));
  QCOMPARE(modeBadge->text(), QStringLiteral("LIVE"));
  QVERIFY(titleLabel->text().contains(QStringLiteral("直播")));
  QCOMPARE(requiredChild<QToolButton>(harness.window, "playPauseButton"),
           playPauseButton);
  QCOMPARE(requiredChild<QSlider>(harness.window, "progressSlider"),
           progressSlider);
  QCOMPARE(requiredChild<QListView>(harness.window, "playlistView"),
           playlistView);
}

void MainWindowTest::parsesSynchronizedAndPlainLyrics() {
  const QString source = QStringLiteral(
      "[ar:测试歌手]\n[00:01.2][00:02.34]第一句\n[01:03.456]第二句\n");
  const QVector<LyricLine> lines =
      lyrics_internal::parseSynchronizedLyrics(source);
  QCOMPARE(lines.size(), 3);
  QCOMPARE(lines[0].timeMilliseconds, 1200);
  QCOMPARE(lines[1].timeMilliseconds, 2340);
  QCOMPARE(lines[2].timeMilliseconds, 63456);
  QCOMPARE(lines[2].text, QStringLiteral("第二句"));
  QCOMPARE(lyrics_internal::plainTextFromLyrics(source),
           QStringLiteral("第一句\n第二句"));
}

void MainWindowTest::matchesQualifiedTitleByDurationWhenArtistCreditDiffers() {
  QVERIFY(lyrics_internal::isAcceptableTrackMatch(
      QStringLiteral("够爱(《终极一家》电视剧插曲)"), QStringLiteral("东城卫"),
      292126, QStringLiteral("够爱"), QStringLiteral("曾沛慈"), 291666));
  QVERIFY(!lyrics_internal::isAcceptableTrackMatch(
      QStringLiteral("够爱(《终极一家》电视剧插曲)"), QStringLiteral("东城卫"),
      292126, QStringLiteral("够爱"), QStringLiteral("其他歌手"), 245000));
}

void MainWindowTest::selectsKugouNestedAudioVersionAndDecodesLyrics() {
  const QByteArray searchPayload = R"json({
    "data": {"info": [
      {
        "hash": "wrong-duration",
        "songname": "够爱",
        "singername": "东城卫",
        "duration": 572,
        "group": [{
          "hash": "matched-audio",
          "songname": "够爱",
          "singername": "曾沛慈",
          "duration": 291
        }]
      },
      {
        "hash": "wrong-instrumental",
        "songname": "够爱 (纯音乐)",
        "songname_original": "够爱",
        "singername": "东城卫",
        "duration": 291
      }
    ]}
  })json";
  QCOMPARE(lyrics_internal::bestKugouTrackIdentity(
               searchPayload, QStringLiteral("够爱(电视剧插曲)"),
               QStringLiteral("东城卫"), 292126),
           QStringLiteral("matched-audio"));

  const QString source = QStringLiteral("[00:01.00]第一句\n");
  const QByteArray downloadPayload = QByteArrayLiteral("{\"content\":\"") +
                                     source.toUtf8().toBase64() +
                                     QByteArrayLiteral("\"}");
  QCOMPARE(lyrics_internal::decodeKugouLyricsPayload(downloadPayload), source);
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
  QVERIFY(harness.engine.commands().back().kind ==
          test::FakeEngineCommandKind::Play);
  QCOMPARE(statusText(harness), QStringLiteral("正在打开..."));
  QVERIFY(
      requiredChild<QToolButton>(harness.window, "stopButton")->isEnabled());
}

void MainWindowTest::opensValidatedNetworkUrlWithoutLoggingPrivateParts() {
  GuiHarness harness;
  const QString address = QStringLiteral(
      "https://user:secret@example.test/live/channel.m3u8?token=private#main");

  harness.presenter.openNetworkUrl(address);
  const auto commands = harness.engine.commands();
  QCOMPARE(static_cast<int>(commands.size()), 1);
  QVERIFY(commands.front().kind == test::FakeEngineCommandKind::Open);
  QVERIFY(commands.front().media.has_value());
  QVERIFY(commands.front().media->kind == core::MediaSourceKind::NetworkStream);
  QCOMPARE(commands.front().media->source, address.toUtf8().toStdString());
  QCOMPARE(commands.front().media->displayName, std::string("channel.m3u8"));

  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("channel.m3u8"));
  const std::string logs = harness.logOutput.str();
  QVERIFY(logs.find("network_media_added") != std::string::npos);
  QVERIFY(logs.find("channel.m3u8") != std::string::npos);
  QVERIFY(logs.find("user:secret") == std::string::npos);
  QVERIFY(logs.find("token=private") == std::string::npos);

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 1);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetVolume),
               1);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted), 1);
  const auto playingCommands = harness.engine.commands();
  const auto volumeCommand = std::find_if(
      playingCommands.begin(), playingCommands.end(), [](const auto &command) {
        return command.kind == test::FakeEngineCommandKind::SetVolume;
      });
  const auto muteCommand = std::find_if(
      playingCommands.begin(), playingCommands.end(), [](const auto &command) {
        return command.kind == test::FakeEngineCommandKind::SetMuted;
      });
  QVERIFY(volumeCommand != playingCommands.end());
  QVERIFY(muteCommand != playingCommands.end());
  QCOMPARE(volumeCommand->volume, 100);
  QVERIFY(!muteCommand->flag);
  harness.engine.emitPositionChanged(
      core::PlaybackPosition{5s, std::nullopt, false});
  QTRY_COMPARE(requiredChild<QLabel>(harness.window, "positionLabel")->text(),
               QStringLiteral("00:05 / --:--"));
  QVERIFY(
      !requiredChild<QSlider>(harness.window, "progressSlider")->isEnabled());
}

void MainWindowTest::expandsPlaylistUrlsAndFallsBackForSingleHlsStreams() {
  GuiHarness harness;
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const playlistUrlEdit =
      requiredChild<QLineEdit>(harness.window, "livePlaylistUrlEdit");
  const QString playlistUrl =
      QStringLiteral("https://example.test/tv/channels.m3u?token=private");

  harness.presenter.openNetworkUrl(playlistUrl);

  QCOMPARE(harness.livePlaylistService.loadCount, 1);
  QCOMPARE(harness.livePlaylistService.lastPlaylistUrl, playlistUrl);
  QVERIFY(harness.livePlaylistService.isPending);
  QCOMPARE(playlistUrlEdit->text(), playlistUrl);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);

  LivePlaylistLoadResult result;
  result.library.channels = {
      {"第一路", "", "https://stream.example/one", "", "", ""},
      {"第二路", "", "https://stream.example/two", "", "", ""}};
  harness.livePlaylistService.complete(std::move(result));
  QCOMPARE(playlistView->model()->rowCount(), 2);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("第一路"));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);

  const QString m3uUrlWithHlsLikeResponse =
      QStringLiteral("https://example.test/tv/still-a-list.m3u");
  harness.presenter.openNetworkUrl(m3uUrlWithHlsLikeResponse);
  harness.livePlaylistService.fail(LivePlaylistLoadError::HlsMediaManifest);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);
  QCOMPARE(playlistView->model()->rowCount(), 2);

  const QString hlsUrl =
      QStringLiteral("https://example.test/live/channel.m3u8");
  harness.presenter.openNetworkUrl(hlsUrl);
  QCOMPARE(harness.livePlaylistService.loadCount, 3);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 1);
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->source,
           hlsUrl.toUtf8().toStdString());
  QCOMPARE(playlistView->model()->rowCount(), 3);
}

void MainWindowTest::separatesLocalAndLiveListsWithoutStoppingPlayback() {
  GuiHarness harness;
  auto *const tabs = requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const openButton =
      requiredChild<QPushButton>(harness.window, "openFileButton");
  auto *const playlistUrlEdit =
      requiredChild<QLineEdit>(harness.window, "livePlaylistUrlEdit");

  QCOMPARE(tabs->currentIndex(), 0);
  QVERIFY(!openButton->isHidden());
  QVERIFY(playlistUrlEdit->isHidden());
  harness.presenter.addLocalFiles(
      {QStringLiteral("C:/local/one.mp3"), QStringLiteral("C:/local/two.mp4")});
  QCOMPARE(playlistView->model()->rowCount(), 2);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("one.mp3"));

  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/direct.m3u8"));
  QCOMPARE(tabs->currentIndex(), 1);
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("direct.m3u8"));
  QCOMPARE(playlistView->selectionMode(), QAbstractItemView::SingleSelection);
  QCOMPARE(playlistView->contextMenuPolicy(), Qt::CustomContextMenu);
  QVERIFY(openButton->isHidden());
  QVERIFY(!playlistUrlEdit->isHidden());

  const auto commandCountBeforeSwitch = harness.engine.commands().size();
  tabs->setCurrentIndex(0);
  QCoreApplication::processEvents();
  QCOMPARE(playlistView->model()->rowCount(), 2);
  QCOMPARE(playlistView->selectionMode(), QAbstractItemView::ExtendedSelection);
  QCOMPARE(playlistView->contextMenuPolicy(), Qt::CustomContextMenu);
  QCOMPARE(harness.engine.commands().size(), commandCountBeforeSwitch);

  requiredChild<QToolButton>(harness.window, "networkRefreshButton")->click();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 3);
  QCOMPARE(playlistView->model()->rowCount(), 2);
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->source,
           std::string("https://example.test/live/direct.m3u8"));
  const auto commandCountAfterRefresh = harness.engine.commands().size();

  tabs->setCurrentIndex(1);
  QCoreApplication::processEvents();
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(harness.engine.commands().size(), commandCountAfterRefresh);
}

void MainWindowTest::controlsAndMarksLiveSourcesFromRightClickMenu() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/one.m3u8"));
  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/two.m3u8"));
  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/three.m3u8"));

  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const model = playlistView->model();
  QCOMPARE(playlistView->contextMenuPolicy(), Qt::CustomContextMenu);
  auto *const menu =
      requiredChild<QMenu>(harness.window, "livePlaylistContextMenu");
  auto *const playbackAction =
      requiredChild<QAction>(harness.window, "livePlaylistPlaybackAction");
  auto *const stopAction =
      requiredChild<QAction>(harness.window, "livePlaylistStopAction");
  auto *const markAction =
      requiredChild<QAction>(harness.window, "livePlaylistMarkAction");
  auto *const favoriteAction =
      requiredChild<QAction>(harness.window, "livePlaylistFavoriteAction");
  auto *const locateButton =
      requiredChild<QPushButton>(harness.window, "livePlaylistLocateButton");
  QCOMPARE(menu->actions().size(), 4);
  QVERIFY(harness.window.findChild<QAction *>("livePlaylistRemoveAction") ==
          nullptr);

  const int opensBeforeContext =
      commandCount(harness, test::FakeEngineCommandKind::Open);
  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBeforeContext);
  QCOMPARE(playbackAction->text(), QStringLiteral("播放"));
  QVERIFY(playbackAction->isEnabled());
  QCOMPARE(stopAction->text(), QStringLiteral("停止"));
  QVERIFY(!stopAction->isEnabled());
  QCOMPARE(markAction->text(), QStringLiteral("标记"));
  QCOMPARE(favoriteAction->text(), QStringLiteral("收藏"));
  menu->hide();

  favoriteAction->trigger();
  QVERIFY(model->index(0, 0).data(PlaylistModel::kFavoriteRole).toBool());
  QVERIFY(model->index(0, 0)
              .data(Qt::DisplayRole)
              .toString()
              .startsWith(QStringLiteral("★ ")));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBeforeContext);
  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  QCOMPARE(favoriteAction->text(), QStringLiteral("取消收藏"));
  menu->hide();
  favoriteAction->trigger();
  QVERIFY(!model->index(0, 0).data(PlaylistModel::kFavoriteRole).toBool());
  QVERIFY(!model->index(0, 0)
               .data(Qt::DisplayRole)
               .toString()
               .startsWith(QStringLiteral("★ ")));

  playbackAction->trigger();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBeforeContext + 1);
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 1);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));

  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  QCOMPARE(playbackAction->text(), QStringLiteral("暂停"));
  QVERIFY(playbackAction->isEnabled());
  QVERIFY(stopAction->isEnabled());
  menu->hide();
  playbackAction->trigger();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Pause), 1);

  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  QVERIFY(stopAction->isEnabled());
  menu->hide();
  stopAction->trigger();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop), 1);

  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  menu->hide();
  markAction->trigger();
  const QModelIndex markedIndex = model->index(0, 0);
  QVERIFY(markedIndex.data(Qt::UserRole + 1).toBool());
  QVERIFY(markedIndex.data(Qt::DisplayRole)
              .toString()
              .contains(QStringLiteral("【不可用】")));
  QCOMPARE(markedIndex.data(Qt::ToolTipRole).toString(),
           QStringLiteral("此直播源不可用，右键可取消标记"));
  QVERIFY(markedIndex.data(Qt::ForegroundRole).isValid());
  QVERIFY(markedIndex.data(Qt::BackgroundRole).isValid());
  QVERIFY(markedIndex.data(Qt::FontRole).value<QFont>().bold());
  QVERIFY(!(model->flags(markedIndex) & Qt::ItemIsEnabled));
  QVERIFY(!(model->flags(markedIndex) & Qt::ItemIsSelectable));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop), 2);
  QVERIFY(!locateButton->isEnabled());

  const int opensBeforeMarkedActivation =
      commandCount(harness, test::FakeEngineCommandKind::Open);
  QVERIFY(QMetaObject::invokeMethod(playlistView, "doubleClicked",
                                    Qt::DirectConnection,
                                    Q_ARG(QModelIndex, markedIndex)));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBeforeMarkedActivation);
  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  QCOMPARE(markAction->text(), QStringLiteral("取消标记"));
  QVERIFY(!playbackAction->isEnabled());
  QVERIFY(!stopAction->isEnabled());
  menu->hide();
  markAction->trigger();
  QVERIFY(!model->index(0, 0).data(Qt::UserRole + 1).toBool());
  QVERIFY(model->flags(model->index(0, 0)) & Qt::ItemIsEnabled);

  const QPoint secondRowCenter =
      playlistView->visualRect(model->index(1, 0)).center();
  QMouseEvent rightDoubleClick(
      QEvent::MouseButtonDblClick, secondRowCenter, Qt::RightButton,
      Qt::RightButton, Qt::NoModifier);
  QApplication::sendEvent(playlistView->viewport(), &rightDoubleClick);
  menu->hide();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBeforeMarkedActivation);
  QCOMPARE(model->rowCount(), 3);
  QCOMPARE(model->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("one.m3u8"));
  QCOMPARE(model->index(1, 0).data(Qt::UserRole).toString(),
           QStringLiteral("two.m3u8"));
}

void MainWindowTest::keepsLiveListPositionAndLocatesCurrentPlayback() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  for (int row = 0; row < 40; ++row) {
    harness.presenter.openNetworkUrl(
        QStringLiteral("https://example.test/live/%1.m3u8")
            .arg(row, 2, 10, QChar('0')));
  }

  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const model = playlistView->model();
  auto *const scrollBar = playlistView->verticalScrollBar();
  auto *const locateButton =
      requiredChild<QPushButton>(harness.window, "livePlaylistLocateButton");
  auto *const menu =
      requiredChild<QMenu>(harness.window, "livePlaylistContextMenu");
  auto *const markAction =
      requiredChild<QAction>(harness.window, "livePlaylistMarkAction");
  auto *const favoriteAction =
      requiredChild<QAction>(harness.window, "livePlaylistFavoriteAction");
  QCOMPARE(model->rowCount(), 40);
  QVERIFY(locateButton->isVisible());
  QVERIFY(locateButton->isEnabled());

  playlistView->scrollTo(model->index(20, 0),
                         QAbstractItemView::PositionAtCenter);
  QCoreApplication::processEvents();
  QCOMPARE(playlistView->currentIndex().row(), 39);
  QVERIFY(requestPlaylistContextMenu(*playlistView, 20));
  menu->hide();
  playlistView->selectionModel()->clearSelection();
  const int scrollPositionBeforeMark = scrollBar->value();
  QVERIFY(scrollPositionBeforeMark > 0);
  connect(model, &QAbstractItemModel::dataChanged, scrollBar,
          [scrollBar] { scrollBar->setValue(scrollBar->maximum()); });
  markAction->trigger();
  QCoreApplication::processEvents();
  QCOMPARE(scrollBar->value(), scrollPositionBeforeMark);
  QVERIFY(model->index(20, 0)
              .data(Qt::DisplayRole)
              .toString()
              .contains(QStringLiteral("【不可用】")));
  QCOMPARE(playlistView->currentIndex().row(), 39);

  playlistView->scrollTo(model->index(21, 0),
                         QAbstractItemView::PositionAtCenter);
  QCoreApplication::processEvents();
  QVERIFY(requestPlaylistContextMenu(*playlistView, 21));
  menu->hide();
  playlistView->selectionModel()->clearSelection();
  const int scrollPositionBeforeFavorite = scrollBar->value();
  favoriteAction->trigger();
  QCoreApplication::processEvents();
  QCOMPARE(scrollBar->value(), scrollPositionBeforeFavorite);
  QVERIFY(model->index(21, 0).data(PlaylistModel::kFavoriteRole).toBool());
  QVERIFY(model->index(21, 0)
              .data(Qt::DisplayRole)
              .toString()
              .startsWith(QStringLiteral("★ ")));
  QCOMPARE(playlistView->currentIndex().row(), 39);

  playlistView->setCurrentIndex(model->index(0, 0));
  playlistView->scrollTo(model->index(0, 0),
                         QAbstractItemView::PositionAtTop);
  QCoreApplication::processEvents();
  QCOMPARE(playlistView->currentIndex().row(), 0);
  QTest::mouseClick(locateButton, Qt::LeftButton);
  QCOMPARE(playlistView->currentIndex().row(), 39);
  QVERIFY(scrollBar->value() > 0);
}

void MainWindowTest::replacesRemoteLiveListAtomicallyAndKeepsItOnFailure() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  harness.presenter.openLocalFile(QStringLiteral("C:/local/still-playing.mp3"));
  const int opensBeforeLoad =
      commandCount(harness, test::FakeEngineCommandKind::Open);
  auto *const tabs = requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const playlistUrlEdit =
      requiredChild<QLineEdit>(harness.window, "livePlaylistUrlEdit");
  auto *const loadButton =
      requiredChild<QPushButton>(harness.window, "livePlaylistLoadButton");
  auto *const statusLabel =
      requiredChild<QLabel>(harness.window, "livePlaylistStatusLabel");

  tabs->setCurrentIndex(1);
  playlistUrlEdit->setText(QStringLiteral(
      "https://user:secret@example.test/list.m3u?token=private"));
  QTest::mouseClick(loadButton, Qt::LeftButton);
  QCOMPARE(harness.livePlaylistService.loadCount, 1);
  QVERIFY(harness.livePlaylistService.isPending);
  QVERIFY(!loadButton->isEnabled());
  QVERIFY(statusLabel->text().contains(QStringLiteral("正在读取")));

  LivePlaylistLoadResult firstResult;
  firstResult.library.channels = {
      core::LiveChannel{"第一路", "分类不显示", "https://example.test/one", "",
                        "", ""},
      core::LiveChannel{"第二路", "另一个分类", "rtsp://192.0.2.2/live", "", "",
                        ""},
  };
  firstResult.duplicateChannelCount = 2;
  firstResult.skippedChannelCount = 1;
  harness.livePlaylistService.complete(std::move(firstResult));

  QVERIFY(loadButton->isEnabled());
  QCOMPARE(playlistView->model()->rowCount(), 2);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("第一路"));
  QCOMPARE(playlistView->model()->index(1, 0).data(Qt::UserRole).toString(),
           QStringLiteral("第二路"));
  QVERIFY(!playlistView->model()
               ->index(0, 0)
               .data(Qt::DisplayRole)
               .toString()
               .contains(QStringLiteral("分类不显示")));
  QVERIFY(statusLabel->text().contains(QStringLiteral("已载入 2 项")));
  QVERIFY(statusLabel->text().contains(QStringLiteral("重复 2 项")));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBeforeLoad);

  const QModelIndex secondChannel = playlistView->model()->index(1, 0);
  QVERIFY(QMetaObject::invokeMethod(playlistView, "doubleClicked",
                                    Qt::DirectConnection,
                                    Q_ARG(QModelIndex, secondChannel)));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBeforeLoad + 1);
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->source,
           std::string("rtsp://192.0.2.2/live"));
  QCOMPARE(harness.engine.commands().back().media->displayName,
           QStringLiteral("第二路").toUtf8().toStdString());
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 1);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));

  QTest::mouseClick(loadButton, Qt::LeftButton);
  QCOMPARE(harness.livePlaylistService.loadCount, 2);
  harness.livePlaylistService.fail(LivePlaylistLoadError::NetworkFailure);
  QCOMPARE(playlistView->model()->rowCount(), 2);
  QVERIFY(statusLabel->text().contains(QStringLiteral("读取失败")));
  QVERIFY(!statusLabel->text().contains(QStringLiteral("token=private")));
  QVERIFY(harness.logOutput.str().find("token=private") == std::string::npos);

  LivePlaylistLoadResult replacement;
  replacement.library.channels = {
      core::LiveChannel{"替换后", "", "https://example.test/replaced", "", "",
                        ""},
  };
  QTest::mouseClick(loadButton, Qt::LeftButton);
  harness.livePlaylistService.complete(std::move(replacement));
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("替换后"));
  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  auto *const playbackAction =
      requiredChild<QAction>(harness.window, "livePlaylistPlaybackAction");
  QCOMPARE(playbackAction->text(), QStringLiteral("播放"));
  QVERIFY(playbackAction->isEnabled());
  requiredChild<QMenu>(harness.window, "livePlaylistContextMenu")->hide();
}

void MainWindowTest::
    keepsNetworkStreamsNonSeekableWhenEngineReportsLiveWindow() {
  GuiHarness harness;
  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/channel.m3u8"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);

  auto *const positionLabel =
      requiredChild<QLabel>(harness.window, "positionLabel");
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");

  harness.engine.emitPositionChanged(core::PlaybackPosition{5s, 24s, true});
  QTRY_COMPARE(positionLabel->text(), QStringLiteral("00:05 / --:--"));
  QCOMPARE(progressSlider->value(), 0);
  QVERIFY(!progressSlider->isEnabled());

  harness.engine.emitDurationChanged(24s);
  QTRY_COMPARE(positionLabel->text(), QStringLiteral("00:05 / --:--"));
  QCOMPARE(progressSlider->value(), 0);
  QVERIFY(!progressSlider->isEnabled());
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 0);
}

void MainWindowTest::rejectsInvalidNetworkUrlBeforeCallingEngine() {
  GuiHarness harness;

  harness.presenter.openNetworkUrl(
      QStringLiteral("file:///C:/private/live.m3u8"));

  QCOMPARE(static_cast<int>(harness.engine.commands().size()), 0);
  QCOMPARE(requiredChild<QListView>(harness.window, "playlistView")
               ->model()
               ->rowCount(),
           0);
  auto *const errorLabel =
      requiredChild<QLabel>(harness.window, "playbackErrorLabel");
  QVERIFY(!errorLabel->isHidden());
  QVERIFY(errorLabel->text().contains(QStringLiteral("暂不支持")));
  QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));
  const std::string logs = harness.logOutput.str();
  QVERIFY(logs.find("network_url_rejected") != std::string::npos);
  QVERIFY(logs.find("C:/private") == std::string::npos);
}

void MainWindowTest::rendersNetworkBufferingAndStopsTimeoutAfterPlaying() {
  GuiHarness harness;
  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/channel.m3u8"));
  auto *const timeoutTimer =
      harness.presenter.findChild<QTimer *>("networkOpenTimeoutTimer");
  QVERIFY(timeoutTimer != nullptr);
  QVERIFY(timeoutTimer->isActive());
  QCOMPARE(statusText(harness), QStringLiteral("正在连接..."));
  QVERIFY(
      requiredChild<QToolButton>(harness.window, "stopButton")->isEnabled());

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 1);
  harness.engine.emitBufferingChanged(42);
  harness.engine.emitStateChanged(core::PlaybackState::Buffering);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在缓冲 42%"));
  harness.engine.emitBufferingChanged(67);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在缓冲 67%"));

  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));
  QVERIFY(!timeoutTimer->isActive());
}

void MainWindowTest::
    cancelsAndTimesOutNetworkOpeningWithoutAcceptingLateEvents() {
  {
    GuiHarness harness;
    harness.presenter.openNetworkUrl(
        QStringLiteral("https://example.test/live/cancel.m3u8"));
    QTest::mouseClick(requiredChild<QToolButton>(harness.window, "stopButton"),
                      Qt::LeftButton);
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop), 1);
    QCOMPARE(statusText(harness), QStringLiteral("已取消连接"));
    QVERIFY(!harness.presenter.findChild<QTimer *>("networkOpenTimeoutTimer")
                 ->isActive());

    harness.engine.emitStateChanged(core::PlaybackState::Buffering);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QCoreApplication::processEvents();
    QCOMPARE(statusText(harness), QStringLiteral("已取消连接"));

    QTest::mouseClick(
        requiredChild<QToolButton>(harness.window, "playPauseButton"),
        Qt::LeftButton);
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 2);
    QCOMPARE(statusText(harness), QStringLiteral("正在连接..."));
  }

  {
    GuiHarness harness;
    harness.presenter.openNetworkUrl(
        QStringLiteral("https://example.test/live/timeout.m3u8"));
    auto *const timeoutTimer =
        harness.presenter.findChild<QTimer *>("networkOpenTimeoutTimer");
    QVERIFY(timeoutTimer != nullptr);
    timeoutTimer->start(1);
    QTRY_COMPARE(statusText(harness), QStringLiteral("连接超时"));
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop), 1);
    QVERIFY(requiredChild<QLabel>(harness.window, "playbackErrorLabel")
                ->text()
                .contains(QStringLiteral("连接网络媒体超时")));
    QVERIFY(harness.logOutput.str().find("network_open_timeout") !=
            std::string::npos);

    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QCoreApplication::processEvents();
    QCOMPARE(statusText(harness), QStringLiteral("连接超时"));
    QTest::mouseClick(
        requiredChild<QToolButton>(harness.window, "playPauseButton"),
        Qt::LeftButton);
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 2);
    QCOMPARE(statusText(harness), QStringLiteral("正在连接..."));
  }
}

void MainWindowTest::doesNotAutomaticallyReconnectInterruptedNetwork() {
  {
    GuiHarness harness;
    auto *const refreshButton =
        requiredChild<QToolButton>(harness.window, "networkRefreshButton");
    QVERIFY(refreshButton->isHidden());
    harness.presenter.openNetworkUrl(
        QStringLiteral("https://example.test/live/manual.m3u8?token=private"));
    QVERIFY(!refreshButton->isHidden());
    QVERIFY(refreshButton->isEnabled());
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 1);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    harness.engine.emitStateChanged(core::PlaybackState::Failed);

    QTRY_COMPARE(statusText(harness), QStringLiteral("直播已断开，请点击刷新"));
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 1);
    QVERIFY(harness.presenter.findChild<QTimer *>("networkReconnectTimer") ==
            nullptr);
    QVERIFY(harness.logOutput.str().find("network_reconnect") ==
            std::string::npos);
    QVERIFY(harness.logOutput.str().find("token=private") == std::string::npos);
  }

  {
    GuiHarness harness;
    harness.presenter.openNetworkUrl(
        QStringLiteral("https://example.test/live/end.m3u8"));
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    harness.engine.emitEndReached();

    QTRY_COMPARE(statusText(harness), QStringLiteral("直播已断开，请点击刷新"));
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 1);
  }
}

void MainWindowTest::refreshesCurrentNetworkByButtonAndF5() {
  GuiHarness harness;
  auto *const refreshButton =
      requiredChild<QToolButton>(harness.window, "networkRefreshButton");
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/refresh.m3u8"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(harness.window.recentNetworkUrls().size(), 1);

  QTest::mouseClick(refreshButton, Qt::LeftButton);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 2);
  QCOMPARE(statusText(harness), QStringLiteral("正在刷新直播..."));
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(harness.window.recentNetworkUrls().size(), 1);
  QVERIFY(harness.logOutput.str().find("network_refresh_requested") !=
          std::string::npos);

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 2);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));

  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();
  QTest::keyClick(&harness.window, Qt::Key_F5);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 3);
  QCOMPARE(statusText(harness), QStringLiteral("正在刷新直播..."));
  QCOMPARE(playlistView->model()->rowCount(), 1);

  GuiHarness localHarness;
  localHarness.presenter.openLocalFile(QStringLiteral("C:/media/local.mp3"));
  auto *const localRefreshButton =
      requiredChild<QToolButton>(localHarness.window, "networkRefreshButton");
  QVERIFY(localRefreshButton->isHidden());
  localHarness.window.show();
  localHarness.window.activateWindow();
  QCoreApplication::processEvents();
  const int localOpenCount =
      commandCount(localHarness, test::FakeEngineCommandKind::Open);
  QTest::keyClick(&localHarness.window, Qt::Key_F5);
  QCoreApplication::processEvents();
  QCOMPARE(commandCount(localHarness, test::FakeEngineCommandKind::Open),
           localOpenCount);
}

void MainWindowTest::remembersRecentNetworkUrlsForCurrentSession() {
  GuiHarness harness;
  QVERIFY(harness.window.recentNetworkUrls().isEmpty());

  harness.presenter.openNetworkUrl(QStringLiteral("not-a-network-url"));
  QVERIFY(harness.window.recentNetworkUrls().isEmpty());

  QStringList addresses;
  for (int index = 0; index < 12; ++index) {
    const QString address =
        QStringLiteral("https://example.test/live/%1.m3u8?token=%2")
            .arg(index)
            .arg(index);
    addresses.append(address);
    harness.presenter.openNetworkUrl(address);
  }

  QStringList expected;
  for (int index = 11; index >= 2; --index) {
    expected.append(addresses.at(index));
  }
  QCOMPARE(harness.window.recentNetworkUrls(), expected);

  harness.presenter.openNetworkUrl(addresses.at(5));
  expected.removeAll(addresses.at(5));
  expected.prepend(addresses.at(5));
  QCOMPARE(harness.window.recentNetworkUrls(), expected);

  GuiHarness newSession;
  QVERIFY(newSession.window.recentNetworkUrls().isEmpty());
}

void MainWindowTest::routesPauseResumeAndStopThroughPresenter() {
  GuiHarness harness;
  openAndReachPlaying(harness);

  auto *const playPauseButton =
      requiredChild<QToolButton>(harness.window, "playPauseButton");
  auto *const stopButton =
      requiredChild<QToolButton>(harness.window, "stopButton");
  QVERIFY(playPauseButton->isEnabled());
  QCOMPARE(playPauseButton->accessibleName(), QStringLiteral("暂停"));
  const qint64 pauseIconKey = playPauseButton->icon().cacheKey();
  QTest::mouseClick(playPauseButton, Qt::LeftButton);
  QVERIFY(harness.engine.commands().back().kind ==
          test::FakeEngineCommandKind::Pause);

  harness.engine.emitStateChanged(core::PlaybackState::Paused);
  QTRY_COMPARE(playPauseButton->accessibleName(), QStringLiteral("播放"));
  QVERIFY(playPauseButton->isEnabled());
  QVERIFY(playPauseButton->icon().cacheKey() != pauseIconKey);
  QTest::mouseClick(playPauseButton, Qt::LeftButton);
  QVERIFY(harness.engine.commands().back().kind ==
          test::FakeEngineCommandKind::Play);

  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_VERIFY(stopButton->isEnabled());
  QTest::mouseClick(stopButton, Qt::LeftButton);
  QVERIFY(harness.engine.commands().back().kind ==
          test::FakeEngineCommandKind::Stop);

  harness.engine.emitStateChanged(core::PlaybackState::Stopped);
  QTRY_COMPARE(statusText(harness), QStringLiteral("已停止"));
  QVERIFY(playPauseButton->isEnabled());
  QCOMPARE(playPauseButton->accessibleName(), QStringLiteral("播放"));
  QVERIFY(!stopButton->isEnabled());
}

void MainWindowTest::rendersBufferingAsNonInteractiveWait() {
  GuiHarness harness;
  harness.presenter.openLocalFile(QStringLiteral("C:/buffering.wav"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()), 2);

  harness.engine.emitStateChanged(core::PlaybackState::Buffering);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在缓冲..."));
  QVERIFY(!requiredChild<QToolButton>(harness.window, "playPauseButton")
               ->isEnabled());
  QVERIFY(
      requiredChild<QToolButton>(harness.window, "stopButton")->isEnabled());
}

void MainWindowTest::allowsReplayAfterNaturalEnd() {
  {
    GuiHarness harness;
    openAndReachPlaying(harness);

    harness.engine.emitEndReached();
    QTRY_COMPARE(statusText(harness), QStringLiteral("播放结束"));
    QVERIFY(requiredChild<QToolButton>(harness.window, "playPauseButton")
                ->isEnabled());
    QVERIFY(
        !requiredChild<QToolButton>(harness.window, "stopButton")->isEnabled());

    const int opensBefore =
        commandCount(harness, test::FakeEngineCommandKind::Open);
    const int playsBefore =
        commandCount(harness, test::FakeEngineCommandKind::Play);
    QTest::mouseClick(
        requiredChild<QToolButton>(harness.window, "playPauseButton"),
        Qt::LeftButton);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
                 opensBefore + 1);
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play),
                 playsBefore + 1);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
    QCOMPARE(harness.engine.commands().back().position, 0ms);
  }

  {
    GuiHarness harness;
    openAndReachPlaying(harness);
    harness.window.show();
    QCoreApplication::processEvents();
    harness.engine.emitEndReached();
    QTRY_COMPARE(statusText(harness), QStringLiteral("播放结束"));

    const int opensBefore =
        commandCount(harness, test::FakeEngineCommandKind::Open);
    const int playsBefore =
        commandCount(harness, test::FakeEngineCommandKind::Play);
    QTest::keyClick(&harness.window, Qt::Key_Space);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
                 opensBefore + 1);
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play),
                 playsBefore + 1);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
    QCOMPARE(harness.engine.commands().back().position, 0ms);
  }
}

void MainWindowTest::keepsProgressAtEndAfterLatePositionEvents() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  auto *const positionLabel =
      requiredChild<QLabel>(harness.window, "positionLabel");

  harness.engine.emitDurationChanged(6s);
  harness.engine.emitPositionChanged(core::PlaybackPosition{2500ms, 6s, true});
  harness.engine.emitEndReached();

  QTRY_COMPARE(statusText(harness), QStringLiteral("播放结束"));
  QCOMPARE(progressSlider->value(), 1000);
  QCOMPARE(positionLabel->text(), QStringLiteral("00:06 / 00:06"));

  harness.engine.emitPositionChanged(core::PlaybackPosition{3s, 6s, true});
  harness.engine.emitDurationChanged(6s);
  QCoreApplication::processEvents();
  QCOMPARE(progressSlider->value(), 1000);
  QCOMPARE(positionLabel->text(), QStringLiteral("00:06 / 00:06"));
}

void MainWindowTest::keepsNaturalEndInteractiveAfterLateStoppedEvent() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");

  harness.engine.emitPositionChanged(core::PlaybackPosition{6s, 6s, false});
  harness.engine.emitEndReached();
  harness.engine.emitPositionChanged(core::PlaybackPosition{0ms, 6s, false});
  harness.engine.emitStateChanged(core::PlaybackState::Stopped);
  QTRY_COMPARE(statusText(harness), QStringLiteral("播放结束"));
  QCOMPARE(progressSlider->value(), 1000);
  QVERIFY(progressSlider->isEnabled());

  const int stopsBeforeClick =
      commandCount(harness, test::FakeEngineCommandKind::Stop);
  const int opensBeforeClick =
      commandCount(harness, test::FakeEngineCommandKind::Open);
  const int playsBeforeClick =
      commandCount(harness, test::FakeEngineCommandKind::Play);
  QTest::mouseClick(
      progressSlider, Qt::LeftButton, Qt::NoModifier,
      QPoint(progressSlider->width() / 2, progressSlider->height() / 2));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop),
           stopsBeforeClick);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
               opensBeforeClick + 1);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Play),
           playsBeforeClick);

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play),
               playsBeforeClick + 1);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
  const auto seekCommand = harness.engine.commands().back();
  QVERIFY(seekCommand.kind == test::FakeEngineCommandKind::Seek);
  QVERIFY(seekCommand.position >= 2900ms && seekCommand.position <= 3100ms);
}

void MainWindowTest::resumesAfterSeekingFromNaturalEnd() {
  {
    GuiHarness harness;
    openAndReachPlaying(harness);
    harness.window.show();
    QCoreApplication::processEvents();
    auto *const progressSlider =
        requiredChild<QSlider>(harness.window, "progressSlider");
    // libVLC 在 Ended 附近可能把 seekable 临时改为
    // false，结束态仍应允许重启定位。
    harness.engine.emitPositionChanged(
        core::PlaybackPosition{120s, 120s, false});
    harness.engine.emitEndReached();
    QTRY_COMPARE(statusText(harness), QStringLiteral("播放结束"));
    QVERIFY(progressSlider->isEnabled());

    const int seeksBeforeClick =
        commandCount(harness, test::FakeEngineCommandKind::Seek);
    const int playsBeforeClick =
        commandCount(harness, test::FakeEngineCommandKind::Play);
    const int opensBeforeClick =
        commandCount(harness, test::FakeEngineCommandKind::Open);
    const int stopsBeforeClick =
        commandCount(harness, test::FakeEngineCommandKind::Stop);
    QTest::mouseClick(
        progressSlider, Qt::LeftButton, Qt::NoModifier,
        QPoint(progressSlider->width() / 4, progressSlider->height() / 2));
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop),
             stopsBeforeClick);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
                 opensBeforeClick + 1);
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek),
             seeksBeforeClick);
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Play),
             playsBeforeClick);
    const int restartedProgress = progressSlider->value();
    QVERIFY(restartedProgress >= 230 && restartedProgress <= 270);

    harness.engine.emitPositionChanged(core::PlaybackPosition{0ms, 120s, true});
    QCoreApplication::processEvents();
    QCOMPARE(progressSlider->value(), restartedProgress);
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play),
                 playsBeforeClick + 1);
    harness.engine.emitPositionChanged(core::PlaybackPosition{0ms, 120s, true});
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek),
                 seeksBeforeClick + 1);
    const auto commands = harness.engine.commands();
    QVERIFY(commands[commands.size() - 3].kind ==
            test::FakeEngineCommandKind::Open);
    QVERIFY(commands[commands.size() - 2].kind ==
            test::FakeEngineCommandKind::Play);
    QVERIFY(commands.back().kind == test::FakeEngineCommandKind::Seek);
    const auto expectedPosition = std::chrono::milliseconds(
        120000LL * restartedProgress / kProgressMaximum);
    QCOMPARE(commands.back().position, expectedPosition);
    harness.engine.emitPositionChanged(core::PlaybackPosition{31s, 120s, true});
    QTRY_COMPARE(progressSlider->value(), 258);
    QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));
  }

  {
    GuiHarness harness;
    openAndReachPlaying(harness);
    auto *const progressSlider =
        requiredChild<QSlider>(harness.window, "progressSlider");
    harness.engine.emitPositionChanged(
        core::PlaybackPosition{120s, 120s, true});
    harness.engine.emitEndReached();
    QTRY_VERIFY(progressSlider->isEnabled());

    progressSlider->setSliderDown(true);
    progressSlider->setSliderPosition(500);
    progressSlider->setSliderDown(false);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 2);
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop), 0);
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 0);
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 2);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
    const auto commands = harness.engine.commands();
    QVERIFY(commands[commands.size() - 3].kind ==
            test::FakeEngineCommandKind::Open);
    QVERIFY(commands[commands.size() - 2].kind ==
            test::FakeEngineCommandKind::Play);
    QCOMPARE(commands.back().position, 60s);
  }
}

void MainWindowTest::displaysEngineErrorWithoutBlockingDialog() {
  GuiHarness harness;
  auto *const errorLabel =
      requiredChild<QLabel>(harness.window, "playbackErrorLabel");

  harness.engine.emitError(core::PlaybackError{
      core::PlaybackErrorKind::SourceNotFound,
      "Missing test media",
      "找不到测试媒体。",
  });

  QTRY_COMPARE(statusText(harness), QStringLiteral("播放失败"));
  QCOMPARE(errorLabel->text(), QStringLiteral("找不到测试媒体。"));
  QVERIFY(!errorLabel->isHidden());
  QVERIFY(!requiredChild<QToolButton>(harness.window, "playPauseButton")
               ->isEnabled());

  harness.presenter.openLocalFile(QStringLiteral("C:/新的媒体.wav"));
  QVERIFY(errorLabel->isHidden());
}

void MainWindowTest::keepsOtherPlaylistItemsUsableAfterFailure() {
  GuiHarness harness;
  harness.presenter.addLocalFiles(
      {QStringLiteral("C:/bad.mp4"), QStringLiteral("C:/good.mp4")});
  harness.engine.emitError(core::PlaybackError{
      core::PlaybackErrorKind::UnsupportedFormat,
      "invalid media",
      "无法播放媒体“bad.mp4”，文件内容可能损坏或格式不受支持。",
  });
  QTRY_COMPARE(statusText(harness), QStringLiteral("播放失败"));

  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  QVERIFY(QMetaObject::invokeMethod(
      playlistView, "doubleClicked", Qt::DirectConnection,
      Q_ARG(QModelIndex, playlistView->model()->index(1, 0))));
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->displayName,
           std::string("good.mp4"));
  QVERIFY(
      requiredChild<QLabel>(harness.window, "playbackErrorLabel")->isHidden());
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));
}

void MainWindowTest::writesLifecycleLogsWithoutMediaPath() {
  GuiHarness harness;
  const QString source = QStringLiteral("C:/private/folder/秘密 视频.mp4");
  harness.presenter.openLocalFile(source);
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  harness.engine.emitError(core::PlaybackError{
      core::PlaybackErrorKind::UnsupportedFormat,
      "libVLC reported invalid media",
      "无法播放媒体。",
  });
  QTRY_COMPARE(statusText(harness), QStringLiteral("播放失败"));

  const std::string logs = harness.logOutput.str();
  QVERIFY(logs.find("media_open_requested") != std::string::npos);
  QVERIFY(logs.find("秘密 视频.mp4") != std::string::npos);
  QVERIFY(logs.find("state_changed") != std::string::npos);
  QVERIFY(logs.find("unsupported_format") != std::string::npos);
  QVERIFY(logs.find("C:/private/folder") == std::string::npos);
}

void MainWindowTest::appliesWorkerThreadStateOnGuiThread() {
  GuiHarness harness;
  harness.presenter.openLocalFile(QStringLiteral("C:/thread-test.wav"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()), 2);

  QThread *handledThread = nullptr;
  connect(&harness.presenter, &PlayerPresenter::stateApplied, &harness.window,
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
  QThread *handledThread = nullptr;
  core::PlaybackPosition receivedPosition;
  OptionalDuration receivedDuration;
  int receivedBufferingPercentage = 0;
  core::AudioWaveform receivedWaveform;
  core::PlaybackError receivedError;

  connect(
      &bridge, &EngineEventBridge::stateChanged, &receiver,
      [&receivedCount, &handledThread](core::PlaybackState) {
        ++receivedCount;
        handledThread = QThread::currentThread();
      },
      Qt::QueuedConnection);
  connect(
      &bridge, &EngineEventBridge::positionChanged, &receiver,
      [&receivedCount, &receivedPosition](core::PlaybackPosition position) {
        ++receivedCount;
        receivedPosition = position;
      },
      Qt::QueuedConnection);
  connect(
      &bridge, &EngineEventBridge::durationChanged, &receiver,
      [&receivedCount, &receivedDuration](OptionalDuration duration) {
        ++receivedCount;
        receivedDuration = duration;
      },
      Qt::QueuedConnection);
  connect(
      &bridge, &EngineEventBridge::bufferingChanged, &receiver,
      [&receivedCount, &receivedBufferingPercentage](const int percentage) {
        ++receivedCount;
        receivedBufferingPercentage = percentage;
      },
      Qt::QueuedConnection);
  connect(
      &bridge, &EngineEventBridge::audioWaveformChanged, &receiver,
      [&receivedCount, &receivedWaveform](core::AudioWaveform waveform) {
        ++receivedCount;
        receivedWaveform = std::move(waveform);
      },
      Qt::QueuedConnection);
  connect(
      &bridge, &EngineEventBridge::endReached, &receiver,
      [&receivedCount] { ++receivedCount; }, Qt::QueuedConnection);
  connect(
      &bridge, &EngineEventBridge::errorOccurred, &receiver,
      [&receivedCount, &receivedError](core::PlaybackError error) {
        ++receivedCount;
        receivedError = std::move(error);
      },
      Qt::QueuedConnection);

  std::thread worker([&bridge] {
    core::AudioWaveform waveform;
    waveform.samples[12] = 0.6F;
    waveform.intensity = 0.7F;
    bridge.onStateChanged(core::PlaybackState::Playing);
    bridge.onPositionChanged(core::PlaybackPosition{750ms, 3s, true});
    bridge.onDurationChanged(3s);
    bridge.onBufferingChanged(48);
    bridge.onAudioWaveformChanged(waveform);
    bridge.onEndReached();
    bridge.onError(core::PlaybackError{
        core::PlaybackErrorKind::Unknown,
        "Worker event",
        "工作线程错误",
    });
  });
  worker.join();

  QTRY_COMPARE(receivedCount, 7);
  QCOMPARE(handledThread, QCoreApplication::instance()->thread());
  QCOMPARE(receivedPosition.current, 750ms);
  QVERIFY(receivedPosition.total ==
          std::optional<std::chrono::milliseconds>(3s));
  QVERIFY(receivedDuration == std::optional<std::chrono::milliseconds>(3s));
  QCOMPARE(receivedBufferingPercentage, 48);
  QCOMPARE(receivedWaveform.samples[12], 0.6F);
  QCOMPARE(receivedWaveform.intensity, 0.7F);
  QCOMPARE(receivedError.userMessage, std::string("工作线程错误"));
}

void MainWindowTest::rendersPositionDurationAndSeekability() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  auto *const positionLabel =
      requiredChild<QLabel>(harness.window, "positionLabel");

  harness.engine.emitPositionChanged(core::PlaybackPosition{65s, 125s, true});
  QTRY_COMPARE(positionLabel->text(), QStringLiteral("01:05 / 02:05"));
  QCOMPARE(progressSlider->value(), 520);
  QVERIFY(progressSlider->isEnabled());
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 0);

  harness.engine.emitPositionChanged(
      core::PlaybackPosition{3661s, 7322s, true});
  QTRY_COMPARE(positionLabel->text(), QStringLiteral("1:01:01 / 2:02:02"));
  QCOMPARE(progressSlider->value(), 500);

  harness.engine.emitPositionChanged(
      core::PlaybackPosition{2s, std::nullopt, false});
  QTRY_COMPARE(positionLabel->text(), QStringLiteral("00:02 / --:--"));
  QCOMPARE(progressSlider->value(), 0);
  QVERIFY(!progressSlider->isEnabled());
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 0);

  harness.presenter.openLocalFile(QStringLiteral("C:/audio/next.wav"));
  QCOMPARE(positionLabel->text(), QStringLiteral("00:00 / --:--"));
  QCOMPARE(progressSlider->value(), 0);
  QVERIFY(!progressSlider->isEnabled());
}

void MainWindowTest::seeksOnceWhenProgressTrackIsClicked() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  harness.engine.emitPositionChanged(core::PlaybackPosition{10s, 120s, true});
  QTRY_VERIFY(progressSlider->isEnabled());

  const std::array targetValues{250, 500, 750};
  for (int index = 0; index < static_cast<int>(targetValues.size()); ++index) {
    const int targetValue = targetValues[static_cast<std::size_t>(index)];
    const int clickX =
        qRound((progressSlider->width() - 1) *
               static_cast<double>(targetValue) / kProgressMaximum);
    QTest::mouseClick(progressSlider, Qt::LeftButton, Qt::NoModifier,
                      QPoint(clickX, progressSlider->height() / 2));

    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek),
                 index + 1);
    QVERIFY(qAbs(progressSlider->value() - targetValue) <= 20);
    const auto expectedPosition = std::chrono::milliseconds(
        120000LL * progressSlider->value() / kProgressMaximum);
    QCOMPARE(harness.engine.commands().back().position, expectedPosition);
    QCOMPARE(statusText(harness), QStringLiteral("正在播放"));
  }

  harness.engine.emitStateChanged(core::PlaybackState::Paused);
  QTRY_COMPARE(statusText(harness), QStringLiteral("已暂停"));
  const int seeksBeforePausedClick =
      commandCount(harness, test::FakeEngineCommandKind::Seek);
  QTest::mouseClick(
      progressSlider, Qt::LeftButton, Qt::NoModifier,
      QPoint(progressSlider->width() * 2 / 5, progressSlider->height() / 2));
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek),
               seeksBeforePausedClick + 1);
  QCOMPARE(statusText(harness), QStringLiteral("已暂停"));

  GuiHarness disabledHarness;
  auto *const disabledSlider =
      requiredChild<QSlider>(disabledHarness.window, "progressSlider");
  QTest::mouseClick(disabledSlider, Qt::LeftButton, Qt::NoModifier,
                    disabledSlider->rect().center());
  QCOMPARE(commandCount(disabledHarness, test::FakeEngineCommandKind::Seek), 0);
}

void MainWindowTest::keepsSeekPreviewStableAndSeeksOnlyOnRelease() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  auto *const positionLabel =
      requiredChild<QLabel>(harness.window, "positionLabel");
  harness.engine.emitPositionChanged(core::PlaybackPosition{30s, 120s, true});
  QTRY_VERIFY(progressSlider->isEnabled());
  QCOMPARE(progressSlider->value(), 250);

  const int commandsBeforeDrag =
      static_cast<int>(harness.engine.commands().size());
  progressSlider->setSliderDown(true);
  progressSlider->setSliderPosition(750);
  QTRY_COMPARE(positionLabel->text(), QStringLiteral("01:30 / 02:00"));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 0);

  harness.engine.emitPositionChanged(core::PlaybackPosition{40s, 120s, true});
  QCoreApplication::processEvents();
  QCOMPARE(positionLabel->text(), QStringLiteral("01:30 / 02:00"));

  progressSlider->setSliderDown(false);
  QTRY_COMPARE(static_cast<int>(harness.engine.commands().size()),
               commandsBeforeDrag + 1);
  const auto commands = harness.engine.commands();
  QVERIFY(commands.back().kind == test::FakeEngineCommandKind::Seek);
  QCOMPARE(commands.back().position, 90s);
  QCOMPARE(statusText(harness), QStringLiteral("正在播放"));
}

void MainWindowTest::keepsPausedSeekEnabledAcrossRepeatedDrags() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  harness.engine.emitPositionChanged(core::PlaybackPosition{30s, 120s, true});
  harness.engine.emitStateChanged(core::PlaybackState::Paused);
  QTRY_COMPARE(statusText(harness), QStringLiteral("已暂停"));
  QTRY_VERIFY(progressSlider->isEnabled());

  progressSlider->setSliderDown(true);
  progressSlider->setSliderPosition(500);
  progressSlider->setSliderDown(false);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
  QCOMPARE(harness.engine.commands().back().position, 60s);

  // libVLC 适配层会吸收暂停定位产生的缓冲事件，Presenter 继续保持暂停语义。
  harness.engine.emitPositionChanged(core::PlaybackPosition{60s, 120s, true});
  QTRY_COMPARE(statusText(harness), QStringLiteral("已暂停"));
  QTRY_VERIFY(progressSlider->isEnabled());
  progressSlider->setSliderDown(true);
  progressSlider->setSliderPosition(750);
  progressSlider->setSliderDown(false);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 2);
  QCOMPARE(harness.engine.commands().back().position, 90s);
  QCOMPARE(statusText(harness), QStringLiteral("已暂停"));
  QVERIFY(progressSlider->isEnabled());
}

void MainWindowTest::routesVolumeAndMuteWithoutChangingPlaybackState() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const volumeSlider =
      requiredChild<QSlider>(harness.window, "volumeSlider");
  auto *const volumeLabel =
      requiredChild<QLabel>(harness.window, "volumeLabel");
  auto *const volumeButton =
      requiredChild<QToolButton>(harness.window, "volumeButton");
  auto *const volumePopup =
      requiredChild<QWidget>(harness.window, "volumePopup");

  QTest::mouseMove(volumeButton, volumeButton->rect().center());
  QEvent volumeEnter(QEvent::Enter);
  QApplication::sendEvent(volumeButton, &volumeEnter);
  QTRY_VERIFY(volumePopup->isVisible());
  const QRect buttonGlobal(volumeButton->mapToGlobal(QPoint(0, 0)),
                           volumeButton->size());
  const QRect popupGlobal(volumePopup->mapToGlobal(QPoint(0, 0)),
                          volumePopup->size());
  QCOMPARE(popupGlobal.center().x(), buttonGlobal.center().x() - 16);
  QCOMPARE(popupGlobal.bottom(), buttonGlobal.top() + 3);

  harness.window.resize(harness.window.width() + 120, harness.window.height());
  QCoreApplication::processEvents();
  QTRY_COMPARE(volumePopup->mapToGlobal(volumePopup->rect().center()).x(),
               volumeButton->mapToGlobal(volumeButton->rect().center()).x() -
                   16);
  QTRY_COMPARE(volumePopup->mapToGlobal(volumePopup->rect().bottomLeft()).y(),
               volumeButton->mapToGlobal(volumeButton->rect().topLeft()).y() +
                   3);

  QEvent buttonLeaveForPopup(QEvent::Leave);
  QEvent popupEnter(QEvent::Enter);
  QApplication::sendEvent(volumeButton, &buttonLeaveForPopup);
  QApplication::sendEvent(volumePopup, &popupEnter);
  QTest::qWait(320);
  QVERIFY(volumePopup->isVisible());

  volumeSlider->setValue(64);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetVolume),
               1);
  QCOMPARE(harness.engine.commands().back().volume, 64);
  QCOMPARE(volumeLabel->text(), QStringLiteral("64%"));
  QVERIFY(volumeButton->text().isEmpty());
  const qint64 audibleIconKey = volumeButton->icon().cacheKey();

  QTest::mouseClick(volumeButton, Qt::LeftButton);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted), 1);
  QVERIFY(harness.engine.commands().back().flag);
  QCOMPARE(volumeButton->accessibleName(), QStringLiteral("已静音"));
  QVERIFY(volumeButton->icon().cacheKey() != audibleIconKey);
  QCOMPARE(volumeLabel->text(), QStringLiteral("0%"));
  QCOMPARE(volumeSlider->value(), 0);
  QTest::qWait(180);
  QVERIFY(volumePopup->isVisible());

  QTest::mouseClick(volumeButton, Qt::LeftButton);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted), 2);
  QVERIFY(!harness.engine.commands().back().flag);
  QCOMPARE(volumeButton->accessibleName(), QStringLiteral("音量 64%"));
  QCOMPARE(volumeLabel->text(), QStringLiteral("64%"));
  QCOMPARE(volumeSlider->value(), 64);
  QVERIFY(volumePopup->isVisible());
  QCOMPARE(statusText(harness), QStringLiteral("正在播放"));

  QCursor::setPos(harness.window.mapToGlobal(QPoint(2, 2)));
  QEvent leaveEvent(QEvent::Leave);
  QApplication::sendEvent(volumeButton, &leaveEvent);
  volumePopup->hide();
  QCursor::setPos(volumeButton->mapToGlobal(volumeButton->rect().center()));
  QEvent enterEvent(QEvent::Enter);
  QApplication::sendEvent(volumeButton, &enterEvent);
  QTest::qWait(40);
  QTest::mouseClick(volumeButton, Qt::LeftButton);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted), 3);
  QCOMPARE(volumeSlider->value(), 0);
  QTest::qWait(180);
  QVERIFY(volumePopup->isHidden());
  volumeSlider->setValue(20);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetVolume),
               2);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted), 4);
  QVERIFY(!harness.engine.commands().back().flag);
  QCOMPARE(volumeButton->accessibleName(), QStringLiteral("音量 20%"));
  QCOMPARE(volumeSlider->value(), 20);
}

void MainWindowTest::selectsPlaybackRateFromHoverMenuAndKeepsCurrentValue() {
  {
    GuiHarness harness;
    harness.window.show();
    QCoreApplication::processEvents();
    auto *const rateButton =
        requiredChild<QToolButton>(harness.window, "playbackRateButton");
    QTest::mouseMove(rateButton, rateButton->rect().center());
    QEvent rateEnter(QEvent::Enter);
    QApplication::sendEvent(rateButton, &rateEnter);
    QTRY_VERIFY(rateButton->menu()->isVisible());
    QVERIFY(rateButton->menu()->geometry().bottom() <
            rateButton->mapToGlobal(QPoint(0, 0)).y());
    QCursor::setPos(harness.window.mapToGlobal(QPoint(2, 2)));
    QEvent buttonLeave(QEvent::Leave);
    QEvent menuLeave(QEvent::Leave);
    QApplication::sendEvent(rateButton, &buttonLeave);
    QApplication::sendEvent(rateButton->menu(), &menuLeave);
    QTRY_VERIFY(!rateButton->menu()->isVisible());

    QTest::mouseClick(rateButton, Qt::LeftButton);
    QTRY_VERIFY(rateButton->menu()->isVisible());
    QVERIFY(rateButton->menu()->geometry().bottom() <
            rateButton->mapToGlobal(QPoint(0, 0)).y());
    rateButton->menu()->hide();
  }

  GuiHarness harness;
  auto *const rateButton =
      requiredChild<QToolButton>(harness.window, "playbackRateButton");
  auto *const rate75 =
      requiredChild<QAction>(harness.window, "playbackRateAction75");
  auto *const rate150 =
      requiredChild<QAction>(harness.window, "playbackRateAction150");
  auto *const rate300 =
      requiredChild<QAction>(harness.window, "playbackRateAction300");

  rate75->trigger();
  QTRY_COMPARE(rateButton->text(), QStringLiteral("0.75×"));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::SetPlaybackRate),
           0);
  openAndReachPlaying(harness);
  QTRY_COMPARE(
      commandCount(harness, test::FakeEngineCommandKind::SetPlaybackRate), 1);
  QCOMPARE(harness.engine.commands().back().playbackRate, 0.75);

  rate150->trigger();
  QTRY_COMPARE(
      commandCount(harness, test::FakeEngineCommandKind::SetPlaybackRate), 2);
  QCOMPARE(harness.engine.commands().back().playbackRate, 1.5);
  QCOMPARE(rateButton->text(), QStringLiteral("1.5×"));

  rate300->trigger();
  QTRY_COMPARE(
      commandCount(harness, test::FakeEngineCommandKind::SetPlaybackRate), 3);
  QCOMPARE(harness.engine.commands().back().playbackRate, 3.0);
  QCOMPARE(rateButton->text(), QStringLiteral("3.0×"));

  harness.presenter.openLocalFile(
      QStringLiteral("C:/媒体 库/第二首 测试音频.wav"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(
      commandCount(harness, test::FakeEngineCommandKind::SetPlaybackRate), 4);
  QCOMPARE(harness.engine.commands().back().playbackRate, 3.0);
  QCOMPARE(rateButton->text(), QStringLiteral("3.0×"));
}

void MainWindowTest::keepsHoverMenusStableWithoutMouseGrab() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();

  const std::array<const char *, 3> buttonNames{
      "keyboardSeekStepButton", "playbackRateButton", "playbackModeButton"};
  for (const char *const buttonName : buttonNames) {
    auto *const button = requiredChild<QToolButton>(harness.window, buttonName);
    auto *const menu = button->menu();
    QVERIFY(menu != nullptr);
    QCOMPARE(menu->windowType(), Qt::Tool);
    QVERIFY(menu->testAttribute(Qt::WA_ShowWithoutActivating));

    QEvent enterEvent(QEvent::Enter);
    QApplication::sendEvent(button, &enterEvent);
    QTRY_VERIFY(menu->isVisible());
    QTest::qWait(320);
    QVERIFY(menu->isVisible());

    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(button, &leaveEvent);
    auto *const firstAction = menu->actions().front();
    QSignalSpy triggeredSpy(firstAction, &QAction::triggered);
    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                      menu->actionGeometry(firstAction).center());
    QTRY_COMPARE(triggeredSpy.count(), 1);
    QTRY_VERIFY(menu->isHidden());
  }
}

void MainWindowTest::doesNotOpenHoverMenuAfterImmediateLeave() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const rateButton =
      requiredChild<QToolButton>(harness.window, "playbackRateButton");

  QCursor::setPos(rateButton->mapToGlobal(rateButton->rect().center()));
  QEvent enterEvent(QEvent::Enter);
  QApplication::sendEvent(rateButton, &enterEvent);
  QTest::qWait(40);

  QCursor::setPos(harness.window.mapToGlobal(QPoint(2, 2)));
  QEvent leaveEvent(QEvent::Leave);
  QApplication::sendEvent(rateButton, &leaveEvent);
  QTest::qWait(180);

  QVERIFY(!rateButton->menu()->isVisible());
}

void MainWindowTest::holdsRightKeyAtTwoTimesAndRestoresSelectedRate() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  harness.engine.emitPositionChanged(core::PlaybackPosition{60s, 120s, true});
  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();
  auto *const rateButton =
      requiredChild<QToolButton>(harness.window, "playbackRateButton");
  requiredChild<QAction>(harness.window, "playbackRateAction50")->trigger();
  QTRY_COMPARE(
      commandCount(harness, test::FakeEngineCommandKind::SetPlaybackRate), 1);
  QCOMPARE(harness.engine.commands().back().playbackRate, 0.5);
  QCOMPARE(rateButton->text(), QStringLiteral("0.5×"));

  const int seeksBefore =
      commandCount(harness, test::FakeEngineCommandKind::Seek);
  QTest::keyPress(&harness.window, Qt::Key_Right);
  QTRY_COMPARE_WITH_TIMEOUT(
      commandCount(harness, test::FakeEngineCommandKind::SetPlaybackRate), 2,
      1000);
  QCOMPARE(harness.engine.commands().back().playbackRate, 2.0);
  QCOMPARE(rateButton->text(), QStringLiteral("2.0×"));

  QKeyEvent repeatedPress(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier,
                          QString{}, true, 2);
  QApplication::sendEvent(&harness.window, &repeatedPress);
  QCoreApplication::processEvents();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek),
           seeksBefore);

  QTest::keyRelease(&harness.window, Qt::Key_Right);
  QTRY_COMPARE(
      commandCount(harness, test::FakeEngineCommandKind::SetPlaybackRate), 3);
  QCOMPARE(harness.engine.commands().back().playbackRate, 0.5);
  QCOMPARE(rateButton->text(), QStringLiteral("0.5×"));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek),
           seeksBefore);
}

void MainWindowTest::routesKeyboardPlaybackVolumeAndSeek() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  harness.engine.emitPositionChanged(core::PlaybackPosition{60s, 120s, true});
  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();

  QTest::keyClick(&harness.window, Qt::Key_Space);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Pause), 1);
  harness.engine.emitStateChanged(core::PlaybackState::Paused);
  QTRY_COMPARE(statusText(harness), QStringLiteral("已暂停"));
  QTest::keyClick(&harness.window, Qt::Key_Space);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 2);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);

  QTest::keyClick(&harness.window, Qt::Key_Down);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetVolume),
               1);
  QCOMPARE(harness.engine.commands().back().volume, 95);
  QCOMPARE(requiredChild<QSlider>(harness.window, "volumeSlider")->value(), 95);
  QTest::keyClick(&harness.window, Qt::Key_Up);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetVolume),
               2);
  QCOMPARE(harness.engine.commands().back().volume, 100);

  QTest::keyClick(&harness.window, Qt::Key_Left);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
  QCOMPARE(harness.engine.commands().back().position, 55s);
  QTest::keyClick(&harness.window, Qt::Key_Right);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 2);
  QCOMPARE(harness.engine.commands().back().position, 60s);

  const int volumeCommands =
      commandCount(harness, test::FakeEngineCommandKind::SetVolume);
  QTest::keyClick(&harness.window, Qt::Key_Up);
  QCoreApplication::processEvents();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::SetVolume),
           volumeCommands);

  GuiHarness emptyHarness;
  emptyHarness.window.show();
  emptyHarness.window.activateWindow();
  QCoreApplication::processEvents();
  QTest::keyClick(&emptyHarness.window, Qt::Key_Space);
  QTest::keyClick(&emptyHarness.window, Qt::Key_Left);
  QCOMPARE(commandCount(emptyHarness, test::FakeEngineCommandKind::Play), 0);
  QCOMPARE(commandCount(emptyHarness, test::FakeEngineCommandKind::Pause), 0);
  QCOMPARE(commandCount(emptyHarness, test::FakeEngineCommandKind::Seek), 0);
}

void MainWindowTest::routesCtrlArrowKeysToPlaylistNavigation() {
  GuiHarness harness;
  const QStringList paths{QStringLiteral("C:/媒体/第一首.mp3"),
                          QStringLiteral("C:/媒体/第二首.mp3"),
                          QStringLiteral("C:/媒体/第三首.mp3")};
  harness.presenter.addLocalFiles(paths);
  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();

  const int opensBefore =
      commandCount(harness, test::FakeEngineCommandKind::Open);
  QTest::keyClick(&harness.window, Qt::Key_Left, Qt::ControlModifier);
  QCoreApplication::processEvents();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBefore);

  QTest::keyClick(&harness.window, Qt::Key_Right, Qt::ControlModifier);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
               opensBefore + 1);
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->source,
           paths.at(1).toUtf8().toStdString());

  QTest::keyClick(&harness.window, Qt::Key_Left, Qt::ControlModifier);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
               opensBefore + 2);
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->source,
           paths.front().toUtf8().toStdString());
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 0);
}

void MainWindowTest::usesConfiguredKeyboardSeekStepAndClampsBoundaries() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  harness.engine.emitPositionChanged(core::PlaybackPosition{60s, 120s, true});
  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();
  auto *const stepButton =
      requiredChild<QToolButton>(harness.window, "keyboardSeekStepButton");
  QTest::mouseClick(stepButton, Qt::LeftButton);
  QTRY_VERIFY(stepButton->menu()->isVisible());
  QVERIFY(stepButton->menu()->geometry().bottom() <
          stepButton->mapToGlobal(QPoint(0, 0)).y());
  stepButton->menu()->hide();
  harness.window.activateWindow();
  QCoreApplication::processEvents();
  requiredChild<QAction>(harness.window, "keyboardSeekStepAction10")->trigger();
  QCOMPARE(stepButton->text(), QStringLiteral("10 秒"));

  QTest::keyClick(&harness.window, Qt::Key_Right);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
  QCOMPARE(harness.engine.commands().back().position, 70s);
  QTest::keyClick(&harness.window, Qt::Key_Left);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 2);
  QCOMPARE(harness.engine.commands().back().position, 60s);

  requiredChild<QAction>(harness.window, "keyboardSeekStepAction20")->trigger();
  QCOMPARE(stepButton->text(), QStringLiteral("20 秒"));
  harness.engine.emitPositionChanged(core::PlaybackPosition{3s, 120s, true});
  QTRY_COMPARE(
      requiredChild<QSlider>(harness.window, "progressSlider")->value(), 25);
  QTest::keyClick(&harness.window, Qt::Key_Left);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 3);
  QCOMPARE(harness.engine.commands().back().position, 0ms);

  harness.engine.emitPositionChanged(core::PlaybackPosition{119s, 120s, true});
  QTRY_COMPARE(
      requiredChild<QSlider>(harness.window, "progressSlider")->value(), 991);
  QTest::keyClick(&harness.window, Qt::Key_Right);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 4);
  QCOMPARE(harness.engine.commands().back().position, 120s);
}

void MainWindowTest::appliesBurstPositionEventsOnGuiThread() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  auto *const positionLabel =
      requiredChild<QLabel>(harness.window, "positionLabel");

  std::thread worker([&harness] {
    for (int index = 1; index <= 400; ++index) {
      harness.engine.emitPositionChanged(core::PlaybackPosition{
          std::chrono::milliseconds(index * 10), 10s, true});
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

  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const model = playlistView->model();
  QCOMPARE(model->rowCount(), 3);
  QVERIFY(model->data(model->index(0, 0))
              .toString()
              .contains(QStringLiteral("第一首.mp3")));
  QVERIFY(model->data(model->index(1, 0))
              .toString()
              .contains(QStringLiteral("第二段.mp4")));
  QVERIFY(model->data(model->index(2, 0))
              .toString()
              .contains(QStringLiteral("第三首.wav")));
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->source,
           paths.front().toUtf8().toStdString());
  QCOMPARE(playlistView->currentIndex().row(), 0);
  QVERIFY(
      requiredChild<QToolButton>(harness.window, "nextButton")->isEnabled());

  const QModelIndex third = model->index(2, 0);
  QVERIFY(QMetaObject::invokeMethod(playlistView, "doubleClicked",
                                    Qt::DirectConnection,
                                    Q_ARG(QModelIndex, third)));
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->source,
           paths.back().toUtf8().toStdString());
  QCOMPARE(playlistView->currentIndex().row(), 2);
  QVERIFY(requiredChild<QToolButton>(harness.window, "previousButton")
              ->isEnabled());
  QVERIFY(
      !requiredChild<QToolButton>(harness.window, "nextButton")->isEnabled());
}

void MainWindowTest::usesPlaylistContextMenuForSingleItemActions() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  const QStringList paths{
      QStringLiteral("C:/menu/one.mp3"), QStringLiteral("C:/menu/two.mp3"),
      QStringLiteral("C:/menu/three.mp3"), QStringLiteral("C:/menu/four.mp3")};
  harness.presenter.addLocalFiles(paths);
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const model = playlistView->model();
  auto *const menu =
      requiredChild<QMenu>(harness.window, "playlistContextMenu");
  auto *const playAction =
      requiredChild<QAction>(harness.window, "playlistPlayAction");
  auto *const pauseAction =
      requiredChild<QAction>(harness.window, "playlistPauseAction");
  auto *const stopAction =
      requiredChild<QAction>(harness.window, "playlistStopAction");
  auto *const renameAction =
      requiredChild<QAction>(harness.window, "playlistRenameAction");
  auto *const moveTopAction =
      requiredChild<QAction>(harness.window, "playlistMoveTopAction");
  auto *const moveUpAction =
      requiredChild<QAction>(harness.window, "playlistMoveUpAction");
  auto *const moveDownAction =
      requiredChild<QAction>(harness.window, "playlistMoveDownAction");
  auto *const removeAction =
      requiredChild<QAction>(harness.window, "playlistRemoveAction");

  QVERIFY(requestPlaylistContextMenu(*playlistView, 2));
  QVERIFY(playAction->isEnabled());
  QVERIFY(!pauseAction->isEnabled());
  QVERIFY(!stopAction->isEnabled());
  QVERIFY(renameAction->isEnabled());
  QVERIFY(moveTopAction->isEnabled());
  QVERIFY(moveUpAction->isEnabled());
  QVERIFY(moveDownAction->isEnabled());
  QVERIFY(removeAction->isEnabled());
  menu->hide();
  moveUpAction->trigger();
  QCOMPARE(model->index(1, 0).data(Qt::UserRole).toString(),
           QStringLiteral("three.mp3"));

  QVERIFY(requestPlaylistContextMenu(*playlistView, 1));
  menu->hide();
  moveTopAction->trigger();
  QCOMPARE(model->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("three.mp3"));

  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  QVERIFY(!moveTopAction->isEnabled());
  QVERIFY(!moveUpAction->isEnabled());
  QVERIFY(moveDownAction->isEnabled());
  menu->hide();
  moveDownAction->trigger();
  QCOMPARE(model->index(1, 0).data(Qt::UserRole).toString(),
           QStringLiteral("three.mp3"));

  QVERIFY(requestPlaylistContextMenu(*playlistView, 1));
  bool renameDialogHandled = false;
  QTimer::singleShot(0, [&renameDialogHandled] {
    auto *const dialog =
        qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
    if (dialog != nullptr) {
      dialog->setTextValue(QStringLiteral("列表里的新名字"));
      renameDialogHandled = true;
      dialog->accept();
    }
  });
  menu->hide();
  renameAction->trigger();
  QVERIFY(renameDialogHandled);
  QCOMPARE(model->index(1, 0).data(Qt::UserRole).toString(),
           QStringLiteral("列表里的新名字"));

  QVERIFY(requestPlaylistContextMenu(*playlistView, 1));
  menu->hide();
  playAction->trigger();
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->source,
           paths.at(2).toUtf8().toStdString());
  QCOMPARE(harness.engine.commands().back().media->displayName,
           QStringLiteral("列表里的新名字").toUtf8().toStdString());
}

void MainWindowTest::
    enablesPlaylistContextPlaybackActionsBySelectedItemState() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  const QStringList paths{QStringLiteral("C:/menu/current.mp3"),
                          QStringLiteral("C:/menu/other.mp3")};
  harness.presenter.addLocalFiles(paths);
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 1);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));

  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const menu =
      requiredChild<QMenu>(harness.window, "playlistContextMenu");
  auto *const playAction =
      requiredChild<QAction>(harness.window, "playlistPlayAction");
  auto *const pauseAction =
      requiredChild<QAction>(harness.window, "playlistPauseAction");
  auto *const stopAction =
      requiredChild<QAction>(harness.window, "playlistStopAction");

  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  QVERIFY(!playAction->isEnabled());
  QVERIFY(pauseAction->isEnabled());
  QVERIFY(stopAction->isEnabled());
  menu->hide();

  QVERIFY(requestPlaylistContextMenu(*playlistView, 1));
  QVERIFY(playAction->isEnabled());
  QVERIFY(!pauseAction->isEnabled());
  QVERIFY(!stopAction->isEnabled());
  menu->hide();

  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  menu->hide();
  pauseAction->trigger();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Pause), 1);
  harness.engine.emitStateChanged(core::PlaybackState::Paused);
  QTRY_COMPARE(statusText(harness), QStringLiteral("已暂停"));

  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  QVERIFY(playAction->isEnabled());
  QVERIFY(!pauseAction->isEnabled());
  QVERIFY(stopAction->isEnabled());
  menu->hide();
  playAction->trigger();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 2);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 1);

  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  menu->hide();
  stopAction->trigger();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop), 1);
  harness.engine.emitStateChanged(core::PlaybackState::Stopped);
  QTRY_COMPARE(statusText(harness), QStringLiteral("已停止"));

  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  QVERIFY(playAction->isEnabled());
  QVERIFY(!pauseAction->isEnabled());
  QVERIFY(!stopAction->isEnabled());
  menu->hide();
}

void MainWindowTest::supportsCtrlShiftMultiSelectionAndOnlyAllowsRemoval() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  harness.presenter.addLocalFiles({QStringLiteral("C:/multi/one.mp3"),
                                   QStringLiteral("C:/multi/two.mp3"),
                                   QStringLiteral("C:/multi/three.mp3"),
                                   QStringLiteral("C:/multi/four.mp3"),
                                   QStringLiteral("C:/multi/five.mp3")});
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const model = playlistView->model();
  QCOMPARE(playlistView->selectionMode(), QAbstractItemView::ExtendedSelection);

  const auto rowCenter = [playlistView, model](const int row) {
    return playlistView->visualRect(model->index(row, 0)).center();
  };
  QTest::mouseClick(playlistView->viewport(), Qt::LeftButton, Qt::NoModifier,
                    rowCenter(1));
  QTest::mouseClick(playlistView->viewport(), Qt::LeftButton,
                    Qt::ControlModifier, rowCenter(3));
  QCOMPARE(playlistView->selectionModel()->selectedRows().size(), 2);

  QTest::mouseClick(playlistView->viewport(), Qt::LeftButton, Qt::NoModifier,
                    rowCenter(0));
  QTest::mouseClick(playlistView->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                    rowCenter(2));
  QCOMPARE(playlistView->selectionModel()->selectedRows().size(), 3);

  QVERIFY(requestPlaylistContextMenu(*playlistView, 1));
  QCOMPARE(playlistView->selectionModel()->selectedRows().size(), 3);
  QVERIFY(!requiredChild<QAction>(harness.window, "playlistPlayAction")
               ->isEnabled());
  QVERIFY(!requiredChild<QAction>(harness.window, "playlistPauseAction")
               ->isEnabled());
  QVERIFY(!requiredChild<QAction>(harness.window, "playlistStopAction")
               ->isEnabled());
  QVERIFY(!requiredChild<QAction>(harness.window, "playlistRenameAction")
               ->isEnabled());
  QVERIFY(!requiredChild<QAction>(harness.window, "playlistMoveTopAction")
               ->isEnabled());
  QVERIFY(!requiredChild<QAction>(harness.window, "playlistMoveUpAction")
               ->isEnabled());
  QVERIFY(!requiredChild<QAction>(harness.window, "playlistMoveDownAction")
               ->isEnabled());
  auto *const removeAction =
      requiredChild<QAction>(harness.window, "playlistRemoveAction");
  QVERIFY(removeAction->isEnabled());
  requiredChild<QMenu>(harness.window, "playlistContextMenu")->hide();
  removeAction->trigger();

  QCOMPARE(model->rowCount(), 2);
  QCOMPARE(model->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("four.mp3"));
  QCOMPARE(model->index(1, 0).data(Qt::UserRole).toString(),
           QStringLiteral("five.mp3"));
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->displayName,
           std::string("four.mp3"));
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

  QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mimeData, Qt::LeftButton,
                  Qt::NoModifier);
  QApplication::sendEvent(&harness.window, &drop);
  QVERIFY(drop.isAccepted());

  auto *const model =
      requiredChild<QListView>(harness.window, "playlistView")->model();
  QCOMPARE(model->rowCount(), 2);
  QVERIFY(model->data(model->index(0, 0))
              .toString()
              .contains(QStringLiteral("甲.mp3")));
  QVERIFY(model->data(model->index(1, 0))
              .toString()
              .contains(QStringLiteral("乙.mp4")));
}

void MainWindowTest::exposesPlaylistModelRolesAndSelectionMarker() {
  core::Playlist playlist;
  playlist.add(core::makeMediaItem("C:/list/one.mp3", "第一首.mp3"));
  playlist.add(core::makeMediaItem("C:/list/two.mp4", "第二段.mp4"));
  PlaylistModel model(playlist);

  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.rowCount(model.index(0, 0)), 0);
  const QModelIndex first = model.index(0, 0);
  const QModelIndex second = model.index(1, 0);
  QCOMPARE(model.data(first, Qt::DisplayRole),
           model.data(first, Qt::AccessibleTextRole));
  QCOMPARE(model.data(first, Qt::UserRole).toString(),
           QStringLiteral("第一首.mp3"));
  QVERIFY(model.data(first).toString().startsWith(QStringLiteral("▶ 1. ")));
  QVERIFY(model.data(second).toString().startsWith(QStringLiteral("   2. ")));
  QVERIFY(!model.data(first, Qt::DecorationRole).isValid());
  QVERIFY(!model.data(QModelIndex{}, Qt::DisplayRole).isValid());

  QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
  QVERIFY(playlist.select(1));
  model.refresh();
  QCOMPARE(resetSpy.count(), 1);
  QVERIFY(model.data(model.index(0, 0))
              .toString()
              .startsWith(QStringLiteral("   1. ")));
  QVERIFY(model.data(model.index(1, 0))
              .toString()
              .startsWith(QStringLiteral("▶ 2. ")));
}

void MainWindowTest::advancesNaturalEndAccordingToPlaybackMode() {
  const QStringList paths{QStringLiteral("C:/list/one.mp3"),
                          QStringLiteral("C:/list/two.mp3")};
  for (int modeIndex = 0; modeIndex < 4; ++modeIndex) {
    GuiHarness harness;
    harness.presenter.addLocalFiles(paths);
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    auto *const playlistView =
        requiredChild<QListView>(harness.window, "playlistView");
    const QModelIndex second = playlistView->model()->index(1, 0);
    QVERIFY(QMetaObject::invokeMethod(playlistView, "doubleClicked",
                                      Qt::DirectConnection,
                                      Q_ARG(QModelIndex, second)));
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));
    auto *const modeButton =
        requiredChild<QToolButton>(harness.window, "playbackModeButton");
    const qint64 sequentialIconKey = modeButton->icon().cacheKey();
    modeButton->menu()->actions().at(modeIndex)->trigger();
    const std::array modeNames{
        QStringLiteral("顺序播放"), QStringLiteral("列表循环"),
        QStringLiteral("单曲循环"), QStringLiteral("随机播放")};
    QCOMPARE(modeButton->accessibleName(),
             QStringLiteral("播放模式：%1").arg(modeNames.at(modeIndex)));
    if (modeIndex > 0) {
      QVERIFY(modeButton->icon().cacheKey() != sequentialIconKey);
    }
    const int opensBeforeEnd =
        commandCount(harness, test::FakeEngineCommandKind::Open);

    harness.engine.emitEndReached();
    if (modeIndex == 0) {
      QTRY_COMPARE(statusText(harness), QStringLiteral("播放结束"));
      QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
               opensBeforeEnd);
      continue;
    }

    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
                 opensBeforeEnd + 1);
    const auto command = harness.engine.commands().back();
    QVERIFY(command.media.has_value());
    const QString expected = modeIndex == 2 ? paths.back() : paths.front();
    QCOMPARE(command.media->source, expected.toUtf8().toStdString());
  }
}

void MainWindowTest::cyclesPlaybackModeOnButtonClickAndUpdatesHoverMenu() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const modeButton =
      requiredChild<QToolButton>(harness.window, "playbackModeButton");
  auto *const modeMenu = modeButton->menu();
  QEvent enterEvent(QEvent::Enter);
  QApplication::sendEvent(modeButton, &enterEvent);
  QTRY_VERIFY(modeMenu->isVisible());

  const std::array modeNames{
      QStringLiteral("顺序播放"), QStringLiteral("列表循环"),
      QStringLiteral("单曲循环"), QStringLiteral("随机播放")};
  const std::array expectedModes{1, 2, 3, 0};
  for (const int expectedMode : expectedModes) {
    QTest::mouseClick(modeButton, Qt::LeftButton);
    QCOMPARE(modeButton->accessibleName(),
             QStringLiteral("播放模式：%1").arg(modeNames.at(expectedMode)));
    QVERIFY(modeMenu->isVisible());
    for (int modeIndex = 0; modeIndex < 4; ++modeIndex) {
      QCOMPARE(modeMenu->actions().at(modeIndex)->isChecked(),
               modeIndex == expectedMode);
    }
  }
}

void MainWindowTest::ignoresLateEndEventAfterSwitchingToFailedItem() {
  GuiHarness harness;
  harness.presenter.addLocalFiles({QStringLiteral("C:/list/old.mp4"),
                                   QStringLiteral("C:/list/bad.mp4"),
                                   QStringLiteral("C:/list/good.mp4")});
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));

  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  const QModelIndex failedItem = playlistView->model()->index(1, 0);
  QVERIFY(QMetaObject::invokeMethod(playlistView, "doubleClicked",
                                    Qt::DirectConnection,
                                    Q_ARG(QModelIndex, failedItem)));
  const int opensAfterSwitch =
      commandCount(harness, test::FakeEngineCommandKind::Open);

  // 旧媒体的迟到结束事件不得替新媒体执行顺序播放推进。
  harness.engine.emitEndReached();
  harness.engine.emitError(core::PlaybackError{
      core::PlaybackErrorKind::UnsupportedFormat,
      "invalid media",
      "无法播放媒体。",
  });
  QTRY_COMPARE(statusText(harness), QStringLiteral("播放失败"));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensAfterSwitch);
  QCOMPARE(playlistView->currentIndex().row(), 1);

  harness.engine.emitEndReached();
  QCoreApplication::processEvents();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensAfterSwitch);
  QCOMPARE(playlistView->currentIndex().row(), 1);
  QCOMPARE(statusText(harness), QStringLiteral("播放失败"));
}

void MainWindowTest::removesCurrentItemsWithoutLeavingInvalidSelection() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  harness.presenter.addLocalFiles({QStringLiteral("C:/remove/one.mp3"),
                                   QStringLiteral("C:/remove/two.mp3")});
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const removeAction =
      requiredChild<QAction>(harness.window, "playlistRemoveAction");
  auto *const contextMenu =
      requiredChild<QMenu>(harness.window, "playlistContextMenu");

  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  contextMenu->hide();
  removeAction->trigger();
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(playlistView->currentIndex().row(), 0);
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->displayName,
           std::string("two.mp3"));

  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  contextMenu->hide();
  removeAction->trigger();
  QCOMPARE(playlistView->model()->rowCount(), 0);
  QVERIFY(!removeAction->isEnabled());
  QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));
  QVERIFY(harness.engine.commands().back().kind ==
          test::FakeEngineCommandKind::Stop);

  // 移除最后一项后，内核可能迟到发送停止事件；空列表仍保持空闲界面。
  harness.engine.emitStateChanged(core::PlaybackState::Stopped);
  QCoreApplication::processEvents();
  QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));

  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/remove.m3u8"));
  auto *const timeoutTimer =
      harness.presenter.findChild<QTimer *>("networkOpenTimeoutTimer");
  QVERIFY(timeoutTimer != nullptr);
  QVERIFY(timeoutTimer->isActive());
  QCOMPARE(playlistView->contextMenuPolicy(), Qt::CustomContextMenu);
  QVERIFY(!removeAction->isEnabled());
  QVERIFY(requestPlaylistContextMenu(*playlistView, 0));
  requiredChild<QMenu>(harness.window, "livePlaylistContextMenu")->hide();
  removeAction->trigger();
  QVERIFY(timeoutTimer->isActive());
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(statusText(harness), QStringLiteral("正在连接..."));
}

void MainWindowTest::ignoresRejectedStateEventsAndCommandsAfterShutdown() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  QSignalSpy stateSpy(&harness.presenter, &PlayerPresenter::stateApplied);

  harness.engine.emitStateChanged(core::PlaybackState::Idle);
  QCoreApplication::processEvents();
  QCOMPARE(statusText(harness), QStringLiteral("正在播放"));
  QCOMPARE(stateSpy.count(), 0);

  harness.presenter.shutdown();
  const int commandsAfterShutdown =
      static_cast<int>(harness.engine.commands().size());
  harness.presenter.addLocalFiles({QStringLiteral("C:/ignored/after.wav")});
  harness.engine.emitStateChanged(core::PlaybackState::Paused);
  harness.engine.emitError(core::PlaybackError{
      core::PlaybackErrorKind::Unknown,
      "late error",
      "不应显示",
  });
  QCoreApplication::processEvents();
  QCOMPARE(static_cast<int>(harness.engine.commands().size()),
           commandsAfterShutdown);
  QCOMPARE(statusText(harness), QStringLiteral("正在播放"));
  QVERIFY(
      requiredChild<QLabel>(harness.window, "playbackErrorLabel")->isHidden());
}

void MainWindowTest::mapsEveryErrorKindToStableLogValue() {
  const std::array cases{
      std::pair{core::PlaybackErrorKind::SourceNotFound, "source_not_found"},
      std::pair{core::PlaybackErrorKind::SourceUnreadable, "source_unreadable"},
      std::pair{core::PlaybackErrorKind::UnsupportedFormat,
                "unsupported_format"},
      std::pair{core::PlaybackErrorKind::AudioDeviceUnavailable,
                "audio_device_unavailable"},
      std::pair{core::PlaybackErrorKind::EngineNotInitialized,
                "engine_not_initialized"},
      std::pair{core::PlaybackErrorKind::Unknown, "unknown"},
  };

  for (const auto &[kind, expected] : cases) {
    GuiHarness harness;
    harness.engine.emitError(core::PlaybackError{kind, "detail", "用户提示"});
    QTRY_COMPARE(statusText(harness), QStringLiteral("播放失败"));
    const std::string logs = harness.logOutput.str();
    QVERIFY(logs.find(std::string("kind=\"") + expected + '"') !=
            std::string::npos);
  }
}

void MainWindowTest::switchesBetweenVideoSurfaceAndAudioVisualization() {
  GuiHarness harness;
  auto *const videoOutput =
      requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget");

  harness.presenter.openLocalFile(QStringLiteral("C:/video/sample.mp4"));
  QVERIFY(!videoOutput->isVideoActive());
  QCOMPARE(videoOutput->placeholderText(),
           QStringLiteral("正在准备视频画面..."));

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_VERIFY(videoOutput->isVideoActive());
  QCOMPARE(videoOutput->placeholderText(), QString());
  QVERIFY(videoOutput->testAttribute(Qt::WA_NoSystemBackground));
  QVERIFY(!videoOutput->testAttribute(Qt::WA_OpaquePaintEvent));

  harness.presenter.openLocalFile(QStringLiteral("C:/audio/sample.flac"));
  QVERIFY(!videoOutput->isVideoActive());
  QVERIFY(videoOutput->isAudioVisualizationActive());
  QCOMPARE(videoOutput->placeholderText(), QString());
  QVERIFY(!videoOutput->testAttribute(Qt::WA_NoSystemBackground));
  QVERIFY(videoOutput->testAttribute(Qt::WA_OpaquePaintEvent));
}

void MainWindowTest::rendersBottomUpwardAudioWaveformAndTogglesFullScreen() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const videoOutput =
      requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget");
  auto *const fullScreenButton =
      requiredChild<QToolButton>(harness.window, "fullScreenButton");

  harness.presenter.openLocalFile(QStringLiteral("C:/audio/song.mp3"));
  QVERIFY(videoOutput->isAudioVisualizationActive());
  QVERIFY(!videoOutput->isAudioVisualizationAnimating());
  QVERIFY(!fullScreenButton->isEnabled());

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_VERIFY(fullScreenButton->isEnabled());
  QTRY_VERIFY(videoOutput->isAudioVisualizationAnimating());
  harness.engine.emitPositionChanged(core::PlaybackPosition{30s, 120s, true});
  QTRY_COMPARE(videoOutput->audioVisualizationProgress(), 250);
  const int initialAnimationFrame = videoOutput->animationFrame();
  core::AudioWaveform waveform;
  waveform.samples[5] = -0.4F;
  waveform.samples[64] = 0.85F;
  waveform.intensity = 0.72F;
  harness.engine.emitAudioWaveformChanged(waveform);
  QTRY_VERIFY(videoOutput->animationFrame() > initialAnimationFrame);
  QCOMPARE(videoOutput->audioWaveform().samples[5], -0.4F);
  QCOMPARE(videoOutput->audioWaveform().samples[64], 0.85F);
  const float raisedIntensity = videoOutput->audioVisualizationIntensity();
  QVERIFY(qAbs(raisedIntensity - 0.3744F) < 0.001F);

  const int firstWaveformFrame = videoOutput->animationFrame();
  harness.engine.emitAudioWaveformChanged(waveform);
  QTRY_VERIFY(videoOutput->animationFrame() > firstWaveformFrame);
  const float continuedIntensity = videoOutput->audioVisualizationIntensity();
  QVERIFY(continuedIntensity > raisedIntensity);

  waveform.samples[5] = -0.3F;
  waveform.intensity = 0.0F;
  harness.engine.emitAudioWaveformChanged(waveform);
  QTRY_VERIFY(videoOutput->animationFrame() > firstWaveformFrame + 1);
  const float releasedIntensity = videoOutput->audioVisualizationIntensity();
  QVERIFY(releasedIntensity < continuedIntensity);
  QVERIFY(releasedIntensity > continuedIntensity * 0.6F);
  QVERIFY(releasedIntensity < continuedIntensity * 0.65F);

  harness.engine.emitStateChanged(core::PlaybackState::Paused);
  QTRY_VERIFY(!videoOutput->isAudioVisualizationAnimating());
  QTest::mouseClick(fullScreenButton, Qt::LeftButton);
  QTRY_VERIFY(harness.window.isFullScreen());
  QTRY_VERIFY(fullScreenButton->isVisible());
  QCOMPARE(fullScreenButton->accessibleName(), QStringLiteral("退出全屏"));

  QTest::mouseClick(fullScreenButton, Qt::LeftButton);
  QTRY_VERIFY(!harness.window.isFullScreen());
  QCOMPARE(fullScreenButton->accessibleName(), QStringLiteral("进入全屏"));
}

void MainWindowTest::togglesLyricsBesideVolumeAndTracksSynchronizedLine() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const volumeButton =
      requiredChild<QToolButton>(harness.window, "volumeButton");
  auto *const lyricsButton =
      requiredChild<QToolButton>(harness.window, "lyricsButton");
  auto *const lyricsView =
      requiredChild<LyricsView>(harness.window, "lyricsView");
  auto *const videoOutput =
      requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget");

  harness.presenter.openLocalFile(QStringLiteral("C:/audio/周杰伦 - 晴天.mp3"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  harness.engine.emitDurationChanged(120s);
  harness.engine.emitPositionChanged(core::PlaybackPosition{12s, 120s, true});
  QTRY_VERIFY(lyricsButton->isEnabled());
  QTRY_COMPARE(requiredChild<QLabel>(harness.window, "positionLabel")->text(),
               QStringLiteral("00:12 / 02:00"));
  QVERIFY(geometryInsideWindow(*volumeButton, harness.window).right() <
          geometryInsideWindow(*lyricsButton, harness.window).left());

  QTest::mouseClick(lyricsButton, Qt::LeftButton);
  QCOMPARE(harness.lyricsService.requestCount, 1);
  QCOMPARE(harness.lyricsService.lastQuery.filePath,
           QStringLiteral("C:/audio/周杰伦 - 晴天.mp3"));
  QCOMPARE(harness.lyricsService.lastQuery.durationMilliseconds, 120000);
  QVERIFY(lyricsButton->isChecked());
  QVERIFY(lyricsView->isVisible());
  QVERIFY(!videoOutput->isVisible());
  QCOMPARE(requiredChild<QLabel>(harness.window, "lyricsMessageTitle")->text(),
           QStringLiteral("正在查找歌词"));

  LyricsResult result;
  result.kind = LyricsResultKind::Ready;
  result.title = QStringLiteral("晴天");
  result.artist = QStringLiteral("周杰伦");
  result.sourceName = QStringLiteral("网易云音乐");
  result.synchronizedLines = {
      {0, QStringLiteral("第一句")},
      {10000, QStringLiteral("第二句")},
      {20000, QStringLiteral("第三句")},
  };
  harness.lyricsService.complete(result);
  QTRY_COMPARE(
      requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")->text(),
      QStringLiteral("第二句"));
  QCOMPARE(requiredChild<QLabel>(harness.window, "lyricsSourceLabel")->text(),
           QStringLiteral("来源 · 网易云音乐"));
  auto *const timingControls =
      requiredChild<QWidget>(harness.window, "lyricsTimingControls");
  auto *const timingSlowButton =
      requiredChild<QToolButton>(harness.window, "lyricsTimingSlowButton");
  auto *const timingFastButton =
      requiredChild<QToolButton>(harness.window, "lyricsTimingFastButton");
  auto *const timingResetButton =
      requiredChild<QToolButton>(harness.window, "lyricsTimingResetButton");
  QVERIFY(timingControls->isVisible());
  QTest::mouseClick(timingResetButton, Qt::LeftButton);
  QCOMPARE(lyricsView->timingOffsetMilliseconds(), 0);
  QCOMPARE(
      requiredChild<QLabel>(harness.window, "lyricsTimingOffsetLabel")->text(),
      QStringLiteral("歌词同步 0.0 秒"));

  harness.engine.emitPositionChanged(core::PlaybackPosition{21s, 120s, true});
  QTRY_COMPARE(
      requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")->text(),
      QStringLiteral("第三句"));

  harness.engine.emitPositionChanged(
      core::PlaybackPosition{19600ms, 120s, true});
  QTRY_COMPARE(
      requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")->text(),
      QStringLiteral("第二句"));
  QTest::mouseClick(timingFastButton, Qt::LeftButton);
  QCOMPARE(lyricsView->timingOffsetMilliseconds(), 500);
  QCOMPARE(
      requiredChild<QLabel>(harness.window, "lyricsTimingOffsetLabel")->text(),
      QStringLiteral("歌词快 0.5 秒"));
  QTRY_COMPARE(
      requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")->text(),
      QStringLiteral("第三句"));

  lyricsView->setResult(result);
  QCOMPARE(lyricsView->timingOffsetMilliseconds(), 500);
  QTest::mouseClick(timingResetButton, Qt::LeftButton);
  QCOMPARE(lyricsView->timingOffsetMilliseconds(), 0);
  QTRY_COMPARE(
      requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")->text(),
      QStringLiteral("第二句"));

  harness.engine.emitPositionChanged(
      core::PlaybackPosition{20100ms, 120s, true});
  QTRY_COMPARE(
      requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")->text(),
      QStringLiteral("第三句"));
  QTest::mouseClick(timingSlowButton, Qt::LeftButton);
  QCOMPARE(lyricsView->timingOffsetMilliseconds(), -500);
  QCOMPARE(
      requiredChild<QLabel>(harness.window, "lyricsTimingOffsetLabel")->text(),
      QStringLiteral("歌词慢 0.5 秒"));
  QTRY_COMPARE(
      requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")->text(),
      QStringLiteral("第二句"));
  QTest::mouseClick(timingResetButton, Qt::LeftButton);
  QCOMPARE(lyricsView->timingOffsetMilliseconds(), 0);
  QTRY_COMPARE(
      requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")->text(),
      QStringLiteral("第三句"));

  const QByteArray legacyIdentity =
      QStringLiteral("%1|%2|%3")
          .arg(QStringLiteral("周杰伦 - 晴天.mp3"), result.title, result.artist)
          .toUtf8();
  const QString identityHash = QString::fromLatin1(
      QCryptographicHash::hash(legacyIdentity, QCryptographicHash::Sha256)
          .toHex());
  const QString offsetKey =
      QStringLiteral("lyrics/timingOffsets/%1").arg(identityHash);
  const QString versionKey =
      QStringLiteral("lyrics/timingOffsetVersions/%1").arg(identityHash);
  const QString organization = QCoreApplication::organizationName().isEmpty()
                                   ? QStringLiteral("MediaHub")
                                   : QCoreApplication::organizationName();
  const QString application = QCoreApplication::applicationName().isEmpty()
                                  ? QStringLiteral("MediaHub")
                                  : QCoreApplication::applicationName();
  QSettings legacySettings(QSettings::IniFormat, QSettings::UserScope,
                           organization, application);
  legacySettings.setValue(offsetKey, 500);
  legacySettings.remove(versionKey);
  legacySettings.sync();
  lyricsView->setResult(result);
  QCOMPARE(lyricsView->timingOffsetMilliseconds(), -500);
  QCOMPARE(
      requiredChild<QLabel>(harness.window, "lyricsTimingOffsetLabel")->text(),
      QStringLiteral("歌词慢 0.5 秒"));
  QTRY_COMPARE(
      requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")->text(),
      QStringLiteral("第二句"));
  QTest::mouseClick(timingResetButton, Qt::LeftButton);
  QCOMPARE(lyricsView->timingOffsetMilliseconds(), 0);

  lyricsView->setFixedSize(1600, 1040);
  QTRY_VERIFY(requiredChild<QLabel>(harness.window, "synchronizedLyricLine2")
                  ->styleSheet()
                  .contains(QStringLiteral("font-size: 91px")));
  QTRY_VERIFY(requiredChild<QLabel>(harness.window, "synchronizedLyricLine1")
                  ->styleSheet()
                  .contains(QStringLiteral("font-size: 56px")));
  QTRY_VERIFY(requiredChild<QTextBrowser>(harness.window, "plainLyricsBrowser")
                  ->styleSheet()
                  .contains(QStringLiteral("font-size: 42px")));
  QTRY_VERIFY(requiredChild<QLabel>(harness.window, "lyricsMediaNameLabel")
                  ->styleSheet()
                  .contains(QStringLiteral("font-size: 33px")));
  QTRY_VERIFY(requiredChild<QLabel>(harness.window, "lyricsSourceLabel")
                  ->styleSheet()
                  .contains(QStringLiteral("font-size: 24px")));
  QTRY_VERIFY(requiredChild<QLabel>(harness.window, "lyricsTimingOffsetLabel")
                  ->styleSheet()
                  .contains(QStringLiteral("font-size: 27px")));
  QCOMPARE(timingFastButton->minimumHeight(), 51);
  QVERIFY(timingFastButton->styleSheet().contains(
      QStringLiteral("font-size: 27px")));

  QTest::mouseClick(lyricsButton, Qt::LeftButton);
  QVERIFY(!lyricsButton->isChecked());
  QVERIFY(!lyricsView->isVisible());
  QVERIFY(videoOutput->isVisible());
  QTest::mouseClick(lyricsButton, Qt::LeftButton);
  QCOMPARE(harness.lyricsService.requestCount, 1);
  QVERIFY(lyricsView->isVisible());
}

void MainWindowTest::collapsesAndExpandsPlaylistWithMediaResize() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const videoOutput =
      requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget");
  auto *const playlistPanel =
      requiredChild<QWidget>(harness.window, "playlistPanel");
  auto *const toggleButton =
      requiredChild<QToolButton>(harness.window, "playlistToggleButton");
  const int expandedVideoWidth = videoOutput->width();
  const qint64 collapseIconKey = toggleButton->icon().cacheKey();
  QVERIFY(playlistPanel->isVisible());
  QCOMPARE(toggleButton->accessibleName(), QStringLiteral("收起播放列表"));

  QTest::mouseClick(toggleButton, Qt::LeftButton);
  QTRY_VERIFY(playlistPanel->isHidden());
  QTRY_VERIFY(videoOutput->width() > expandedVideoWidth);
  QCOMPARE(toggleButton->accessibleName(), QStringLiteral("展开播放列表"));
  QVERIFY(toggleButton->icon().cacheKey() != collapseIconKey);
  const int collapsedVideoWidth = videoOutput->width();

  QTest::mouseClick(toggleButton, Qt::LeftButton);
  QTRY_VERIFY(playlistPanel->isVisible());
  QTRY_VERIFY(videoOutput->width() < collapsedVideoWidth);
  QCOMPARE(toggleButton->accessibleName(), QStringLiteral("收起播放列表"));
}

void MainWindowTest::resizesVideoSurfaceAndTogglesFullScreen() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const videoOutput =
      requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget");
  const QSize initialSize = videoOutput->size();
  auto *const openButton =
      requiredChild<QPushButton>(harness.window, "openFileButton");
  auto *const fullScreenButton =
      requiredChild<QToolButton>(harness.window, "fullScreenButton");
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  auto *const volumeButton =
      requiredChild<QToolButton>(harness.window, "volumeButton");
  auto *const lyricsButton =
      requiredChild<QToolButton>(harness.window, "lyricsButton");
  auto *const seekStepButton =
      requiredChild<QToolButton>(harness.window, "keyboardSeekStepButton");
  auto *const playbackRateButton =
      requiredChild<QToolButton>(harness.window, "playbackRateButton");
  auto *const previousButton =
      requiredChild<QToolButton>(harness.window, "previousButton");
  auto *const playPauseButton =
      requiredChild<QToolButton>(harness.window, "playPauseButton");
  auto *const nextButton =
      requiredChild<QToolButton>(harness.window, "nextButton");
  auto *const stopButton =
      requiredChild<QToolButton>(harness.window, "stopButton");
  auto *const playbackModeButton =
      requiredChild<QToolButton>(harness.window, "playbackModeButton");
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  const std::array<QWidget *, 10> timelineControls{
      previousButton,     playPauseButton,  nextButton,         stopButton,
      volumeButton,       lyricsButton,     playbackRateButton, seekStepButton,
      playbackModeButton, fullScreenButton,
  };
  QVERIFY(harness.window.rect().contains(
      geometryInsideWindow(*openButton, harness.window)));
  QVERIFY(harness.window.rect().contains(
      geometryInsideWindow(*progressSlider, harness.window)));
  const int timelineCenter =
      verticalCenterInsideWindow(*progressSlider, harness.window);
  QVERIFY(
      geometryInsideWindow(*progressSlider, harness.window).right() <
      geometryInsideWindow(*timelineControls.front(), harness.window).left());
  for (std::size_t index = 0; index < timelineControls.size(); ++index) {
    auto *const control = timelineControls.at(index);
    QVERIFY(harness.window.rect().contains(
        geometryInsideWindow(*control, harness.window)));
    QVERIFY(qAbs(verticalCenterInsideWindow(*control, harness.window) -
                 timelineCenter) <= 1);
    if (index + 1 < timelineControls.size()) {
      QVERIFY(
          geometryInsideWindow(*control, harness.window).right() <
          geometryInsideWindow(*timelineControls.at(index + 1), harness.window)
              .left());
    }
  }
  QVERIFY(harness.window.rect().contains(
      geometryInsideWindow(*playlistView, harness.window)));

  harness.window.resize(1200, 800);
  QTRY_VERIFY(videoOutput->size().width() > initialSize.width());
  QTRY_VERIFY(videoOutput->size().height() > initialSize.height());
  QVERIFY(harness.window.rect().contains(
      geometryInsideWindow(*openButton, harness.window)));
  QVERIFY(harness.window.rect().contains(
      geometryInsideWindow(*progressSlider, harness.window)));
  for (auto *const control : timelineControls) {
    QVERIFY(harness.window.rect().contains(
        geometryInsideWindow(*control, harness.window)));
  }
  QVERIFY(harness.window.rect().contains(
      geometryInsideWindow(*playlistView, harness.window)));

  harness.window.resize(harness.window.minimumSize());
  QCoreApplication::processEvents();
  QVERIFY(progressSlider->width() >= 100);
  QVERIFY(harness.window.rect().contains(
      geometryInsideWindow(*progressSlider, harness.window)));
  for (auto *const control : timelineControls) {
    QVERIFY(harness.window.rect().contains(
        geometryInsideWindow(*control, harness.window)));
  }

  harness.presenter.openLocalFile(QStringLiteral("C:/video/fullscreen.mkv"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_VERIFY(fullScreenButton->isEnabled());
  const qint64 enterFullScreenIconKey = fullScreenButton->icon().cacheKey();

  QTest::mouseClick(fullScreenButton, Qt::LeftButton);
  QTRY_VERIFY(harness.window.isFullScreen());
  QTRY_VERIFY(fullScreenButton->isVisible());
  QCOMPARE(fullScreenButton->accessibleName(), QStringLiteral("退出全屏"));
  QVERIFY(fullScreenButton->icon().cacheKey() != enterFullScreenIconKey);

  QTest::mouseClick(fullScreenButton, Qt::LeftButton);
  QTRY_VERIFY(!harness.window.isFullScreen());
  QCOMPARE(fullScreenButton->accessibleName(), QStringLiteral("进入全屏"));
  QVERIFY(fullScreenButton->isVisible());
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

void MainWindowTest::runsShutdownFallbackOnlyAfterTimeout() {
  std::mutex mutex;
  std::condition_variable invoked;
  bool wasInvoked = false;
  ShutdownWatchdog watchdog(20ms, [&] {
    {
      const std::lock_guard lock(mutex);
      wasInvoked = true;
    }
    invoked.notify_all();
  });

  watchdog.arm();
  std::unique_lock lock(mutex);
  QVERIFY(invoked.wait_for(lock, 1s, [&] { return wasInvoked; }));
}

void MainWindowTest::cancelsShutdownFallbackAfterNormalCleanup() {
  std::mutex mutex;
  int invocationCount = 0;
  {
    ShutdownWatchdog watchdog(20ms, [&] {
      const std::lock_guard lock(mutex);
      ++invocationCount;
    });
    watchdog.arm();
    watchdog.complete();
  }
  const std::lock_guard lock(mutex);
  QCOMPARE(invocationCount, 0);
}

} // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::MainWindowTest)

#include "main_window_test.moc"
