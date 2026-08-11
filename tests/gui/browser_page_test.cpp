#include <QTest>

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

#include "browser_page.h"
#include "browser_navigation_policy.h"
#include "fakes/fake_browser_backend.h"

namespace mediahub::gui {

class BrowserPageTest final : public QObject {
    Q_OBJECT

 private slots:
    void normalizesOnlySupportedTopLevelAddresses();
    void routesNavigationAndIgnoresLateGeneration();
    void requiresConfirmationBeforeClearingBrowsingData();
    void routesNavigationToolbarCommands();
    void detachesListenerAndShutsDownOnDestruction();
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
    QTest::mouseClick(
        dialog->findChild<QPushButton*>(QStringLiteral("browserClearDataConfirmButton")),
        Qt::LeftButton);
    QCOMPARE(backend.count(test::FakeBrowserCommandKind::ClearBrowsingData), 1);
    QCOMPARE(backend.lastCommand().generation, std::uint64_t{2});

    backend.emitError(2, BrowserErrorKind::ClearDataFailed, -1);
    auto* errorLabel = page.findChild<QLabel*>(QStringLiteral("browserErrorLabel"));
    QVERIFY(errorLabel != nullptr);
    QVERIFY(errorLabel->isVisible());
    QVERIFY(errorLabel->text().contains(QStringLiteral("清除")));
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

void BrowserPageTest::detachesListenerAndShutsDownOnDestruction() {
    test::FakeBrowserBackend backend;
    {
        BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    }

    QCOMPARE(backend.count(test::FakeBrowserCommandKind::SetEventListener), 2);
    QCOMPARE(backend.lastCommand().kind, test::FakeBrowserCommandKind::Shutdown);
}

}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserPageTest)

#include "browser_page_test.moc"
