#include <QTest>

#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTabBar>

#include <limits>

#include "browser_download_widget.h"
#include "browser_data_store.h"
#include "main_window.h"
#include "browser_page.h"
#include "browser_permission_dialog.h"
#include "browser_navigation_policy.h"
#include "fakes/fake_browser_backend.h"
#include "ui_theme.h"

namespace mediahub::gui {
namespace {

class MemoryBrowserDataStore final : public BrowserDataStore {
 public:
    QVector<BrowserHistoryEntry> loadHistory() override { return history; }

    void saveHistory(const QVector<BrowserHistoryEntry>& value) override {
        history = value;
        ++saveCount;
    }

    QVector<BrowserFavoriteEntry> loadFavorites() override {
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
};

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
    void opensHistoryInCurrentOrNewTabFromMouseGesture();
    void opensFavoritesInCurrentOrNewTabFromMouseGesture();
    void addsEditsDeduplicatesAndRemovesFavorites();
    void convertsNewWindowRequestsToTabsAndKeepsBlankTab();
    void switchesTabsWithoutReloadAndKeepsIndependentState();
    void closingBackgroundTabKeepsCurrentPageRequests();
    void requiresConfirmationBeforeClearingBrowsingData();
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
    void deactivatesAndActivatesBrowserInSafeOrder();
    void escapeExitsWebFullScreenFirst();
    void mainWindowPrioritizesWebFullScreenAndRestoresChrome();
    void shutdownDetachesListenerBeforeBackend();
    void detachesListenerAndShutsDownOnDestruction();
    void appliesResponsiveSizeAcrossToolbarBreakpoints();
    void routesBrowserHistoryShortcuts();
    void routesWebShortcutsWithoutNativeConflicts();
    void routesNativeAcceleratorsThroughCurrentGeneration();
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

    auto* const address =
        page.findChild<QLineEdit*>(QStringLiteral("browserAddressEdit"));
    QVERIFY(address != nullptr);
    address->setText(QStringLiteral("example.com/one"));
    QTest::keyClick(address, Qt::Key_Return);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Navigate), 1);
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
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::Navigate), 2);
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
    QCOMPARE(backend.lastCommand().text, QStringLiteral("about:blank"));

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
    MemoryBrowserDataStore dataStore;
    dataStore.history = {{QStringLiteral("https://history.example"),
                          QStringLiteral("History"), 1}};
    dataStore.favorites = {{QStringLiteral("https://favorite.example"),
                            QStringLiteral("Favorite"), QStringLiteral("note")}};
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"), nullptr,
                     &dataStore);
    page.show();
    QCoreApplication::processEvents();
    backend.emitReady(1);
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
    QCOMPARE(backend.lastCommand().text, QStringLiteral("https://www.microsoft.com/edge"));
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

void BrowserPageTest::deactivatesAndActivatesBrowserInSafeOrder() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    backend.commands.clear();

    page.deactivate();
    page.activate();

    const std::vector<test::FakeBrowserCommandKind> expected{
        test::FakeBrowserCommandKind::SetAudioMuted,
        test::FakeBrowserCommandKind::SetSuspended,
        test::FakeBrowserCommandKind::SetVisible,
        test::FakeBrowserCommandKind::SetVisible,
        test::FakeBrowserCommandKind::SetSuspended,
        test::FakeBrowserCommandKind::SetAudioMuted,
    };
    QCOMPARE(backend.commands.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        QCOMPARE(backend.commands[index].kind, expected[index]);
    }
    QVERIFY(backend.commands[0].flag);
    QVERIFY(backend.commands[1].flag);
    QVERIFY(!backend.commands[2].flag);
    QVERIFY(backend.commands[3].flag);
    QVERIFY(!backend.commands[4].flag);
    QVERIFY(!backend.commands[5].flag);
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

    backend.emitAcceleratorRequested(1, BrowserAccelerator::ExitFullScreen);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ExitFullScreen), 0);
    page.onFullScreenChanged(1, true);
    backend.emitAcceleratorRequested(1, BrowserAccelerator::ExitFullScreen);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ExitFullScreen), 1);
}

}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserPageTest)

#include "browser_page_test.moc"
