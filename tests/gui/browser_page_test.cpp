#include <QTest>

#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <limits>

#include "browser_download_widget.h"
#include "browser_page.h"
#include "browser_permission_dialog.h"
#include "browser_navigation_policy.h"
#include "fakes/fake_browser_backend.h"

namespace mediahub::gui {

class BrowserPageTest final : public QObject {
    Q_OBJECT

 private slots:
    void normalizesOnlySupportedTopLevelAddresses();
    void routesNavigationAndIgnoresLateGeneration();
    void requiresConfirmationBeforeClearingBrowsingData();
    void clearCompletionShowsBlankPageOnlyForCurrentGeneration();
    void routesNavigationToolbarCommands();
    void routesOriginAwarePermissionChoicesAndRejectsUnsafeRequests();
    void screenCaptureAllowsOnlyCurrentRequest();
    void replacedPermissionIgnoresLateUiSignals();
    void confirmsExternalProtocolsAndSessionCertificateExceptions();
    void validatesDownloadDestinationAndTracksOneTask();
    void rejectsReservedDownloadNamesWithMultipleSuffixes();
    void downloadTerminalStatesIgnoreStaleRequests();
    void cancelKeepsDownloadActiveUntilBackendTerminalState();
    void cancelFailureAllowsRetryWithoutEndingDownload();
    void navigationRejectsUnansweredSensitiveRequests();
    void navigationKeepsStartedDownload();
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

void BrowserPageTest::cancelKeepsDownloadActiveUntilBackendTerminalState() {
    test::FakeBrowserBackend backend;
    BrowserPage page(backend, QStringLiteral("C:/temporary-profile"));
    auto* download = page.findChild<BrowserDownloadWidget*>(
        QStringLiteral("browserDownloadWidget"));
    QVERIFY(download != nullptr);

    backend.emitDownloadRequested(90, QStringLiteral("https://files.example"),
                                  QStringLiteral("active.bin"), 300);
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
