#include "main_window.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QScrollBar>
#include <QShortcut>
#include <QTabBar>
#include <QTableWidget>
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
#include "app_state_store.h"
#include "fakes/fake_browser_backend.h"
#include "fakes/fake_player_engine.h"
#include "lyrics_service.h"
#include "lyrics_view.h"
#include "mediahub/core/playlist.h"
#include "player_presenter.h"
#include "playlist_model.h"
#include "shutdown_watchdog.h"
#include "theme_background_widget.h"
#include "theme_settings_dialog.h"
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

  void loadLocalFile(const QString &filePath) override {
    ++localLoadCount;
    lastLocalFilePath = filePath;
    isPending = true;
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
  QString lastLocalFilePath;
  int loadCount{0};
  int localLoadCount{0};
  int cancelCount{0};
  bool isPending{false};
};

class FakeAppStateStore final : public AppStateStore {
public:
  AppStateSnapshot load() override {
    ++loadCount;
    return snapshot;
  }

  void save(const AppStateSnapshot &value) override {
    ++saveCount;
    snapshot = value;
  }

  AppStateSnapshot snapshot;
  int loadCount{0};
  int saveCount{0};
};

struct GuiHarness {
  std::ostringstream logOutput;
  logging::Logger logger{logOutput};
  test::FakePlayerEngine engine;
  EngineEventBridge eventBridge;
  test::FakeBrowserBackend browserBackend;
  MainWindow window;
  FakeLyricsService lyricsService;
  FakeLivePlaylistService livePlaylistService;
  PlayerPresenter presenter;

  explicit GuiHarness(AppStateStore *const appStateStore = nullptr,
                      const bool supportsConcurrentDownloads = false)
      : browserBackend(supportsConcurrentDownloads),
        window(&browserBackend, QStringLiteral("C:/temporary-profile")),
        presenter(engine, eventBridge, window, nullptr, &logger, &lyricsService,
                  &livePlaylistService, appStateStore) {}
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

core::OpenRequestId latestOpenRequestId(const GuiHarness& harness) {
  const auto commands = harness.engine.commands();
  const auto openCommand = std::find_if(
      commands.rbegin(), commands.rend(), [](const auto& command) {
        return command.kind == test::FakeEngineCommandKind::Open;
      });
  return openCommand == commands.rend() ? 0 : openCommand->openRequestId;
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
  void expandsLocalIptvPlaylistAndFallsBackForLocalHlsManifest();
  void reparsesLegacyLocalPlaylistItemsOnActivation();
  void separatesLocalAndLiveListsWithoutStoppingPlayback();
  void switchesToWebWithoutChangingPlaylistsAndPausesNativePlayback();
  void pausesOnlyActiveNativePlaybackWhenEnteringWeb();
  void showsAudibleWebTabCountAcrossDisplayModes();
  void stylesDisplayModesAsStableSegmentedControl();
  void opensMatureThemeCustomizerAndAppliesBackgroundControls();
  void coalescesRapidInteractiveThemePreviews();
  void controlsAndMarksLiveSourcesFromRightClickMenu();
  void keepsLiveListPositionAndLocatesCurrentPlayback();
  void filtersLivePlaylistWithoutChangingSourceRows();
  void persistsLiveFavoritesButNotUnavailableMarks();
  void replacesRemoteLiveListAtomicallyAndKeepsItOnFailure();
  void keepsNetworkStreamsNonSeekableWhenEngineReportsLiveWindow();
  void rejectsInvalidNetworkUrlBeforeCallingEngine();
  void rendersNetworkBufferingAndStopsTimeoutAfterPlaying();
  void startsNetworkTimeoutOnlyAfterEngineBeginsOpening();
  void keepsStopAvailableWhenRetiredPlayerCapacityIsExhausted();
  void cancelsAndTimesOutNetworkOpeningWithoutAcceptingLateEvents();
  void doesNotAutomaticallyReconnectInterruptedNetwork();
  void refreshesCurrentNetworkByButtonAndF5();
  void remembersRecentNetworkUrlsForCurrentSession();
  void roundTripsAppStateThroughSettingsFile();
  void loadsLegacyV04StateWithoutNewFields();
  void restoresPersistedStateWithoutStartingPlayback();
  void persistsPlaylistChangesAndSuccessfulLiveHistory();
  void restoresRecentLocalMediaAndResumesOnce();
  void clearsResumeAfterStopAndNaturalEnd();
  void removesMissingRecentLocalMediaWithoutOpeningEngine();
  void fillsAndDeletesLiveUrlHistoryWithoutLoading();
  void opensLiveSourceMemoWithCtrlM();
  void keepsArrowKeysInsideLiveSourceMemoEditor();
  void managesLiveSourceMemosWithSaveShortcutAndReturnPrompt();
  void savesUnsavedLiveSourceMemosWhenWindowCloses();
  void cancelsLivePlaylistLoadingAndIgnoresLateResult();
  void routesPauseResumeAndStopThroughPresenter();
  void rendersBufferingAsNonInteractiveWait();
  void allowsReplayAfterNaturalEnd();
  void replaysSequentialLastVideoOnFreshSurfaceAfterNaturalEnd();
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
  void routesExplicitMuteShortcutsAndLeavesCtrlF5Unused();
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
  void scalesPlaylistTypographyAcrossWindowBreakpoints();
  void keepsPresentationModesInsideResponsiveBounds();
  void resizesVideoSurfaceAndTogglesFullScreen();
  void togglesFullScreenWithF11();
  void showsSupportedShortcutsFromHelpMenu();
  void confirmsBeforeClosingWithActiveWebDownloads();
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

void MainWindowTest::switchesToWebWithoutChangingPlaylistsAndPausesNativePlayback() {
  GuiHarness harness;
  harness.presenter.addLocalFiles(
      {QStringLiteral("C:/media/first.wav"), QStringLiteral("C:/media/second.wav")});
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));

  harness.window.show();
  QCoreApplication::processEvents();
  auto* const playlist = requiredChild<QListView>(harness.window, "playlistView");
  const int originalRows = playlist->model()->rowCount();
  const int originalPlayCommands =
      commandCount(harness, test::FakeEngineCommandKind::Play);

  QTest::mouseClick(requiredChild<QToolButton>(harness.window, "webModeButton"),
                    Qt::LeftButton);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Pause), 1);
  QCOMPARE(playlist->model()->rowCount(), originalRows);
  QVERIFY(requiredChild<QWidget>(harness.window, "browserPage")->isVisible());

  const int originalMuteCommands = harness.browserBackend.count(
      test::FakeBrowserCommandKind::SetAudioMuted);
  const int originalSuspendCommands = harness.browserBackend.count(
      test::FakeBrowserCommandKind::SetSuspended);
  const std::size_t originalBrowserCommandCount =
      harness.browserBackend.commands.size();
  QTest::mouseClick(requiredChild<QToolButton>(harness.window, "localModeButton"),
                    Qt::LeftButton);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Play),
           originalPlayCommands);
  QCOMPARE(playlist->model()->rowCount(), originalRows);
  QCOMPARE(harness.browserBackend.count(
               test::FakeBrowserCommandKind::SetAudioMuted),
           originalMuteCommands);
  QCOMPARE(harness.browserBackend.count(
               test::FakeBrowserCommandKind::SetSuspended),
           originalSuspendCommands);
  QCOMPARE(harness.browserBackend.commands.size(),
           originalBrowserCommandCount + 1);
  QCOMPARE(harness.browserBackend.lastCommand().kind,
           test::FakeBrowserCommandKind::SetVisible);
  QVERIFY(!harness.browserBackend.lastCommand().flag);
}

void MainWindowTest::pausesOnlyActiveNativePlaybackWhenEnteringWeb() {
  const auto verifyPauseCount = [](const core::PlaybackState targetState,
                                   const int expectedPauseCount) {
    GuiHarness harness;
    harness.presenter.openLocalFile(QStringLiteral("C:/media/state.wav"));
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    if (targetState == core::PlaybackState::Buffering ||
        targetState == core::PlaybackState::Playing ||
        targetState == core::PlaybackState::Paused) {
      harness.engine.emitStateChanged(core::PlaybackState::Buffering);
    }
    if (targetState == core::PlaybackState::Playing ||
        targetState == core::PlaybackState::Paused) {
      harness.engine.emitStateChanged(core::PlaybackState::Playing);
    }
    if (targetState == core::PlaybackState::Paused) {
      harness.engine.emitStateChanged(core::PlaybackState::Paused);
    }

    harness.window.show();
    QCoreApplication::processEvents();
    QTest::mouseClick(
        requiredChild<QToolButton>(harness.window, "webModeButton"),
        Qt::LeftButton);
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Pause),
             expectedPauseCount);
    QTest::mouseClick(
        requiredChild<QToolButton>(harness.window, "webModeButton"),
        Qt::LeftButton);
    QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Pause),
             expectedPauseCount);
  };

  verifyPauseCount(core::PlaybackState::Opening, 1);
  verifyPauseCount(core::PlaybackState::Buffering, 1);
  verifyPauseCount(core::PlaybackState::Playing, 1);
  verifyPauseCount(core::PlaybackState::Paused, 0);
}

void MainWindowTest::showsAudibleWebTabCountAcrossDisplayModes() {
  GuiHarness harness;
  auto* const browserPage =
      requiredChild<QWidget>(harness.window, "browserPage");
  auto* const localModeButton =
      requiredChild<QToolButton>(harness.window, "localModeButton");
  auto* const liveModeButton =
      requiredChild<QToolButton>(harness.window, "liveModeButton");
  auto* const webModeButton =
      requiredChild<QToolButton>(harness.window, "webModeButton");
  const std::size_t originalEngineCommandCount = harness.engine.commands().size();

  QCOMPARE(webModeButton->text(), QStringLiteral("网页"));
  QVERIFY(localModeButton->isChecked());
  QVERIFY(QMetaObject::invokeMethod(browserPage, "audibleTabCountChanged",
                                    Qt::DirectConnection, Q_ARG(int, 1)));
  QCOMPARE(webModeButton->text(), QStringLiteral("网页 · 1"));
  QVERIFY(webModeButton->toolTip().contains(QStringLiteral("1 个网页标签")));
  QVERIFY(localModeButton->isChecked());
  QCOMPARE(harness.engine.commands().size(), originalEngineCommandCount);

  harness.window.showDisplayMode(DisplayMode::Live);
  QVERIFY(liveModeButton->isChecked());
  QCOMPARE(webModeButton->text(), QStringLiteral("网页 · 1"));
  QVERIFY(QMetaObject::invokeMethod(browserPage, "audibleTabCountChanged",
                                    Qt::DirectConnection, Q_ARG(int, 3)));
  QCOMPARE(webModeButton->text(), QStringLiteral("网页 · 3"));
  QVERIFY(liveModeButton->isChecked());

  harness.window.showDisplayMode(DisplayMode::Web);
  QVERIFY(webModeButton->isChecked());
  QCOMPARE(webModeButton->text(), QStringLiteral("网页 · 3"));
  harness.window.showDisplayMode(DisplayMode::Local);
  QCOMPARE(webModeButton->text(), QStringLiteral("网页 · 3"));

  QVERIFY(QMetaObject::invokeMethod(browserPage, "audibleTabCountChanged",
                                    Qt::DirectConnection, Q_ARG(int, 0)));
  QCOMPARE(webModeButton->text(), QStringLiteral("网页"));
  QCOMPARE(webModeButton->toolTip(), QStringLiteral("切换到网页模式"));
  QVERIFY(localModeButton->isChecked());
}

void MainWindowTest::stylesDisplayModesAsStableSegmentedControl() {
  GuiHarness harness;
  auto* const brand = requiredChild<QLabel>(harness.window, "brandLabel");
  auto* const rail = requiredChild<QFrame>(harness.window, "displayModeRail");
  auto* const local =
      requiredChild<QToolButton>(harness.window, "localModeButton");
  auto* const live =
      requiredChild<QToolButton>(harness.window, "liveModeButton");
  auto* const web =
      requiredChild<QToolButton>(harness.window, "webModeButton");

  const QList<QToolButton*> segments{local, live, web};
  for (QToolButton* const segment : segments) {
    QCOMPARE(segment->parentWidget(), rail);
    QCOMPARE(segment->size(), QSize(88, 32));
    QCOMPARE(segment->property("modeSegment").toBool(), true);
    QVERIFY(!segment->accessibleName().isEmpty());
    QVERIFY(!segment->toolTip().isEmpty());
  }
  QCOMPARE(local->accessibleName(), QStringLiteral("本地模式"));
  QCOMPARE(live->accessibleName(), QStringLiteral("直播模式"));
  QCOMPARE(web->accessibleName(), QStringLiteral("网页模式"));
  QCOMPARE(brand->text(), QStringLiteral("MediaHub"));
  harness.window.show();
  QCoreApplication::processEvents();
  QVERIFY(brand->geometry().left() < rail->geometry().left());

  const QString& styleSheet = mainWindowStyleSheet();
  QVERIFY(styleSheet.contains(QStringLiteral("QFrame#displayModeRail")));
  QVERIFY(styleSheet.contains(QStringLiteral("QToolButton#localModeButton:checked")));
  QVERIFY(styleSheet.contains(QStringLiteral("QToolButton#liveModeButton:checked")));
  QVERIFY(styleSheet.contains(QStringLiteral("QToolButton#webModeButton:checked")));
}

void MainWindowTest::opensMatureThemeCustomizerAndAppliesBackgroundControls() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString backgroundPath =
      QDir(directory.path()).filePath(QStringLiteral("theme-background.png"));
  QImage background(96, 64, QImage::Format_RGB32);
  background.fill(QColor(QStringLiteral("#386b8f")));
  QVERIFY(background.save(backgroundPath, "PNG"));

  FakeAppStateStore store;
  GuiHarness harness(&store);
  auto* const themeButton =
      harness.window.findChild<QToolButton*>(QStringLiteral("themeButton"));
  QVERIFY(themeButton != nullptr);
  QVERIFY(themeButton->text().isEmpty());
  QVERIFY(!themeButton->icon().isNull());
  QCOMPARE(themeButton->size(), QSize(32, 32));
  for (const char* const objectName : {"fileMenuButton", "viewMenuButton",
                                       "helpMenuButton"}) {
    auto* const button = harness.window.findChild<QToolButton*>(
        QString::fromLatin1(objectName));
    QVERIFY(button != nullptr);
    QVERIFY(button->text().isEmpty());
    QVERIFY(!button->icon().isNull());
    QVERIFY(button->menu() != nullptr);
    QCOMPARE(button->size(), QSize(32, 32));
  }
  QVERIFY(!harness.window.menuBar()->isVisible());
  bool inspectedDialog = false;

  QTimer::singleShot(0, [&] {
    auto* const dialog =
        qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      return;
    }
    auto* const title = dialog->findChild<QLabel*>(
        QStringLiteral("themeSettingsTitle"));
    auto* const greenPreset = dialog->findChild<QToolButton*>(
        QStringLiteral("themePresetGreenButton"));
    auto* const lightMode = dialog->findChild<QToolButton*>(
        QStringLiteral("themeModeLightButton"));
    auto* const redSlider = dialog->findChild<QSlider*>(
        QStringLiteral("themeRedSlider"));
    auto* const greenSlider = dialog->findChild<QSlider*>(
        QStringLiteral("themeGreenSlider"));
    auto* const blueSlider = dialog->findChild<QSlider*>(
        QStringLiteral("themeBlueSlider"));
    auto* const blurSlider = dialog->findChild<QSlider*>(
        QStringLiteral("themeBlurSlider"));
    auto* const opacitySlider = dialog->findChild<QSlider*>(
        QStringLiteral("themeOpacitySlider"));
    auto* const applyButton = dialog->findChild<QPushButton*>(
        QStringLiteral("themeApplyButton"));
    if (title == nullptr || greenPreset == nullptr || lightMode == nullptr ||
        redSlider == nullptr || greenSlider == nullptr ||
        blueSlider == nullptr || blurSlider == nullptr ||
        opacitySlider == nullptr || applyButton == nullptr) {
      dialog->reject();
      return;
    }
    QCOMPARE(dialog->objectName(), QStringLiteral("themeSettingsDialog"));
    QCOMPARE(title->text(), QStringLiteral("个性化主题"));
    QCOMPARE(blurSlider->orientation(), Qt::Horizontal);
    QCOMPARE(opacitySlider->orientation(), Qt::Horizontal);
    QCOMPARE(blurSlider->minimum(), 0);
    QCOMPARE(blurSlider->maximum(), 100);
    QCOMPARE(opacitySlider->minimum(), 0);
    QCOMPARE(opacitySlider->maximum(), 100);
    QCOMPARE(redSlider->maximum(), 255);
    QCOMPARE(greenSlider->maximum(), 255);
    QCOMPARE(blueSlider->maximum(), 255);
    QTest::mouseClick(lightMode, Qt::LeftButton);
    QTest::mouseClick(greenPreset, Qt::LeftButton);
    QVERIFY(!greenPreset->icon().isNull());
    redSlider->setValue(64);
    greenSlider->setValue(128);
    blueSlider->setValue(192);
    bool acceptedBackground = false;
    QVERIFY(QMetaObject::invokeMethod(
        dialog, "setBackgroundImagePath", Qt::DirectConnection,
        Q_RETURN_ARG(bool, acceptedBackground), Q_ARG(QString, backgroundPath)));
    QVERIFY(acceptedBackground);
    blurSlider->setValue(42);
    opacitySlider->setValue(68);
    inspectedDialog = true;
    applyButton->click();
  });
  QTest::mouseClick(themeButton, Qt::LeftButton);

  QVERIFY(inspectedDialog);
  auto* const centralSurface =
      requiredChild<QWidget>(harness.window, "centralSurface");
  QCOMPARE(centralSurface->property("accentKey").toString(),
           QStringLiteral("custom"));
  QCOMPARE(centralSurface->property("appearanceMode").toString(),
           QStringLiteral("light"));
  QCOMPARE(centralSurface->property("backgroundBlur").toInt(), 42);
  QCOMPARE(centralSurface->property("backgroundOpacity").toInt(), 68);
  QVERIFY(centralSurface->property("customBackground").toBool());
  QVERIFY(store.saveCount >= 1);
  QCOMPARE(store.snapshot.themeSettings.accentKey, QStringLiteral("custom"));
  QCOMPARE(store.snapshot.themeSettings.appearanceMode,
           QStringLiteral("light"));
  QCOMPARE(store.snapshot.themeSettings.customAccentColor,
           QStringLiteral("#4080c0"));
  QCOMPARE(store.snapshot.themeSettings.backgroundImagePath, backgroundPath);
  QCOMPARE(store.snapshot.themeSettings.backgroundBlur, 42);
  QCOMPARE(store.snapshot.themeSettings.backgroundOpacity, 68);

  GuiHarness restored(&store);
  QCOMPARE(restored.window.themeSettings().accentKey,
           store.snapshot.themeSettings.accentKey);
  QCOMPARE(restored.window.themeSettings().appearanceMode,
           QStringLiteral("light"));
  QCOMPARE(restored.window.themeSettings().customAccentColor,
           QStringLiteral("#4080c0"));
  QCOMPARE(restored.window.themeSettings().backgroundImagePath,
           store.snapshot.themeSettings.backgroundImagePath);
  QCOMPARE(restored.window.themeSettings().backgroundBlur, 42);
  QCOMPARE(restored.window.themeSettings().backgroundOpacity, 68);
  QCOMPARE(requiredChild<QWidget>(restored.window, "centralSurface")
               ->property("customBackground")
               .toBool(),
           true);
  restored.window.show();
  QCoreApplication::processEvents();
  auto* const restoredPlaylist =
      requiredChild<QListView>(restored.window, "playlistView");
  restoredPlaylist->ensurePolished();
  QCOMPARE(restoredPlaylist->palette().color(QPalette::Base),
           QColor(QStringLiteral("#e5eef2")));
  auto* const optionPopup =
      requiredChild<QMenu>(restored.window, "optionPopup");
  optionPopup->ensurePolished();
  QCOMPARE(optionPopup->palette().color(QPalette::Window),
           QColor(QStringLiteral("#ffffff")));
  QVERIFY(requiredChild<LyricsView>(restored.window, "lyricsView")
              ->styleSheet()
              .contains(QStringLiteral("rgba(")));
  auto* const restoredCentral =
      requiredChild<QWidget>(restored.window, "centralSurface");
  auto* const backgroundWidget =
      static_cast<ThemeBackgroundWidget*>(restoredCentral);
  auto* const videoOutput =
      requiredChild<VideoOutputWidget>(restored.window, "videoOutputWidget");
  const QImage alignedBackground =
      backgroundWidget->alignedBackgroundFor(videoOutput);
  QCOMPARE(alignedBackground.size(), videoOutput->size());
  QVERIFY(!alignedBackground.isNull());
  QVERIFY(alignedBackground.pixelColor(0, 0).alpha() > 0);
  QVERIFY(alignedBackground.pixelColor(alignedBackground.width() - 1,
                                       alignedBackground.height() - 1)
              .alpha() > 0);
}

void MainWindowTest::coalescesRapidInteractiveThemePreviews() {
  ThemeSettingsDialog dialog(ThemeSettings{});
  dialog.show();
  QCoreApplication::processEvents();
  auto* const redSlider =
      dialog.findChild<QSlider*>(QStringLiteral("themeRedSlider"));
  auto* const greenSlider =
      dialog.findChild<QSlider*>(QStringLiteral("themeGreenSlider"));
  auto* const blueSlider =
      dialog.findChild<QSlider*>(QStringLiteral("themeBlueSlider"));
  auto* const previewTimer =
      dialog.findChild<QTimer*>(QStringLiteral("themePreviewTimer"));
  QVERIFY(redSlider != nullptr);
  QVERIFY(greenSlider != nullptr);
  QVERIFY(blueSlider != nullptr);
  QVERIFY(previewTimer != nullptr);

  QSignalSpy previewSpy(&dialog, &ThemeSettingsDialog::previewChanged);
  for (int value = 32; value <= 160; value += 8) {
    redSlider->setValue(value);
    greenSlider->setValue(255 - value);
    blueSlider->setValue(value / 2);
  }

  QCOMPARE(previewSpy.count(), 0);
  QVERIFY(previewTimer->isActive());
  QTRY_COMPARE_WITH_TIMEOUT(previewSpy.count(), 1, 500);
  QCOMPARE(dialog.settings().customAccentColor,
           QColor(redSlider->value(), greenSlider->value(), blueSlider->value())
               .name(QColor::HexRgb));

  const int directTarget = 30;
  const int clickX = qRound((redSlider->width() - 1) *
                            static_cast<double>(directTarget) / 255.0);
  const QPoint clickPosition(clickX, redSlider->height() / 2);
  QMouseEvent pressEvent(QEvent::MouseButtonPress, clickPosition,
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(redSlider, &pressEvent);
  QVERIFY(redSlider->isSliderDown());
  QVERIFY(qAbs(redSlider->value() - directTarget) <= 1);
  QCOMPARE(previewSpy.count(), 1);
  QVERIFY(!previewTimer->isActive());

  const int dragTarget = 200;
  const int dragX = qRound((redSlider->width() - 1) *
                           static_cast<double>(dragTarget) / 255.0);
  const QPoint dragPosition(dragX, redSlider->height() / 2);
  QMouseEvent moveEvent(QEvent::MouseMove, dragPosition, Qt::NoButton,
                        Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(redSlider, &moveEvent);
  QVERIFY(qAbs(redSlider->value() - dragTarget) <= 1);
  QCOMPARE(previewSpy.count(), 1);
  QVERIFY(!previewTimer->isActive());

  QMouseEvent releaseEvent(QEvent::MouseButtonRelease, dragPosition,
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(redSlider, &releaseEvent);
  QCOMPARE(previewSpy.count(), 2);
  QCOMPARE(dialog.settings().customAccentColor,
           QColor(redSlider->value(), greenSlider->value(), blueSlider->value())
               .name(QColor::HexRgb));
  QVERIFY(!previewTimer->isActive());
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
  QCOMPARE(harness.window.size(), QSize(1200, 800));
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
  QCOMPARE(requiredChild<QPushButton>(harness.window, "openFileButton")->text(),
           QStringLiteral("添加媒体"));
  QVERIFY(requiredChild<QTabBar>(harness.window, "playlistKindTabs")
              ->isHidden());
  auto* const playerDock =
      requiredChild<QWidget>(harness.window, "playerDock");
  QCOMPARE(requiredChild<QWidget>(harness.window, "mediaCard")->parentWidget(),
           playerDock);
  QCOMPARE(
      requiredChild<QWidget>(harness.window, "transportPanel")->parentWidget(),
      playerDock);
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
  auto *const videoOutput =
      requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget");

  QCOMPARE(centralSurface->property("themeMode").toString(),
           QStringLiteral("video"));
  QCOMPARE(presentationModeKey(videoOutput->presentationMode()),
           QStringLiteral("video"));
  QCOMPARE(modeBadge->text(), QStringLiteral("视频"));
  QVERIFY(modeBadge->isHidden());
  QCOMPARE(titleLabel->text(), QStringLiteral("本地媒体"));

  harness.presenter.openLocalFile(QStringLiteral("C:/music/theme-song.mp3"));
  QTRY_COMPARE(centralSurface->property("themeMode").toString(),
               QStringLiteral("audio"));
  QCOMPARE(presentationModeKey(videoOutput->presentationMode()),
           QStringLiteral("audio"));
  QCOMPARE(modeBadge->text(), QStringLiteral("音频"));
  QCOMPARE(titleLabel->text(), QStringLiteral("本地媒体"));
  QCOMPARE(requiredChild<QToolButton>(harness.window, "playPauseButton"),
           playPauseButton);
  QCOMPARE(requiredChild<QSlider>(harness.window, "progressSlider"),
           progressSlider);
  QCOMPARE(requiredChild<QListView>(harness.window, "playlistView"),
           playlistView);

  harness.presenter.openLocalFile(QStringLiteral("C:/video/theme-movie.mp4"));
  QTRY_COMPARE(centralSurface->property("themeMode").toString(),
               QStringLiteral("video"));
  QCOMPARE(presentationModeKey(videoOutput->presentationMode()),
           QStringLiteral("video"));
  QCOMPARE(modeBadge->text(), QStringLiteral("视频"));

  auto *const playlistTabs =
      requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  QCOMPARE(playlistTabs->tabText(0), QStringLiteral("本地列表"));
  QCOMPARE(playlistTabs->tabText(1), QStringLiteral("直播列表"));
  playlistTabs->setCurrentIndex(1);
  QTRY_COMPARE(centralSurface->property("themeMode").toString(),
               QStringLiteral("live"));
  QCOMPARE(presentationModeKey(videoOutput->presentationMode()),
           QStringLiteral("live"));
  QCOMPARE(modeBadge->text(), QStringLiteral("直播"));
  QCOMPARE(titleLabel->text(), QStringLiteral("直播"));
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

void MainWindowTest::
    expandsLocalIptvPlaylistAndFallsBackForLocalHlsManifest() {
  GuiHarness harness;
  auto *const tabs = requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const statusLabel =
      requiredChild<QLabel>(harness.window, "livePlaylistStatusLabel");
  const QString playlistPath = QStringLiteral("C:/IPTV 清单/cn_all.m3u8");

  harness.presenter.openLocalFile(playlistPath);

  QCOMPARE(harness.livePlaylistService.localLoadCount, 1);
  QCOMPARE(harness.livePlaylistService.lastLocalFilePath, playlistPath);
  QCOMPARE(harness.livePlaylistService.loadCount, 0);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);
  QCOMPARE(tabs->currentIndex(), 1);
  QVERIFY(statusLabel->text().contains(QStringLiteral("本地直播清单")));

  LivePlaylistLoadResult result;
  result.library.channels = {
      {"CCTV-1", "", "http://192.0.2.1/live/index.m3u8", "", "", ""},
      {"凤凰中文", "", "https://example.test/phoenix.m3u8", "", "", ""},
  };
  harness.livePlaylistService.complete(std::move(result));

  QCOMPARE(playlistView->model()->rowCount(), 2);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("CCTV-1"));
  QCOMPARE(playlistView->model()->index(1, 0).data(Qt::UserRole).toString(),
           QStringLiteral("凤凰中文"));
  QVERIFY(statusLabel->text().contains(QStringLiteral("已载入 2 项")));
  QVERIFY(harness.logOutput.str().find("IPTV") == std::string::npos);

  tabs->setCurrentIndex(0);
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("cn_all.m3u8"));
  harness.presenter.openLocalFile(playlistPath);
  QCOMPARE(harness.livePlaylistService.localLoadCount, 2);
  LivePlaylistLoadResult repeatedResult;
  repeatedResult.library.channels = {
      {"CCTV-1", "", "http://192.0.2.1/live/index.m3u8", "", "", ""},
  };
  harness.livePlaylistService.complete(std::move(repeatedResult));
  tabs->setCurrentIndex(0);
  QCOMPARE(playlistView->model()->rowCount(), 1);

  const QString hlsPath = QStringLiteral("C:/IPTV 清单/single.m3u8");
  harness.presenter.openLocalFile(hlsPath);
  QCOMPARE(harness.livePlaylistService.localLoadCount, 3);
  harness.livePlaylistService.fail(LivePlaylistLoadError::HlsMediaManifest);

  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 1);
  QVERIFY(harness.engine.commands().back().media.has_value());
  QCOMPARE(harness.engine.commands().back().media->source,
           hlsPath.toUtf8().toStdString());
  QVERIFY(harness.engine.commands().back().media->kind ==
          core::MediaSourceKind::LocalFile);
  QCOMPARE(tabs->currentIndex(), 0);
  QCOMPARE(playlistView->model()->rowCount(), 2);

  harness.presenter.addLocalFiles(
      {QStringLiteral("C:/one.mp3"), QStringLiteral("C:/channels.m3u")});
  QCOMPARE(harness.livePlaylistService.localLoadCount, 3);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 1);
}

void MainWindowTest::reparsesLegacyLocalPlaylistItemsOnActivation() {
  FakeAppStateStore store;
  const QString playlistPath = QStringLiteral("C:/旧列表/cn_all.m3u8");
  store.snapshot.localPlaylist = {core::makeMediaItem(
      playlistPath.toUtf8().toStdString(), "cn_all.m3u8")};
  GuiHarness harness(&store);
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");

  QCOMPARE(playlistView->model()->rowCount(), 1);
  QVERIFY(QMetaObject::invokeMethod(
      playlistView, "doubleClicked", Qt::DirectConnection,
      Q_ARG(QModelIndex, playlistView->model()->index(0, 0))));

  QCOMPARE(harness.livePlaylistService.localLoadCount, 1);
  QCOMPARE(harness.livePlaylistService.lastLocalFilePath, playlistPath);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);

  LivePlaylistLoadResult result;
  result.library.channels = {
      {"CCTV-1", "", "http://192.0.2.1/live/index.m3u8", "", "", ""},
  };
  harness.livePlaylistService.complete(std::move(result));

  QCOMPARE(requiredChild<QTabBar>(harness.window, "playlistKindTabs")
               ->currentIndex(),
           1);
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("CCTV-1"));
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
  auto *const livePlaylistTools =
      requiredChild<QFrame>(harness.window, "livePlaylistTools");
  auto *const playlistTitle =
      requiredChild<QLabel>(harness.window, "playlistTitleLabel");
  auto *const livePlaylistLoadButton =
      requiredChild<QPushButton>(harness.window, "livePlaylistLoadButton");
  auto *const livePlaylistLocateButton =
      requiredChild<QPushButton>(harness.window, "livePlaylistLocateButton");

  QCOMPARE(tabs->currentIndex(), 0);
  QCOMPARE(playlistTitle->text(), QStringLiteral("本地队列"));
  QVERIFY(!openButton->isHidden());
  QVERIFY(livePlaylistTools->isHidden());
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
  QCOMPARE(playlistTitle->text(), QStringLiteral("直播频道"));
  QVERIFY(openButton->isHidden());
  QVERIFY(!livePlaylistTools->isHidden());
  QVERIFY(!playlistUrlEdit->isHidden());
  QCOMPARE(livePlaylistLoadButton->parentWidget(), livePlaylistTools);
  QCOMPARE(livePlaylistLocateButton->parentWidget(), livePlaylistTools);

  const auto commandCountBeforeSwitch = harness.engine.commands().size();
  tabs->setCurrentIndex(0);
  QCoreApplication::processEvents();
  QCOMPARE(playlistView->model()->rowCount(), 2);
  QCOMPARE(playlistView->selectionMode(), QAbstractItemView::ExtendedSelection);
  QCOMPARE(playlistView->contextMenuPolicy(), Qt::CustomContextMenu);
  QCOMPARE(playlistTitle->text(), QStringLiteral("本地队列"));
  QVERIFY(livePlaylistTools->isHidden());
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

void MainWindowTest::filtersLivePlaylistWithoutChangingSourceRows() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  const QStringList urls{
      QStringLiteral("https://example.test/Alpha-News.m3u8"),
      QStringLiteral("https://example.test/中文音乐.m3u8"),
      QStringLiteral("https://example.test/beta-Sports.m3u8"),
  };
  for (const QString& url : urls) {
    harness.presenter.openNetworkUrl(url);
  }

  auto* const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto* const searchEdit =
      requiredChild<QLineEdit>(harness.window, "livePlaylistSearchEdit");
  auto* const locateButton = requiredChild<QPushButton>(
      harness.window, "livePlaylistLocateButton");
  auto* const model = playlistView->model();
  QCOMPARE(model->rowCount(), 3);

  searchEdit->setText(QStringLiteral("ALPHA"));
  QVERIFY(!playlistView->isRowHidden(0));
  QVERIFY(playlistView->isRowHidden(1));
  QVERIFY(playlistView->isRowHidden(2));
  searchEdit->setText(QStringLiteral("中文"));
  QVERIFY(playlistView->isRowHidden(0));
  QVERIFY(!playlistView->isRowHidden(1));
  QVERIFY(playlistView->isRowHidden(2));
  auto* const liveMenu =
      requiredChild<QMenu>(harness.window, "livePlaylistContextMenu");
  auto* const favoriteAction =
      requiredChild<QAction>(harness.window, "livePlaylistFavoriteAction");
  QVERIFY(requestPlaylistContextMenu(*playlistView, 1));
  liveMenu->hide();
  favoriteAction->trigger();
  QVERIFY(model->index(1, 0).data(PlaylistModel::kFavoriteRole).toBool());

  const int opensBeforeActivation =
      commandCount(harness, test::FakeEngineCommandKind::Open);
  QVERIFY(QMetaObject::invokeMethod(
      playlistView, "doubleClicked", Qt::DirectConnection,
      Q_ARG(QModelIndex, model->index(1, 0))));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBeforeActivation + 1);
  const auto commands = harness.engine.commands();
  QVERIFY(commands.back().media.has_value());
  QCOMPARE(QString::fromUtf8(commands.back().media->source.c_str()), urls.at(1));

  searchEdit->setText(QStringLiteral("没有匹配项"));
  QVERIFY(playlistView->isRowHidden(0));
  QVERIFY(playlistView->isRowHidden(1));
  QVERIFY(playlistView->isRowHidden(2));
  const int stopsBeforeLocate =
      commandCount(harness, test::FakeEngineCommandKind::Stop);
  QTest::mouseClick(locateButton, Qt::LeftButton);
  QCOMPARE(searchEdit->text(), QString{});
  QCOMPARE(playlistView->currentIndex().row(), 1);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop),
           stopsBeforeLocate);

  searchEdit->setText(QStringLiteral("gamma"));
  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/Gamma-Movie.m3u8"));
  QCOMPARE(model->rowCount(), 4);
  QVERIFY(!playlistView->isRowHidden(3));
  searchEdit->clear();
  for (int row = 0; row < model->rowCount(); ++row) {
    QVERIFY(!playlistView->isRowHidden(row));
  }
}

void MainWindowTest::persistsLiveFavoritesButNotUnavailableMarks() {
  FakeAppStateStore store;
  const QString firstUrl =
      QStringLiteral("https://example.test/live/first.m3u8");
  const QString secondUrl =
      QStringLiteral("https://example.test/live/second.m3u8");
  store.snapshot.favoriteLiveSourceUrls = QStringList{firstUrl};

  {
    GuiHarness harness(&store);
    harness.window.show();
    QCoreApplication::processEvents();
    harness.presenter.openNetworkUrl(firstUrl);
    harness.presenter.openNetworkUrl(secondUrl);
    auto* const view =
        requiredChild<QListView>(harness.window, "playlistView");
    auto* const menu =
        requiredChild<QMenu>(harness.window, "livePlaylistContextMenu");
    auto* const markAction =
        requiredChild<QAction>(harness.window, "livePlaylistMarkAction");
    auto* const favoriteAction =
        requiredChild<QAction>(harness.window, "livePlaylistFavoriteAction");
    QVERIFY(view->model()->index(0, 0)
                .data(PlaylistModel::kFavoriteRole)
                .toBool());

    QVERIFY(requestPlaylistContextMenu(*view, 0));
    menu->hide();
    markAction->trigger();
    QVERIFY(view->model()->index(0, 0)
                .data(PlaylistModel::kMarkedRole)
                .toBool());
    QVERIFY(requestPlaylistContextMenu(*view, 1));
    menu->hide();
    favoriteAction->trigger();
    QCOMPARE(store.snapshot.favoriteLiveSourceUrls.size(), 2);
    QVERIFY(store.snapshot.favoriteLiveSourceUrls.contains(firstUrl));
    QVERIFY(store.snapshot.favoriteLiveSourceUrls.contains(secondUrl));
  }

  GuiHarness restored(&store);
  restored.presenter.openNetworkUrl(firstUrl);
  restored.presenter.openNetworkUrl(secondUrl);
  auto* const restoredView =
      requiredChild<QListView>(restored.window, "playlistView");
  for (int row = 0; row < 2; ++row) {
    const QModelIndex index = restoredView->model()->index(row, 0);
    QVERIFY(index.data(PlaylistModel::kFavoriteRole).toBool());
    QVERIFY(!index.data(PlaylistModel::kMarkedRole).toBool());
  }
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
  QVERIFY(loadButton->isEnabled());
  QCOMPARE(loadButton->text(), QStringLiteral("取消载入"));
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
  QCOMPARE(loadButton->text(), QStringLiteral("载入清单"));
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
  QVERIFY(!timeoutTimer->isActive());
  QCOMPARE(statusText(harness), QStringLiteral("正在连接..."));
  QVERIFY(
      requiredChild<QToolButton>(harness.window, "stopButton")->isEnabled());

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_VERIFY(timeoutTimer->isActive());
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

void MainWindowTest::startsNetworkTimeoutOnlyAfterEngineBeginsOpening() {
  GuiHarness harness;
  auto *const timeoutTimer =
      harness.presenter.findChild<QTimer *>("networkOpenTimeoutTimer");
  QVERIFY(timeoutTimer != nullptr);

  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/first.m3u8"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));

  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/second.m3u8"));
  QCOMPARE(statusText(harness), QStringLiteral("正在连接..."));
  QVERIFY(!timeoutTimer->isActive());

  std::vector<void*> openSurfaces;
  for (const auto& command : harness.engine.commands()) {
    if (command.kind == test::FakeEngineCommandKind::Open) {
      openSurfaces.push_back(command.nativeHandle);
    }
  }
  QCOMPARE(openSurfaces.size(), std::size_t{2});
  QVERIFY(openSurfaces[0] != nullptr);
  QVERIFY(openSurfaces[1] != nullptr);
  QVERIFY(openSurfaces[0] != openSurfaces[1]);

  const auto openCommands = harness.engine.commands();
  std::vector<core::OpenRequestId> openRequestIds;
  for (const auto& command : openCommands) {
    if (command.kind == test::FakeEngineCommandKind::Open) {
      openRequestIds.push_back(command.openRequestId);
    }
  }
  QCOMPARE(openRequestIds.size(), std::size_t{2});
  harness.engine.emitOpenStarted(openRequestIds[0]);
  QCoreApplication::processEvents();
  QVERIFY(!timeoutTimer->isActive());

  harness.engine.emitOpenStarted(openRequestIds[1]);
  QTRY_VERIFY(timeoutTimer->isActive());
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
}

void MainWindowTest::
    keepsStopAvailableWhenRetiredPlayerCapacityIsExhausted() {
  GuiHarness harness;
  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/first.m3u8"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  harness.presenter.openNetworkUrl(
      QStringLiteral("https://example.test/live/rejected.m3u8"));

  harness.engine.emitStateChanged(core::PlaybackState::Failed);
  harness.engine.emitError(core::PlaybackError{
      core::PlaybackErrorKind::EngineBusy,
      "Retired player capacity exhausted",
      "多个旧直播仍在退出，请稍候后重试。",
  });
  QTRY_COMPARE(statusText(harness),
               QStringLiteral("旧直播正在退出，请稍候后重试"));
  auto* const stopButton =
      requiredChild<QToolButton>(harness.window, "stopButton");
  QVERIFY(stopButton->isEnabled());
  QVERIFY(requiredChild<QLabel>(harness.window, "playbackErrorLabel")
              ->text()
              .contains(QStringLiteral("多个旧直播仍在退出")));

  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(statusText(harness),
               QStringLiteral("旧直播正在退出，请稍候后重试"));
  QVERIFY(stopButton->isEnabled());
  QTest::mouseClick(stopButton, Qt::LeftButton);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Stop), 1);
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

void MainWindowTest::roundTripsAppStateThroughSettingsFile() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString settingsFile =
      QDir(directory.path()).filePath(QStringLiteral("state.ini"));
  AppStateSnapshot expected;
  expected.localPlaylist = {
      core::MediaItem{"C:/媒体 库/第一首.mp3",
                      core::MediaSourceKind::LocalFile, "列表名 一"},
      core::MediaItem{"C:/video/two.mp4", core::MediaSourceKind::LocalFile,
                      "第二个视频"},
  };
  expected.recentLocalMedia = {
      LocalPlaybackRecord{expected.localPlaylist.at(1), 42'000, 180'000},
  };
  expected.lastLivePlaylistUrl =
      QStringLiteral("https://example.test/最后清单.m3u?token=private");
  expected.livePlaylistUrlHistory = QStringList{
      expected.lastLivePlaylistUrl,
      QStringLiteral("https://example.test/older.m3u"),
  };
  expected.favoriteLiveSourceUrls = QStringList{
      QStringLiteral("https://stream.example/收藏一号.m3u8"),
      QStringLiteral("https://stream.example/favorite-two.m3u8"),
  };
  expected.liveSourceMemos = {
      LiveSourceMemo{QStringLiteral("https://live.example/一号.m3u8"),
                     QStringLiteral("常用 中文源")},
      LiveSourceMemo{QStringLiteral("rtmp://live.example/backup"),
                     QStringLiteral("备用源")},
  };
  expected.themeSettings = ThemeSettings{
      QStringLiteral("custom"),
      QStringLiteral("C:/Users/example/Pictures/theme.jpg"), 37, 64,
      QStringLiteral("light"), QStringLiteral("#4c86b8")};

  {
    QSettingsAppStateStore store(settingsFile);
    store.save(expected);
  }
  QSettingsAppStateStore restoredStore(settingsFile);
  const AppStateSnapshot restored = restoredStore.load();

  QCOMPARE(restored.localPlaylist.size(), expected.localPlaylist.size());
  for (std::size_t index = 0; index < expected.localPlaylist.size(); ++index) {
    QCOMPARE(restored.localPlaylist.at(index).source,
             expected.localPlaylist.at(index).source);
    QCOMPARE(restored.localPlaylist.at(index).displayName,
             expected.localPlaylist.at(index).displayName);
    QCOMPARE(restored.localPlaylist.at(index).kind,
             core::MediaSourceKind::LocalFile);
  }
  QCOMPARE(restored.lastLivePlaylistUrl, expected.lastLivePlaylistUrl);
  QCOMPARE(restored.recentLocalMedia.size(),
           expected.recentLocalMedia.size());
  QCOMPARE(restored.recentLocalMedia.front().item,
           expected.recentLocalMedia.front().item);
  QCOMPARE(restored.recentLocalMedia.front().positionMilliseconds,
           qint64(42'000));
  QCOMPARE(restored.recentLocalMedia.front().durationMilliseconds,
           qint64(180'000));
  QCOMPARE(restored.livePlaylistUrlHistory,
           expected.livePlaylistUrlHistory);
  QCOMPARE(restored.favoriteLiveSourceUrls,
           expected.favoriteLiveSourceUrls);
  QCOMPARE(restored.liveSourceMemos.size(), expected.liveSourceMemos.size());
  for (int index = 0; index < expected.liveSourceMemos.size(); ++index) {
    QCOMPARE(restored.liveSourceMemos.at(index).sourceUrl,
             expected.liveSourceMemos.at(index).sourceUrl);
    QCOMPARE(restored.liveSourceMemos.at(index).note,
             expected.liveSourceMemos.at(index).note);
  }
  QCOMPARE(restored.themeSettings.accentKey, expected.themeSettings.accentKey);
  QCOMPARE(restored.themeSettings.backgroundImagePath,
           expected.themeSettings.backgroundImagePath);
  QCOMPARE(restored.themeSettings.backgroundBlur,
           expected.themeSettings.backgroundBlur);
  QCOMPARE(restored.themeSettings.backgroundOpacity,
           expected.themeSettings.backgroundOpacity);
  QCOMPARE(restored.themeSettings.appearanceMode,
           expected.themeSettings.appearanceMode);
  QCOMPARE(restored.themeSettings.customAccentColor,
           expected.themeSettings.customAccentColor);
}

void MainWindowTest::loadsLegacyV04StateWithoutNewFields() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString settingsFile =
      QDir(directory.path()).filePath(QStringLiteral("v04-state.ini"));
  {
    QSettings settings(settingsFile, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("playbackState"));
    settings.setValue(QStringLiteral("version"), 1);
    settings.beginWriteArray(QStringLiteral("localPlaylist"));
    settings.setArrayIndex(0);
    settings.setValue(QStringLiteral("source"),
                      QStringLiteral("C:/legacy/旧歌曲.mp3"));
    settings.setValue(QStringLiteral("displayName"),
                      QStringLiteral("旧歌曲"));
    settings.endArray();
    settings.setValue(QStringLiteral("lastLivePlaylistUrl"),
                      QStringLiteral("https://example.test/legacy.m3u"));
    settings.endGroup();
    settings.sync();
  }

  QSettingsAppStateStore store(settingsFile);
  const AppStateSnapshot restored = store.load();
  QCOMPARE(restored.localPlaylist.size(), std::size_t(1));
  QCOMPARE(restored.localPlaylist.front().displayName,
           std::string("旧歌曲"));
  QCOMPARE(restored.lastLivePlaylistUrl,
           QStringLiteral("https://example.test/legacy.m3u"));
  QVERIFY(restored.recentLocalMedia.empty());
  QVERIFY(restored.favoriteLiveSourceUrls.isEmpty());
  QCOMPARE(restored.themeSettings.accentKey, QStringLiteral("default"));
  QVERIFY(restored.themeSettings.backgroundImagePath.isEmpty());
  QCOMPARE(restored.themeSettings.backgroundBlur, 0);
  QCOMPARE(restored.themeSettings.backgroundOpacity, 55);
  QCOMPARE(restored.themeSettings.appearanceMode, QStringLiteral("dark"));
  QVERIFY(restored.themeSettings.customAccentColor.isEmpty());
}

void MainWindowTest::restoresPersistedStateWithoutStartingPlayback() {
  FakeAppStateStore store;
  store.snapshot.localPlaylist = {
      core::MediaItem{"C:/media/one.mp3", core::MediaSourceKind::LocalFile,
                      "第一首"},
      core::MediaItem{"C:/media/two.mp4", core::MediaSourceKind::LocalFile,
                      "第二段"},
  };
  store.snapshot.lastLivePlaylistUrl =
      QStringLiteral("https://example.test/last.m3u");
  store.snapshot.livePlaylistUrlHistory = QStringList{
      QStringLiteral("https://example.test/last.m3u"),
      QStringLiteral("https://example.test/older.m3u"),
  };

  GuiHarness harness(&store);
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const playlistUrlEdit =
      requiredChild<QLineEdit>(harness.window, "livePlaylistUrlEdit");

  QCOMPARE(store.loadCount, 1);
  QCOMPARE(playlistView->model()->rowCount(), 2);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("第一首"));
  QCOMPARE(playlistView->model()->index(1, 0).data(Qt::UserRole).toString(),
           QStringLiteral("第二段"));
  QCOMPARE(playlistUrlEdit->text(), store.snapshot.lastLivePlaylistUrl);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);
  QCOMPARE(harness.livePlaylistService.loadCount, 0);
  QCOMPARE(statusText(harness), QStringLiteral("未打开媒体"));

  playlistUrlEdit->setText(
      QStringLiteral("https://example.test/edited-before-close.m3u"));
  harness.presenter.shutdown();
  QCOMPARE(store.snapshot.lastLivePlaylistUrl,
           QStringLiteral("https://example.test/edited-before-close.m3u"));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);
}

void MainWindowTest::persistsPlaylistChangesAndSuccessfulLiveHistory() {
  FakeAppStateStore store;
  GuiHarness harness(&store);
  harness.presenter.addLocalFiles({QStringLiteral("C:/list/one.mp3"),
                                   QStringLiteral("C:/list/two.mp4")});
  QCOMPARE(store.snapshot.localPlaylist.size(), std::size_t(2));

  harness.window.playlistItemMoveRequested(1, 0);
  QCOMPARE(store.snapshot.localPlaylist.at(0).displayName,
           std::string("two.mp4"));
  harness.window.playlistItemRenameRequested(0, QStringLiteral("置顶视频"));
  QCOMPARE(QString::fromUtf8(
               store.snapshot.localPlaylist.at(0).displayName.c_str()),
           QStringLiteral("置顶视频"));
  harness.window.playlistItemsRemoveRequested(QList<int>{1});
  QCOMPARE(store.snapshot.localPlaylist.size(), std::size_t(1));

  auto *const tabs = requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  auto *const urlEdit =
      requiredChild<QLineEdit>(harness.window, "livePlaylistUrlEdit");
  auto *const loadButton = requiredChild<QPushButton>(
      harness.window, "livePlaylistLoadButton");
  tabs->setCurrentIndex(1);
  for (int index = 0; index < 22; ++index) {
    const QString url =
        QStringLiteral("https://example.test/list/%1.m3u").arg(index);
    urlEdit->setText(url);
    QTest::mouseClick(loadButton, Qt::LeftButton);
    LivePlaylistLoadResult result;
    result.library.channels = {
        core::LiveChannel{QStringLiteral("频道 %1").arg(index).toStdString(),
                          "", "https://stream.example/live", "", "", ""},
    };
    harness.livePlaylistService.complete(std::move(result));
  }
  QCOMPARE(store.snapshot.livePlaylistUrlHistory.size(), 20);
  QCOMPARE(store.snapshot.livePlaylistUrlHistory.front(),
           QStringLiteral("https://example.test/list/21.m3u"));
  QCOMPARE(store.snapshot.livePlaylistUrlHistory.back(),
           QStringLiteral("https://example.test/list/2.m3u"));

  urlEdit->setText(QStringLiteral("https://example.test/broken.m3u"));
  QTest::mouseClick(loadButton, Qt::LeftButton);
  harness.livePlaylistService.fail(LivePlaylistLoadError::InvalidFormat);
  QCOMPARE(store.snapshot.lastLivePlaylistUrl,
           QStringLiteral("https://example.test/broken.m3u"));
  QVERIFY(!store.snapshot.livePlaylistUrlHistory.contains(
      QStringLiteral("https://example.test/broken.m3u")));
}

void MainWindowTest::restoresRecentLocalMediaAndResumesOnce() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString mediaPath =
      QDir(directory.path()).filePath(QStringLiteral("长视频.mp4"));
  QFile mediaFile(mediaPath);
  QVERIFY(mediaFile.open(QIODevice::WriteOnly));
  mediaFile.write("MediaHub resume test");
  mediaFile.close();

  FakeAppStateStore store;
  const core::MediaItem item{mediaPath.toUtf8().toStdString(),
                             core::MediaSourceKind::LocalFile,
                             "长视频.mp4"};
  store.snapshot.localPlaylist = {item};
  store.snapshot.recentLocalMedia = {
      LocalPlaybackRecord{item, 60'000, 120'000},
  };

  GuiHarness harness(&store);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);
  QCOMPARE(harness.livePlaylistService.loadCount, 0);
  auto* const recentAction =
      requiredChild<QAction>(harness.window, "recentLocalMediaAction0");
  QVERIFY(recentAction->text().contains(QStringLiteral("继续 01:00")));
  recentAction->trigger();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 1);
  QCOMPARE(store.snapshot.localPlaylist.size(), std::size_t(1));

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play), 1);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  harness.engine.emitPositionChanged(
      core::PlaybackPosition{0ms, 120s, true});
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
  const auto commandsAfterResume = harness.engine.commands();
  const auto seekCommand = std::find_if(
      commandsAfterResume.rbegin(), commandsAfterResume.rend(),
      [](const test::FakeEngineCommand& command) {
        return command.kind == test::FakeEngineCommandKind::Seek;
      });
  QVERIFY(seekCommand != commandsAfterResume.rend());
  QCOMPARE(seekCommand->position, 60s);

  harness.engine.emitPositionChanged(
      core::PlaybackPosition{60s, 120s, true});
  harness.engine.emitPositionChanged(
      core::PlaybackPosition{61s, 120s, true});
  QCoreApplication::processEvents();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);

  auto* const persistTimer = harness.presenter.findChild<QTimer*>(
      QStringLiteral("appStatePersistTimer"));
  QVERIFY(persistTimer != nullptr);
  persistTimer->setInterval(0);
  harness.engine.emitPositionChanged(
      core::PlaybackPosition{70s, 120s, true});
  QTRY_COMPARE(store.snapshot.recentLocalMedia.front().positionMilliseconds,
               qint64(70'000));
}

void MainWindowTest::clearsResumeAfterStopAndNaturalEnd() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString mediaPath =
      QDir(directory.path()).filePath(QStringLiteral("长音频.wav"));
  QFile mediaFile(mediaPath);
  QVERIFY(mediaFile.open(QIODevice::WriteOnly));
  mediaFile.write("MediaHub stop and end test");
  mediaFile.close();
  const core::MediaItem item{mediaPath.toUtf8().toStdString(),
                             core::MediaSourceKind::LocalFile,
                             "长音频.wav"};

  FakeAppStateStore stopStore;
  stopStore.snapshot.localPlaylist = {item};
  stopStore.snapshot.recentLocalMedia = {
      LocalPlaybackRecord{item, 60'000, 120'000},
  };
  {
    GuiHarness harness(&stopStore);
    requiredChild<QAction>(harness.window, "recentLocalMediaAction0")
        ->trigger();
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    harness.engine.emitPositionChanged(
        core::PlaybackPosition{60s, 120s, true});
    QTRY_COMPARE(statusText(harness), QStringLiteral("正在播放"));
    emit harness.window.stopRequested();
    QCOMPARE(stopStore.snapshot.recentLocalMedia.front().positionMilliseconds,
             qint64(0));
  }

  FakeAppStateStore endStore;
  endStore.snapshot.localPlaylist = {item};
  endStore.snapshot.recentLocalMedia = {
      LocalPlaybackRecord{item, 45'000, 120'000},
  };
  GuiHarness ended(&endStore);
  requiredChild<QAction>(ended.window, "recentLocalMediaAction0")->trigger();
  ended.engine.emitStateChanged(core::PlaybackState::Opening);
  ended.engine.emitStateChanged(core::PlaybackState::Playing);
  ended.engine.emitPositionChanged(
      core::PlaybackPosition{45s, 120s, true});
  QTRY_COMPARE(statusText(ended), QStringLiteral("正在播放"));
  ended.engine.emitEndReached();
  QTRY_COMPARE(endStore.snapshot.recentLocalMedia.front().positionMilliseconds,
               qint64(0));
}

void MainWindowTest::removesMissingRecentLocalMediaWithoutOpeningEngine() {
  FakeAppStateStore store;
  const core::MediaItem missing{
      "C:/missing/MediaHub-v0.5-not-found.mp4",
      core::MediaSourceKind::LocalFile, "已移除视频.mp4"};
  store.snapshot.recentLocalMedia = {
      LocalPlaybackRecord{missing, 30'000, 120'000},
  };
  GuiHarness harness(&store);
  requiredChild<QAction>(harness.window, "recentLocalMediaAction0")->trigger();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);
  QVERIFY(store.snapshot.recentLocalMedia.empty());
  QVERIFY(requiredChild<QLabel>(harness.window, "playbackErrorLabel")
              ->text()
              .contains(QStringLiteral("已不存在")));
}

void MainWindowTest::fillsAndDeletesLiveUrlHistoryWithoutLoading() {
  FakeAppStateStore store;
  store.snapshot.lastLivePlaylistUrl =
      QStringLiteral("https://example.test/current.m3u");
  store.snapshot.livePlaylistUrlHistory = QStringList{
      QStringLiteral("https://example.test/one.m3u"),
      QStringLiteral("https://example.test/two.m3u"),
  };
  GuiHarness harness(&store);
  auto *const tabs = requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  tabs->setCurrentIndex(1);
  auto *const historyButton = requiredChild<QToolButton>(
      harness.window, "livePlaylistHistoryButton");
  auto *const urlEdit =
      requiredChild<QLineEdit>(harness.window, "livePlaylistUrlEdit");
  QCOMPARE(historyButton->text(), QString{});
  QCOMPARE(historyButton->accessibleName(), QStringLiteral("历史直播源"));
  QCOMPARE(historyButton->toolTip(), QStringLiteral("历史直播源"));
  QVERIFY(!historyButton->icon().isNull());

  bool selectedFromHistory = false;
  QTimer::singleShot(0, [&] {
    auto *const dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      return;
    }
    auto *const list = dialog->findChild<QListWidget *>(
        QStringLiteral("liveUrlHistoryList"));
    auto *const useButton = dialog->findChild<QPushButton *>(
        QStringLiteral("liveUrlHistoryUseButton"));
    if (list == nullptr || useButton == nullptr) {
      return;
    }
    QVERIFY(!dialog->windowFlags().testFlag(
        Qt::WindowContextHelpButtonHint));
    QVERIFY(!dialog->styleSheet().isEmpty());
    QVERIFY(list->alternatingRowColors());
    QVERIFY(useButton->isDefault());
    QVERIFY(useButton->minimumHeight() >= 46);
    QVERIFY(useButton->minimumWidth() >= 142);
    list->setCurrentRow(1);
    selectedFromHistory = true;
    useButton->click();
  });
  QTest::mouseClick(historyButton, Qt::LeftButton);
  QVERIFY(selectedFromHistory);
  QCOMPARE(urlEdit->text(), QStringLiteral("https://example.test/two.m3u"));
  QCOMPARE(harness.livePlaylistService.loadCount, 0);

  bool deletedFromHistory = false;
  QTimer::singleShot(0, [&] {
    auto *const dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      return;
    }
    auto *const list = dialog->findChild<QListWidget *>(
        QStringLiteral("liveUrlHistoryList"));
    auto *const deleteButton = dialog->findChild<QPushButton *>(
        QStringLiteral("liveUrlHistoryDeleteButton"));
    auto *const cancelButton = dialog->findChild<QPushButton *>(
        QStringLiteral("liveUrlHistoryCancelButton"));
    if (list == nullptr || deleteButton == nullptr || cancelButton == nullptr) {
      return;
    }
    list->setCurrentRow(0);
    deleteButton->click();
    deletedFromHistory = true;
    cancelButton->click();
  });
  QTest::mouseClick(historyButton, Qt::LeftButton);
  QVERIFY(deletedFromHistory);
  QCOMPARE(store.snapshot.livePlaylistUrlHistory,
           QStringList{QStringLiteral("https://example.test/two.m3u")});
  QCOMPARE(urlEdit->text(), QStringLiteral("https://example.test/two.m3u"));
  QCOMPARE(harness.livePlaylistService.loadCount, 0);
}

void MainWindowTest::opensLiveSourceMemoWithCtrlM() {
  GuiHarness harness;
  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();
  auto *const memoAction =
      requiredChild<QAction>(harness.window, "liveSourceMemoAction");
  QCOMPARE(memoAction->shortcut(),
           QKeySequence(static_cast<int>(Qt::CTRL) |
                        static_cast<int>(Qt::Key_M)));
  QCOMPARE(memoAction->shortcutContext(), Qt::WindowShortcut);
  bool openedFromShortcut = false;

  QTimer::singleShot(0, [&] {
    auto *const dialog =
        qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      return;
    }
    openedFromShortcut =
        dialog->objectName() == QStringLiteral("liveSourceMemoDialog");
    dialog->close();
  });
  QTest::keyClick(&harness.window, Qt::Key_M, Qt::ControlModifier);

  QVERIFY(openedFromShortcut);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);
  QCOMPARE(harness.livePlaylistService.loadCount, 0);
}

void MainWindowTest::keepsArrowKeysInsideLiveSourceMemoEditor() {
  FakeAppStateStore store;
  store.snapshot.liveSourceMemos = {
      LiveSourceMemo{QStringLiteral("https://live.example/channel.m3u8"),
                     QStringLiteral("主线路")},
  };
  GuiHarness harness(&store);
  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();
  auto *const memoAction =
      requiredChild<QAction>(harness.window, "liveSourceMemoAction");
  QSignalSpy seekSpy(&harness.window, &MainWindow::seekRelativeRequested);
  bool inspectedEditor = false;
  int cursorPositionAfterLeft = -1;
  int cursorPositionAfterRight = -1;

  QTimer::singleShot(0, [&] {
    auto *const dialog =
        qobject_cast<QDialog *>(QApplication::activeModalWidget());
    QVERIFY(dialog != nullptr);
    auto *const table = dialog->findChild<QTableWidget *>(
        QStringLiteral("liveSourceMemoTable"));
    QVERIFY(table != nullptr);
    table->setCurrentCell(0, 0);
    table->editItem(table->item(0, 0));
    QCoreApplication::processEvents();

    auto *const editor =
        qobject_cast<QLineEdit *>(QApplication::focusWidget());
    QVERIFY(editor != nullptr);
    QVERIFY(table->isAncestorOf(editor));
    editor->setCursorPosition(1);
    QTest::keyClick(editor, Qt::Key_Left);
    cursorPositionAfterLeft = editor->cursorPosition();
    QTest::keyClick(editor, Qt::Key_Right);
    cursorPositionAfterRight = editor->cursorPosition();
    inspectedEditor = true;
    dialog->reject();
  });
  memoAction->trigger();

  QVERIFY(inspectedEditor);
  QCOMPARE(cursorPositionAfterLeft, 0);
  QCOMPARE(cursorPositionAfterRight, 1);
  QCOMPARE(seekSpy.count(), 0);
}

void MainWindowTest::managesLiveSourceMemosWithSaveShortcutAndReturnPrompt() {
  FakeAppStateStore store;
  const QString longNote = QStringLiteral(
      "这个备注用于记录主线路、备用线路、节目区域、维护时间和切换说明，默认视图中"
      "必须完整换行显示，不能等到点击编辑后才能阅读。");
  store.snapshot.liveSourceMemos = {
      LiveSourceMemo{QStringLiteral("https://live.example/original.m3u8"),
                     longNote},
  };
  GuiHarness harness(&store);
  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();
  auto *const memoAction =
      requiredChild<QAction>(harness.window, "liveSourceMemoAction");
  auto *const helpMenu = qobject_cast<QMenu *>(memoAction->parent());
  QVERIFY(helpMenu != nullptr);
  QVERIFY(helpMenu->title().contains(QStringLiteral("帮助")));
  bool inspectedDialog = false;
  bool usesDesktopToolStyling = false;
  bool cancelledSavePrompt = false;
  bool styledSavePrompt = false;
  bool acceptedSavePrompt = false;
  bool discardedClosePrompt = false;

  QTimer::singleShot(0, [&] {
    auto *const dialog =
        qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      return;
    }
    auto *const table = dialog->findChild<QTableWidget *>(
        QStringLiteral("liveSourceMemoTable"));
    auto *const addButton = dialog->findChild<QPushButton *>(
        QStringLiteral("liveSourceMemoAddButton"));
    auto *const saveButton = dialog->findChild<QPushButton *>(
        QStringLiteral("liveSourceMemoSaveButton"));
    auto *const returnButton = dialog->findChild<QPushButton *>(
        QStringLiteral("liveSourceMemoReturnButton"));
    auto *const saveShortcut = dialog->findChild<QShortcut *>(
        QStringLiteral("liveSourceMemoSaveShortcut"));
    auto *const notice = dialog->findChild<QLabel *>(
        QStringLiteral("liveSourceMemoNotice"));
    auto *const eyebrow = dialog->findChild<QLabel *>(
        QStringLiteral("liveSourceMemoEyebrow"));
    auto *const title = dialog->findChild<QLabel *>(
        QStringLiteral("liveSourceMemoTitle"));
    auto *const countLabel = dialog->findChild<QLabel *>(
        QStringLiteral("liveSourceMemoCountBadge"));
    if (table == nullptr || addButton == nullptr || saveButton == nullptr ||
        returnButton == nullptr || saveShortcut == nullptr || notice == nullptr ||
        eyebrow == nullptr || title == nullptr || countLabel == nullptr) {
      return;
    }
    QVERIFY(!dialog->windowFlags().testFlag(
        Qt::WindowContextHelpButtonHint));
    QVERIFY(!dialog->styleSheet().isEmpty());
    usesDesktopToolStyling =
        dialog->windowTitle() == QStringLiteral("直播源备忘") &&
        eyebrow->isHidden() &&
        title->text() == QStringLiteral("直播源备忘") &&
        notice->text() ==
            QStringLiteral("仅保存在本机，不会自动载入或播放") &&
        countLabel->maximumHeight() <= 28 &&
        addButton->text() == QStringLiteral("新增") &&
        saveButton->text() == QStringLiteral("保存") &&
        addButton->maximumHeight() <= 38 &&
        saveButton->maximumHeight() <= 38;
    QCOMPARE(table->columnCount(), 2);
    QCOMPARE(table->horizontalHeaderItem(0)->text(),
             QStringLiteral("直播源地址"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("备注"));
    QVERIFY(table->wordWrap());
    QCOMPARE(table->textElideMode(), Qt::ElideNone);
    QCOMPARE(table->verticalHeader()->sectionResizeMode(0),
             QHeaderView::ResizeToContents);
    QCOMPARE(saveShortcut->key(), QKeySequence::Save);
    QCOMPARE(saveShortcut->context(), Qt::WidgetWithChildrenShortcut);
    QCOMPARE(saveShortcut->parent(), dialog);
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 1)->text(), longNote);
    QCoreApplication::processEvents();
    QVERIFY(table->rowHeight(0) >
            table->verticalHeader()->minimumSectionSize());

    addButton->click();
    QCOMPARE(table->rowCount(), 2);
    table->setFocus();
    QCoreApplication::processEvents();
    table->item(1, 0)->setText(
        QStringLiteral("https://live.example/new.m3u8"));
    table->item(1, 1)->setText(QStringLiteral("新增备用"));
    QCoreApplication::processEvents();
    QCOMPARE(store.snapshot.liveSourceMemos.size(), 1);

    QTimer::singleShot(20, [&] {
      auto *const confirmation =
          qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (confirmation == nullptr) {
        return;
      }
      auto *const rejectButton = confirmation->findChild<QPushButton *>(
          QStringLiteral("memoConfirmationRejectButton"));
      auto *const confirmationEyebrow = confirmation->findChild<QLabel *>(
          QStringLiteral("memoConfirmationEyebrow"));
      cancelledSavePrompt =
          confirmation->objectName() ==
              QStringLiteral("liveSourceMemoSaveConfirmation") &&
          rejectButton != nullptr;
      styledSavePrompt =
          confirmationEyebrow != nullptr && confirmationEyebrow->isHidden() &&
          !confirmation->styleSheet().contains(QStringLiteral("qlineargradient"));
      if (rejectButton != nullptr) {
        rejectButton->click();
      } else {
        confirmation->reject();
      }
    });
    saveButton->click();
    QVERIFY(cancelledSavePrompt);
    QCOMPARE(store.snapshot.liveSourceMemos.size(), 1);
    QVERIFY(dialog->isVisible());

    QTimer::singleShot(20, [&] {
      auto *const confirmation =
          qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (confirmation == nullptr) {
        return;
      }
      auto *const acceptButton = confirmation->findChild<QPushButton *>(
          QStringLiteral("memoConfirmationAcceptButton"));
      acceptedSavePrompt =
          confirmation->objectName() ==
              QStringLiteral("liveSourceMemoSaveConfirmation") &&
          acceptButton != nullptr;
      if (acceptButton != nullptr) {
        acceptButton->click();
      } else {
        confirmation->reject();
      }
    });
    QVERIFY(QMetaObject::invokeMethod(saveShortcut, "activated",
                                      Qt::DirectConnection));
    QVERIFY(acceptedSavePrompt);
    QCOMPARE(store.snapshot.liveSourceMemos.size(), 2);
    QCOMPARE(store.snapshot.liveSourceMemos.at(1).note,
             QStringLiteral("新增备用"));
    QVERIFY(dialog->isVisible());

    table->item(1, 1)->setText(QStringLiteral("这次不保存"));
    QTimer::singleShot(20, [&] {
      auto *const confirmation =
          qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (confirmation == nullptr) {
        return;
      }
      auto *const rejectButton = confirmation->findChild<QPushButton *>(
          QStringLiteral("memoConfirmationRejectButton"));
      discardedClosePrompt =
          confirmation->objectName() ==
              QStringLiteral("liveSourceMemoCloseConfirmation") &&
          rejectButton != nullptr;
      if (rejectButton != nullptr) {
        rejectButton->click();
      } else {
        confirmation->reject();
      }
    });
    inspectedDialog = true;
    returnButton->click();
    QVERIFY(discardedClosePrompt);
  });
  memoAction->trigger();

  QVERIFY(inspectedDialog);
  QVERIFY(usesDesktopToolStyling);
  QVERIFY(styledSavePrompt);
  QCOMPARE(store.snapshot.liveSourceMemos.size(), 2);
  QCOMPARE(store.snapshot.liveSourceMemos.at(1).note,
           QStringLiteral("新增备用"));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);
  QCOMPARE(harness.livePlaylistService.loadCount, 0);
}

void MainWindowTest::savesUnsavedLiveSourceMemosWhenWindowCloses() {
  FakeAppStateStore store;
  GuiHarness harness(&store);
  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();
  auto *const memoAction =
      requiredChild<QAction>(harness.window, "liveSourceMemoAction");
  bool closedWithSave = false;
  bool acceptedClosePrompt = false;

  QTimer::singleShot(0, [&] {
    auto *const dialog =
        qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      return;
    }
    auto *const table = dialog->findChild<QTableWidget *>(
        QStringLiteral("liveSourceMemoTable"));
    auto *const addButton = dialog->findChild<QPushButton *>(
        QStringLiteral("liveSourceMemoAddButton"));
    if (table == nullptr || addButton == nullptr) {
      return;
    }
    addButton->click();
    table->setFocus();
    QCoreApplication::processEvents();
    table->item(0, 0)->setText(
        QStringLiteral("rtmp://live.example/close-save"));
    table->item(0, 1)->setText(QStringLiteral("关闭时保存"));
    QCoreApplication::processEvents();
    QTimer::singleShot(20, [&] {
      auto *const confirmation =
          qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (confirmation == nullptr) {
        return;
      }
      auto *const acceptButton = confirmation->findChild<QPushButton *>(
          QStringLiteral("memoConfirmationAcceptButton"));
      acceptedClosePrompt =
          confirmation->objectName() ==
              QStringLiteral("liveSourceMemoCloseConfirmation") &&
          acceptButton != nullptr;
      if (acceptButton != nullptr) {
        acceptButton->click();
      } else {
        confirmation->reject();
      }
    });
    closedWithSave = true;
    dialog->close();
  });
  memoAction->trigger();

  QVERIFY(closedWithSave);
  QVERIFY(acceptedClosePrompt);
  QCOMPARE(store.snapshot.liveSourceMemos.size(), 1);
  QCOMPARE(store.snapshot.liveSourceMemos.front().note,
           QStringLiteral("关闭时保存"));
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open), 0);
  QCOMPARE(harness.livePlaylistService.loadCount, 0);
}

void MainWindowTest::cancelsLivePlaylistLoadingAndIgnoresLateResult() {
  GuiHarness harness;
  auto *const tabs = requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const urlEdit =
      requiredChild<QLineEdit>(harness.window, "livePlaylistUrlEdit");
  auto *const loadButton = requiredChild<QPushButton>(
      harness.window, "livePlaylistLoadButton");
  auto *const statusLabel = requiredChild<QLabel>(
      harness.window, "livePlaylistStatusLabel");
  tabs->setCurrentIndex(1);

  urlEdit->setText(QStringLiteral("https://example.test/old.m3u"));
  QTest::mouseClick(loadButton, Qt::LeftButton);
  LivePlaylistLoadResult oldResult;
  oldResult.library.channels = {
      core::LiveChannel{"保留频道", "", "https://stream.example/old", "",
                        "", ""},
  };
  harness.livePlaylistService.complete(std::move(oldResult));
  QCOMPARE(playlistView->model()->rowCount(), 1);

  urlEdit->setText(QStringLiteral("https://example.test/new.m3u"));
  QTest::mouseClick(loadButton, Qt::LeftButton);
  QCOMPARE(loadButton->text(), QStringLiteral("取消载入"));
  QVERIFY(loadButton->isEnabled());
  QVERIFY(!urlEdit->isEnabled());
  QTest::mouseClick(loadButton, Qt::LeftButton);
  QCOMPARE(harness.livePlaylistService.cancelCount, 1);
  QCOMPARE(loadButton->text(), QStringLiteral("载入清单"));
  QVERIFY(urlEdit->isEnabled());
  QCOMPARE(statusLabel->text(), QStringLiteral("已取消载入直播清单"));

  LivePlaylistLoadResult lateResult;
  lateResult.library.channels = {
      core::LiveChannel{"迟到频道", "", "https://stream.example/late", "",
                        "", ""},
  };
  harness.livePlaylistService.complete(std::move(lateResult));
  harness.livePlaylistService.fail(LivePlaylistLoadError::InvalidFormat);
  QCOMPARE(playlistView->model()->rowCount(), 1);
  QCOMPARE(playlistView->model()->index(0, 0).data(Qt::UserRole).toString(),
           QStringLiteral("保留频道"));
  QCOMPARE(statusLabel->text(), QStringLiteral("已取消载入直播清单"));
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
    harness.engine.emitOpenStarted(latestOpenRequestId(harness));
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
    harness.engine.emitOpenStarted(latestOpenRequestId(harness));
    harness.engine.emitStateChanged(core::PlaybackState::Opening);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play),
                 playsBefore + 1);
    harness.engine.emitStateChanged(core::PlaybackState::Playing);
    QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
    QCOMPARE(harness.engine.commands().back().position, 0ms);
  }
}

void MainWindowTest::
    replaysSequentialLastVideoOnFreshSurfaceAfterNaturalEnd() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  harness.presenter.addLocalFiles({QStringLiteral("C:/list/first.mp3"),
                                   QStringLiteral("C:/list/last.mp4")});
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);

  auto* const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  const QModelIndex lastItem = playlistView->model()->index(1, 0);
  QVERIFY(QMetaObject::invokeMethod(playlistView, "doubleClicked",
                                    Qt::DirectConnection,
                                    Q_ARG(QModelIndex, lastItem)));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  const auto playingCommands = harness.engine.commands();
  const auto playingOpen = std::find_if(
      playingCommands.rbegin(), playingCommands.rend(), [](const auto& command) {
        return command.kind == test::FakeEngineCommandKind::Open;
      });
  QVERIFY(playingOpen != playingCommands.rend());
  void* const firstVideoSurface = playingOpen->nativeHandle;
  QVERIFY(firstVideoSurface != nullptr);

  harness.engine.emitEndReached();
  QTRY_COMPARE(statusText(harness), QStringLiteral("播放结束"));
  QCOMPARE(playlistView->currentIndex().row(), 1);
  const int opensBeforeReplay =
      commandCount(harness, test::FakeEngineCommandKind::Open);
  const int playsBeforeReplay =
      commandCount(harness, test::FakeEngineCommandKind::Play);

  QTest::mouseClick(
      requiredChild<QToolButton>(harness.window, "playPauseButton"),
      Qt::LeftButton);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
               opensBeforeReplay + 1);
  const auto replayCommands = harness.engine.commands();
  const auto replayOpen = std::find_if(
      replayCommands.rbegin(), replayCommands.rend(), [](const auto& command) {
        return command.kind == test::FakeEngineCommandKind::Open;
      });
  QVERIFY(replayOpen != replayCommands.rend());
  QVERIFY(replayOpen->nativeHandle != nullptr);
  QVERIFY(replayOpen->nativeHandle != firstVideoSurface);

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Play),
               playsBeforeReplay + 1);
  harness.engine.emitStateChanged(core::PlaybackState::Playing);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::Seek), 1);
  QCOMPARE(harness.engine.commands().back().position, 0ms);
  QVERIFY(requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget")
              ->isVideoActive());
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

  harness.engine.emitOpenStarted(latestOpenRequestId(harness));
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
    harness.engine.emitOpenStarted(latestOpenRequestId(harness));
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
  core::OpenRequestId receivedOpenRequestId = 0;
  void* releasedVideoSurface = nullptr;

  connect(
      &bridge, &EngineEventBridge::openStarted, &receiver,
      [&receivedCount, &receivedOpenRequestId](
          const core::OpenRequestId requestId) {
        ++receivedCount;
        receivedOpenRequestId = requestId;
      },
      Qt::QueuedConnection);

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
  connect(
      &bridge, &EngineEventBridge::videoSurfaceReleased, &receiver,
      [&receivedCount, &releasedVideoSurface](void* const nativeHandle) {
        ++receivedCount;
        releasedVideoSurface = nativeHandle;
      },
      Qt::QueuedConnection);

  std::thread worker([&bridge] {
    core::AudioWaveform waveform;
    waveform.samples[12] = 0.6F;
    waveform.intensity = 0.7F;
    bridge.onOpenStarted(42);
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
    bridge.onVideoSurfaceReleased(reinterpret_cast<void*>(0x1234));
  });
  worker.join();

  QTRY_COMPARE(receivedCount, 9);
  QCOMPARE(receivedOpenRequestId, core::OpenRequestId{42});
  QCOMPARE(releasedVideoSurface, reinterpret_cast<void*>(0x1234));
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

void MainWindowTest::routesExplicitMuteShortcutsAndLeavesCtrlF5Unused() {
  GuiHarness harness;
  openAndReachPlaying(harness);
  harness.window.show();
  harness.window.activateWindow();
  QCoreApplication::processEvents();
  auto *const volumeButton =
      requiredChild<QToolButton>(harness.window, "volumeButton");
  const int initialMuteCommands =
      commandCount(harness, test::FakeEngineCommandKind::SetMuted);

  QTest::keyClick(&harness.window, Qt::Key_Down, Qt::ControlModifier);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted),
               initialMuteCommands + 1);
  QVERIFY(harness.engine.commands().back().flag);
  QCOMPARE(volumeButton->accessibleName(), QStringLiteral("已静音"));
  QTest::keyClick(&harness.window, Qt::Key_Down, Qt::ControlModifier);
  QCoreApplication::processEvents();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted),
           initialMuteCommands + 1);

  QTest::keyClick(&harness.window, Qt::Key_Up, Qt::ControlModifier);
  QTRY_COMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted),
               initialMuteCommands + 2);
  QVERIFY(!harness.engine.commands().back().flag);
  QVERIFY(volumeButton->accessibleName().contains(QStringLiteral("音量")));
  QTest::keyClick(&harness.window, Qt::Key_Up, Qt::ControlModifier);
  QCoreApplication::processEvents();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted),
           initialMuteCommands + 2);

  const int opensBeforeCtrlF5 =
      commandCount(harness, test::FakeEngineCommandKind::Open);
  QTest::keyClick(&harness.window, Qt::Key_F5, Qt::ControlModifier);
  QCoreApplication::processEvents();
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::SetMuted),
           initialMuteCommands + 2);
  QCOMPARE(commandCount(harness, test::FakeEngineCommandKind::Open),
           opensBeforeCtrlF5);
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
  harness.engine.emitOpenStarted(latestOpenRequestId(harness));
  QTRY_VERIFY(timeoutTimer->isActive());
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
      std::pair{core::PlaybackErrorKind::EngineBusy, "engine_busy"},
      std::pair{core::PlaybackErrorKind::Unknown, "unknown"},
  };

  for (const auto &[kind, expected] : cases) {
    GuiHarness harness;
    harness.engine.emitError(core::PlaybackError{kind, "detail", "用户提示"});
    if (kind == core::PlaybackErrorKind::EngineBusy) {
      QTRY_COMPARE(statusText(harness),
                   QStringLiteral("旧直播正在退出，请稍候后重试"));
    } else {
      QTRY_COMPARE(statusText(harness), QStringLiteral("播放失败"));
    }
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
  QCOMPARE(presentationModeKey(videoOutput->presentationMode()),
           QStringLiteral("video"));
  QVERIFY(!videoOutput->isVideoActive());
  QCOMPARE(videoOutput->placeholderText(),
           QStringLiteral("正在准备视频画面..."));

  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QTRY_VERIFY(videoOutput->isVideoActive());
  QCOMPARE(videoOutput->placeholderText(), QString());
  QVERIFY(videoOutput->testAttribute(Qt::WA_NoSystemBackground));
  QVERIFY(!videoOutput->testAttribute(Qt::WA_OpaquePaintEvent));

  harness.presenter.openLocalFile(QStringLiteral("C:/audio/sample.flac"));
  QCOMPARE(presentationModeKey(videoOutput->presentationMode()),
           QStringLiteral("audio"));
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

  core::AudioWaveform renderedWaveform;
  renderedWaveform.samples.fill(0.85F);
  renderedWaveform.intensity = 1.0F;
  for (int frame = 0; frame < 12; ++frame) {
    harness.engine.emitAudioWaveformChanged(renderedWaveform);
  }
  QCoreApplication::processEvents();
  const QImage renderedWaveformImage = videoOutput->grab().toImage();
  int upperSignalPixels = 0;
  int lowerSignalPixels = 0;
  for (int y = 12; y < renderedWaveformImage.height() - 12; ++y) {
    for (int x = 12; x < renderedWaveformImage.width() - 12; ++x) {
      const QColor color = renderedWaveformImage.pixelColor(x, y);
      const bool isWaveformSignal =
          color.blue() >= 130 && color.blue() >= color.green() + 35 &&
          color.green() >= color.red() + 35;
      if (!isWaveformSignal) {
        continue;
      }
      if (y < renderedWaveformImage.height() / 2) {
        ++upperSignalPixels;
      } else {
        ++lowerSignalPixels;
      }
    }
  }
  QVERIFY(upperSignalPixels > 30);
  QVERIFY2(upperSignalPixels * 5 > lowerSignalPixels * 6,
           qPrintable(QStringLiteral("upper=%1 lower=%2")
                          .arg(upperSignalPixels)
                          .arg(lowerSignalPixels)));

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

void MainWindowTest::scalesPlaylistTypographyAcrossWindowBreakpoints() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const centralSurface =
      requiredChild<QWidget>(harness.window, "centralSurface");
  auto *const playlistPanel =
      requiredChild<QWidget>(harness.window, "playlistPanel");
  auto *const playlistTitle =
      requiredChild<QLabel>(harness.window, "playlistTitleLabel");
  auto *const playlistTabs =
      requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  auto *const playlistView =
      requiredChild<QListView>(harness.window, "playlistView");
  auto *const livePlaylistSearch =
      requiredChild<QLineEdit>(harness.window, "livePlaylistSearchEdit");

  const std::array<QSize, 4> sizes{harness.window.minimumSize(),
                                  QSize(960, 720), QSize(1200, 800),
                                  QSize(1600, 900)};
  std::array<int, 4> panelWidths{};
  std::array<int, 4> titleFontSizes{};
  std::array<int, 4> listFontHeights{};
  std::array<int, 4> searchFontHeights{};
  for (std::size_t index = 0; index < sizes.size(); ++index) {
    const QSize size = sizes.at(index);
    harness.window.resize(size);
    QTRY_COMPARE(harness.window.size(), size);

    playlistTabs->setCurrentIndex(0);
    QTRY_COMPARE(centralSurface->property("themeMode").toString(),
                 QStringLiteral("video"));
    const QSize localPanelSize = playlistPanel->size();
    const int localTitleHeight = playlistTitle->fontMetrics().height();
    const int localTabHeight = playlistTabs->fontMetrics().height();
    const int localListHeight = playlistView->fontMetrics().height();

    playlistTabs->setCurrentIndex(1);
    QTRY_COMPARE(centralSurface->property("themeMode").toString(),
                 QStringLiteral("live"));
    QCOMPARE(playlistPanel->size(), localPanelSize);
    QCOMPARE(playlistTitle->fontMetrics().height(), localTitleHeight);
    QCOMPARE(playlistTabs->fontMetrics().height(), localTabHeight);
    QCOMPARE(playlistView->fontMetrics().height(), localListHeight);
    panelWidths.at(index) = playlistPanel->width();
    titleFontSizes.at(index) = playlistTitle->font().pixelSize();
    listFontHeights.at(index) = playlistView->fontMetrics().height();
    searchFontHeights.at(index) =
        livePlaylistSearch->fontMetrics().height();
  }

  for (std::size_t index = 1; index < sizes.size(); ++index) {
    QVERIFY(panelWidths.at(index - 1) <= panelWidths.at(index));
    QVERIFY(titleFontSizes.at(index - 1) < titleFontSizes.at(index));
    QVERIFY(listFontHeights.at(index - 1) < listFontHeights.at(index));
    QVERIFY(searchFontHeights.at(index - 1) < searchFontHeights.at(index));
  }
  QVERIFY(panelWidths.front() < panelWidths.back());
}

void MainWindowTest::keepsPresentationModesInsideResponsiveBounds() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const centralSurface =
      requiredChild<QWidget>(harness.window, "centralSurface");
  auto *const headerPanel =
      requiredChild<QWidget>(harness.window, "headerPanel");
  auto *const videoOutput =
      requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget");
  auto *const playlistPanel =
      requiredChild<QWidget>(harness.window, "playlistPanel");
  auto *const mediaCard = requiredChild<QWidget>(harness.window, "mediaCard");
  auto *const transportPanel =
      requiredChild<QWidget>(harness.window, "transportPanel");
  auto *const progressSlider =
      requiredChild<QSlider>(harness.window, "progressSlider");
  auto *const playlistTabs =
      requiredChild<QTabBar>(harness.window, "playlistKindTabs");
  auto *const livePlaylistLoadButton =
      requiredChild<QPushButton>(harness.window, "livePlaylistLoadButton");
  auto *const livePlaylistLocateButton =
      requiredChild<QPushButton>(harness.window, "livePlaylistLocateButton");
  const std::array<QWidget *, 10> timelineControls{
      requiredChild<QToolButton>(harness.window, "previousButton"),
      requiredChild<QToolButton>(harness.window, "playPauseButton"),
      requiredChild<QToolButton>(harness.window, "nextButton"),
      requiredChild<QToolButton>(harness.window, "stopButton"),
      requiredChild<QToolButton>(harness.window, "volumeButton"),
      requiredChild<QToolButton>(harness.window, "lyricsButton"),
      requiredChild<QToolButton>(harness.window, "playbackRateButton"),
      requiredChild<QToolButton>(harness.window, "keyboardSeekStepButton"),
      requiredChild<QToolButton>(harness.window, "playbackModeButton"),
      requiredChild<QToolButton>(harness.window, "fullScreenButton"),
  };
  const QList<QWidget *> responsiveWidgets{
      centralSurface, headerPanel, videoOutput, playlistPanel,
      mediaCard,      transportPanel, progressSlider,
  };

  const QSize initialWindowSize = harness.window.size();
  playlistTabs->setCurrentIndex(1);
  QTRY_COMPARE(centralSurface->property("themeMode").toString(),
               QStringLiteral("live"));
  QCOMPARE(harness.window.size(), initialWindowSize);
  playlistTabs->setCurrentIndex(0);
  QTRY_COMPARE(centralSurface->property("themeMode").toString(),
               QStringLiteral("video"));
  QCOMPARE(harness.window.size(), initialWindowSize);

  const auto verifyVisibleBounds = [&]() {
    QCoreApplication::processEvents();
    QVERIFY(progressSlider->width() >= 100);
    for (auto *const widget : responsiveWidgets) {
      if (widget->isVisible()) {
        QVERIFY(harness.window.rect().contains(
            geometryInsideWindow(*widget, harness.window)));
      }
    }
    for (auto *const control : timelineControls) {
      QVERIFY(control->isVisible());
      QVERIFY(harness.window.rect().contains(
          geometryInsideWindow(*control, harness.window)));
    }
  };

  const std::array<QSize, 3> sizes{
      QSize(960, 720), QSize(1200, 800), harness.window.minimumSize()};
  for (const QSize &size : sizes) {
    harness.window.resize(size);
    QTRY_COMPARE(harness.window.size(), size);

    playlistTabs->setCurrentIndex(0);
    harness.presenter.openLocalFile(QStringLiteral("C:/video/layout.mp4"));
    QTRY_COMPARE(centralSurface->property("themeMode").toString(),
                 QStringLiteral("video"));
    QCOMPARE(harness.window.size(), size);
    verifyVisibleBounds();

    harness.presenter.openLocalFile(QStringLiteral("C:/audio/layout.mp3"));
    QTRY_COMPARE(centralSurface->property("themeMode").toString(),
                 QStringLiteral("audio"));
    QCOMPARE(harness.window.size(), size);
    verifyVisibleBounds();

    playlistTabs->setCurrentIndex(1);
    QTRY_COMPARE(centralSurface->property("themeMode").toString(),
                 QStringLiteral("live"));
    QCOMPARE(harness.window.size(), size);
    verifyVisibleBounds();
    for (int index = 0; index < playlistTabs->count(); ++index) {
      const int textWidth = playlistTabs->fontMetrics().horizontalAdvance(
          playlistTabs->tabText(index));
      QVERIFY(playlistTabs->tabRect(index).width() >= textWidth + 8);
    }
    const std::array<QPushButton *, 2> liveButtons{
        livePlaylistLoadButton, livePlaylistLocateButton};
    for (auto *const button : liveButtons) {
      const int textWidth =
          button->fontMetrics().horizontalAdvance(button->text());
      QVERIFY2(button->width() >= textWidth + 10,
               qPrintable(QStringLiteral("窗口 %1x%2，按钮“%3”宽度 %4，文本宽度 %5")
                              .arg(size.width())
                              .arg(size.height())
                              .arg(button->text())
                              .arg(button->width())
                              .arg(textWidth)));
    }
  }
}

void MainWindowTest::resizesVideoSurfaceAndTogglesFullScreen() {
  GuiHarness harness;
  harness.window.show();
  QCoreApplication::processEvents();
  auto *const videoOutput =
      requiredChild<VideoOutputWidget>(harness.window, "videoOutputWidget");
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
      playbackModeButton, previousButton,     playPauseButton, nextButton,
      stopButton,         volumeButton,       lyricsButton,    playbackRateButton,
      seekStepButton,     fullScreenButton,
  };
  QTRY_VERIFY(harness.window.rect().contains(
      geometryInsideWindow(*openButton, harness.window)));
  QVERIFY(harness.window.rect().contains(
      geometryInsideWindow(*progressSlider, harness.window)));
  const int controlsCenter =
      verticalCenterInsideWindow(*timelineControls.front(), harness.window);
  QVERIFY(geometryInsideWindow(*progressSlider, harness.window).bottom() <
          geometryInsideWindow(*timelineControls.front(), harness.window).top());
  for (std::size_t index = 0; index < timelineControls.size(); ++index) {
    auto *const control = timelineControls.at(index);
    QVERIFY(harness.window.rect().contains(
        geometryInsideWindow(*control, harness.window)));
    QVERIFY(qAbs(verticalCenterInsideWindow(*control, harness.window) -
                 controlsCenter) <= 2);
    if (index + 1 < timelineControls.size()) {
      QVERIFY(
          geometryInsideWindow(*control, harness.window).right() <
          geometryInsideWindow(*timelineControls.at(index + 1), harness.window)
              .left());
    }
  }
  QVERIFY(harness.window.rect().contains(
      geometryInsideWindow(*playlistView, harness.window)));

  harness.window.resize(960, 720);
  QTRY_COMPARE(harness.window.size(), QSize(960, 720));
  const QSize initialSize = videoOutput->size();
  harness.window.resize(1200, 800);
  QTRY_VERIFY(videoOutput->size().width() > initialSize.width());
  QTRY_VERIFY(videoOutput->size().height() > initialSize.height());
  QTRY_VERIFY(harness.window.rect().contains(
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

void MainWindowTest::togglesFullScreenWithF11() {
  GuiHarness harness;
  harness.window.show();
  harness.window.activateWindow();
  harness.presenter.openLocalFile(QStringLiteral("C:/video/f11.mp4"));
  harness.engine.emitStateChanged(core::PlaybackState::Opening);
  QCoreApplication::processEvents();
  QVERIFY(requiredChild<QAction>(harness.window, "fullScreenAction")
              ->isEnabled());

  QTest::keyClick(&harness.window, Qt::Key_F11);
  QTRY_VERIFY(harness.window.isFullScreen());
  QTest::keyClick(&harness.window, Qt::Key_F11);
  QTRY_VERIFY(!harness.window.isFullScreen());

  QTest::keyClick(&harness.window, Qt::Key_F11);
  QTRY_VERIFY(harness.window.isFullScreen());
  QTest::keyClick(&harness.window, Qt::Key_Escape);
  QTRY_VERIFY(!harness.window.isFullScreen());
}

void MainWindowTest::showsSupportedShortcutsFromHelpMenu() {
  GuiHarness harness;
  auto *const helpAction =
      requiredChild<QAction>(harness.window, "shortcutHelpAction");
  bool inspectedDialog = false;
  bool usesDesktopToolStyling = false;
  QTimer::singleShot(0, [&] {
    auto *const dialog =
        qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      return;
    }
    QVERIFY(!dialog->windowFlags().testFlag(
        Qt::WindowContextHelpButtonHint));
    QVERIFY(!dialog->styleSheet().isEmpty());
    QVERIFY(dialog->findChild<QFrame *>(
                QStringLiteral("shortcutHelpHeader")) != nullptr);
    auto *const eyebrow = dialog->findChild<QLabel *>(
        QStringLiteral("shortcutHelpEyebrow"));
    auto *const title = dialog->findChild<QLabel *>(
        QStringLiteral("shortcutHelpTitle"));
    auto *const countLabel = dialog->findChild<QLabel *>(
        QStringLiteral("shortcutHelpCountBadge"));
    auto *const table = dialog->findChild<QTableWidget *>(
        QStringLiteral("shortcutHelpTable"));
    auto *const buttons = dialog->findChild<QDialogButtonBox *>(
        QStringLiteral("shortcutHelpButtons"));
    if (eyebrow == nullptr || title == nullptr || countLabel == nullptr ||
        table == nullptr || buttons == nullptr) {
      return;
    }
    usesDesktopToolStyling =
        eyebrow->isHidden() && title->text() == QStringLiteral("快捷键") &&
        countLabel->maximumHeight() <= 28;
    QStringList shortcuts;
    for (int row = 0; row < table->rowCount(); ++row) {
      shortcuts.append(table->item(row, 0)->text());
    }
    const auto descriptionFor = [&table](const QString& shortcut) {
      for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->text() == shortcut) {
          return table->item(row, 1)->text();
        }
      }
      return QString{};
    };
    inspectedDialog =
        shortcuts.contains(QStringLiteral("F5")) &&
        shortcuts.contains(QStringLiteral("F11")) &&
        shortcuts.contains(QStringLiteral("Ctrl+下键")) &&
        shortcuts.contains(QStringLiteral("Ctrl+上键")) &&
        shortcuts.contains(QStringLiteral("Ctrl+M")) &&
        shortcuts.contains(QStringLiteral("Ctrl+S")) &&
        !shortcuts.contains(QStringLiteral("Ctrl+F5")) &&
        descriptionFor(QStringLiteral("Ctrl+L")).contains(QStringLiteral("聚焦并全选")) &&
        descriptionFor(QStringLiteral("Ctrl+L")).contains(QStringLiteral("打开网络地址")) &&
        descriptionFor(QStringLiteral("Alt+左键")) == QStringLiteral("网页后退") &&
        descriptionFor(QStringLiteral("Alt+右键")) == QStringLiteral("网页前进") &&
        descriptionFor(QStringLiteral("Ctrl+R")).contains(QStringLiteral("刷新当前网页")) &&
        descriptionFor(QStringLiteral("F5")).contains(QStringLiteral("刷新当前直播")) &&
        descriptionFor(QStringLiteral("Esc")).contains(QStringLiteral("优先退出网页全屏"));
    auto *const okButton = buttons->button(QDialogButtonBox::Ok);
    QCOMPARE(okButton->objectName(), QStringLiteral("shortcutHelpOkButton"));
    QCOMPARE(okButton->text(), QStringLiteral("确定"));
    QVERIFY(okButton->isDefault());
    usesDesktopToolStyling =
        usesDesktopToolStyling && okButton->minimumHeight() >= 34 &&
        okButton->minimumWidth() >= 96 && okButton->maximumHeight() <= 38;
    QVERIFY(!okButton->styleSheet().isEmpty());
    QCoreApplication::processEvents();
    const QImage renderedButton = okButton->grab().toImage();
    QVERIFY(!renderedButton.isNull());
    const QColor renderedBackground = renderedButton.pixelColor(
        12, renderedButton.height() / 2);
    QVERIFY(renderedBackground.blue() > renderedBackground.red() + 80);
    QVERIFY(renderedBackground.blue() > renderedBackground.green() + 30);
    okButton->click();
  });
  helpAction->trigger();
  QVERIFY(inspectedDialog);
  QVERIFY(usesDesktopToolStyling);
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

void MainWindowTest::confirmsBeforeClosingWithActiveWebDownloads() {
  GuiHarness harness(nullptr, true);
  harness.window.show();
  QCoreApplication::processEvents();
  harness.browserBackend.emitReady(1);
  harness.browserBackend.emitTabDownloadRequested(
      1, 501, QStringLiteral("https://download.example"),
      QStringLiteral("archive.bin"), 1024);
  harness.browserBackend.emitTabDownloadUpdated(
      1, 501, BrowserDownloadState::RetryableFailure, 512, 1024);

  harness.window.close();
  QCoreApplication::processEvents();
  QVERIFY(harness.window.isVisible());
  auto* const dialog = requiredChild<QDialog>(
      harness.window, "browserActiveDownloadExitDialog");
  QVERIFY(dialog->isVisible());
  QVERIFY(requiredChild<QLabel>(harness.window, "browserActiveDownloadExitLabel")
              ->text()
              .contains(QStringLiteral("1 个网页下载任务")));
  QCOMPARE(harness.browserBackend.count(
               test::FakeBrowserCommandKind::Shutdown),
           0);

  QTest::mouseClick(
      requiredChild<QPushButton>(harness.window,
                                 "browserActiveDownloadReturnButton"),
      Qt::LeftButton);
  QVERIFY(harness.window.isVisible());
  QVERIFY(!dialog->isVisible());

  harness.window.close();
  QCoreApplication::processEvents();
  QTest::mouseClick(
      requiredChild<QPushButton>(harness.window,
                                 "browserActiveDownloadExitButton"),
      Qt::LeftButton);
  QTRY_VERIFY(!harness.window.isVisible());
  QCOMPARE(harness.browserBackend.count(
               test::FakeBrowserCommandKind::CancelDownload),
           1);
  QCOMPARE(harness.browserBackend.count(
               test::FakeBrowserCommandKind::Shutdown),
           1);
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
