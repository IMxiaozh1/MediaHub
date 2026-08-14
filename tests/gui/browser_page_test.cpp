#include <QTest>

#include <QAction>
#include <QAbstractItemModel>
#include <QBuffer>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTabBar>

#include <limits>

#include "browser_download_widget.h"
#include "browser_download_center.h"
#include "browser_data_store.h"
#include "main_window.h"
#include "browser_page.h"
#include "browser_permission_dialog.h"
#include "browser_navigation_policy.h"
#include "browser_session_store.h"
#include "browser_startup_settings.h"
#include "browser_tab_group_dialog.h"
#include "fakes/fake_browser_backend.h"
#include "ui_theme.h"

namespace mediahub::gui {
namespace {

class MemoryBrowserDataStore final : public BrowserDataStore {
 public:
    QVector<BrowserHistoryEntry> loadHistory() override {
        ++historyLoadCount;
        return history;
    }

    void saveHistory(const QVector<BrowserHistoryEntry>& value) override {
        history = value;
        ++saveCount;
    }

    QVector<BrowserFavoriteEntry> loadFavorites() override {
        ++favoriteLoadCount;
        return favorites;
    }

    void saveFavorites(
        const QVector<BrowserFavoriteEntry>& value) override {
        favorites = value;
        ++favoriteSaveCount;
    }

    QVector<BrowserHistoryEntry> history;
    QVector<BrowserFavoriteEntry> favorites;
    int saveCount{0};
    int favoriteSaveCount{0};
    int historyLoadCount{0};
    int favoriteLoadCount{0};
};

class MemoryBrowserSessionStore final : public BrowserSessionStore {
 public:
    std::optional<BrowserSessionState> load() override { return loaded; }

    bool save(const BrowserSessionState& value) override {
        saved = value;
        ++saveCount;
        return canSave;
    }

    bool clear() override {
        loaded = BrowserSessionState{};
        ++clearCount;
        return true;
    }

    std::optional<BrowserSessionState> loaded{BrowserSessionState{}};
    BrowserSessionState saved;
    int saveCount{0};
    int clearCount{0};
    bool canSave{true};
};

class MemoryBrowserStartupSettingsStore final
    : public BrowserStartupSettingsStore {
 public:
    BrowserStartupSettings load() override { return settings; }
    void save(const BrowserStartupSettings& value) override { settings = value; }
    void clear() override {
        settings = BrowserStartupSettings{};
        ++clearCount;
    }

    BrowserStartupSettings settings;
    int clearCount{0};
};

QByteArray makeBrowserFavicon(const QColor& color) {
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return bytes;
}

}  // namespace

class BrowserPageTest final : public QObject {
    Q_OBJECT

 private slots:
    void normalizesOnlySupportedTopLevelAddresses();
    void routesNavigationAndIgnoresLateGeneration();
    void keepsAddressEditableAndSelectsOldAddressOnFocus();
    void reusesCurrentPageAndRecordsSuccessfulNavigation();
    void persistsAndNormalizesBrowserHistory();
    void persistsAndNormalizesBrowserFavorites();
    void limitsPersistedBrowserRecords();
    void cachesBrowserDataAndSkipsUnchangedListRebuilds();
    void coalescesHistoryAndFavoriteSearchInput();
    void opensHistoryInCurrentOrNewTabFromMouseGesture();
    void opensFavoritesInCurrentOrNewTabFromMouseGesture();
    void addsEditsDeduplicatesAndRemovesFavorites();
    void searchesDeletesAndClearsHistoryWithConfirmation();
    void searchesEditsDeletesAndReordersFavorites();
    void importsAndExportsFavoritesWithoutPartialWrites();
    void opensBingOnceAndProvidesActiveTabShortcuts();
    void findsWithinCurrentTabAndStopsOnContextChanges();
    void tabContextMenuTargetsStableTab();
    void searchesOpenTabsByTitleAndDomain();
    void skipsHiddenTabSearchAndGroupListRebuilds();
    void pinsTabsAndProtectsBulkClose();
    void protectsAudibleAndDownloadingTabsFromBulkClose();
    void enforcesConfiguredMaximumTabCount();
    void ctrlWConfirmsBeforeClosingPinnedTab();
    void closesLastTabToBingAndRestoresOriginalState();
    void restoresPinnedStateAfterConfirmedClose();
    void recoversOnlyTheFailedTabAfterUserConfirmation();
    void failedRecoveryKeepsRecoverableFailureVisible();
    void throttlesRepeatedTabRecoveryWithoutPermanentLockout();
    void browserProcessExitRequiresApplicationRestart();
    void disablesUnsafeTabSleepWithoutSuspendingWebMedia();
    void groupsTabsWithoutSuspendingThemAndRestoresGroupSession();
    void convertsNewWindowRequestsToTabsAndKeepsBlankTab();
    void switchesTabsWithoutReloadAndKeepsIndependentState();
    void closingBackgroundTabKeepsCurrentPageRequests();
    void requiresConfirmationBeforeClearingBrowsingData();
    void cachesFaviconsByOriginAndClearsThemWithBrowsingData();
    void clearCompletionShowsBlankPageOnlyForCurrentGeneration();
    void clearingProfileKeepsBrowserRecordsAndOneRealBlankTab();
    void routesNavigationToolbarCommands();
    void routesOriginAwarePermissionChoicesAndRejectsUnsafeRequests();
    void screenCaptureAllowsOnlyCurrentRequest();
    void replacedPermissionIgnoresLateUiSignals();
    void confirmsExternalProtocolsAndSessionCertificateExceptions();
    void validatesDownloadDestinationAndTracksOneTask();
    void rejectsReservedDownloadNamesWithMultipleSuffixes();
    void downloadTerminalStatesIgnoreStaleRequests();
    void pendingDownloadCancelWaitsForBackendTerminalState();
    void cancelFailureAllowsRetryWithoutEndingDownload();
    void navigationRejectsUnansweredSensitiveRequests();
    void navigationKeepsStartedDownload();
    void managesConcurrentDownloadsIndependently();
    void routesInterruptedDownloadRetryToBackend();
    void hidingPageKeepsConcurrentDownloadProgress();
    void webFullScreenDefersConcurrentDownloadCenterPresentation();
    void shutdownCancelsEveryConcurrentDownload();
    void deactivatesAndActivatesBrowserInSafeOrder();
    void tracksIndependentTabAudioAndMuteState();
    void updatesOnlyChangedAudioRowAndSkipsHiddenRebuilds();
    void audioCenterTargetsStableTabAfterManualReorder();
    void escapeExitsWebFullScreenFirst();
    void mainWindowPrioritizesWebFullScreenAndRestoresChrome();
    void shutdownDetachesListenerBeforeBackend();
    void detachesListenerAndShutsDownOnDestruction();
    void appliesResponsiveSizeAcrossToolbarBreakpoints();
    void routesBrowserHistoryShortcuts();
    void routesWebShortcutsWithoutNativeConflicts();
    void routesNativeAcceleratorsThroughCurrentGeneration();
    void reopensClosedTabsWithoutLosingFailedRestore();
    void restoresStartupSessionAndSavesItBeforeShutdown();
    void periodicallyCheckpointsBrowserSession();
    void restoresCurrentPinnedTabByStableIdentity();
    void restoresMixedPinnedOrderByStableIdentity();
    void opensConfiguredStartupPagesAndHomepage();
};

void BrowserPageTest::normalizesOnlySupportedTopLevelAddresses() {
    QCOMPARE(normalizeBrowserAddress(QStringLiteral(" example.com/live ")).url,
             QStringLiteral("https://example.com/live"));
    QCOMPARE(normalizeBrowserAddress(QStringLiteral("http://127.0.0.1:8080/a?q=1")).kind,
             BrowserAddressKind::Web);
    QCOMPARE(normalizeBrowserAddress(QStringLiteral("javascript:alert(1)")).kind,
             BrowserAddressKind::Blocked);
    QCOMPARE(normalizeBrowserAddress(QStringLiteral("data:text/plain,x")).kind,
             BrowserAddressKind::Blocked);
    QCOMPARE(normalizeBrowserAddress(QStringLiteral("mailto:user@example.com")).kind,
             BrowserAddressKind::ExternalProtocol);
}

void BrowserPageTest::routesNavigationAndIgnoresLateGeneration() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();

    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Initialize), 1);
    backend.emitReady(1);

    auto* addressEdit =
        page.findChild<QLineEdit*>(QStringLiteral("browserAddressEdit"));
    QVERIFY(addressEdit != nullptr);
    addressEdit->setText(QStringLiteral("example.com"));
    QTest::keyClick(addressEdit, Qt::Key_Return);

    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text, QStringLiteral("https://example.com"));
    QCOMPARE(backend.lastCommand().generation, std::uint64_t{2});

    backend.emitNavigationCompleted(1, QStringLiteral("https://late.invalid"));
    QCOMPARE(addressEdit->text(), QStringLiteral("example.com"));

    backend.emitNavigationCompleted(
        2, QStringLiteral("https://example.com/welcome"), QStringLiteral("Welcome"), true,
        false);
    QCOMPARE(addressEdit->text(), QStringLiteral("https://example.com/welcome"));
    QCOMPARE(page.findChild<QLabel*>(QStringLiteral("browserTitleLabel"))->text(),
             QStringLiteral("Welcome"));
    QVERIFY(page.findChild<QToolButton*>(QStringLiteral("browserBackButton"))->isEnabled());
    QVERIFY(!page.findChild<QToolButton*>(QStringLiteral("browserForwardButton"))->isEnabled());
}

void BrowserPageTest::keepsAddressEditableAndSelectsOldAddressOnFocus() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://example.com/old"), QStringLiteral("Old"));

    auto* const address =
        page.findChild<QLineEdit*>(QStringLiteral("browserAddressEdit"));
    QVERIFY(address != nullptr);
    QVERIFY(!address->isReadOnly());
    address->clearFocus();
    QTest::mouseClick(address, Qt::LeftButton);
    QCOMPARE(address->selectedText(), address->text());

    QTest::keyClicks(address, QStringLiteral("example.org/new"));
    QCOMPARE(address->text(), QStringLiteral("example.org/new"));
    QTest::keyClick(address, Qt::Key_Return);
    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text, QStringLiteral("https://example.org/new"));
}

void BrowserPageTest::reusesCurrentPageAndRecordsSuccessfulNavigation() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    const int initialNavigateCount =
        backend.count(test::FakeBrowserCommandKind::Navigate);

    auto* const address =
        page.findChild<QLineEdit*>(QStringLiteral("browserAddressEdit"));
    QVERIFY(address != nullptr);
    address->setText(QStringLiteral("example.com/one"));
    QTest::keyClick(address, Qt::Key_Return);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Navigate),
             initialNavigateCount + 1);
    backend.emitNavigationCompleted(
        2, QStringLiteral("https://example.com/one"), QStringLiteral("First"));
    const qint64 firstVisit = dataStore.history.constFirst().visitedAtMilliseconds;
    backend.emitDocumentStateChanged(
        2, QStringLiteral("https://example.com/one"), QStringLiteral("Updated"));
    QCOMPARE(dataStore.saveCount, 2);
    QCOMPARE(dataStore.history.size(), 1);
    QCOMPARE(dataStore.history.constFirst().title, QStringLiteral("Updated"));
    QCOMPARE(dataStore.history.constFirst().visitedAtMilliseconds, firstVisit);

    address->setText(QStringLiteral("https://example.com/two"));
    QTest::keyClick(address, Qt::Key_Return);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Navigate),
             initialNavigateCount + 2);
    backend.emitNavigationCompleted(
        3, QStringLiteral("https://example.com/two"), QStringLiteral("Second"));

    QCOMPARE(dataStore.saveCount, 3);
    QCOMPARE(dataStore.history.size(), 2);
    QCOMPARE(dataStore.history.at(0).url,
             QStringLiteral("https://example.com/two"));
    QCOMPARE(dataStore.history.at(1).url,
             QStringLiteral("https://example.com/one"));
    QCOMPARE(dataStore.history.at(1).title, QStringLiteral("Updated"));
    QCOMPARE(dataStore.history.at(1).visitedAtMilliseconds, firstVisit);

    address->setText(QStringLiteral("https://example.com/stopped"));
    QTest::keyClick(address, Qt::Key_Return);
    QCOMPARE(page.state(), BrowserPageState::Navigating);
    backend.emitNavigationStopped(
        4, QStringLiteral("https://example.com/two"),
        QStringLiteral("Second"), true, false);
    QCOMPARE(page.state(), BrowserPageState::Ready);
    QCOMPARE(dataStore.saveCount, 3);
    QCOMPARE(dataStore.history.size(), 2);
    QCOMPARE(dataStore.history.at(0).url,
             QStringLiteral("https://example.com/two"));
    QCOMPARE(dataStore.history.at(1).visitedAtMilliseconds, firstVisit);
}

void BrowserPageTest::persistsAndNormalizesBrowserHistory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsFile = directory.filePath(QStringLiteral("browser.ini"));
    QSettingsBrowserDataStore writer(settingsFile);
    writer.saveHistory({
        {QStringLiteral("HTTPS://Example.com/path?code=secret#token"),
         QStringLiteral("First"), -1},
        {QStringLiteral("https://example.com/path"), QStringLiteral("Duplicate"), 2},
        {QStringLiteral("javascript:alert(1)"), QStringLiteral("Blocked"), 3},
        {QStringLiteral("https://other.example"), QStringLiteral("Other"), 4},
    });

    QSettingsBrowserDataStore reader(settingsFile);
    const QVector<BrowserHistoryEntry> history = reader.loadHistory();
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.at(0).url, QStringLiteral("https://example.com/path"));
    QCOMPARE(history.at(0).title, QStringLiteral("First"));
    QCOMPARE(history.at(0).visitedAtMilliseconds, qint64{0});
    QCOMPARE(history.at(1).url, QStringLiteral("https://other.example"));
}

void BrowserPageTest::persistsAndNormalizesBrowserFavorites() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsFile = directory.filePath(QStringLiteral("browser.ini"));
    QSettingsBrowserDataStore writer(settingsFile);
    writer.saveFavorites({
        {QStringLiteral("HTTPS://Example.com/path?token=secret#private"),
         QStringLiteral(" First "),
         QStringLiteral(" note ")},
        {QStringLiteral("https://example.com/path"),
         QStringLiteral("Duplicate"), QStringLiteral("ignored")},
        {QStringLiteral("javascript:alert(1)"), QStringLiteral("Blocked"),
         QStringLiteral("blocked")},
        {QStringLiteral("https://other.example"), QStringLiteral("Other"),
         QStringLiteral("Second note")},
    });

    QSettingsBrowserDataStore reader(settingsFile);
    const QVector<BrowserFavoriteEntry> favorites = reader.loadFavorites();
    QCOMPARE(favorites.size(), 2);
    QCOMPARE(favorites.at(0).url,
             QStringLiteral("https://example.com/path"));
    QCOMPARE(favorites.at(0).title, QStringLiteral("First"));
    QCOMPARE(favorites.at(0).note, QStringLiteral("note"));
    QCOMPARE(favorites.at(1).url, QStringLiteral("https://other.example"));
    QCOMPARE(reader.loadHistory().size(), 0);
}

void BrowserPageTest::limitsPersistedBrowserRecords() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString settingsFile = directory.filePath(QStringLiteral("browser.ini"));
    QSettingsBrowserDataStore store(settingsFile);

    QVector<BrowserHistoryEntry> history;
    history.reserve(510);
    for (int index = 0; index < 510; ++index) {
        history.append(
            {QStringLiteral("https://history-%1.example").arg(index),
             QStringLiteral("History %1").arg(index), index});
    }
    store.saveHistory(history);
    const QVector<BrowserHistoryEntry> storedHistory = store.loadHistory();
    QCOMPARE(storedHistory.size(), 500);
    QCOMPARE(storedHistory.constFirst().url,
             QStringLiteral("https://history-0.example"));
    QCOMPARE(storedHistory.constLast().url,
             QStringLiteral("https://history-499.example"));

    QVector<BrowserFavoriteEntry> favorites;
    favorites.reserve(5010);
    for (int index = 0; index < 5010; ++index) {
        favorites.append(
            {QStringLiteral("https://favorite-%1.example").arg(index),
             QStringLiteral("Favorite %1").arg(index),
             QStringLiteral("Note %1").arg(index)});
    }
    store.saveFavorites(favorites);
    const QVector<BrowserFavoriteEntry> storedFavorites = store.loadFavorites();
    QCOMPARE(storedFavorites.size(), 5000);
    QCOMPARE(storedFavorites.constFirst().url,
             QStringLiteral("https://favorite-0.example"));
    QCOMPARE(storedFavorites.constLast().url,
             QStringLiteral("https://favorite-4999.example"));
    QCOMPARE(store.loadHistory().size(), 500);
}

void BrowserPageTest::cachesBrowserDataAndSkipsUnchangedListRebuilds() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    dataStore.history.reserve(500);
    for (int index = 0; index < 500; ++index) {
        dataStore.history.append(
            {QStringLiteral("https://history-%1.example/page").arg(index),
             QStringLiteral("History %1").arg(index), index});
    }
    dataStore.favorites = {
        {QStringLiteral("https://favorite-one.example/page"),
         QStringLiteral("Favorite One"), QStringLiteral("first")},
        {QStringLiteral("https://favorite-two.example/page"),
         QStringLiteral("Favorite Two"), QStringLiteral("second")},
    };
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();

    auto* const historyButton =
        page.findChild<QToolButton*>(QStringLiteral("browserHistoryButton"));
    QVERIFY(historyButton != nullptr);
    QTest::mouseClick(historyButton, Qt::LeftButton);
    auto* const historyDialog =
        page.findChild<QDialog*>(QStringLiteral("browserHistoryDialog"));
    auto* const historyList =
        page.findChild<QListWidget*>(QStringLiteral("browserHistoryList"));
    QVERIFY(historyDialog != nullptr);
    QVERIFY(historyList != nullptr);
    QCOMPARE(historyList->count(), 500);
    QCOMPARE(dataStore.historyLoadCount, 1);

    QSignalSpy historyRowsInserted(historyList->model(),
                                   &QAbstractItemModel::rowsInserted);
    QSignalSpy historyRowsRemoved(historyList->model(),
                                  &QAbstractItemModel::rowsRemoved);
    QSignalSpy historyModelReset(historyList->model(),
                                 &QAbstractItemModel::modelReset);
    historyDialog->hide();
    QTest::mouseClick(historyButton, Qt::LeftButton);
    QCOMPARE(dataStore.historyLoadCount, 1);
    QCOMPARE(historyRowsInserted.count(), 0);
    QCOMPARE(historyRowsRemoved.count(), 0);
    QCOMPARE(historyModelReset.count(), 0);

    historyDialog->hide();
    page.onNavigationCompleted(
        1, QStringLiteral("https://latest.example/page?secret=value"),
        QStringLiteral("Latest"), false, false);
    QCOMPARE(dataStore.historyLoadCount, 1);
    QCOMPARE(dataStore.saveCount, 1);
    QCOMPARE(historyRowsInserted.count(), 0);
    QCOMPARE(historyRowsRemoved.count(), 0);
    QCOMPARE(historyModelReset.count(), 0);

    QTest::mouseClick(historyButton, Qt::LeftButton);
    QCOMPARE(dataStore.historyLoadCount, 1);
    QCOMPARE(historyList->count(), 500);
    QCOMPARE(historyList->item(0)->data(Qt::UserRole).toString(),
             QStringLiteral("https://latest.example/page"));
    QVERIFY(historyRowsInserted.count() + historyRowsRemoved.count() +
                historyModelReset.count() >
            0);

    historyRowsInserted.clear();
    historyRowsRemoved.clear();
    historyModelReset.clear();
    historyDialog->hide();
    QTest::mouseClick(historyButton, Qt::LeftButton);
    QCOMPARE(dataStore.historyLoadCount, 1);
    QCOMPARE(historyRowsInserted.count(), 0);
    QCOMPARE(historyRowsRemoved.count(), 0);
    QCOMPARE(historyModelReset.count(), 0);
    historyDialog->hide();

    auto* const favoritesButton =
        page.findChild<QToolButton*>(QStringLiteral("browserFavoritesButton"));
    QVERIFY(favoritesButton != nullptr);
    QTest::mouseClick(favoritesButton, Qt::LeftButton);
    auto* const favoritesDialog =
        page.findChild<QDialog*>(QStringLiteral("browserFavoritesDialog"));
    auto* const favoritesList =
        page.findChild<QListWidget*>(QStringLiteral("browserFavoritesList"));
    QVERIFY(favoritesDialog != nullptr);
    QVERIFY(favoritesList != nullptr);
    QCOMPARE(favoritesList->count(), 2);
    QCOMPARE(dataStore.favoriteLoadCount, 1);

    QSignalSpy favoriteRowsInserted(favoritesList->model(),
                                    &QAbstractItemModel::rowsInserted);
    QSignalSpy favoriteRowsRemoved(favoritesList->model(),
                                   &QAbstractItemModel::rowsRemoved);
    QSignalSpy favoriteModelReset(favoritesList->model(),
                                  &QAbstractItemModel::modelReset);
    favoritesDialog->hide();
    QTest::mouseClick(favoritesButton, Qt::LeftButton);
    QCOMPARE(dataStore.favoriteLoadCount, 1);
    QCOMPARE(favoriteRowsInserted.count(), 0);
    QCOMPARE(favoriteRowsRemoved.count(), 0);
    QCOMPARE(favoriteModelReset.count(), 0);
}

void BrowserPageTest::coalescesHistoryAndFavoriteSearchInput() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    dataStore.history.reserve(500);
    for (int index = 0; index < 500; ++index) {
        dataStore.history.append(
            {QStringLiteral("https://history-%1.example/page").arg(index),
             QStringLiteral("History %1").arg(index), index});
    }
    dataStore.favorites = {
        {QStringLiteral("https://one.example/page"), QStringLiteral("One"),
         QStringLiteral("first note")},
        {QStringLiteral("https://two.example/page"), QStringLiteral("Two"),
         QStringLiteral("second note")},
        {QStringLiteral("https://three.example/page"), QStringLiteral("Three"),
         QStringLiteral("third note")},
    };
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();

    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserHistoryButton")),
        Qt::LeftButton);
    auto* const historySearch = page.findChild<QLineEdit*>(
        QStringLiteral("browserHistorySearchEdit"));
    auto* const historyTimer = page.findChild<QTimer*>(
        QStringLiteral("browserHistorySearchTimer"));
    auto* const historyList = page.findChild<QListWidget*>(
        QStringLiteral("browserHistoryList"));
    QVERIFY(historySearch != nullptr);
    QVERIFY(historyTimer != nullptr);
    QVERIFY(historyList != nullptr);
    QCOMPARE(historyList->count(), 500);

    QSignalSpy historyRowsInserted(historyList->model(),
                                   &QAbstractItemModel::rowsInserted);
    QSignalSpy historyRowsRemoved(historyList->model(),
                                  &QAbstractItemModel::rowsRemoved);
    QSignalSpy historyModelReset(historyList->model(),
                                 &QAbstractItemModel::modelReset);
    historySearch->setText(QStringLiteral("history"));
    historySearch->setText(QStringLiteral("history-4"));
    historySearch->setText(QStringLiteral("history-499.example"));
    QVERIFY(historyTimer->isActive());
    QCOMPARE(historyRowsInserted.count(), 0);
    QCOMPARE(historyRowsRemoved.count(), 0);
    QCOMPARE(historyModelReset.count(), 0);
    QTRY_COMPARE(historyList->count(), 1);
    QVERIFY(!historyTimer->isActive());
    QVERIFY(historyList->item(0)->text().contains(
        QStringLiteral("history-499.example")));
    QVERIFY(historyRowsInserted.count() + historyRowsRemoved.count() +
                historyModelReset.count() >
            0);

    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserFavoritesButton")),
        Qt::LeftButton);
    auto* const favoritesSearch = page.findChild<QLineEdit*>(
        QStringLiteral("browserFavoritesSearchEdit"));
    auto* const favoritesTimer = page.findChild<QTimer*>(
        QStringLiteral("browserFavoritesSearchTimer"));
    auto* const favoritesList = page.findChild<QListWidget*>(
        QStringLiteral("browserFavoritesList"));
    QVERIFY(favoritesSearch != nullptr);
    QVERIFY(favoritesTimer != nullptr);
    QVERIFY(favoritesList != nullptr);
    QCOMPARE(favoritesList->count(), 3);

    QSignalSpy favoriteRowsInserted(favoritesList->model(),
                                    &QAbstractItemModel::rowsInserted);
    QSignalSpy favoriteRowsRemoved(favoritesList->model(),
                                   &QAbstractItemModel::rowsRemoved);
    QSignalSpy favoriteModelReset(favoritesList->model(),
                                  &QAbstractItemModel::modelReset);
    favoritesSearch->setText(QStringLiteral("note"));
    favoritesSearch->setText(QStringLiteral("third"));
    favoritesSearch->setText(QStringLiteral("third note"));
    QVERIFY(favoritesTimer->isActive());
    QCOMPARE(favoriteRowsInserted.count(), 0);
    QCOMPARE(favoriteRowsRemoved.count(), 0);
    QCOMPARE(favoriteModelReset.count(), 0);
    QTRY_COMPARE(favoritesList->count(), 1);
    QVERIFY(!favoritesTimer->isActive());
    QVERIFY(favoritesList->item(0)->text().contains(
        QStringLiteral("third note")));
    QVERIFY(favoriteRowsInserted.count() + favoriteRowsRemoved.count() +
                favoriteModelReset.count() >
            0);
}

void BrowserPageTest::opensHistoryInCurrentOrNewTabFromMouseGesture() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    dataStore.history = {
        {QStringLiteral("https://history.example/one"), QStringLiteral("One"), 2},
        {QStringLiteral("https://history.example/two"), QStringLiteral("Two"), 1},
    };
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto openHistory = [&page]() {
        QTest::mouseClick(
            page.findChild<QToolButton*>(QStringLiteral("browserHistoryButton")),
            Qt::LeftButton);
        auto* const list =
            page.findChild<QListWidget*>(QStringLiteral("browserHistoryList"));
        QCoreApplication::processEvents();
        return list;
    };

    QListWidget* list = openHistory();
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text,
             QStringLiteral("https://history.example/one"));

    list = openHistory();
    const auto commandCountBeforeControlClick = backend.commands.size();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      list->visualItemRect(list->item(1)).center());
    QCOMPARE(backend.commands.at(commandCountBeforeControlClick).kind,
             test::FakeBrowserCommandKind::CreateTab);
    QCOMPARE(backend.commands.at(commandCountBeforeControlClick).text,
             QStringLiteral("https://history.example/two"));

    list = openHistory();
    const auto commandCountBeforeMiddleClick = backend.commands.size();
    QTest::mouseClick(list->viewport(), Qt::MiddleButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    QCOMPARE(backend.commands.at(commandCountBeforeMiddleClick).kind,
             test::FakeBrowserCommandKind::CreateTab);
    QCOMPARE(backend.commands.at(commandCountBeforeMiddleClick).text,
             QStringLiteral("https://history.example/one"));
}

void BrowserPageTest::opensFavoritesInCurrentOrNewTabFromMouseGesture() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    dataStore.favorites = {
        {QStringLiteral("https://favorite.example/one"),
         QStringLiteral("One"), QStringLiteral("first")},
        {QStringLiteral("https://favorite.example/two"),
         QStringLiteral("Two"), QStringLiteral("second")},
    };
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto openFavorites = [&page]() {
        QTest::mouseClick(
            page.findChild<QToolButton*>(QStringLiteral("browserFavoritesButton")),
            Qt::LeftButton);
        auto* const list =
            page.findChild<QListWidget*>(QStringLiteral("browserFavoritesList"));
        QCoreApplication::processEvents();
        return list;
    };

    QListWidget* list = openFavorites();
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text,
             QStringLiteral("https://favorite.example/one"));

    list = openFavorites();
    const auto commandCountBeforeControlClick = backend.commands.size();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      list->visualItemRect(list->item(1)).center());
    QCOMPARE(backend.commands.at(commandCountBeforeControlClick).kind,
             test::FakeBrowserCommandKind::CreateTab);
    QCOMPARE(backend.commands.at(commandCountBeforeControlClick).text,
             QStringLiteral("https://favorite.example/two"));

    list = openFavorites();
    const auto commandCountBeforeMiddleClick = backend.commands.size();
    QTest::mouseClick(list->viewport(), Qt::MiddleButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    QCOMPARE(backend.commands.at(commandCountBeforeMiddleClick).kind,
             test::FakeBrowserCommandKind::CreateTab);
    QCOMPARE(backend.commands.at(commandCountBeforeMiddleClick).text,
             QStringLiteral("https://favorite.example/one"));
}

void BrowserPageTest::addsEditsDeduplicatesAndRemovesFavorites() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    dataStore.favorites = {
        {QStringLiteral("https://existing.example/page"),
         QStringLiteral("Existing"), QStringLiteral("old")},
    };
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://current.example/page"),
        QStringLiteral("Current"));

    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserFavoritesButton")),
        Qt::LeftButton);
    auto* const list =
        page.findChild<QListWidget*>(QStringLiteral("browserFavoritesList"));
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 1);
    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserFavoriteAddButton")),
        Qt::LeftButton);
    auto* const title = page.findChild<QLineEdit*>(
        QStringLiteral("browserFavoriteTitleEdit"));
    auto* const url = page.findChild<QLineEdit*>(
        QStringLiteral("browserFavoriteUrlEdit"));
    auto* const note = page.findChild<QLineEdit*>(
        QStringLiteral("browserFavoriteNoteEdit"));
    QVERIFY(title != nullptr);
    QVERIFY(url != nullptr);
    QVERIFY(note != nullptr);
    QCOMPARE(title->text(), QStringLiteral("Current"));
    QCOMPARE(url->text(), QStringLiteral("https://current.example/page"));
    note->setText(QStringLiteral("new note"));
    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserFavoriteSaveButton")),
        Qt::LeftButton);
    QCOMPARE(dataStore.favorites.size(), 2);
    QCOMPARE(dataStore.favorites.at(0).note, QStringLiteral("new note"));

    list->setCurrentRow(0);
    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserFavoriteEditButton")),
        Qt::LeftButton);
    title->setText(QStringLiteral("Merged"));
    url->setText(QStringLiteral("https://existing.example/page"));
    note->setText(QStringLiteral("updated"));
    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserFavoriteSaveButton")),
        Qt::LeftButton);
    QCOMPARE(dataStore.favorites.size(), 1);
    QCOMPARE(dataStore.favorites.at(0).url,
             QStringLiteral("https://existing.example/page"));
    QCOMPARE(dataStore.favorites.at(0).title, QStringLiteral("Merged"));
    QCOMPARE(dataStore.favorites.at(0).note, QStringLiteral("updated"));

    list->setCurrentRow(0);
    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserFavoriteRemoveButton")),
        Qt::LeftButton);
    QVERIFY(dataStore.favorites.isEmpty());
    QCOMPARE(list->count(), 0);
}

void BrowserPageTest::searchesDeletesAndClearsHistoryWithConfirmation() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    dataStore.history = {
        {QStringLiteral("https://one.example/page"), QStringLiteral("Alpha"), 3},
        {QStringLiteral("https://two.example/page"), QStringLiteral("Beta"), 2},
        {QStringLiteral("https://three.example/page"), QStringLiteral("Gamma"), 1},
    };
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();

    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserHistoryButton")),
        Qt::LeftButton);
    auto* const search = page.findChild<QLineEdit*>(
        QStringLiteral("browserHistorySearchEdit"));
    auto* const list =
        page.findChild<QListWidget*>(QStringLiteral("browserHistoryList"));
    auto* const removeButton = page.findChild<QPushButton*>(
        QStringLiteral("browserHistoryRemoveButton"));
    QVERIFY(search != nullptr);
    QVERIFY(list != nullptr);
    QVERIFY(removeButton != nullptr);

    search->setText(QStringLiteral("two.example"));
    QTRY_COMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(),
             QStringLiteral("Beta\nhttps://two.example/page"));
    list->setCurrentRow(0);
    QTest::mouseClick(removeButton, Qt::LeftButton);
    QCOMPARE(dataStore.history.size(), 2);
    QCOMPARE(dataStore.history.at(0).title, QStringLiteral("Alpha"));
    QCOMPARE(dataStore.history.at(1).title, QStringLiteral("Gamma"));
    QCOMPARE(dataStore.saveCount, 1);

    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserHistoryClearButton")),
        Qt::LeftButton);
    auto* const clearDialog = page.findChild<QDialog*>(
        QStringLiteral("browserHistoryClearDialog"));
    QVERIFY(clearDialog != nullptr);
    QVERIFY(clearDialog->isVisible());
    QCOMPARE(dataStore.history.size(), 2);
    QTest::mouseClick(
        clearDialog->findChild<QPushButton*>(
            QStringLiteral("browserHistoryClearConfirmButton")),
        Qt::LeftButton);
    QVERIFY(dataStore.history.isEmpty());
    QCOMPARE(dataStore.saveCount, 2);
    QCOMPARE(list->count(), 0);
}

void BrowserPageTest::searchesEditsDeletesAndReordersFavorites() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    dataStore.favorites = {
        {QStringLiteral("https://one.example/page"), QStringLiteral("One"),
         QStringLiteral("first note")},
        {QStringLiteral("https://two.example/page"), QStringLiteral("Two"),
         QStringLiteral("second note")},
        {QStringLiteral("https://three.example/page"), QStringLiteral("Three"),
         QStringLiteral("third note")},
    };
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();

    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserFavoritesButton")),
        Qt::LeftButton);
    auto* const search = page.findChild<QLineEdit*>(
        QStringLiteral("browserFavoritesSearchEdit"));
    auto* const list =
        page.findChild<QListWidget*>(QStringLiteral("browserFavoritesList"));
    QVERIFY(search != nullptr);
    QVERIFY(list != nullptr);

    search->setText(QStringLiteral("SECOND NOTE"));
    QTRY_COMPARE(list->count(), 1);
    QVERIFY(!list->dragEnabled());
    list->setCurrentRow(0);
    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserFavoriteEditButton")),
        Qt::LeftButton);
    auto* const title = page.findChild<QLineEdit*>(
        QStringLiteral("browserFavoriteTitleEdit"));
    QCOMPARE(title->text(), QStringLiteral("Two"));
    title->setText(QStringLiteral("Two edited"));
    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserFavoriteSaveButton")),
        Qt::LeftButton);
    QCOMPARE(dataStore.favorites.at(1).title, QStringLiteral("Two edited"));

    search->setText(QStringLiteral("three.example"));
    QTRY_COMPARE(list->count(), 1);
    list->setCurrentRow(0);
    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserFavoriteRemoveButton")),
        Qt::LeftButton);
    QCOMPARE(dataStore.favorites.size(), 2);
    QCOMPARE(dataStore.favorites.at(0).title, QStringLiteral("One"));
    QCOMPARE(dataStore.favorites.at(1).title, QStringLiteral("Two edited"));

    search->clear();
    QTRY_COMPARE(list->count(), 2);
    QVERIFY(list->dragEnabled());
    QListWidgetItem* const moved = list->takeItem(1);
    QVERIFY(moved != nullptr);
    list->insertItem(0, moved);
    QVERIFY(QMetaObject::invokeMethod(&page, "persistFavoriteListOrder",
                                      Qt::DirectConnection));
    QCOMPARE(dataStore.favorites.at(0).title, QStringLiteral("Two edited"));
    QCOMPARE(dataStore.favorites.at(1).title, QStringLiteral("One"));
    QCOMPARE(list->item(0)->text().section(QLatin1Char('\n'), 0, 0),
             QStringLiteral("Two edited"));
}

void BrowserPageTest::importsAndExportsFavoritesWithoutPartialWrites() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    dataStore.favorites = {
        {QStringLiteral("https://existing.example/page"),
         QStringLiteral("Existing"), QStringLiteral("kept")},
    };
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();
    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserFavoritesButton")),
        Qt::LeftButton);

    const QByteArray html(
        "<DL><p>"
        "<DT><A HREF=\"https://existing.example/page?secret=1\">Duplicate</A>"
        "<DT><A HREF=\"https://new.example/path#private\">New</A>"
        "<DD>imported note"
        "<DT><A HREF=\"javascript:alert(1)\">Blocked</A>"
        "</DL><p>");
    QVERIFY(QMetaObject::invokeMethod(
        &page, "prepareFavoriteImport", Qt::DirectConnection,
        Q_ARG(QByteArray, html)));
    QCOMPARE(dataStore.favoriteSaveCount, 0);
    QCOMPARE(dataStore.favorites.size(), 1);
    auto* const importDialog = page.findChild<QDialog*>(
        QStringLiteral("browserFavoriteImportDialog"));
    auto* const summary = page.findChild<QLabel*>(
        QStringLiteral("browserFavoriteImportSummaryLabel"));
    QVERIFY(importDialog != nullptr);
    QVERIFY(importDialog->isVisible());
    QVERIFY(summary->text().contains(QStringLiteral("新增 1 项")));
    QVERIFY(!summary->text().contains(QStringLiteral("existing.example")));
    QTest::mouseClick(
        importDialog->findChild<QPushButton*>(
            QStringLiteral("browserFavoriteImportConfirmButton")),
        Qt::LeftButton);
    QCOMPARE(dataStore.favoriteSaveCount, 1);
    QCOMPARE(dataStore.favorites.size(), 2);
    QCOMPARE(dataStore.favorites.at(1).url,
             QStringLiteral("https://new.example/path"));
    QCOMPARE(dataStore.favorites.at(1).note, QStringLiteral("imported note"));

    const auto savedFavorites = dataStore.favorites;
    QVERIFY(QMetaObject::invokeMethod(
        &page, "prepareFavoriteImport", Qt::DirectConnection,
        Q_ARG(QByteArray, QByteArray("<html>no bookmarks</html>"))));
    QCOMPARE(dataStore.favoriteSaveCount, 1);
    QCOMPARE(dataStore.favorites.size(), savedFavorites.size());
    for (int index = 0; index < savedFavorites.size(); ++index) {
        QCOMPARE(dataStore.favorites.at(index).url, savedFavorites.at(index).url);
        QCOMPARE(dataStore.favorites.at(index).title,
                 savedFavorites.at(index).title);
        QCOMPARE(dataStore.favorites.at(index).note,
                 savedFavorites.at(index).note);
    }
    auto* const status = page.findChild<QLabel*>(
        QStringLiteral("browserFavoriteTransferStatusLabel"));
    QVERIFY(status != nullptr);
    QVERIFY(status->text().contains(QStringLiteral("原收藏夹已保留")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString exportPath =
        directory.filePath(QStringLiteral("private-bookmarks.html"));
    QVERIFY(QMetaObject::invokeMethod(
        &page, "exportFavoritesToFile", Qt::DirectConnection,
        Q_ARG(QString, exportPath)));
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    const QByteArray exportedHtml = exported.readAll();
    QVERIFY(exportedHtml.contains("https://existing.example/page"));
    QVERIFY(exportedHtml.contains("https://new.example/path"));
    QVERIFY(!status->text().contains(exportPath));
    QCOMPARE(status->text(), QStringLiteral("收藏已安全导出"));
}

void BrowserPageTest::opensBingOnceAndProvidesActiveTabShortcuts() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    page.activateWindow();
    QCoreApplication::processEvents();

    backend.emitReady(1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Navigate), 1);
    QCOMPARE(backend.lastCommand().text, QStringLiteral("https://www.bing.com/"));
    backend.emitReady(1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Navigate), 1);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    auto* const newTabButton = page.findChild<QToolButton*>(
        QStringLiteral("browserNewTabButton"));
    QVERIFY(tabBar != nullptr);
    QVERIFY(newTabButton != nullptr);
    QCOMPARE(newTabButton->text(), QStringLiteral("+"));

    QTest::mouseClick(newTabButton, Qt::LeftButton);
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).kind,
             test::FakeBrowserCommandKind::CreateTab);
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).text,
             QStringLiteral("https://www.bing.com/"));
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::ActivateTab);
    QCOMPARE(tabBar->currentIndex(), 1);

    QTest::keyClick(&page, Qt::Key_Tab, Qt::ControlModifier);
    QCOMPARE(tabBar->currentIndex(), 0);
    QTest::keyClick(&page, Qt::Key_Backtab,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(tabBar->currentIndex(), 1);

    QTest::keyClick(&page, Qt::Key_W, Qt::ControlModifier);
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(tabBar->currentIndex(), 0);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 1);

    QTest::keyClick(&page, Qt::Key_W, Qt::ControlModifier);
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text, QStringLiteral("https://www.bing.com/"));

    QTest::keyClick(&page, Qt::Key_T, Qt::ControlModifier);
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).text,
             QStringLiteral("https://www.bing.com/"));
}

void BrowserPageTest::findsWithinCurrentTabAndStopsOnContextChanges() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    page.activateWindow();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto* const findBar =
        page.findChild<QWidget*>(QStringLiteral("browserFindBar"));
    auto* const findEdit =
        page.findChild<QLineEdit*>(QStringLiteral("browserFindEdit"));
    auto* const resultLabel =
        page.findChild<QLabel*>(QStringLiteral("browserFindResultLabel"));
    QVERIFY(findBar != nullptr);
    QVERIFY(findEdit != nullptr);
    QVERIFY(resultLabel != nullptr);
    QVERIFY(!findBar->isVisible());

    QTest::keyClick(&page, Qt::Key_F, Qt::ControlModifier);
    QVERIFY(findBar->isVisible());
    QVERIFY(findEdit->hasFocus());
    findEdit->setText(QStringLiteral("fixed test term"));
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::FindInPage);
    QVERIFY(backend.lastCommand().flag);

    backend.emitFindResultChanged(1, 1, 0, 3);
    QCOMPARE(resultLabel->text(), QStringLiteral("1/3"));
    QTest::keyClick(findEdit, Qt::Key_Return);
    QVERIFY(backend.lastCommand().flag);
    QTest::keyClick(findEdit, Qt::Key_Return, Qt::ShiftModifier);
    QVERIFY(!backend.lastCommand().flag);

    backend.emitFindResultChanged(1, 2, 1, 9);
    QCOMPARE(resultLabel->text(), QStringLiteral("1/3"));
    QTest::keyClick(findEdit, Qt::Key_Escape);
    QVERIFY(!findBar->isVisible());
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::StopFinding);
    QVERIFY(backend.lastCommand().flag);
    QVERIFY(findEdit->text().isEmpty());

    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);
    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    QTest::keyClick(&page, Qt::Key_F, Qt::ControlModifier);
    findEdit->setText(QStringLiteral("second fixed term"));
    tabBar->moveTab(1, 0);
    backend.emitFindResultChanged(2, 2, 1, 4);
    QCOMPARE(resultLabel->text(), QStringLiteral("2/4"));
    backend.emitTabCloseRequested(1);
    backend.emitFindResultChanged(2, 2, 2, 4);
    QCOMPARE(resultLabel->text(), QStringLiteral("3/4"));
    const int stopCount =
        backend.count(test::FakeBrowserCommandKind::StopFinding);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://three.example")));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::StopFinding),
             stopCount + 1);
    QVERIFY(!findBar->isVisible());
}

void BrowserPageTest::tabContextMenuTargetsStableTab() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://one.example/page"), QStringLiteral("One"),
        false, false);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);
    backend.emitTabNavigationCompleted(
        2, 2, QStringLiteral("https://two.example/page"), QStringLiteral("Two"),
        false, false);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    QCOMPARE(tabBar->currentIndex(), 1);
    const QPoint firstTabPosition = tabBar->tabRect(0).center();
    QTimer::singleShot(0, &page, [&page] {
        auto* const menu = page.findChild<QMenu*>(
            QStringLiteral("browserTabContextMenu"));
        QVERIFY(menu != nullptr);
        QAction* const action = menu->findChild<QAction*>(
            QStringLiteral("browserTabMenuMuteAction"));
        QVERIFY(action != nullptr);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(action).center());
    });
    QMetaObject::invokeMethod(tabBar, "customContextMenuRequested",
                              Qt::DirectConnection,
                              Q_ARG(QPoint, firstTabPosition));
    QCOMPARE(tabBar->currentIndex(), 1);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::SetTabAudioMuted);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{1});

    QTimer::singleShot(0, &page, [&page] {
        auto* const menu = page.findChild<QMenu*>(
            QStringLiteral("browserTabContextMenu"));
        QVERIFY(menu != nullptr);
        QAction* const action = menu->findChild<QAction*>(
            QStringLiteral("browserTabMenuDuplicateAction"));
        QVERIFY(action != nullptr);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(action).center());
    });
    QMetaObject::invokeMethod(tabBar, "customContextMenuRequested",
                              Qt::DirectConnection,
                              Q_ARG(QPoint, firstTabPosition));
    QCOMPARE(tabBar->count(), 3);
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).kind,
             test::FakeBrowserCommandKind::CreateTab);
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).text,
             QStringLiteral("https://one.example/page"));

    QTimer::singleShot(0, &page, [&page] {
        auto* const menu = page.findChild<QMenu*>(
            QStringLiteral("browserTabContextMenu"));
        QVERIFY(menu != nullptr);
        QAction* const action = menu->findChild<QAction*>(
            QStringLiteral("browserTabMenuCloseRightAction"));
        QVERIFY(action != nullptr);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(action).center());
    });
    QMetaObject::invokeMethod(tabBar, "customContextMenuRequested",
                              Qt::DirectConnection,
                              Q_ARG(QPoint, firstTabPosition));
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(tabBar->tabText(0), QStringLiteral("One"));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 2);
}

void BrowserPageTest::searchesOpenTabsByTitleAndDomain() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://news.example/first"),
        QStringLiteral("Daily Brief"), false, false);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://video.example")));
    backend.emitTabReady(2, 2);
    backend.emitTabNavigationCompleted(
        2, 2, QStringLiteral("https://video.example/watch"),
        QStringLiteral("Night Stream"), false, false);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://work.example")));
    backend.emitTabReady(3, 3);
    backend.emitTabNavigationCompleted(
        3, 3, QStringLiteral("https://work.example/project"),
        QStringLiteral("Night Notes"), false, false);

    auto* const searchButton = page.findChild<QToolButton*>(
        QStringLiteral("browserTabSearchButton"));
    QVERIFY(searchButton != nullptr);
    QTest::mouseClick(searchButton, Qt::LeftButton);
    auto* const dialog = page.findChild<QDialog*>(
        QStringLiteral("browserTabSearchDialog"));
    auto* const searchEdit = page.findChild<QLineEdit*>(
        QStringLiteral("browserTabSearchEdit"));
    auto* const searchList = page.findChild<QListWidget*>(
        QStringLiteral("browserTabSearchList"));
    QVERIFY(dialog != nullptr);
    QVERIFY(searchEdit != nullptr);
    QVERIFY(searchList != nullptr);
    QVERIFY(dialog->isVisible());
    QCOMPARE(searchList->count(), 3);

    searchEdit->setText(QStringLiteral("video.example"));
    QCOMPARE(searchList->count(), 1);
    QVERIFY(searchList->item(0)->text().contains(QStringLiteral("Night Stream")));
    searchEdit->setText(QStringLiteral("Night"));
    QCOMPARE(searchList->count(), 2);
    searchList->setCurrentRow(1);
    QTest::keyClick(searchEdit, Qt::Key_Return);

    auto* const tabBar = page.findChild<QTabBar*>(
        QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    QCOMPARE(tabBar->tabData(tabBar->currentIndex()).toULongLong(),
             qulonglong{3});
    QVERIFY(!dialog->isVisible());

    QTest::keyClick(&page, Qt::Key_A,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(dialog->isVisible());
}

void BrowserPageTest::skipsHiddenTabSearchAndGroupListRebuilds() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://one.example/page"), QStringLiteral("One"),
        false, false);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);
    backend.emitTabNavigationCompleted(
        2, 2, QStringLiteral("https://two.example/page"), QStringLiteral("Two"),
        false, false);

    auto* const searchButton = page.findChild<QToolButton*>(
        QStringLiteral("browserTabSearchButton"));
    auto* const groupButton = page.findChild<QToolButton*>(
        QStringLiteral("browserTabGroupButton"));
    QVERIFY(searchButton != nullptr);
    QVERIFY(groupButton != nullptr);

    QTest::mouseClick(groupButton, Qt::LeftButton);
    auto* const groupDialog = page.findChild<BrowserTabGroupDialog*>(
        QStringLiteral("browserTabGroupDialog"));
    auto* const groupName = page.findChild<QLineEdit*>(
        QStringLiteral("browserTabGroupNameEdit"));
    auto* const groupCreateButton = page.findChild<QPushButton*>(
        QStringLiteral("browserTabGroupCreateButton"));
    auto* const groupList = page.findChild<QListWidget*>(
        QStringLiteral("browserTabGroupList"));
    QVERIFY(groupDialog != nullptr);
    QVERIFY(groupName != nullptr);
    QVERIFY(groupCreateButton != nullptr);
    QVERIFY(groupList != nullptr);
    groupName->setText(QStringLiteral("工作"));
    QTest::mouseClick(groupCreateButton, Qt::LeftButton);
    QCOMPARE(groupList->count(), 1);
    groupDialog->hide();

    QTest::mouseClick(searchButton, Qt::LeftButton);
    auto* const searchDialog = page.findChild<QDialog*>(
        QStringLiteral("browserTabSearchDialog"));
    auto* const searchList = page.findChild<QListWidget*>(
        QStringLiteral("browserTabSearchList"));
    QVERIFY(searchDialog != nullptr);
    QVERIFY(searchList != nullptr);
    QCOMPARE(searchList->count(), 2);
    searchDialog->hide();

    QSignalSpy searchRowsInserted(searchList->model(),
                                  &QAbstractItemModel::rowsInserted);
    QSignalSpy searchRowsRemoved(searchList->model(),
                                 &QAbstractItemModel::rowsRemoved);
    QSignalSpy searchModelReset(searchList->model(),
                                &QAbstractItemModel::modelReset);
    QSignalSpy groupRowsInserted(groupList->model(),
                                 &QAbstractItemModel::rowsInserted);
    QSignalSpy groupRowsRemoved(groupList->model(),
                                &QAbstractItemModel::rowsRemoved);
    QSignalSpy groupModelReset(groupList->model(),
                               &QAbstractItemModel::modelReset);

    QTest::mouseClick(searchButton, Qt::LeftButton);
    QCOMPARE(searchRowsInserted.count(), 0);
    QCOMPARE(searchRowsRemoved.count(), 0);
    QCOMPARE(searchModelReset.count(), 0);
    searchDialog->hide();
    QTest::mouseClick(groupButton, Qt::LeftButton);
    QCOMPARE(groupRowsInserted.count(), 0);
    QCOMPARE(groupRowsRemoved.count(), 0);
    QCOMPARE(groupModelReset.count(), 0);
    groupDialog->hide();

    QVERIFY(backend.emitNewTabRequested(
        QStringLiteral("https://three.example/page")));
    backend.emitTabReady(3, 3);
    backend.emitTabNavigationCompleted(
        3, 3, QStringLiteral("https://three.example/page"),
        QStringLiteral("Three"), false, false);
    QCOMPARE(searchRowsInserted.count(), 0);
    QCOMPARE(searchRowsRemoved.count(), 0);
    QCOMPARE(searchModelReset.count(), 0);
    QCOMPARE(groupRowsInserted.count(), 0);
    QCOMPARE(groupRowsRemoved.count(), 0);
    QCOMPARE(groupModelReset.count(), 0);

    QTest::mouseClick(searchButton, Qt::LeftButton);
    QCOMPARE(searchList->count(), 3);
    QVERIFY(searchRowsInserted.count() + searchRowsRemoved.count() +
                searchModelReset.count() >
            0);
    searchDialog->hide();
    searchRowsInserted.clear();
    searchRowsRemoved.clear();
    searchModelReset.clear();

    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserClearDataButton")),
        Qt::LeftButton);
    auto* const clearDialog = page.findChild<QDialog*>(
        QStringLiteral("browserClearDataDialog"));
    QVERIFY(clearDialog != nullptr);
    QTest::mouseClick(
        clearDialog->findChild<QPushButton*>(
            QStringLiteral("browserClearDataConfirmButton")),
        Qt::LeftButton);
    QCOMPARE(searchRowsInserted.count(), 0);
    QCOMPARE(searchRowsRemoved.count(), 0);
    QCOMPARE(searchModelReset.count(), 0);
    QCOMPARE(groupRowsInserted.count(), 0);
    QCOMPARE(groupRowsRemoved.count(), 0);
    QCOMPARE(groupModelReset.count(), 0);

    QTest::mouseClick(groupButton, Qt::LeftButton);
    QCOMPARE(groupList->count(), 0);
    QVERIFY(groupRowsInserted.count() + groupRowsRemoved.count() +
                groupModelReset.count() >
            0);
    groupDialog->hide();
    QTest::mouseClick(searchButton, Qt::LeftButton);
    QCOMPARE(searchList->count(), 1);
    QVERIFY(searchRowsInserted.count() + searchRowsRemoved.count() +
                searchModelReset.count() >
            0);
}

void BrowserPageTest::pinsTabsAndProtectsBulkClose() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://one.example"), QStringLiteral("One"),
        false, false);
    for (int index = 2; index <= 4; ++index) {
        QVERIFY(backend.emitNewTabRequested(
            QStringLiteral("https://%1.example").arg(index)));
        backend.emitTabReady(index, index);
        backend.emitTabNavigationCompleted(
            index, index, QStringLiteral("https://%1.example").arg(index),
            QString::number(index), false, false);
    }
    auto* const tabBar = page.findChild<QTabBar*>(
        QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);

    const auto chooseMenuAction = [&page, tabBar](const int tabIndex,
                                                   const QString& objectName) {
        QTimer::singleShot(0, &page, [&page, objectName] {
            auto* const menu = page.findChild<QMenu*>(
                QStringLiteral("browserTabContextMenu"));
            QVERIFY(menu != nullptr);
            QAction* const action = menu->findChild<QAction*>(objectName);
            QVERIFY(action != nullptr);
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                              menu->actionGeometry(action).center());
        });
        QMetaObject::invokeMethod(
            tabBar, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(QPoint, tabBar->tabRect(tabIndex).center()));
    };

    chooseMenuAction(2, QStringLiteral("browserTabMenuPinAction"));
    QCOMPARE(tabBar->tabData(0).toULongLong(), qulonglong{3});
    QVERIFY(tabBar->tabButton(0, QTabBar::LeftSide) == nullptr);
    QVERIFY(tabBar->tabButton(0, QTabBar::RightSide) == nullptr);

    chooseMenuAction(1, QStringLiteral("browserTabMenuCloseRightAction"));
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(tabBar->tabData(0).toULongLong(), qulonglong{3});
    QCOMPARE(tabBar->tabData(1).toULongLong(), qulonglong{1});
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 2);

    chooseMenuAction(0, QStringLiteral("browserTabMenuCloseOthersAction"));
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(tabBar->tabData(0).toULongLong(), qulonglong{3});
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 3);

    chooseMenuAction(0, QStringLiteral("browserTabMenuPinAction"));
    QVERIFY(tabBar->tabButton(0, QTabBar::LeftSide) != nullptr ||
            tabBar->tabButton(0, QTabBar::RightSide) != nullptr);
}

void BrowserPageTest::protectsAudibleAndDownloadingTabsFromBulkClose() {
    test::FakeBrowserBackend backend(true);
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://one.example"), QStringLiteral("One"));
    for (int index = 2; index <= 4; ++index) {
        QVERIFY(backend.emitNewTabRequested(
            QStringLiteral("https://%1.example").arg(index)));
        backend.emitTabReady(index, index);
        backend.emitTabNavigationCompleted(
            index, index, QStringLiteral("https://%1.example").arg(index),
            QString::number(index));
    }
    backend.emitTabAudioStateChanged(2, 2, true);
    backend.emitTabDownloadRequested(
        3, 301, QStringLiteral("https://3.example"),
        QStringLiteral("active.bin"), 100);

    auto* const tabBar = page.findChild<QTabBar*>(
        QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    const auto closeOtherTabsFromFirst = [&page, tabBar] {
        QTimer::singleShot(0, &page, [&page] {
            auto* const menu = page.findChild<QMenu*>(
                QStringLiteral("browserTabContextMenu"));
            QVERIFY(menu != nullptr);
            QAction* const action = menu->findChild<QAction*>(
                QStringLiteral("browserTabMenuCloseOthersAction"));
            QVERIFY(action != nullptr);
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                              menu->actionGeometry(action).center());
        });
        QMetaObject::invokeMethod(
            tabBar, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(QPoint, tabBar->tabRect(0).center()));
    };

    closeOtherTabsFromFirst();
    QCOMPARE(tabBar->count(), 3);
    QCOMPARE(tabBar->tabData(0).toULongLong(), qulonglong{1});
    QCOMPARE(tabBar->tabData(1).toULongLong(), qulonglong{2});
    QCOMPARE(tabBar->tabData(2).toULongLong(), qulonglong{3});
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 1);

    backend.emitTabDownloadUpdated(
        3, 301, BrowserDownloadState::Completed, 100, 100);
    closeOtherTabsFromFirst();
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(tabBar->tabData(0).toULongLong(), qulonglong{1});
    QCOMPARE(tabBar->tabData(1).toULongLong(), qulonglong{2});
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 2);
}

void BrowserPageTest::enforcesConfiguredMaximumTabCount() {
    test::FakeBrowserBackend backend;
    MemoryBrowserStartupSettingsStore startup;
    startup.settings.maximumTabCount = 5;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     nullptr, nullptr, &startup);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto* const tabBar = page.findChild<QTabBar*>(
        QStringLiteral("browserTabBar"));
    auto* const newTabButton = page.findChild<QToolButton*>(
        QStringLiteral("browserNewTabButton"));
    QVERIFY(tabBar != nullptr);
    QVERIFY(newTabButton != nullptr);
    for (int index = 2; index <= 5; ++index) {
        QVERIFY(backend.emitNewTabRequested(
            QStringLiteral("https://%1.example").arg(index)));
        backend.emitTabReady(index, index);
    }
    QCOMPARE(tabBar->count(), 5);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CreateTab), 4);
    QVERIFY(!newTabButton->isEnabled());

    QVERIFY(!backend.emitNewTabRequested(QStringLiteral("https://6.example")));
    QCOMPARE(tabBar->count(), 5);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CreateTab), 4);
}

void BrowserPageTest::ctrlWConfirmsBeforeClosingPinnedTab() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);
    auto* const tabBar = page.findChild<QTabBar*>(
        QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);

    QTimer::singleShot(0, &page, [&page] {
        auto* const menu = page.findChild<QMenu*>(
            QStringLiteral("browserTabContextMenu"));
        QVERIFY(menu != nullptr);
        QAction* const action = menu->findChild<QAction*>(
            QStringLiteral("browserTabMenuPinAction"));
        QVERIFY(action != nullptr);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(action).center());
    });
    QMetaObject::invokeMethod(
        tabBar, "customContextMenuRequested", Qt::DirectConnection,
        Q_ARG(QPoint, tabBar->tabRect(1).center()));
    QCOMPARE(tabBar->currentIndex(), 0);

    backend.emitAcceleratorRequested(2, BrowserAccelerator::CloseTab);
    auto* const dialog = page.findChild<QDialog*>(
        QStringLiteral("browserPinnedCloseDialog"));
    QVERIFY(dialog != nullptr);
    QVERIFY(dialog->isVisible());
    QCOMPARE(tabBar->count(), 2);
    QTest::mouseClick(page.findChild<QPushButton*>(
                          QStringLiteral("browserPinnedCloseCancelButton")),
                      Qt::LeftButton);
    QCOMPARE(tabBar->count(), 2);

    backend.emitAcceleratorRequested(2, BrowserAccelerator::CloseTab);
    QTest::mouseClick(page.findChild<QPushButton*>(
                          QStringLiteral("browserPinnedCloseConfirmButton")),
                      Qt::LeftButton);
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 1);
}

void BrowserPageTest::closesLastTabToBingAndRestoresOriginalState() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    page.activateWindow();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://original.example/page"),
        QStringLiteral("Original"));
    backend.emitTabZoomFactorChanged(1, 1, 2.0);

    auto* const tabBar = page.findChild<QTabBar*>(
        QStringLiteral("browserTabBar"));
    auto* const zoomResetButton = page.findChild<QToolButton*>(
        QStringLiteral("browserZoomResetButton"));
    QVERIFY(tabBar != nullptr);
    QVERIFY(zoomResetButton != nullptr);
    QCOMPARE(zoomResetButton->text(), QStringLiteral("200%"));

    QTest::keyClick(&page, Qt::Key_W, Qt::ControlModifier);
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text, QStringLiteral("https://www.bing.com/"));
    QCOMPARE(zoomResetButton->text(), QStringLiteral("100%"));
    bool resetZoomWasSent = false;
    for (const test::FakeBrowserCommand& command : backend.commands) {
        if (command.kind == test::FakeBrowserCommandKind::SetTabZoomFactor &&
            command.requestId == 1 && qFuzzyCompare(command.number, 1.0)) {
            resetZoomWasSent = true;
        }
    }
    QVERIFY(resetZoomWasSent);

    backend.emitAcceleratorRequested(2, BrowserAccelerator::ReopenClosedTab);
    QCOMPARE(tabBar->count(), 2);
    bool originalAddressWasRestored = false;
    for (const test::FakeBrowserCommand& command : backend.commands) {
        if (command.kind == test::FakeBrowserCommandKind::CreateTab &&
            command.text == QStringLiteral("https://original.example/page")) {
            originalAddressWasRestored = true;
        }
    }
    QVERIFY(originalAddressWasRestored);
}

void BrowserPageTest::restoresPinnedStateAfterConfirmedClose() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    QVERIFY(backend.emitNewTabRequested(
        QStringLiteral("https://pinned.example/page")));
    backend.emitTabReady(2, 2);
    backend.emitTabNavigationCompleted(
        2, 2, QStringLiteral("https://pinned.example/page"),
        QStringLiteral("Pinned"));
    auto* const tabBar = page.findChild<QTabBar*>(
        QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);

    QTimer::singleShot(0, &page, [&page] {
        auto* const menu = page.findChild<QMenu*>(
            QStringLiteral("browserTabContextMenu"));
        QVERIFY(menu != nullptr);
        QAction* const action = menu->findChild<QAction*>(
            QStringLiteral("browserTabMenuPinAction"));
        QVERIFY(action != nullptr);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(action).center());
    });
    QMetaObject::invokeMethod(
        tabBar, "customContextMenuRequested", Qt::DirectConnection,
        Q_ARG(QPoint, tabBar->tabRect(1).center()));

    backend.emitAcceleratorRequested(2, BrowserAccelerator::CloseTab);
    auto* const confirmButton = page.findChild<QPushButton*>(
        QStringLiteral("browserPinnedCloseConfirmButton"));
    QVERIFY(confirmButton != nullptr);
    QTest::mouseClick(confirmButton, Qt::LeftButton);
    QCOMPARE(tabBar->count(), 1);

    backend.emitAcceleratorRequested(1, BrowserAccelerator::ReopenClosedTab);
    QCOMPARE(tabBar->count(), 2);
    const int restoredIndex = tabBar->currentIndex();
    QCOMPARE(tabBar->tabText(restoredIndex), QStringLiteral("Pinned"));
    QVERIFY(tabBar->tabButton(restoredIndex, QTabBar::LeftSide) == nullptr);
    QVERIFY(tabBar->tabButton(restoredIndex, QTabBar::RightSide) == nullptr);
}

void BrowserPageTest::recoversOnlyTheFailedTabAfterUserConfirmation() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    auto* const recoveryPage = page.findChild<QWidget*>(
        QStringLiteral("browserProcessFailurePage"));
    auto* const recoveryButton = page.findChild<QPushButton*>(
        QStringLiteral("browserProcessRecoveryButton"));
    QVERIFY(tabBar != nullptr);
    QVERIFY(recoveryPage != nullptr);
    QVERIFY(recoveryButton != nullptr);

    tabBar->setCurrentIndex(0);
    backend.emitTabProcessFailed(
        2, 2, BrowserProcessFailureKind::RenderProcessUnresponsive);
    QVERIFY(!recoveryPage->isVisibleTo(&page));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::RecoverTab), 0);

    tabBar->setCurrentIndex(1);
    QVERIFY(recoveryPage->isVisibleTo(&page));
    QVERIFY(recoveryButton->isVisibleTo(&page));
    QTest::mouseClick(recoveryButton, Qt::LeftButton);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::RecoverTab);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{2});
    QCOMPARE(backend.lastCommand().generation, std::uint64_t{3});

    // 恢复会切换到新代次，崩溃前已经排队的完成事件不得提前结束恢复状态。
    backend.emitTabNavigationCompleted(
        2, 2, QStringLiteral("https://two.example/recovered"),
        QStringLiteral("Recovered"), false, false);
    QCOMPARE(tabBar->tabText(1), QStringLiteral("新标签页"));

    backend.emitTabNavigationCompleted(
        2, 3, QStringLiteral("https://two.example/recovered"),
        QStringLiteral("Recovered"), false, false);
    QVERIFY(!recoveryPage->isVisibleTo(&page));
    QCOMPARE(tabBar->tabText(1), QStringLiteral("Recovered"));
}

void BrowserPageTest::failedRecoveryKeepsRecoverableFailureVisible() {
    test::FakeBrowserBackend backend;
    backend.canRecoverTab = false;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitTabProcessFailed(
        1, 1, BrowserProcessFailureKind::RenderProcessExited);

    auto* const recoveryPage = page.findChild<QWidget*>(
        QStringLiteral("browserProcessFailurePage"));
    auto* const recoveryButton = page.findChild<QPushButton*>(
        QStringLiteral("browserProcessRecoveryButton"));
    auto* const detail = page.findChild<QLabel*>(
        QStringLiteral("browserProcessFailureDetail"));
    QVERIFY(recoveryPage != nullptr);
    QVERIFY(recoveryButton != nullptr);
    QVERIFY(detail != nullptr);

    QTest::mouseClick(recoveryButton, Qt::LeftButton);

    QCOMPARE(backend.count(test::FakeBrowserCommandKind::RecoverTab), 1);
    QVERIFY(recoveryPage->isVisibleTo(&page));
    QVERIFY(recoveryButton->isEnabled());
    QVERIFY(detail->text().contains(QStringLiteral("仍可再次恢复")));
}

void BrowserPageTest::throttlesRepeatedTabRecoveryWithoutPermanentLockout() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.setProperty("browserRecoveryCooldownMilliseconds", 10);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto* const recoveryButton = page.findChild<QPushButton*>(
        QStringLiteral("browserProcessRecoveryButton"));
    auto* const detail = page.findChild<QLabel*>(
        QStringLiteral("browserProcessFailureDetail"));
    QVERIFY(recoveryButton != nullptr);
    QVERIFY(detail != nullptr);

    backend.emitTabProcessFailed(
        1, 1, BrowserProcessFailureKind::RenderProcessExited);
    QVERIFY(recoveryButton->isEnabled());
    QTest::mouseClick(recoveryButton, Qt::LeftButton);
    backend.emitTabProcessFailed(
        1, 2, BrowserProcessFailureKind::RenderProcessExited);
    QVERIFY(recoveryButton->isEnabled());
    QTest::mouseClick(recoveryButton, Qt::LeftButton);
    backend.emitTabProcessFailed(
        1, 3, BrowserProcessFailureKind::RenderProcessExited);

    QVERIFY(!recoveryButton->isEnabled());
    QVERIFY(detail->text().contains(QStringLiteral("稍后再试")));
    QTRY_VERIFY_WITH_TIMEOUT(recoveryButton->isEnabled(), 1000);

    QTest::mouseClick(recoveryButton, Qt::LeftButton);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::RecoverTab);
    QCOMPARE(backend.lastCommand().generation, std::uint64_t{4});
    backend.emitTabNavigationCompleted(
        1, 4, QStringLiteral("https://recovered.example/page"),
        QStringLiteral("Recovered"));
    backend.emitTabProcessFailed(
        1, 4, BrowserProcessFailureKind::RenderProcessExited);
    QVERIFY(recoveryButton->isEnabled());
    QVERIFY(!detail->text().contains(QStringLiteral("稍后再试")));
}

void BrowserPageTest::browserProcessExitRequiresApplicationRestart() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    backend.emitTabProcessFailed(
        1, 1, BrowserProcessFailureKind::BrowserProcessExited);
    auto* const recoveryPage = page.findChild<QWidget*>(
        QStringLiteral("browserProcessFailurePage"));
    auto* const recoveryButton = page.findChild<QPushButton*>(
        QStringLiteral("browserProcessRecoveryButton"));
    auto* const detail = page.findChild<QLabel*>(
        QStringLiteral("browserProcessFailureDetail"));
    auto* const newTab = page.findChild<QToolButton*>(
        QStringLiteral("browserNewTabButton"));
    QVERIFY(recoveryPage != nullptr);
    QVERIFY(recoveryButton != nullptr);
    QVERIFY(detail != nullptr);
    QVERIFY(newTab != nullptr);
    QVERIFY(recoveryPage->isVisibleTo(&page));
    QVERIFY(!recoveryButton->isVisible());
    QVERIFY(detail->text().contains(QStringLiteral("重启 MediaHub")));
    QVERIFY(!newTab->isEnabled());

    backend.emitTabReady(1, 1);
    QVERIFY(recoveryPage->isVisibleTo(&page));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::RecoverTab), 0);
}

void BrowserPageTest::disablesUnsafeTabSleepWithoutSuspendingWebMedia() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);

    QTimer::singleShot(0, &page, [&page] {
        auto* const menu = page.findChild<QMenu*>(
            QStringLiteral("browserTabContextMenu"));
        QVERIFY(menu != nullptr);
        QAction* const action = menu->findChild<QAction*>(
            QStringLiteral("browserTabMenuSleepAction"));
        QVERIFY(action != nullptr);
        QVERIFY(!action->isEnabled());
        QVERIFY(action->text().contains(QStringLiteral("无法安全判断")));
        menu->close();
    });
    QMetaObject::invokeMethod(
        tabBar, "customContextMenuRequested", Qt::DirectConnection,
        Q_ARG(QPoint, tabBar->tabRect(0).center()));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::SetSuspended), 0);
}

void BrowserPageTest::groupsTabsWithoutSuspendingThemAndRestoresGroupSession() {
    MemoryBrowserSessionStore sessions;
    MemoryBrowserStartupSettingsStore startup;
    startup.settings.mode = BrowserStartupMode::RestoreSession;
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     nullptr, &sessions, &startup);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    auto* const groupButton = page.findChild<QToolButton*>(
        QStringLiteral("browserTabGroupButton"));
    QVERIFY(tabBar != nullptr);
    QVERIFY(groupButton != nullptr);
    QTest::mouseClick(groupButton, Qt::LeftButton);
    auto* const groupDialog = page.findChild<BrowserTabGroupDialog*>(
        QStringLiteral("browserTabGroupDialog"));
    auto* const nameEdit = page.findChild<QLineEdit*>(
        QStringLiteral("browserTabGroupNameEdit"));
    auto* const createButton = page.findChild<QPushButton*>(
        QStringLiteral("browserTabGroupCreateButton"));
    QVERIFY(groupDialog != nullptr);
    QVERIFY(nameEdit != nullptr);
    QVERIFY(createButton != nullptr);
    nameEdit->setText(QStringLiteral("直播观察"));
    QTest::mouseClick(createButton, Qt::LeftButton);
    groupDialog->hide();

    auto* const groupList = page.findChild<QListWidget*>(
        QStringLiteral("browserTabGroupList"));
    QVERIFY(groupList != nullptr);
    QCOMPARE(groupList->count(), 1);
    const QString groupId =
        groupList->item(0)->data(Qt::UserRole).toString();
    QVERIFY(!groupId.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(
        &page, "moveTabToGroup", Qt::DirectConnection,
        Q_ARG(std::uint64_t, std::uint64_t{1}), Q_ARG(QString, groupId)));
    QVERIFY(QMetaObject::invokeMethod(
        &page, "moveTabToGroup", Qt::DirectConnection,
        Q_ARG(std::uint64_t, std::uint64_t{2}), Q_ARG(QString, groupId)));
    QVERIFY(tabBar->tabToolTip(0).contains(QStringLiteral("直播观察")));
    QVERIFY(tabBar->tabToolTip(1).contains(QStringLiteral("直播观察")));

    QTest::mouseClick(groupButton, Qt::LeftButton);
    QTest::mouseClick(page.findChild<QPushButton*>(
                          QStringLiteral(
                              "browserTabGroupToggleCollapsedButton")),
                      Qt::LeftButton);
    groupDialog->hide();
    QCoreApplication::processEvents();
    bool isFirstTabCollapsed = false;
    bool isSecondTabCollapsed = false;
    QVERIFY(QMetaObject::invokeMethod(
        &page, "isTabCollapsedForTest", Qt::DirectConnection,
        Q_RETURN_ARG(bool, isFirstTabCollapsed),
        Q_ARG(std::uint64_t, std::uint64_t{1})));
    QVERIFY(QMetaObject::invokeMethod(
        &page, "isTabCollapsedForTest", Qt::DirectConnection,
        Q_RETURN_ARG(bool, isSecondTabCollapsed),
        Q_ARG(std::uint64_t, std::uint64_t{2})));
    QVERIFY(isFirstTabCollapsed);
    QVERIFY(!isSecondTabCollapsed);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::SetSuspended), 0);

    page.shutdown();
    QCOMPARE(sessions.saved.groups.size(), 1);
    QVERIFY(sessions.saved.groups.constFirst().isCollapsed);
    QCOMPARE(sessions.saved.tabs.at(0).groupId,
             sessions.saved.groups.constFirst().id);
    QCOMPARE(sessions.saved.tabs.at(1).groupId,
             sessions.saved.groups.constFirst().id);

    sessions.loaded = sessions.saved;
    test::FakeBrowserBackend restoredBackend;
    BrowserPage restored(restoredBackend,
                         QStringLiteral("C:/temporary-profile-restored"),
                         nullptr, nullptr, &sessions, &startup);
    restored.show();
    QCoreApplication::processEvents();
    restoredBackend.emitReady(1);
    auto* const restoredTabBar = restored.findChild<QTabBar*>(
        QStringLiteral("browserTabBar"));
    QVERIFY(restoredTabBar != nullptr);
    QCOMPARE(restoredTabBar->count(), 2);
    QVERIFY(restoredTabBar->tabToolTip(0).contains(
        QStringLiteral("直播观察")));
    bool hasCollapsedRestoredTab = false;
    for (const std::uint64_t tabId : {std::uint64_t{1}, std::uint64_t{2}}) {
        bool isCollapsed = false;
        QVERIFY(QMetaObject::invokeMethod(
            &restored, "isTabCollapsedForTest", Qt::DirectConnection,
            Q_RETURN_ARG(bool, isCollapsed),
            Q_ARG(std::uint64_t, tabId)));
        hasCollapsedRestoredTab = hasCollapsedRestoredTab || isCollapsed;
    }
    QVERIFY(hasCollapsedRestoredTab);
    QCOMPARE(restoredBackend.count(test::FakeBrowserCommandKind::SetSuspended),
             0);
}

void BrowserPageTest::convertsNewWindowRequestsToTabsAndKeepsBlankTab() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto* const tabBar = page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    QCOMPARE(tabBar->count(), 1);
    QVERIFY(backend.emitNewTabRequested(
        QStringLiteral("https://new.example/login"), 81));
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).kind,
             test::FakeBrowserCommandKind::CreateTab);
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).requestId,
             std::uint64_t{2});
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).newWindowRequestId,
             std::uint64_t{81});
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).text,
             QStringLiteral("https://new.example/login"));
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::ActivateTab);

    QMetaObject::invokeMethod(tabBar, "tabCloseRequested", Qt::DirectConnection,
                              Q_ARG(int, 1));
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 1);
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).requestId,
             std::uint64_t{2});

    page.onFullScreenChanged(1, true);
    QVERIFY(page.isWebFullScreen());
    const auto commandCountBeforeBlank = backend.commands.size();
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("about:blank")));
    QVERIFY(!page.isWebFullScreen());
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ExitFullScreen), 1);
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(backend.commands.at(commandCountBeforeBlank).kind,
             test::FakeBrowserCommandKind::CreateTab);
    QCOMPARE(backend.commands.at(commandCountBeforeBlank).text,
             QStringLiteral("about:blank"));
    QCOMPARE(backend.commands.at(commandCountBeforeBlank + 1).kind,
             test::FakeBrowserCommandKind::ExitFullScreen);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::ActivateTab);
    QMetaObject::invokeMethod(tabBar, "tabCloseRequested", Qt::DirectConnection,
                              Q_ARG(int, 1));

    QMetaObject::invokeMethod(tabBar, "tabCloseRequested", Qt::DirectConnection,
                              Q_ARG(int, 0));
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text, QStringLiteral("https://www.bing.com/"));

    backend.canCreateTab = false;
    QVERIFY(!backend.emitNewTabRequested(
        QStringLiteral("https://rejected.example")));
    QCOMPARE(tabBar->count(), 1);
    backend.canCreateTab = true;
}

void BrowserPageTest::switchesTabsWithoutReloadAndKeepsIndependentState() {
    test::FakeBrowserBackend backend;
    MemoryBrowserDataStore dataStore;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://one.example/start"), QStringLiteral("One"),
        true, false);

    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);
    backend.emitTabNavigationStarted(2, 2);
    backend.emitTabNavigationCompleted(
        2, 2, QStringLiteral("https://two.example/page"),
        QStringLiteral("Two"), false, true);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    auto* const address =
        page.findChild<QLineEdit*>(QStringLiteral("browserAddressEdit"));
    auto* const title =
        page.findChild<QLabel*>(QStringLiteral("browserTitleLabel"));
    auto* const browserHost =
        page.findChild<QWidget*>(QStringLiteral("browserNativeHost"));
    auto* const errorLabel =
        page.findChild<QLabel*>(QStringLiteral("browserErrorLabel"));
    auto* const status =
        page.findChild<QLabel*>(QStringLiteral("browserStatusLabel"));
    QVERIFY(tabBar != nullptr);
    QVERIFY(address != nullptr);
    QVERIFY(title != nullptr);
    QVERIFY(browserHost != nullptr);
    QVERIFY(errorLabel != nullptr);
    QVERIFY(status != nullptr);
    QCOMPARE(address->text(), QStringLiteral("https://two.example/page"));
    QCOMPARE(title->text(), QStringLiteral("Two"));

    backend.emitNavigationCompleted(
        1, QStringLiteral("https://one.example/background"),
        QStringLiteral("One background"), false, true);
    QCOMPARE(address->text(), QStringLiteral("https://two.example/page"));
    QCOMPARE(title->text(), QStringLiteral("Two"));
    QCOMPARE(tabBar->tabText(0), QStringLiteral("One background"));

    backend.emitError(1, BrowserErrorKind::NavigationFailed, -1);
    QCOMPARE(address->text(), QStringLiteral("https://two.example/page"));
    QCOMPARE(title->text(), QStringLiteral("Two"));
    QCOMPARE(page.state(), BrowserPageState::Ready);

    const int navigationCount =
        backend.count(test::FakeBrowserCommandKind::Navigate);
    backend.emitPermissionRequested(50, QStringLiteral("https://two.example"),
                                    BrowserPermissionKind::Camera);
    tabBar->setCurrentIndex(0);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 1);
    QCOMPARE(backend.commands.at(backend.commands.size() - 2).requestId,
             std::uint64_t{50});
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::ActivateTab);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{1});
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Navigate),
             navigationCount);
    QCOMPARE(address->text(), QStringLiteral("https://one.example/background"));
    QCOMPARE(title->text(), QStringLiteral("One background"));
    QCOMPARE(page.state(), BrowserPageState::Failed);
    QVERIFY(errorLabel->isVisibleTo(&page));
    QVERIFY(!browserHost->isVisibleTo(&page));

    tabBar->setCurrentIndex(1);
    QCOMPARE(page.state(), BrowserPageState::Ready);
    QVERIFY(browserHost->isVisibleTo(&page));
    QVERIFY(!errorLabel->isVisibleTo(&page));
    QCOMPARE(status->text(), QStringLiteral("载入完成"));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Navigate),
             navigationCount);

    tabBar->setCurrentIndex(0);

    backend.emitTabNavigationCompleted(
        2, 2, QStringLiteral("https://two.example/background"),
        QStringLiteral("Two background"), true, true);
    QCOMPARE(address->text(), QStringLiteral("https://one.example/background"));
    QCOMPARE(tabBar->tabText(1), QStringLiteral("Two background"));
    QCOMPARE(dataStore.history.constFirst().url,
             QStringLiteral("https://two.example/background"));

    tabBar->moveTab(1, 0);
    QCOMPARE(tabBar->currentIndex(), 1);
    backend.emitTabCloseRequested(2);
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(tabBar->tabText(0), QStringLiteral("One background"));
}

void BrowserPageTest::closingBackgroundTabKeepsCurrentPageRequests() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    QCOMPARE(tabBar->currentIndex(), 1);
    backend.emitPermissionRequested(60, QStringLiteral("https://two.example"),
                                    BrowserPermissionKind::Camera);
    auto* const permissionDialog = page.findChild<BrowserPermissionDialog*>(
        QStringLiteral("browserPermissionDialog"));
    QVERIFY(permissionDialog != nullptr);
    QVERIFY(permissionDialog->isVisible());

    QMetaObject::invokeMethod(tabBar, "tabCloseRequested", Qt::DirectConnection,
                              Q_ARG(int, 0));
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(tabBar->currentIndex(), 0);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 0);
    QVERIFY(permissionDialog->isVisible());

    QTest::mouseClick(
        permissionDialog->findChild<QPushButton*>(
            QStringLiteral("browserPermissionDenyButton")),
        Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 1);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{60});
}

void BrowserPageTest::requiresConfirmationBeforeClearingBrowsingData() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto* clearButton =
        page.findChild<QPushButton*>(QStringLiteral("browserClearDataButton"));
    QVERIFY(clearButton != nullptr);
    QTest::mouseClick(clearButton, Qt::LeftButton);
    auto* dialog = page.findChild<QDialog*>(QStringLiteral("browserClearDataDialog"));
    QVERIFY(dialog != nullptr);
    QTest::mouseClick(
        dialog->findChild<QPushButton*>(QStringLiteral("browserClearDataCancelButton")),
        Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ClearBrowsingData), 0);

    QTest::mouseClick(clearButton, Qt::LeftButton);
    dialog = page.findChild<QDialog*>(QStringLiteral("browserClearDataDialog"));
    QVERIFY(dialog != nullptr);
    backend.emitPermissionRequested(100, QStringLiteral("https://old.example"),
                                    BrowserPermissionKind::Camera);
    backend.emitExternalProtocolRequested(
        101, QStringLiteral("https://old.example"),
        QStringLiteral("mailto:user@example.com"));
    backend.emitCertificateErrorRequested(
        102, QStringLiteral("https://old.example"),
        QStringLiteral("服务器证书无效"));
    backend.emitDownloadRequested(103, QStringLiteral("https://old.example"),
                                  QStringLiteral("old.bin"), 100);
    QTest::mouseClick(
        dialog->findChild<QPushButton*>(QStringLiteral("browserClearDataConfirmButton")),
        Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerExternalProtocol), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerCertificateError), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CancelDownload), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ClearBrowsingData), 1);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::ClearBrowsingData);
    QCOMPARE(backend.lastCommand().generation, std::uint64_t{2});

    backend.emitError(2, BrowserErrorKind::ClearDataFailed, -1);
    auto* errorLabel = page.findChild<QLabel*>(QStringLiteral("browserErrorLabel"));
    QVERIFY(errorLabel != nullptr);
    QVERIFY(errorLabel->isVisible());
    QVERIFY(errorLabel->text().contains(QStringLiteral("清除")));

    auto* const address =
        page.findChild<QLineEdit*>(QStringLiteral("browserAddressEdit"));
    auto* const browserHost =
        page.findChild<QWidget*>(QStringLiteral("browserNativeHost"));
    QVERIFY(address != nullptr);
    QVERIFY(browserHost != nullptr);
    address->setText(QStringLiteral("https://recovered.example"));
    QTest::keyClick(address, Qt::Key_Return);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text,
             QStringLiteral("https://recovered.example"));
    QCOMPARE(backend.lastCommand().generation, std::uint64_t{3});
    backend.emitNavigationCompleted(
        3, QStringLiteral("https://recovered.example"),
        QStringLiteral("Recovered"));
    QCOMPARE(page.state(), BrowserPageState::Ready);
    QVERIFY(browserHost->isVisibleTo(&page));
}

void BrowserPageTest::cachesFaviconsByOriginAndClearsThemWithBrowsingData() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://one.example/start"),
        QStringLiteral("One"));

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);

    const QByteArray firstIcon = makeBrowserFavicon(Qt::red);
    const QByteArray secondIcon = makeBrowserFavicon(Qt::blue);
    QVERIFY(!firstIcon.isEmpty());
    QVERIFY(!secondIcon.isEmpty());

    backend.emitTabFaviconChanged(1, 1, firstIcon);
    QVERIFY(!tabBar->tabIcon(0).isNull());

    backend.emitTabFaviconChanged(1, 1, {});
    QVERIFY(tabBar->tabIcon(0).isNull());

    backend.emitDocumentStateChanged(
        1, QStringLiteral("https://one.example/next"),
        QStringLiteral("Next"));
    QVERIFY(!tabBar->tabIcon(0).isNull());

    backend.emitDocumentStateChanged(
        1, QStringLiteral("https://two.example/home"),
        QStringLiteral("Two"));
    QVERIFY(tabBar->tabIcon(0).isNull());

    backend.emitTabFaviconChanged(1, 1, QByteArrayLiteral("unsafe-png"));
    QVERIFY(tabBar->tabIcon(0).isNull());

    backend.emitTabFaviconChanged(1, 1, secondIcon);
    QVERIFY(!tabBar->tabIcon(0).isNull());

    backend.emitDocumentStateChanged(
        1, QStringLiteral("https://one.example/again"),
        QStringLiteral("One"));
    QVERIFY(!tabBar->tabIcon(0).isNull());

    QTest::mouseClick(
        page.findChild<QPushButton*>(
            QStringLiteral("browserClearDataButton")),
        Qt::LeftButton);
    QTest::mouseClick(
        page.findChild<QPushButton*>(
            QStringLiteral("browserClearDataConfirmButton")),
        Qt::LeftButton);
    QVERIFY(tabBar->tabIcon(0).isNull());

    backend.emitBrowsingDataCleared(2);
    backend.emitTabNavigationCompleted(
        1, 2, QStringLiteral("https://one.example/after-clear"),
        QStringLiteral("One"));
    QVERIFY(tabBar->tabIcon(0).isNull());
}

void BrowserPageTest::clearCompletionShowsBlankPageOnlyForCurrentGeneration() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://signed-in.example/account"),
        QStringLiteral("账户"), true, false);

    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserClearDataButton")),
        Qt::LeftButton);
    auto* dialog = page.findChild<QDialog*>(QStringLiteral("browserClearDataDialog"));
    QTest::mouseClick(
        dialog->findChild<QPushButton*>(QStringLiteral("browserClearDataConfirmButton")),
        Qt::LeftButton);
    auto* addressEdit =
        page.findChild<QLineEdit*>(QStringLiteral("browserAddressEdit"));
    auto* titleLabel =
        page.findChild<QLabel*>(QStringLiteral("browserTitleLabel"));
    auto* statusLabel =
        page.findChild<QLabel*>(QStringLiteral("browserStatusLabel"));
    QCOMPARE(addressEdit->text(),
             QStringLiteral("https://signed-in.example/account"));

    backend.emitBrowsingDataCleared(1);
    QCOMPARE(addressEdit->text(),
             QStringLiteral("https://signed-in.example/account"));
    QCOMPARE(page.state(), BrowserPageState::ClearingData);

    backend.emitBrowsingDataCleared(2);
    QCOMPARE(page.state(), BrowserPageState::Ready);
    QVERIFY(addressEdit->text().isEmpty());
    QCOMPARE(titleLabel->text(), QStringLiteral("网页"));
    QVERIFY(statusLabel->text().contains(QStringLiteral("已清除")));
}

void BrowserPageTest::clearingProfileKeepsBrowserRecordsAndOneRealBlankTab() {
    test::FakeBrowserBackend backend;
    backend.supportsConcurrentDownloadsFlag = true;
    MemoryBrowserDataStore dataStore;
    MemoryBrowserStartupSettingsStore startupSettings;
    startupSettings.settings.homeUrl = QStringLiteral("https://home.example/");
    startupSettings.settings.startupUrls = {
        QStringLiteral("https://startup.example/")};
    startupSettings.settings.maximumTabCount = 37;
    dataStore.history = {{QStringLiteral("https://history.example"),
                          QStringLiteral("History"), 1}};
    dataStore.favorites = {{QStringLiteral("https://favorite.example"),
                            QStringLiteral("Favorite"), QStringLiteral("note")}};
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore, nullptr, &startupSettings);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitDownloadRequested(900, QStringLiteral("https://download.example"),
                                  QStringLiteral("temporary.bin"), 100);
    auto* const downloadCenter = page.findChild<BrowserDownloadCenter*>(
        QStringLiteral("browserDownloadCenter"));
    QVERIFY(downloadCenter != nullptr);
    QCOMPARE(downloadCenter->trackedItemCount(), 1);
    QVERIFY(backend.emitNewTabRequested(
        QStringLiteral("https://signed-in.example/account")));
    backend.emitTabReady(2, 2);
    page.onFullScreenChanged(2, true);
    QVERIFY(page.isWebFullScreen());

    QTest::mouseClick(
        page.findChild<QPushButton*>(QStringLiteral("browserClearDataButton")),
        Qt::LeftButton);
    auto* const dialog =
        page.findChild<QDialog*>(QStringLiteral("browserClearDataDialog"));
    auto* const explanation = page.findChild<QLabel*>(
        QStringLiteral("browserClearDataExplanation"));
    QVERIFY(dialog != nullptr);
    QVERIFY(explanation != nullptr);
    for (const QString& term : {QStringLiteral("Cookie"),
                                QStringLiteral("LocalStorage"),
                                QStringLiteral("IndexedDB"),
                                QStringLiteral("缓存"),
                                QStringLiteral("已保存密码"),
                                QStringLiteral("自动填充"),
                                QStringLiteral("Edge"),
                                QStringLiteral("历史"),
                                QStringLiteral("收藏夹")}) {
        QVERIFY2(explanation->text().contains(term), qPrintable(term));
    }
    QTest::mouseClick(
        dialog->findChild<QPushButton*>(
            QStringLiteral("browserClearDataConfirmButton")),
        Qt::LeftButton);
    QVERIFY(!page.isWebFullScreen());
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ExitFullScreen), 1);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::ClearBrowsingData);
    QCOMPARE(backend.lastCommand().generation, std::uint64_t{3});

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 1);
    const auto closeCommand = std::find_if(
        backend.commands.cbegin(), backend.commands.cend(),
        [](const test::FakeBrowserCommand& command) {
            return command.kind == test::FakeBrowserCommandKind::CloseTab;
        });
    QVERIFY(closeCommand != backend.commands.cend());
    QCOMPARE(closeCommand->requestId, std::uint64_t{1});
    QMetaObject::invokeMethod(tabBar, "tabCloseRequested", Qt::DirectConnection,
                              Q_ARG(int, 0));
    QCOMPARE(tabBar->count(), 1);
    backend.emitBrowsingDataCleared(3);
    QCOMPARE(tabBar->count(), 1);
    QCOMPARE(tabBar->tabText(0), QStringLiteral("新标签页"));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CloseTab), 1);
    QCOMPARE(dataStore.history.size(), 1);
    QCOMPARE(dataStore.favorites.size(), 1);
    QCOMPARE(dataStore.saveCount, 0);
    QCOMPARE(dataStore.favoriteSaveCount, 0);
    QCOMPARE(downloadCenter->trackedItemCount(), 0);
    QCOMPARE(startupSettings.clearCount, 0);
    QCOMPARE(startupSettings.settings.homeUrl,
             QStringLiteral("https://home.example/"));
    QCOMPARE(startupSettings.settings.startupUrls,
             QVector<QString>{QStringLiteral("https://startup.example/")});
    QCOMPARE(startupSettings.settings.maximumTabCount, 37);
}

void BrowserPageTest::routesNavigationToolbarCommands() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(1, QStringLiteral("https://example.com"), {}, true,
                                    true);

    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserBackButton")), Qt::LeftButton);
    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserForwardButton")), Qt::LeftButton);
    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserReloadButton")), Qt::LeftButton);
    QTest::mouseClick(
        page.findChild<QToolButton*>(QStringLiteral("browserHomeButton")), Qt::LeftButton);

    QVERIFY(backend.hasCommand(test::FakeBrowserCommandKind::GoBack));
    QVERIFY(backend.hasCommand(test::FakeBrowserCommandKind::GoForward));
    QVERIFY(backend.hasCommand(test::FakeBrowserCommandKind::ReloadOrStop));
    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text, QStringLiteral("https://www.bing.com/"));
}

void BrowserPageTest::routesOriginAwarePermissionChoicesAndRejectsUnsafeRequests() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));

    backend.emitPermissionRequested(7, QStringLiteral("https://camera.example:8443"),
                                    BrowserPermissionKind::Camera);
    auto* dialog = page.findChild<BrowserPermissionDialog*>(
        QStringLiteral("browserPermissionDialog"));
    QVERIFY(dialog != nullptr);
    QCOMPARE(dialog->originText(), QStringLiteral("https://camera.example:8443"));
    QCOMPARE(dialog->permissionText(), QStringLiteral("摄像头"));
    QTest::mouseClick(dialog->findChild<QPushButton*>(
                          QStringLiteral("browserPermissionAllowOnceButton")),
                      Qt::LeftButton);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{7});
    QCOMPARE(backend.lastCommand().permissionDecision,
             BrowserPermissionDecision::AllowOnce);

    backend.emitPermissionRequested(8, QStringLiteral("https://camera.example"),
                                    BrowserPermissionKind::Microphone);
    dialog = page.findChild<BrowserPermissionDialog*>(
        QStringLiteral("browserPermissionDialog"));
    QVERIFY(dialog != nullptr);
    QCOMPARE(dialog->permissionText(), QStringLiteral("麦克风"));
    QTest::mouseClick(dialog->findChild<QPushButton*>(
                          QStringLiteral("browserPermissionRememberButton")),
                      Qt::LeftButton);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{8});
    QCOMPARE(backend.lastCommand().permissionDecision,
             BrowserPermissionDecision::RememberForOrigin);

    backend.emitPermissionRequested(9, QStringLiteral("https://notify.example"),
                                    BrowserPermissionKind::Notifications);
    dialog = page.findChild<BrowserPermissionDialog*>(
        QStringLiteral("browserPermissionDialog"));
    QVERIFY(dialog != nullptr);
    dialog->reject();
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{9});
    QCOMPARE(backend.lastCommand().permissionDecision,
             BrowserPermissionDecision::Deny);

    backend.emitPermissionRequested(10, QStringLiteral("https://unknown.example"),
                                    BrowserPermissionKind::Other);
    dialog = page.findChild<BrowserPermissionDialog*>(
        QStringLiteral("browserPermissionDialog"));
    QVERIFY(dialog != nullptr);
    QVERIFY(!dialog->findChild<QPushButton*>(
                       QStringLiteral("browserPermissionAllowOnceButton"))
                 ->isEnabled());
    QVERIFY(!dialog->findChild<QPushButton*>(
                       QStringLiteral("browserPermissionRememberButton"))
                 ->isEnabled());
    QTest::mouseClick(dialog->findChild<QPushButton*>(
                          QStringLiteral("browserPermissionDenyButton")),
                      Qt::LeftButton);

    backend.emitPermissionRequested(11,
                                    QStringLiteral("https://camera.example/path?q=1"),
                                    BrowserPermissionKind::Camera);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 5);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{11});
    QCOMPARE(backend.lastCommand().permissionDecision,
             BrowserPermissionDecision::Deny);
}

void BrowserPageTest::screenCaptureAllowsOnlyCurrentRequest() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));

    backend.emitPermissionRequested(12, QStringLiteral("https://share.example"),
                                    BrowserPermissionKind::ScreenCapture);
    auto* dialog = page.findChild<BrowserPermissionDialog*>(
        QStringLiteral("browserPermissionDialog"));
    QVERIFY(dialog != nullptr);
    QCOMPARE(dialog->permissionText(), QStringLiteral("屏幕捕获"));
    QVERIFY(dialog->findChild<QPushButton*>(
                       QStringLiteral("browserPermissionAllowOnceButton"))
                ->isEnabled());
    QVERIFY(!dialog->findChild<QPushButton*>(
                        QStringLiteral("browserPermissionRememberButton"))
                 ->isEnabled());
    auto* limitation = dialog->findChild<QLabel*>(
        QStringLiteral("browserPermissionLimitationLabel"));
    QVERIFY(limitation != nullptr);
    QVERIFY(limitation->text().contains(QStringLiteral("仅本次")));

    QTest::mouseClick(dialog->findChild<QPushButton*>(
                          QStringLiteral("browserPermissionAllowOnceButton")),
                      Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 1);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{12});
    QCOMPARE(backend.lastCommand().permissionDecision,
             BrowserPermissionDecision::AllowOnce);
}

void BrowserPageTest::replacedPermissionIgnoresLateUiSignals() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));

    backend.emitPermissionRequested(60, QStringLiteral("https://old.example"),
                                    BrowserPermissionKind::Camera);
    auto* oldDialog = page.findChild<BrowserPermissionDialog*>(
        QStringLiteral("browserPermissionDialog"));
    QVERIFY(oldDialog != nullptr);
    auto* oldAllow = oldDialog->findChild<QPushButton*>(
        QStringLiteral("browserPermissionAllowOnceButton"));
    auto* oldTimeout = oldDialog->findChild<QTimer*>(
        QStringLiteral("browserPermissionTimeout"));
    QVERIFY(oldAllow != nullptr);
    QVERIFY(oldTimeout != nullptr);

    backend.emitPermissionRequested(61, QStringLiteral("https://new.example"),
                                    BrowserPermissionKind::Microphone);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 1);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{60});
    QCOMPARE(backend.lastCommand().permissionDecision,
             BrowserPermissionDecision::Deny);

    oldAllow->click();
    QVERIFY(QMetaObject::invokeMethod(oldTimeout, "timeout", Qt::DirectConnection));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 1);

    auto* currentDialog = page.findChild<BrowserPermissionDialog*>(
        QStringLiteral("browserPermissionDialog"));
    QVERIFY(currentDialog != nullptr);
    currentDialog->reject();
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 2);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{61});
    QCOMPARE(backend.lastCommand().permissionDecision,
             BrowserPermissionDecision::Deny);
}

void BrowserPageTest::confirmsExternalProtocolsAndSessionCertificateExceptions() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));

    backend.emitExternalProtocolRequested(
        20, QStringLiteral("https://origin.example:8443"),
        QStringLiteral("mailto:user@example.com"));
    auto* protocolDialog =
        page.findChild<QDialog*>(QStringLiteral("browserExternalProtocolDialog"));
    QVERIFY(protocolDialog != nullptr);
    QCOMPARE(protocolDialog
                 ->findChild<QLabel*>(
                     QStringLiteral("browserExternalProtocolOriginLabel"))
                 ->text(),
             QStringLiteral("https://origin.example:8443"));
    QVERIFY(protocolDialog->findChild<QLabel*>(
                              QStringLiteral("browserExternalProtocolTargetLabel"))
                ->text()
                .contains(QStringLiteral("mailto:user@example.com")));
    protocolDialog->reject();
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerExternalProtocol), 1);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::AnswerExternalProtocol);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{20});
    QVERIFY(!backend.lastCommand().flag);
    protocolDialog->reject();
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerExternalProtocol), 1);

    backend.emitExternalProtocolRequested(
        21, QStringLiteral("https://origin.example"),
        QStringLiteral("mailto:user@example.com"));
    protocolDialog =
        page.findChild<QDialog*>(QStringLiteral("browserExternalProtocolDialog"));
    QTest::mouseClick(protocolDialog->findChild<QPushButton*>(
                          QStringLiteral("browserExternalProtocolConfirmButton")),
                      Qt::LeftButton);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{21});
    QVERIFY(backend.lastCommand().flag);
    QTest::mouseClick(protocolDialog->findChild<QPushButton*>(
                          QStringLiteral("browserExternalProtocolConfirmButton")),
                      Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerExternalProtocol), 2);

    backend.emitExternalProtocolRequested(
        22, QStringLiteral("https://origin.example/path"),
        QStringLiteral("mailto:user@example.com"));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerExternalProtocol), 3);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{22});
    QVERIFY(!backend.lastCommand().flag);

    backend.emitCertificateErrorRequested(
        30, QStringLiteral("https://secure.example:9443"),
        QStringLiteral("服务器证书已过期"));
    auto* certificateDialog =
        page.findChild<QDialog*>(QStringLiteral("browserCertificateDialog"));
    QVERIFY(certificateDialog != nullptr);
    QVERIFY(certificateDialog->findChild<QLabel*>(
                                 QStringLiteral("browserCertificateOriginLabel"))
                ->text()
                .contains(QStringLiteral("https://secure.example:9443")));
    QTest::mouseClick(certificateDialog->findChild<QPushButton*>(
                          QStringLiteral("browserCertificateContinueButton")),
                      Qt::LeftButton);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{30});
    QCOMPARE(backend.lastCommand().certificateDecision,
             BrowserCertificateDecision::ContinueForSession);

    backend.emitCertificateErrorRequested(
        31, QStringLiteral("https://secure.example/path"),
        QStringLiteral("服务器证书无效"));
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{31});
    QCOMPARE(backend.lastCommand().certificateDecision,
             BrowserCertificateDecision::ReturnToSafety);

    backend.emitCertificateErrorRequested(
        32, QStringLiteral("https://secure.example"),
        QStringLiteral("服务器证书名称不匹配"));
    certificateDialog =
        page.findChild<QDialog*>(QStringLiteral("browserCertificateDialog"));
    QVERIFY(certificateDialog != nullptr);
    certificateDialog->reject();
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerCertificateError), 3);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{32});
    QCOMPARE(backend.lastCommand().certificateDecision,
             BrowserCertificateDecision::ReturnToSafety);
    certificateDialog->reject();
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerCertificateError), 3);
}

void BrowserPageTest::validatesDownloadDestinationAndTracksOneTask() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString existingPath = directory.filePath(QStringLiteral("existing.txt"));
    QFile existingFile(existingPath);
    QVERIFY(existingFile.open(QIODevice::WriteOnly));
    existingFile.close();

    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    backend.emitDownloadRequested(40, QStringLiteral("https://files.example"),
                                  QStringLiteral("sample.txt"), 1024);
    auto* download = page.findChild<BrowserDownloadWidget*>(
        QStringLiteral("browserDownloadWidget"));
    QVERIFY(download != nullptr);
    QCOMPARE(download->originText(), QStringLiteral("https://files.example"));
    QCOMPARE(download->fileNameText(), QStringLiteral("sample.txt"));
    QVERIFY(download->sizeText().contains(QStringLiteral("1.0 KB")));

    download->submitDestination(existingPath);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ChooseDownloadPath), 0);
    QVERIFY(download->errorText().contains(QStringLiteral("已存在")));
    download->submitDestination(directory.path());
    download->submitDestination(QStringLiteral("relative.txt"));
    download->submitDestination(
        directory.filePath(QStringLiteral("missing/target.txt")));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ChooseDownloadPath), 0);

    const QString destination = directory.filePath(QStringLiteral("new-file.txt"));
    download->submitDestination(destination);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ChooseDownloadPath), 1);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{40});
    QCOMPARE(QDir::fromNativeSeparators(backend.lastCommand().text),
             QDir::fromNativeSeparators(destination));
    download->submitDestination(directory.filePath(QStringLiteral("second.txt")));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ChooseDownloadPath), 1);

    backend.emitDownloadUpdated(40, BrowserDownloadState::InProgress, 512, 1024);
    QCOMPARE(download->progressValue(), 50);
    backend.emitDownloadUpdated(40, BrowserDownloadState::InProgress,
                                std::numeric_limits<std::int64_t>::max() / 2,
                                std::numeric_limits<std::int64_t>::max());
    QCOMPARE(download->progressValue(), 50);
    backend.emitDownloadUpdated(40, BrowserDownloadState::Completed, 1024, 1024);
    QVERIFY(download->stateText().contains(QStringLiteral("完成")));

    backend.emitDownloadRequested(41, QStringLiteral("https://files.example"),
                                  QStringLiteral("next.bin"), -1);
    download->completeDestinationSelection(QString{});
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CancelDownload), 1);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{41});
}

void BrowserPageTest::rejectsReservedDownloadNamesWithMultipleSuffixes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QStringList reservedNames{QStringLiteral("CON.foo.bar"),
                                    QStringLiteral("NUL.anything")};
    std::uint64_t requestId = 120;
    for (const QString& fileName : reservedNames) {
        test::FakeBrowserBackend backend;
        BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
        backend.emitDownloadRequested(requestId, QStringLiteral("https://files.example"),
                                      fileName, 100);
        auto* download = page.findChild<BrowserDownloadWidget*>(
            QStringLiteral("browserDownloadWidget"));
        download->submitDestination(directory.filePath(fileName));
        QCOMPARE(backend.count(test::FakeBrowserCommandKind::ChooseDownloadPath), 0);
        QVERIFY(download->errorText().contains(QStringLiteral("文件名不可接受")));
        ++requestId;
    }
}

void BrowserPageTest::downloadTerminalStatesIgnoreStaleRequests() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    auto* download = page.findChild<BrowserDownloadWidget*>(
        QStringLiteral("browserDownloadWidget"));
    QVERIFY(download != nullptr);

    backend.emitDownloadRequested(70, QStringLiteral("https://files.example"),
                                  QStringLiteral("failed.bin"), 200);
    backend.emitDownloadUpdated(70, BrowserDownloadState::Failed, 25, 200);
    QVERIFY(download->isTerminal());
    QVERIFY(download->stateText().contains(QStringLiteral("失败")));

    backend.emitDownloadRequested(71, QStringLiteral("https://files.example"),
                                  QStringLiteral("cancelled.bin"), 300);
    QVERIFY(!download->isTerminal());
    const QString waitingState = download->stateText();
    backend.emitDownloadUpdated(70, BrowserDownloadState::Cancelled, 25, 200);
    QCOMPARE(download->requestId(), std::uint64_t{71});
    QCOMPARE(download->stateText(), waitingState);

    backend.emitDownloadUpdated(71, BrowserDownloadState::Cancelled, 30, 300);
    QVERIFY(download->isTerminal());
    QVERIFY(download->stateText().contains(QStringLiteral("取消")));
}

void BrowserPageTest::pendingDownloadCancelWaitsForBackendTerminalState() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    auto* download = page.findChild<BrowserDownloadWidget*>(
        QStringLiteral("browserDownloadWidget"));
    QVERIFY(download != nullptr);

    backend.emitDownloadRequested(90, QStringLiteral("https://files.example"),
                                  QStringLiteral("active.bin"), 300);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ChooseDownloadPath), 0);
    QTest::mouseClick(download->findChild<QPushButton*>(
                          QStringLiteral("browserDownloadCancelButton")),
                      Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CancelDownload), 1);
    QVERIFY(!download->isTerminal());
    QVERIFY(download->stateText().contains(QStringLiteral("正在取消")));

    backend.emitDownloadRequested(91, QStringLiteral("https://files.example"),
                                  QStringLiteral("rejected.bin"), 400);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CancelDownload), 2);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{91});
    QCOMPARE(download->requestId(), std::uint64_t{90});

    backend.emitDownloadUpdated(90, BrowserDownloadState::Cancelled, 20, 300);
    QVERIFY(download->isTerminal());
    backend.emitDownloadRequested(92, QStringLiteral("https://files.example"),
                                  QStringLiteral("accepted.bin"), 500);
    QCOMPARE(download->requestId(), std::uint64_t{92});
    QVERIFY(!download->isTerminal());
}

void BrowserPageTest::cancelFailureAllowsRetryWithoutEndingDownload() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    auto* download = page.findChild<BrowserDownloadWidget*>(
        QStringLiteral("browserDownloadWidget"));
    auto* cancelButton = download->findChild<QPushButton*>(
        QStringLiteral("browserDownloadCancelButton"));
    QVERIFY(download != nullptr);
    QVERIFY(cancelButton != nullptr);

    backend.emitDownloadRequested(100, QStringLiteral("https://files.example"),
                                  QStringLiteral("retry.bin"), 600);
    QTest::mouseClick(cancelButton, Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CancelDownload), 1);

    backend.emitDownloadUpdated(100, BrowserDownloadState::CancelFailed, 40, 600);
    QVERIFY(!download->isTerminal());
    QVERIFY(download->stateText().contains(QStringLiteral("取消失败，可重试")));
    QVERIFY(cancelButton->isEnabled());

    QTest::mouseClick(cancelButton, Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CancelDownload), 2);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{100});
    QVERIFY(!download->isTerminal());
}

void BrowserPageTest::navigationRejectsUnansweredSensitiveRequests() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    backend.emitPermissionRequested(50, QStringLiteral("https://old.example"),
                                    BrowserPermissionKind::Camera);
    backend.emitExternalProtocolRequested(
        51, QStringLiteral("https://old.example"),
        QStringLiteral("mailto:user@example.com"));
    backend.emitCertificateErrorRequested(
        52, QStringLiteral("https://old.example"),
        QStringLiteral("服务器证书无效"));
    backend.emitDownloadRequested(53, QStringLiteral("https://old.example"),
                                  QStringLiteral("old.bin"), 100);

    auto* addressEdit =
        page.findChild<QLineEdit*>(QStringLiteral("browserAddressEdit"));
    addressEdit->setText(QStringLiteral("new.example"));
    QTest::keyClick(addressEdit, Qt::Key_Return);

    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerPermission), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerExternalProtocol), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::AnswerCertificateError), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CancelDownload), 1);
    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Navigate);
}

void BrowserPageTest::navigationKeepsStartedDownload() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    backend.emitDownloadRequested(80, QStringLiteral("https://files.example"),
                                  QStringLiteral("active.bin"), 500);
    auto* download = page.findChild<BrowserDownloadWidget*>(
        QStringLiteral("browserDownloadWidget"));
    QVERIFY(download != nullptr);
    download->submitDestination(
        directory.filePath(QStringLiteral("active.bin")));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ChooseDownloadPath), 1);

    auto* addressEdit =
        page.findChild<QLineEdit*>(QStringLiteral("browserAddressEdit"));
    addressEdit->setText(QStringLiteral("next.example"));
    QTest::keyClick(addressEdit, Qt::Key_Return);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CancelDownload), 0);
    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Navigate);
}

void BrowserPageTest::managesConcurrentDownloadsIndependently() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    test::FakeBrowserBackend backend;
    backend.supportsConcurrentDownloadsFlag = true;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    backend.emitDownloadRequested(81, QStringLiteral("https://one.example"),
                                  QStringLiteral("one.bin"), 100);
    backend.emitDownloadRequested(82, QStringLiteral("https://two.example"),
                                  QStringLiteral("two.bin"), 200);
    auto* const center = page.findChild<BrowserDownloadCenter*>(
        QStringLiteral("browserDownloadCenter"));
    QVERIFY(center != nullptr);
    QCOMPARE(center->activeItemCount(), 2);
    QCOMPARE(center->itemSnapshot(81)->state,
             BrowserDownloadCenter::ItemState::WaitingForDestination);
    QCOMPARE(center->itemSnapshot(82)->state,
             BrowserDownloadCenter::ItemState::WaitingForDestination);

    QVERIFY(center->submitDestination(
        81, directory.filePath(QStringLiteral("one.bin"))));
    QVERIFY(center->submitDestination(
        82, directory.filePath(QStringLiteral("two.bin"))));
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ChooseDownloadPath), 2);

    backend.emitDownloadUpdated(81, BrowserDownloadState::InProgress, 40, 100);
    backend.emitDownloadUpdated(82, BrowserDownloadState::InProgress, 150, 200);
    QCOMPARE(center->itemSnapshot(81)->progressValue, 40);
    QCOMPARE(center->itemSnapshot(82)->progressValue, 75);

    QVERIFY(center->requestCancel(81));
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::CancelDownload);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{81});
    backend.emitDownloadUpdated(82, BrowserDownloadState::Completed, 200, 200);
    QCOMPARE(center->itemSnapshot(81)->state,
             BrowserDownloadCenter::ItemState::Cancelling);
    QCOMPARE(center->activeItemCount(), 1);
    backend.emitDownloadUpdated(81, BrowserDownloadState::Cancelled, 40, 100);

    QCOMPARE(center->itemSnapshot(81)->state,
             BrowserDownloadCenter::ItemState::Cancelled);
    QCOMPARE(center->itemSnapshot(82)->state,
             BrowserDownloadCenter::ItemState::Completed);
    QCOMPARE(center->activeItemCount(), 0);
}

void BrowserPageTest::routesInterruptedDownloadRetryToBackend() {
    test::FakeBrowserBackend backend(true);
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitTabDownloadRequested(
        1, 302, QStringLiteral("https://files.example"),
        QStringLiteral("resume.bin"), 100);

    auto* const center = page.findChild<BrowserDownloadCenter*>(
        QStringLiteral("browserDownloadCenter"));
    QVERIFY(center != nullptr);
    backend.emitTabDownloadUpdated(
        1, 302, BrowserDownloadState::RetryableFailure, 40, 100);
    QCOMPARE(center->itemSnapshot(302)->state,
             BrowserDownloadCenter::ItemState::RetryableFailure);
    QCOMPARE(center->activeItemCount(), 1);

    QVERIFY(center->requestRetry(302));
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::RetryDownload);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{302});
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::RetryDownload), 1);
}

void BrowserPageTest::hidingPageKeepsConcurrentDownloadProgress() {
    test::FakeBrowserBackend backend;
    backend.supportsConcurrentDownloadsFlag = true;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitDownloadRequested(83, QStringLiteral("https://files.example"),
                                  QStringLiteral("hidden.bin"), 100);
    auto* const center = page.findChild<BrowserDownloadCenter*>(
        QStringLiteral("browserDownloadCenter"));
    QVERIFY(center != nullptr);

    page.deactivate();
    page.hide();
    backend.emitDownloadUpdated(83, BrowserDownloadState::InProgress, 60, 100);

    QCOMPARE(backend.count(test::FakeBrowserCommandKind::CancelDownload), 0);
    QCOMPARE(center->activeItemCount(), 1);
    QCOMPARE(center->itemSnapshot(83)->progressValue, 60);
}

void BrowserPageTest::webFullScreenDefersConcurrentDownloadCenterPresentation() {
    test::FakeBrowserBackend backend;
    backend.supportsConcurrentDownloadsFlag = true;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.onFullScreenChanged(1, true);

    auto* const center = page.findChild<BrowserDownloadCenter*>(
        QStringLiteral("browserDownloadCenter"));
    QVERIFY(center != nullptr);
    backend.emitDownloadRequested(86, QStringLiteral("https://one.example"),
                                  QStringLiteral("one.bin"), 100);
    backend.emitDownloadRequested(87, QStringLiteral("https://two.example"),
                                  QStringLiteral("two.bin"), 200);

    QCOMPARE(center->activeItemCount(), 2);
    QVERIFY(center->isHidden());

    page.onFullScreenChanged(1, false);

    QVERIFY(!center->isHidden());
    QCOMPARE(center->activeItemCount(), 2);
}

void BrowserPageTest::shutdownCancelsEveryConcurrentDownload() {
    test::FakeBrowserBackend backend;
    backend.supportsConcurrentDownloadsFlag = true;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitDownloadRequested(84, QStringLiteral("https://one.example"),
                                  QStringLiteral("one.bin"), 100);
    backend.emitDownloadRequested(85, QStringLiteral("https://two.example"),
                                  QStringLiteral("two.bin"), 100);
    backend.commands.clear();

    page.shutdown();

    int firstCancellationCount = 0;
    int secondCancellationCount = 0;
    for (const test::FakeBrowserCommand& command : backend.commands) {
        if (command.kind != test::FakeBrowserCommandKind::CancelDownload) {
            continue;
        }
        firstCancellationCount += command.requestId == 84 ? 1 : 0;
        secondCancellationCount += command.requestId == 85 ? 1 : 0;
    }
    QCOMPARE(firstCancellationCount, 1);
    QCOMPARE(secondCancellationCount, 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Shutdown), 1);
}

void BrowserPageTest::deactivatesAndActivatesBrowserInSafeOrder() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    backend.commands.clear();

    page.deactivate();
    page.activate();

    const std::vector<test::FakeBrowserCommandKind> expected{
        test::FakeBrowserCommandKind::SetVisible,
        test::FakeBrowserCommandKind::SetVisible,
    };
    QCOMPARE(backend.commands.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        QCOMPARE(backend.commands[index].kind, expected[index]);
    }
    QVERIFY(!backend.commands[0].flag);
    QVERIFY(backend.commands[1].flag);
}

void BrowserPageTest::tracksIndependentTabAudioAndMuteState() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    QSignalSpy audibleCountSpy(&page, &BrowserPage::audibleTabCountChanged);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    backend.emitTabAudioStateChanged(1, 1, true);
    QCOMPARE(audibleCountSpy.count(), 1);
    QCOMPARE(audibleCountSpy.takeFirst().at(0).toInt(), 1);
    auto* const muteButton = page.findChild<QToolButton*>(
        QStringLiteral("browserCurrentTabMuteButton"));
    auto* const audioTabsButton = page.findChild<QToolButton*>(
        QStringLiteral("browserAudioTabsButton"));
    QVERIFY(muteButton != nullptr);
    QVERIFY(audioTabsButton != nullptr);
    QVERIFY(audioTabsButton->text().contains(QStringLiteral("1")));

    QTest::mouseClick(muteButton, Qt::LeftButton);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::SetTabAudioMuted);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{1});
    QVERIFY(backend.lastCommand().flag);
    QCOMPARE(audibleCountSpy.takeFirst().at(0).toInt(), 0);

    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);
    backend.emitTabAudioStateChanged(2, 2, true);
    QCOMPARE(audibleCountSpy.takeLast().at(0).toInt(), 1);
    QTest::mouseClick(muteButton, Qt::LeftButton);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{2});
    QVERIFY(backend.lastCommand().flag);
    QCOMPARE(audibleCountSpy.takeLast().at(0).toInt(), 0);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    tabBar->setCurrentIndex(0);
    QTest::mouseClick(muteButton, Qt::LeftButton);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{1});
    QVERIFY(!backend.lastCommand().flag);
    QCOMPARE(audibleCountSpy.takeLast().at(0).toInt(), 1);
}

void BrowserPageTest::updatesOnlyChangedAudioRowAndSkipsHiddenRebuilds() {
    test::FakeBrowserBackend backend;
    MemoryBrowserStartupSettingsStore startupSettings;
    startupSettings.settings.maximumTabCount = 100;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     nullptr, nullptr, &startupSettings);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    backend.emitTabAudioStateChanged(1, 1, true);
    for (std::uint64_t tabId = 2; tabId <= 100; ++tabId) {
        QVERIFY(backend.emitNewTabRequested(
            QStringLiteral("https://audio-%1.example").arg(tabId)));
        backend.emitTabAudioStateChanged(tabId, tabId, true);
    }

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    auto* const muteButton = page.findChild<QToolButton*>(
        QStringLiteral("browserCurrentTabMuteButton"));
    auto* const audioTabsButton = page.findChild<QToolButton*>(
        QStringLiteral("browserAudioTabsButton"));
    QVERIFY(tabBar != nullptr);
    QVERIFY(muteButton != nullptr);
    QVERIFY(audioTabsButton != nullptr);

    QTest::mouseClick(audioTabsButton, Qt::LeftButton);
    auto* const audioDialog = page.findChild<QDialog*>(
        QStringLiteral("browserAudioTabsDialog"));
    auto* const audioList = page.findChild<QListWidget*>(
        QStringLiteral("browserAudioTabsList"));
    auto* const audioMuteButton = page.findChild<QPushButton*>(
        QStringLiteral("browserAudioTabMuteButton"));
    QVERIFY(audioDialog != nullptr);
    QVERIFY(audioList != nullptr);
    QVERIFY(audioMuteButton != nullptr);
    QCOMPARE(audioList->count(), 100);

    QSignalSpy rowsInserted(audioList->model(),
                            &QAbstractItemModel::rowsInserted);
    QSignalSpy rowsRemoved(audioList->model(),
                           &QAbstractItemModel::rowsRemoved);
    QSignalSpy modelReset(audioList->model(), &QAbstractItemModel::modelReset);
    QSignalSpy dataChanged(audioList->model(), &QAbstractItemModel::dataChanged);

    constexpr std::uint64_t targetTabId = 50;
    audioDialog->hide();
    tabBar->setCurrentIndex(static_cast<int>(targetTabId - 1));
    const int hiddenMuteCount =
        backend.count(test::FakeBrowserCommandKind::SetTabAudioMuted);
    QTest::mouseClick(muteButton, Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::SetTabAudioMuted),
             hiddenMuteCount + 1);
    QCOMPARE(backend.lastCommand().requestId, targetTabId);
    QVERIFY(backend.lastCommand().flag);
    QCOMPARE(rowsInserted.count(), 0);
    QCOMPARE(rowsRemoved.count(), 0);
    QCOMPARE(modelReset.count(), 0);
    QCOMPARE(dataChanged.count(), 0);

    QTest::mouseClick(audioTabsButton, Qt::LeftButton);
    QCOMPARE(audioList->count(), 100);
    int targetRow = -1;
    for (int row = 0; row < audioList->count(); ++row) {
        if (audioList->item(row)->data(Qt::UserRole).toULongLong() ==
            targetTabId) {
            targetRow = row;
            break;
        }
    }
    QVERIFY(targetRow >= 0);
    audioList->setCurrentRow(targetRow);
    QVERIFY(audioList->item(targetRow)->text().startsWith(
        QStringLiteral("[已静音]")));

    QVector<QString> textBefore;
    textBefore.reserve(audioList->count());
    for (int row = 0; row < audioList->count(); ++row) {
        textBefore.append(audioList->item(row)->text());
    }
    rowsInserted.clear();
    rowsRemoved.clear();
    modelReset.clear();
    dataChanged.clear();
    const int visibleMuteCount =
        backend.count(test::FakeBrowserCommandKind::SetTabAudioMuted);
    QTest::mouseClick(audioMuteButton, Qt::LeftButton);

    QCOMPARE(backend.count(test::FakeBrowserCommandKind::SetTabAudioMuted),
             visibleMuteCount + 1);
    QCOMPARE(backend.lastCommand().requestId, targetTabId);
    QVERIFY(!backend.lastCommand().flag);
    QCOMPARE(rowsInserted.count(), 0);
    QCOMPARE(rowsRemoved.count(), 0);
    QCOMPARE(modelReset.count(), 0);
    QCOMPARE(dataChanged.count(), 1);
    int changedRowCount = 0;
    for (int row = 0; row < audioList->count(); ++row) {
        if (audioList->item(row)->text() != textBefore.at(row)) {
            ++changedRowCount;
            QCOMPARE(row, targetRow);
        }
    }
    QCOMPARE(changedRowCount, 1);
    QVERIFY(audioList->item(targetRow)->text().startsWith(
        QStringLiteral("[正在出声]")));
}

void BrowserPageTest::audioCenterTargetsStableTabAfterManualReorder() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://one.example"), QStringLiteral("One"),
        false, false);
    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://two.example")));
    backend.emitTabReady(2, 2);
    backend.emitTabNavigationCompleted(
        2, 2, QStringLiteral("https://two.example"), QStringLiteral("Two"),
        false, false);
    backend.emitTabAudioStateChanged(1, 1, true);
    backend.emitTabAudioStateChanged(2, 2, true);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    auto* const audioTabsButton = page.findChild<QToolButton*>(
        QStringLiteral("browserAudioTabsButton"));
    QVERIFY(tabBar != nullptr);
    QVERIFY(audioTabsButton != nullptr);
    tabBar->moveTab(0, 1);
    QCOMPARE(tabBar->tabData(0).toULongLong(), qulonglong{2});

    QTest::mouseClick(audioTabsButton, Qt::LeftButton);
    auto* const audioList = page.findChild<QListWidget*>(
        QStringLiteral("browserAudioTabsList"));
    auto* const switchButton = page.findChild<QPushButton*>(
        QStringLiteral("browserAudioTabSwitchButton"));
    QVERIFY(audioList != nullptr);
    QVERIFY(switchButton != nullptr);
    QCOMPARE(audioList->count(), 2);
    for (int row = 0; row < audioList->count(); ++row) {
        if (audioList->item(row)->data(Qt::UserRole).toULongLong() ==
            qulonglong{1}) {
            audioList->setCurrentRow(row);
            break;
        }
    }
    QCOMPARE(audioList->currentItem()->data(Qt::UserRole).toULongLong(),
             qulonglong{1});
    QTest::mouseClick(switchButton, Qt::LeftButton);

    QCOMPARE(tabBar->tabData(tabBar->currentIndex()).toULongLong(),
             qulonglong{1});
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::ActivateTab);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{1});
}

void BrowserPageTest::escapeExitsWebFullScreenFirst() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    auto* toolbar = page.findChild<QWidget*>(QStringLiteral("browserToolbar"));
    QVERIFY(toolbar != nullptr);
    page.onFullScreenChanged(1, true);

    QVERIFY(page.isWebFullScreen());
    QVERIFY(toolbar->isHidden());
    auto* download = page.findChild<BrowserDownloadWidget*>(
        QStringLiteral("browserDownloadWidget"));
    QVERIFY(download != nullptr);
    page.onDownloadRequested(90, QStringLiteral("https://login.example"),
                             QStringLiteral("result.bin"), -1);
    QVERIFY(download->isHidden());
    QTest::keyClick(&page, Qt::Key_Escape);

    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ExitFullScreen), 1);

    page.onFullScreenChanged(1, false);
    QVERIFY(!toolbar->isHidden());
    QVERIFY(!download->isHidden());
}

void BrowserPageTest::mainWindowPrioritizesWebFullScreenAndRestoresChrome() {
    test::FakeBrowserBackend backend;
    MainWindow window(&backend, QStringLiteral("C:/temporary-profile"));
    window.show();
    QCoreApplication::processEvents();
    window.showDisplayMode(DisplayMode::Web);

    auto* page = window.findChild<BrowserPage*>(QStringLiteral("browserPage"));
    auto* modePanel =
        window.findChild<QWidget*>(QStringLiteral("displayModePanel"));
    auto* headerPanel = window.findChild<QWidget*>(QStringLiteral("headerPanel"));
    auto* playlistPanel =
        window.findChild<QWidget*>(QStringLiteral("playlistPanel"));
    QVERIFY(page != nullptr);
    QVERIFY(modePanel != nullptr);
    QVERIFY(headerPanel != nullptr);
    QVERIFY(playlistPanel != nullptr);

    page->onFullScreenChanged(1, true);

    QVERIFY(window.isFullScreen());
    QVERIFY(modePanel->isHidden());
    QVERIFY(headerPanel->isHidden());
    QVERIFY(playlistPanel->isHidden());
    QTest::keyClick(&window, Qt::Key_Escape);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ExitFullScreen), 1);

    page->onFullScreenChanged(1, false);
    QVERIFY(!window.isFullScreen());
    QVERIFY(!modePanel->isHidden());
    QVERIFY(!headerPanel->isHidden());
    QVERIFY(!playlistPanel->isHidden());
}

void BrowserPageTest::shutdownDetachesListenerBeforeBackend() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));

    page.shutdown();

    QVERIFY(backend.commands.size() >= 3);
    const auto commandCount = backend.commands.size();
    QCOMPARE(backend.commands[commandCount - 2].kind,
             test::FakeBrowserCommandKind::SetEventListener);
    QCOMPARE(backend.commands[commandCount - 1].kind,
             test::FakeBrowserCommandKind::Shutdown);
}

void BrowserPageTest::detachesListenerAndShutsDownOnDestruction() {
    test::FakeBrowserBackend backend;
    {
        BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    }

    QCOMPARE(backend.count(test::FakeBrowserCommandKind::SetEventListener), 2);
    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Shutdown);
}

void BrowserPageTest::appliesResponsiveSizeAcrossToolbarBreakpoints() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.setStyleSheet(mainWindowStyleSheet());
    page.show();
    QCoreApplication::processEvents();
    auto *const toolbar = page.findChild<QWidget *>(QStringLiteral("browserToolbar"));
    auto *const address = page.findChild<QLineEdit *>(QStringLiteral("browserAddressEdit"));
    QVERIFY(toolbar != nullptr);
    QVERIFY(address != nullptr);
    QCOMPARE(address->minimumWidth(), 180);
    const QStringList controlNames{
        QStringLiteral("browserBackButton"),
        QStringLiteral("browserForwardButton"),
        QStringLiteral("browserReloadButton"),
        QStringLiteral("browserHomeButton"),
        QStringLiteral("browserAddressEdit"),
        QStringLiteral("browserGoButton"),
        QStringLiteral("browserHistoryButton"),
        QStringLiteral("browserFavoritesButton"),
        QStringLiteral("browserClearDataButton"),
    };

    struct Breakpoint {
        QSize size;
        QString key;
        int addressFontPixels;
    };
    const QList<Breakpoint> breakpoints{
        {QSize(800, 600), QStringLiteral("compact"), 12},
        {QSize(960, 640), QStringLiteral("normal"), 13},
        {QSize(1200, 800), QStringLiteral("large"), 15},
        {QSize(1600, 1000), QStringLiteral("extraLarge"), 17},
    };
    for (const auto &entry : breakpoints) {
        page.resize(entry.size);
        QCoreApplication::processEvents();
        QCOMPARE(page.size(), entry.size);
        QCOMPARE(page.property("responsiveSize").toString(), entry.key);
        QCOMPARE(address->font().pixelSize(), entry.addressFontPixels);
        QVERIFY(page.rect().contains(toolbar->geometry()));
        for (const QString &objectName : controlNames) {
            QWidget *const control = page.findChild<QWidget *>(objectName);
            QVERIFY2(control != nullptr, qPrintable(objectName));
            QVERIFY2(control->isVisible(), qPrintable(objectName));
            QVERIFY2(!control->geometry().isEmpty(), qPrintable(objectName));
            const QRect toolbarGeometry(control->mapTo(toolbar, QPoint{}),
                                        control->size());
            QVERIFY2(toolbar->rect().contains(toolbarGeometry),
                     qPrintable(objectName));
            if (control == address) {
                QVERIFY(address->width() >= address->minimumWidth());
            } else {
                QVERIFY2(control->width() >= control->sizeHint().width(),
                         qPrintable(objectName));
            }
        }
    }
}

void BrowserPageTest::routesBrowserHistoryShortcuts() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();

    auto *const address = page.findChild<QLineEdit *>(QStringLiteral("browserAddressEdit"));
    QVERIFY(address != nullptr);
    address->setFocus();
    QTest::keyClick(address, Qt::Key_Left, Qt::AltModifier);
    QTest::keyClick(address, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::GoBack), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::GoForward), 1);
}

void BrowserPageTest::routesWebShortcutsWithoutNativeConflicts() {
    test::FakeBrowserBackend backend;
    MainWindow window(&backend, QStringLiteral("C:/temporary-profile"));
    window.show();
    window.showDisplayMode(DisplayMode::Web);
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto *const address = window.findChild<QLineEdit *>(
        QStringLiteral("browserAddressEdit"));
    QVERIFY(address != nullptr);
    address->setText(QStringLiteral("https://example.com/path"));
    address->setCursorPosition(0);
    QTest::keyClick(address, Qt::Key_L, Qt::ControlModifier);
    QCOMPARE(address->selectedText(), address->text());

    QTest::keyClick(address, Qt::Key_R, Qt::ControlModifier);
    QTest::keyClick(address, Qt::Key_F5);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ReloadOrStop), 2);
}

void BrowserPageTest::routesNativeAcceleratorsThroughCurrentGeneration() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(1, QStringLiteral("https://example.com"),
                                    QStringLiteral("Example"), true, true);

    auto *const address = page.findChild<QLineEdit *>(
        QStringLiteral("browserAddressEdit"));
    QVERIFY(address != nullptr);
    address->setText(QStringLiteral("https://example.com/path"));
    address->setCursorPosition(0);
    backend.emitAcceleratorRequested(2, BrowserAccelerator::FocusAddress);
    QVERIFY(address->selectedText().isEmpty());
    backend.emitAcceleratorRequested(1, BrowserAccelerator::FocusAddress);
    QCOMPARE(address->selectedText(), address->text());

    backend.emitAcceleratorRequested(1, BrowserAccelerator::Back);
    backend.emitAcceleratorRequested(1, BrowserAccelerator::Forward);
    backend.emitAcceleratorRequested(1, BrowserAccelerator::Reload);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::GoBack), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::GoForward), 1);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ReloadOrStop), 1);

    const int zoomCommandsBefore =
        backend.count(test::FakeBrowserCommandKind::SetTabZoomFactor);
    backend.emitAcceleratorRequested(1, BrowserAccelerator::ZoomIn);
    backend.emitAcceleratorRequested(1, BrowserAccelerator::ZoomOut);
    backend.emitAcceleratorRequested(1, BrowserAccelerator::ResetZoom);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::SetTabZoomFactor),
             zoomCommandsBefore + 3);
    QVERIFY(backend.commands.size() >= 3);
    const auto zoomCommandStart = backend.commands.end() - 3;
    QCOMPARE(zoomCommandStart[0].kind,
             test::FakeBrowserCommandKind::SetTabZoomFactor);
    QCOMPARE(zoomCommandStart[0].requestId, std::uint64_t{1});
    QVERIFY(qFuzzyCompare(zoomCommandStart[0].number, 1.1));
    QCOMPARE(zoomCommandStart[1].kind,
             test::FakeBrowserCommandKind::SetTabZoomFactor);
    QCOMPARE(zoomCommandStart[1].requestId, std::uint64_t{1});
    QVERIFY(qFuzzyCompare(zoomCommandStart[1].number, 1.0));
    QCOMPARE(zoomCommandStart[2].kind,
             test::FakeBrowserCommandKind::SetTabZoomFactor);
    QCOMPARE(zoomCommandStart[2].requestId, std::uint64_t{1});
    QVERIFY(qFuzzyCompare(zoomCommandStart[2].number, 1.0));

    backend.emitAcceleratorRequested(1, BrowserAccelerator::ExitFullScreen);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ExitFullScreen), 0);
    page.onFullScreenChanged(1, true);
    backend.emitAcceleratorRequested(1, BrowserAccelerator::ExitFullScreen);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ExitFullScreen), 1);
}

void BrowserPageTest::reopensClosedTabsWithoutLosingFailedRestore() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(1, QStringLiteral("https://first.example/"),
                                    QStringLiteral("First"));

    QVERIFY(backend.emitNewTabRequested(QStringLiteral("https://second.example/")));
    backend.emitTabReady(2, 2);
    backend.emitTabNavigationCompleted(2, 2,
                                       QStringLiteral("https://second.example/"),
                                       QStringLiteral("Second"));
    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    QCOMPARE(tabBar->count(), 2);
    tabBar->setCurrentIndex(1);
    QTest::keyClick(&page, Qt::Key_W, Qt::ControlModifier);
    QCOMPARE(tabBar->count(), 1);

    backend.canCreateTab = false;
    QTest::keyClick(&page, Qt::Key_T,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(tabBar->count(), 1);
    backend.canCreateTab = true;
    QTest::keyClick(&page, Qt::Key_T,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::SetTabZoomFactor);

    QTest::keyClick(&page, Qt::Key_W, Qt::ControlModifier);
    QTest::keyClick(&page, Qt::Key_T,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(tabBar->count(), 2);
}

void BrowserPageTest::restoresStartupSessionAndSavesItBeforeShutdown() {
    test::FakeBrowserBackend backend;
    MemoryBrowserSessionStore sessions;
    sessions.loaded = BrowserSessionState{
        {{QStringLiteral("https://one.example/path"), QStringLiteral("One"),
          QStringLiteral("work"), true, true, 1.25},
         {QStringLiteral("https://two.example/path"), QStringLiteral("Two"),
          {}, false, false, 0.8}},
        {{QStringLiteral("https://closed.example/path"),
          QStringLiteral("Closed"), {}, false, true, 1.1}},
        1,
        {{QStringLiteral("work"), QStringLiteral("工作"),
          QStringLiteral("#3d8f72"), false}}};
    MemoryBrowserStartupSettingsStore startup;
    startup.settings.mode = BrowserStartupMode::RestoreSession;

    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     nullptr, &sessions, &startup);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(tabBar->currentIndex(), 1);
    QVERIFY(backend.hasFlagCommand(
        test::FakeBrowserCommandKind::SetTabAudioMuted, true));

    page.shutdown();
    QCOMPARE(sessions.saveCount, 1);
    QCOMPARE(sessions.saved.tabs.size(), 2);
    QCOMPARE(sessions.saved.currentIndex, 1);
    QCOMPARE(sessions.saved.closedTabs.size(), 1);
    QCOMPARE(sessions.saved.tabs.at(0).groupId, QStringLiteral("work"));
    QCOMPARE(sessions.saved.tabs.at(0).zoomFactor, 1.25);
}

void BrowserPageTest::periodicallyCheckpointsBrowserSession() {
    test::FakeBrowserBackend backend;
    MemoryBrowserSessionStore sessions;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     nullptr, &sessions);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    backend.emitNavigationCompleted(
        1, QStringLiteral("https://checkpoint.example/page"),
        QStringLiteral("Checkpoint"));

    auto* const checkpointTimer = page.findChild<QTimer*>(
        QStringLiteral("browserSessionCheckpointTimer"));
    QVERIFY(checkpointTimer != nullptr);
    QVERIFY(checkpointTimer->isActive());
    QCOMPARE(checkpointTimer->interval(), 30000);
    QCOMPARE(sessions.saveCount, 0);

    QVERIFY(QMetaObject::invokeMethod(checkpointTimer, "timeout",
                                      Qt::DirectConnection));
    QCOMPARE(sessions.saveCount, 1);
    QCOMPARE(sessions.saved.tabs.size(), 1);
    QCOMPARE(sessions.saved.tabs.constFirst().url,
             QStringLiteral("https://checkpoint.example/page"));
}

void BrowserPageTest::restoresCurrentPinnedTabByStableIdentity() {
    test::FakeBrowserBackend backend;
    MemoryBrowserSessionStore sessions;
    sessions.loaded = BrowserSessionState{
        {{QStringLiteral("https://current.example/"),
          QStringLiteral("Current"), {}, false, false, 1.0},
         {QStringLiteral("https://pinned.example/"),
          QStringLiteral("Pinned"), {}, true, false, 1.0}},
        {}, 0};
    MemoryBrowserStartupSettingsStore startup;
    startup.settings.mode = BrowserStartupMode::RestoreSession;

    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     nullptr, &sessions, &startup);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(tabBar->tabText(0), QStringLiteral("Pinned"));
    QCOMPARE(tabBar->tabText(tabBar->currentIndex()),
             QStringLiteral("Current"));
    QCOMPARE(tabBar->tabData(tabBar->currentIndex()).toULongLong(),
             qulonglong{1});
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::ActivateTab);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{1});
}

void BrowserPageTest::restoresMixedPinnedOrderByStableIdentity() {
    test::FakeBrowserBackend backend;
    MemoryBrowserSessionStore sessions;
    sessions.loaded = BrowserSessionState{
        {{QStringLiteral("https://first.example/"), QStringLiteral("First"),
          {}, false, false, 1.0},
         {QStringLiteral("https://pinned.example/"), QStringLiteral("Pinned"),
          {}, true, false, 1.0},
         {QStringLiteral("https://current.example/"),
          QStringLiteral("Current"), {}, false, false, 1.0}},
        {}, 2};
    MemoryBrowserStartupSettingsStore startup;
    startup.settings.mode = BrowserStartupMode::RestoreSession;

    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     nullptr, &sessions, &startup);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);

    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QVERIFY(tabBar != nullptr);
    QCOMPARE(tabBar->count(), 3);
    QCOMPARE(tabBar->tabText(0), QStringLiteral("Pinned"));
    QCOMPARE(tabBar->tabData(0).toULongLong(), qulonglong{2});
    QCOMPARE(tabBar->tabText(tabBar->currentIndex()),
             QStringLiteral("Current"));
    QCOMPARE(tabBar->tabData(tabBar->currentIndex()).toULongLong(),
             qulonglong{3});
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::ActivateTab);
    QCOMPARE(backend.lastCommand().requestId, std::uint64_t{3});
}

void BrowserPageTest::opensConfiguredStartupPagesAndHomepage() {
    test::FakeBrowserBackend backend;
    MemoryBrowserStartupSettingsStore startup;
    startup.settings.homeUrl = QStringLiteral("https://home.example/path");
    startup.settings.mode = BrowserStartupMode::OpenStartupPages;
    startup.settings.startupUrls = {QStringLiteral("https://one.example/"),
                                    QStringLiteral("https://two.example/")};

    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     nullptr, nullptr, &startup);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
    auto* const tabBar =
        page.findChild<QTabBar*>(QStringLiteral("browserTabBar"));
    QCOMPARE(tabBar->count(), 2);
    QCOMPARE(tabBar->currentIndex(), 0);

    auto* const home = page.findChild<QToolButton*>(
        QStringLiteral("browserHomeButton"));
    QVERIFY(home != nullptr);
    QTest::mouseClick(home, Qt::LeftButton);
    QCOMPARE(backend.lastCommand().kind,
             test::FakeBrowserCommandKind::Navigate);
    QCOMPARE(backend.lastCommand().text,
             QStringLiteral("https://home.example/path"));
}

}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserPageTest)

#include "browser_page_test.moc"
