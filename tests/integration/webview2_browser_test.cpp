#include <WebView2.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>
#include <QtTest>

#include <cstdint>
#include <functional>
#include <sstream>

#include "browser_event_listener.h"
#include "browser_profile_directory.h"
#include "mediahub/browser_webview2/webview2_browser_backend.h"
#include "mediahub/logging/logger.h"
#include "webview2_default_deny.h"
#include "webview2_pending_request.h"
#include "webview2_state.h"

namespace mediahub::browser_webview2 {
namespace {

constexpr std::uint64_t kGeneration = 7;
constexpr qint64 kInitializationTimeoutMilliseconds = 10000;

class FakePermissionArgs final {
 public:
    HRESULT put_State(const COREWEBVIEW2_PERMISSION_STATE value) noexcept {
        ++calls;
        state = value;
        return result;
    }

    int calls{0};
    COREWEBVIEW2_PERMISSION_STATE state{COREWEBVIEW2_PERMISSION_STATE_DEFAULT};
    HRESULT result{S_OK};
};

class FakeDownloadArgs final {
 public:
    HRESULT put_Cancel(const BOOL value) noexcept {
        ++cancelCalls;
        cancel = value;
        return cancelResult;
    }

    HRESULT put_Handled(const BOOL value) noexcept {
        ++handledCalls;
        handled = value;
        return handledResult;
    }

    int cancelCalls{0};
    int handledCalls{0};
    BOOL cancel{FALSE};
    BOOL handled{FALSE};
    HRESULT cancelResult{S_OK};
    HRESULT handledResult{S_OK};
};

class FakeCertificateArgs final {
 public:
    HRESULT put_Action(
        const COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION value) noexcept {
        ++calls;
        action = value;
        return result;
    }

    int calls{0};
    COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION action{
        COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_DEFAULT};
    HRESULT result{S_OK};
};

class FakeCancelArgs final {
 public:
    HRESULT put_Cancel(const BOOL value) noexcept {
        ++calls;
        cancel = value;
        return result;
    }

    int calls{0};
    BOOL cancel{FALSE};
    HRESULT result{S_OK};
};

class FakeHandledArgs final {
 public:
    HRESULT put_Handled(const BOOL value) noexcept {
        ++calls;
        handled = value;
        return result;
    }

    int calls{0};
    BOOL handled{FALSE};
    HRESULT result{S_OK};
};

class FakeClosableController final {
 public:
    HRESULT Close() noexcept {
        ++closeCalls;
        return closeResult;
    }

    int closeCalls{0};
    HRESULT closeResult{S_OK};
};

class FakeDeferral final {
 public:
    HRESULT Complete() noexcept {
        if (order != nullptr) {
            order->push_back(QStringLiteral("deferral"));
        }
        ++calls;
        return result;
    }

    int calls{0};
    HRESULT result{S_OK};
    std::vector<QString>* order{nullptr};
};

class FakePopupArgs final {
 public:
    HRESULT put_Handled(const BOOL value) noexcept {
        if (order != nullptr) {
            order->push_back(QStringLiteral("handled"));
        }
        ++handledCalls;
        handled = value;
        return handledResult;
    }

    HRESULT GetDeferral(FakeDeferral** const value) noexcept {
        if (order != nullptr) {
            order->push_back(QStringLiteral("get_deferral"));
        }
        ++deferralCalls;
        *value = deferral;
        return deferralResult;
    }

    HRESULT put_NewWindow(void* const value) noexcept {
        if (order != nullptr) {
            order->push_back(QStringLiteral("new_window"));
        }
        ++newWindowCalls;
        newWindow = value;
        return newWindowResult;
    }

    int handledCalls{0};
    int deferralCalls{0};
    int newWindowCalls{0};
    BOOL handled{FALSE};
    void* newWindow{nullptr};
    HRESULT handledResult{S_OK};
    HRESULT deferralResult{S_OK};
    HRESULT newWindowResult{S_OK};
    FakeDeferral* deferral{nullptr};
    std::vector<QString>* order{nullptr};
};

class FakeCertificatePreparationArgs final {
 public:
    HRESULT put_Action(
        const COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION value) noexcept {
        ++actionCalls;
        action = value;
        return actionResult;
    }

    HRESULT GetDeferral(FakeDeferral** const value) noexcept {
        ++deferralCalls;
        *value = deferral;
        return deferralResult;
    }

    int actionCalls{0};
    int deferralCalls{0};
    COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION action{
        COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_DEFAULT};
    HRESULT actionResult{S_OK};
    HRESULT deferralResult{S_OK};
    FakeDeferral* deferral{nullptr};
};

class FakeExternalPreparationArgs final {
 public:
    HRESULT put_Cancel(const BOOL value) noexcept {
        ++cancelCalls;
        cancel = value;
        return cancelResult;
    }

    HRESULT GetDeferral(FakeDeferral** const value) noexcept {
        ++deferralCalls;
        *value = deferral;
        return deferralResult;
    }

    int cancelCalls{0};
    int deferralCalls{0};
    BOOL cancel{FALSE};
    HRESULT cancelResult{S_OK};
    HRESULT deferralResult{S_OK};
    FakeDeferral* deferral{nullptr};
};

class FakePermissionDecisionArgs final {
 public:
    HRESULT put_SavesInProfile(const BOOL value) noexcept {
        ++saveCalls;
        savesInProfile = value;
        return saveResult;
    }

    HRESULT put_State(const COREWEBVIEW2_PERMISSION_STATE value) noexcept {
        ++stateCalls;
        state = value;
        return stateResult;
    }

    int saveCalls{0};
    int stateCalls{0};
    BOOL savesInProfile{FALSE};
    COREWEBVIEW2_PERMISSION_STATE state{COREWEBVIEW2_PERMISSION_STATE_DEFAULT};
    HRESULT saveResult{S_OK};
    HRESULT stateResult{S_OK};
};

class FakeDownloadDecisionArgs final {
 public:
    HRESULT put_ResultFilePath(const wchar_t* const value) {
        ++pathCalls;
        path = value != nullptr ? QString::fromWCharArray(value) : QString{};
        return pathResult;
    }

    HRESULT put_Cancel(const BOOL value) noexcept {
        if (order != nullptr) {
            order->push_back(QStringLiteral("args"));
        }
        ++cancelCalls;
        cancel = value;
        return cancelResult;
    }

    int pathCalls{0};
    int cancelCalls{0};
    QString path;
    BOOL cancel{TRUE};
    HRESULT pathResult{S_OK};
    HRESULT cancelResult{S_OK};
    std::vector<QString>* order{nullptr};
};

class FakeDownloadOperation final {
 public:
    HRESULT Cancel() noexcept {
        if (order != nullptr) {
            order->push_back(QStringLiteral("operation"));
        }
        ++cancelCalls;
        return result;
    }

    int cancelCalls{0};
    HRESULT result{S_OK};
    std::vector<QString>* order{nullptr};
};

class FakeDownloadPreparationArgs final {
 public:
    HRESULT put_Cancel(const BOOL value) noexcept {
        ++cancelCalls;
        cancel = value;
        return cancelResult;
    }

    HRESULT put_Handled(const BOOL value) noexcept {
        ++handledCalls;
        handled = value;
        return handledResult;
    }

    HRESULT get_DownloadOperation(FakeDownloadOperation** const value) noexcept {
        ++operationCalls;
        *value = operation;
        return operationResult;
    }

    HRESULT GetDeferral(FakeDeferral** const value) noexcept {
        ++deferralCalls;
        *value = deferral;
        return deferralResult;
    }

    int cancelCalls{0};
    int handledCalls{0};
    int operationCalls{0};
    int deferralCalls{0};
    BOOL cancel{FALSE};
    BOOL handled{FALSE};
    HRESULT cancelResult{S_OK};
    HRESULT handledResult{S_OK};
    HRESULT operationResult{S_OK};
    HRESULT deferralResult{S_OK};
    FakeDownloadOperation* operation{nullptr};
    FakeDeferral* deferral{nullptr};
};

class FakeActiveDownload final {
 public:
    void resetSubscriptions() noexcept {
        ++resetCalls;
        hasSubscription = false;
    }

    int resetCalls{0};
    bool hasSubscription{false};
    bool isCancelRequested{false};
};

class FakeCancelableDownload final {
 public:
    bool isCancelRequested{false};
};

class FakeDownloadSnapshotOperation final {
 public:
    HRESULT get_State(COREWEBVIEW2_DOWNLOAD_STATE* const value) {
        calls.push_back(QStringLiteral("state"));
        *value = state;
        return stateResult;
    }

    HRESULT get_BytesReceived(INT64* const value) {
        calls.push_back(QStringLiteral("bytes"));
        *value = receivedBytes;
        return bytesResult;
    }

    HRESULT get_TotalBytesToReceive(INT64* const value) {
        calls.push_back(QStringLiteral("total"));
        *value = totalBytes;
        return totalResult;
    }

    HRESULT get_InterruptReason(
        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON* const value) {
        calls.push_back(QStringLiteral("reason"));
        *value = interruptReason;
        return reasonResult;
    }

    std::vector<QString> calls;
    COREWEBVIEW2_DOWNLOAD_STATE state{COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS};
    COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON interruptReason{
        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE};
    INT64 receivedBytes{0};
    INT64 totalBytes{-1};
    HRESULT stateResult{S_OK};
    HRESULT bytesResult{S_OK};
    HRESULT totalResult{S_OK};
    HRESULT reasonResult{S_OK};
};

// 调用线程：GUI 主线程，在有界事件循环中等待明确谓词。
bool waitUntil(const std::function<bool()>& predicate, const qint64 timeoutMilliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return predicate();
}

// 调用线程：GUI 主线程，等待 WebView2 释放文件后清理测试专用 Profile。
bool removeTemporaryProfile(const QString& directory,
                            const qint64 timeoutMilliseconds) {
    const QString cleanDirectory =
        QDir::fromNativeSeparators(QDir::cleanPath(directory));
    const QString temporaryRoot =
        QDir::fromNativeSeparators(QDir::cleanPath(QDir::tempPath()));
    const QString expectedPrefix = temporaryRoot + QLatin1Char('/');
    if (!cleanDirectory.startsWith(expectedPrefix, Qt::CaseInsensitive) ||
        !QFileInfo(cleanDirectory).fileName().startsWith(
            QStringLiteral("mediahub_webview2_tests-"))) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    do {
        QDirIterator iterator(cleanDirectory,
                              QDir::AllEntries | QDir::NoDotAndDotDot |
                                  QDir::Hidden | QDir::System,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            const QFileDevice::Permissions permissions = QFile::permissions(path);
            static_cast<void>(
                QFile::setPermissions(path, permissions | QFileDevice::WriteOwner));
        }
        const QFileDevice::Permissions rootPermissions =
            QFile::permissions(cleanDirectory);
        static_cast<void>(QFile::setPermissions(
            cleanDirectory, rootPermissions | QFileDevice::WriteOwner));
        if (!QDir(cleanDirectory).exists() ||
            QDir(cleanDirectory).removeRecursively()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    } while (timer.elapsed() < timeoutMilliseconds);
    return !QDir(cleanDirectory).exists();
}

class RecordingBrowserListener final : public gui::BrowserEventListener {
 public:
    void onBrowserReady(const std::uint64_t generation) override {
        recordCallbackThread();
        generation_ = generation;
        isReady_ = true;
    }

    void onBrowserError(const std::uint64_t generation,
                        const gui::BrowserErrorKind kind,
                        const long errorCode) override {
        recordCallbackThread();
        generation_ = generation;
        errorKind_ = kind;
        errorCode_ = errorCode;
        hasError_ = true;
    }

    void onNavigationStarted(std::uint64_t) override { recordCallbackThread(); }

    void onNavigationCompleted(std::uint64_t, const QString&, const QString&, bool,
                               bool) override {
        recordCallbackThread();
    }

    void onFullScreenChanged(std::uint64_t, bool) override { recordCallbackThread(); }

    void onPermissionRequested(std::uint64_t, const QString&,
                               gui::BrowserPermissionKind) override {
        recordCallbackThread();
    }

    void onExternalProtocolRequested(std::uint64_t, const QString&,
                                     const QString&) override {
        recordCallbackThread();
    }

    void onCertificateErrorRequested(std::uint64_t, const QString&,
                                     const QString&) override {
        recordCallbackThread();
    }

    void onDownloadRequested(std::uint64_t, const QString&, const QString&,
                             std::int64_t) override {
        recordCallbackThread();
    }

    void onDownloadUpdated(std::uint64_t, gui::BrowserDownloadState, std::int64_t,
                           std::int64_t) override {
        recordCallbackThread();
    }

    void onBrowsingDataCleared(std::uint64_t) override { recordCallbackThread(); }
    void onPopupRejected() override { recordCallbackThread(); }

    [[nodiscard]] bool reachedTerminalState() const noexcept {
        return isReady_ || hasError_;
    }

    [[nodiscard]] bool isReady() const noexcept { return isReady_; }
    [[nodiscard]] bool wasCalledFromWrongThread() const noexcept {
        return wasCalledFromWrongThread_;
    }

    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool hasError() const noexcept { return hasError_; }
    [[nodiscard]] gui::BrowserErrorKind errorKind() const noexcept { return errorKind_; }
    [[nodiscard]] long errorCode() const noexcept { return errorCode_; }

 private:
    void recordCallbackThread() {
        if (QThread::currentThread() != QApplication::instance()->thread()) {
            wasCalledFromWrongThread_ = true;
        }
    }

    bool isReady_{false};
    bool hasError_{false};
    bool wasCalledFromWrongThread_{false};
    std::uint64_t generation_{0};
    gui::BrowserErrorKind errorKind_{gui::BrowserErrorKind::InitializationFailed};
    long errorCode_{0};
};

class WebView2BrowserTest final : public QObject {
    Q_OBJECT

 private slots:
    void suspensionCoordinatesPendingResume();
    void suspensionHandlesFailureAndStaleCompletion();
    void suspensionIgnoresCompletionAfterInvalidation();
    void popupCoordinatorLimitsThreeWindowsAndRejectsShutdown();
    void popupRequestCompletesEverySafetyActionExactlyOnce();
    void navigationBindsExplicitGenerationsInOrder();
    void navigationStopsBeforeStartingAndIgnoresOldCompletion();
    void navigationUsesCurrentGenerationForHistory();
    void clearDataWaitsForMatchingInternalBlankNavigation();
    void defaultDenyPoliciesApplyExactArguments();
    void pendingSensitiveDecisionsCompleteExactlyOnce();
    void permissionRejectionCompletesWithoutArgs3();
    void screenCaptureDecisionsAllowOnlyOnce();
    void downloadPreparationAttemptsEverySafetyAction();
    void securityPromptPreparationCompletesAfterSetterFailure();
    void downloadDecisionRejectsUnsafeDestination();
    void downloadStartFailuresCompleteAndCleanSubscriptions();
    void pendingDownloadCancellationAwaitsObservedTerminal();
    void pendingDownloadCancellationFailureRemainsRetryable();
    void pendingDownloadCancellationFallsBackWithoutSubscriptions();
    void downloadCancellationFailureAllowsRetryAndShutdownRepeats();
    void downloadTerminalSnapshotSurvivesProgressReadFailures();
    void shutdownPermanentlyRejectsReinitialization();
    void controllerCompletionClosesOnlyStaleController();
    void shutdownFullScreenExitRemainsReachableAndOrdered();
    void rejectsEmptyAndRelativeProfilePaths();
    void profileDirectoryRequiresAbsoluteApplicationData();
    void requiresEverySensitiveHandlerBeforeReady();
    void reportsRuntimeStatusWithoutBlockingGuiThread();
};

void WebView2BrowserTest::suspensionCoordinatesPendingResume() {
    SuspensionCoordinator coordinator(false);
    QCOMPARE(coordinator.controllerReady().action, SuspensionAction::None);

    const SuspensionStep suspend = coordinator.request(true);
    QCOMPARE(suspend.action, SuspensionAction::TrySuspend);
    QVERIFY(coordinator.mustMute());

    QCOMPARE(coordinator.request(false).action, SuspensionAction::None);
    QVERIFY(coordinator.mustMute());

    const SuspensionStep resume =
        coordinator.completeTrySuspend(suspend.requestSerial, true, true);
    QCOMPARE(resume.action, SuspensionAction::Resume);
    QVERIFY(coordinator.mustMute());
    QCOMPARE(coordinator.completeTrySuspend(suspend.requestSerial, true, true).action,
             SuspensionAction::None);

    QCOMPARE(coordinator.completeResume(resume.requestSerial, true).action,
             SuspensionAction::None);
    QVERIFY(!coordinator.mustMute());
}

void WebView2BrowserTest::suspensionHandlesFailureAndStaleCompletion() {
    SuspensionCoordinator coordinator(false);
    static_cast<void>(coordinator.controllerReady());

    const SuspensionStep suspend = coordinator.request(true);
    QCOMPARE(suspend.action, SuspensionAction::TrySuspend);
    QCOMPARE(coordinator.request(false).action, SuspensionAction::None);

    QCOMPARE(coordinator.completeTrySuspend(suspend.requestSerial + 1, true, true).action,
             SuspensionAction::None);
    QVERIFY(coordinator.mustMute());

    QCOMPARE(coordinator.completeTrySuspend(suspend.requestSerial, true, false).action,
             SuspensionAction::None);
    QVERIFY(!coordinator.mustMute());

    const SuspensionStep failedSuspend = coordinator.request(true);
    QCOMPARE(failedSuspend.action, SuspensionAction::TrySuspend);
    QCOMPARE(coordinator.completeTrySuspend(failedSuspend.requestSerial, false, false)
                 .action,
             SuspensionAction::None);
    QVERIFY(coordinator.mustMute());

    SuspensionCoordinator resumeFailure(false);
    static_cast<void>(resumeFailure.controllerReady());
    const SuspensionStep suspendBeforeResumeFailure = resumeFailure.request(true);
    QCOMPARE(resumeFailure
                 .completeTrySuspend(suspendBeforeResumeFailure.requestSerial, true, true)
                 .action,
             SuspensionAction::None);
    const SuspensionStep failedResume = resumeFailure.request(false);
    QCOMPARE(failedResume.action, SuspensionAction::Resume);
    QCOMPARE(resumeFailure.completeResume(failedResume.requestSerial, false).action,
             SuspensionAction::None);
    QVERIFY(resumeFailure.mustMute());
    QCOMPARE(resumeFailure.request(false).action, SuspensionAction::None);
}

void WebView2BrowserTest::suspensionIgnoresCompletionAfterInvalidation() {
    SuspensionCoordinator coordinator(false);
    static_cast<void>(coordinator.controllerReady());
    const SuspensionStep suspend = coordinator.request(true);
    QCOMPARE(suspend.action, SuspensionAction::TrySuspend);

    coordinator.invalidate();
    QCOMPARE(coordinator.completeTrySuspend(suspend.requestSerial, true, true).action,
             SuspensionAction::None);
    QVERIFY(coordinator.mustMute());
}

void WebView2BrowserTest::popupCoordinatorLimitsThreeWindowsAndRejectsShutdown() {
    PopupCoordinator coordinator;

    QVERIFY(coordinator.tryReserve());
    QVERIFY(coordinator.tryReserve());
    QVERIFY(coordinator.tryReserve());
    QVERIFY(!coordinator.tryReserve());
    QCOMPARE(coordinator.activeCount(), std::size_t{3});

    coordinator.release();
    QVERIFY(coordinator.tryReserve());
    coordinator.beginShutdown();
    QVERIFY(!coordinator.tryReserve());
    QCOMPARE(coordinator.activeCount(), std::size_t{0});

    coordinator.reset();
    QVERIFY(coordinator.tryReserve());
}

void WebView2BrowserTest::popupRequestCompletesEverySafetyActionExactlyOnce() {
    std::vector<QString> order;
    FakeDeferral deferral;
    deferral.order = &order;
    FakePopupArgs args;
    args.order = &order;
    args.deferral = &deferral;
    args.handledResult = E_ACCESSDENIED;
    FakeDeferral* preparedDeferral = nullptr;

    QCOMPARE(preparePopupRequest(&args, &preparedDeferral), E_ACCESSDENIED);
    QCOMPARE(preparedDeferral, &deferral);
    QCOMPARE(args.handledCalls, 1);
    QCOMPARE(args.deferralCalls, 1);

    int popupWebView = 0;
    QCOMPARE(completePopupRequest(&args, preparedDeferral, &popupWebView),
             E_ACCESSDENIED);
    QCOMPARE(args.newWindowCalls, 1);
    QCOMPARE(args.handledCalls, 2);
    QCOMPARE(deferral.calls, 1);
    QCOMPARE(order,
             std::vector<QString>({QStringLiteral("handled"),
                                   QStringLiteral("get_deferral"),
                                   QStringLiteral("new_window"),
                                   QStringLiteral("handled"),
                                   QStringLiteral("deferral")}));
}

void WebView2BrowserTest::navigationBindsExplicitGenerationsInOrder() {
    NavigationTracker tracker(1);
    tracker.acceptNavigate(11);
    tracker.acceptNavigate(12);

    const NavigationStart first = tracker.start(101);
    QCOMPARE(first.generation, std::uint64_t{11});
    QVERIFY(first.shouldReport);

    const NavigationStart redirect = tracker.start(101);
    QCOMPARE(redirect.generation, std::uint64_t{11});
    QVERIFY(!redirect.shouldReport);

    const NavigationStart second = tracker.start(202);
    QCOMPARE(second.generation, std::uint64_t{12});
    QVERIFY(second.shouldReport);

    const NavigationCompletion oldCompletion = tracker.complete(101);
    QVERIFY(!oldCompletion.shouldReport);
    QVERIFY(tracker.isNavigating());

    const NavigationCompletion activeCompletion = tracker.complete(202);
    QVERIFY(activeCompletion.shouldReport);
    QCOMPARE(activeCompletion.generation, std::uint64_t{12});
    QVERIFY(!tracker.isNavigating());
}

void WebView2BrowserTest::navigationStopsBeforeStartingAndIgnoresOldCompletion() {
    NavigationTracker tracker(3);
    tracker.acceptNavigate(21);
    QVERIFY(tracker.isNavigating());

    QCOMPARE(tracker.start(301).generation, std::uint64_t{21});
    tracker.acceptNavigate(22);
    QCOMPARE(tracker.start(302).generation, std::uint64_t{22});

    const NavigationCompletion oldFailure = tracker.complete(301);
    QVERIFY(!oldFailure.shouldReport);
    QVERIFY(tracker.isNavigating());
    QCOMPARE(tracker.complete(302).generation, std::uint64_t{22});
}

void WebView2BrowserTest::navigationUsesCurrentGenerationForHistory() {
    NavigationTracker tracker(5);
    tracker.acceptNavigate(31);
    QCOMPARE(tracker.start(401).generation, std::uint64_t{31});
    QVERIFY(tracker.complete(401).shouldReport);

    tracker.setCurrentGeneration(32);
    const NavigationStart history = tracker.start(402);
    QVERIFY(history.shouldReport);
    QCOMPARE(history.generation, std::uint64_t{32});
    QVERIFY(tracker.complete(402).shouldReport);

    QVERIFY(!tracker.complete(401).shouldReport);
}

void WebView2BrowserTest::clearDataWaitsForMatchingInternalBlankNavigation() {
    ClearDataNavigationCoordinator coordinator;
    coordinator.begin(40);
    QVERIFY(coordinator.dataAndCertificatesCleared(40));
    QVERIFY(!coordinator.ownsNavigation(700));
    QCOMPARE(coordinator.complete(700, true).outcome,
             ClearDataNavigationOutcome::None);

    QVERIFY(!coordinator.start(701, false));
    QVERIFY(coordinator.start(702, true));
    QVERIFY(coordinator.ownsNavigation(702));
    QCOMPARE(coordinator.complete(701, true).outcome,
             ClearDataNavigationOutcome::None);
    const ClearDataNavigationCompletion success =
        coordinator.complete(702, true);
    QCOMPARE(success.outcome, ClearDataNavigationOutcome::Succeeded);
    QCOMPARE(success.generation, std::uint64_t{40});
    QCOMPARE(coordinator.complete(702, true).outcome,
             ClearDataNavigationOutcome::None);

    coordinator.begin(41);
    QVERIFY(coordinator.dataAndCertificatesCleared(41));
    QVERIFY(coordinator.start(703, true));
    const ClearDataNavigationCompletion failed =
        coordinator.complete(703, false);
    QCOMPARE(failed.outcome, ClearDataNavigationOutcome::Failed);
    QCOMPARE(failed.generation, std::uint64_t{41});

    coordinator.begin(42);
    QVERIFY(coordinator.dataAndCertificatesCleared(42));
    const ClearDataNavigationCompletion requestFailed =
        coordinator.blankRequestFailed(42);
    QCOMPARE(requestFailed.outcome, ClearDataNavigationOutcome::Failed);
    QCOMPARE(requestFailed.generation, std::uint64_t{42});
}

void WebView2BrowserTest::defaultDenyPoliciesApplyExactArguments() {
    FakePermissionArgs permission;
    QCOMPARE(denyPermission(&permission), S_OK);
    QCOMPARE(permission.calls, 1);
    QCOMPARE(permission.state, COREWEBVIEW2_PERMISSION_STATE_DENY);

    FakeDownloadArgs download;
    QCOMPARE(cancelDownload(&download), S_OK);
    QCOMPARE(download.cancelCalls, 1);
    QCOMPARE(download.handledCalls, 1);
    QCOMPARE(download.cancel, TRUE);
    QCOMPARE(download.handled, TRUE);

    FakeCertificateArgs certificate;
    QCOMPARE(cancelCertificateError(&certificate), S_OK);
    QCOMPARE(certificate.calls, 1);
    QCOMPARE(certificate.action,
             COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL);

    FakeCancelArgs externalUri;
    QCOMPARE(cancelExternalUri(&externalUri), S_OK);
    QCOMPARE(externalUri.calls, 1);
    QCOMPARE(externalUri.cancel, TRUE);

    FakeHandledArgs newWindow;
    QCOMPARE(rejectNewWindow(&newWindow), S_OK);
    QCOMPARE(newWindow.calls, 1);
    QCOMPARE(newWindow.handled, TRUE);

    QVERIFY(denyPermission<FakePermissionArgs>(nullptr) == E_POINTER);
    QVERIFY(cancelDownload<FakeDownloadArgs>(nullptr) == E_POINTER);
    QVERIFY(cancelCertificateError<FakeCertificateArgs>(nullptr) == E_POINTER);
    QVERIFY(cancelExternalUri<FakeCancelArgs>(nullptr) == E_POINTER);
    QVERIFY(rejectNewWindow<FakeHandledArgs>(nullptr) == E_POINTER);
}

void WebView2BrowserTest::pendingSensitiveDecisionsCompleteExactlyOnce() {
    PendingRequestStore<int> requests;
    QVERIFY(requests.insert(7, 70));
    QVERIFY(!requests.insert(7, 71));
    const std::optional<int> request = requests.take(7);
    QVERIFY(request.has_value());
    QCOMPARE(*request, 70);
    QVERIFY(!requests.take(7).has_value());

    FakePermissionDecisionArgs permission;
    FakeDeferral permissionDeferral;
    QCOMPARE(completePermissionDecision(
                 &permission, &permissionDeferral,
                 gui::BrowserPermissionDecision::RememberForOrigin),
             S_OK);
    QCOMPARE(permission.saveCalls, 1);
    QCOMPARE(permission.savesInProfile, TRUE);
    QCOMPARE(permission.stateCalls, 1);
    QCOMPARE(permission.state, COREWEBVIEW2_PERMISSION_STATE_ALLOW);
    QCOMPARE(permissionDeferral.calls, 1);

    FakePermissionDecisionArgs deniedPermission;
    FakeDeferral deniedPermissionDeferral;
    QCOMPARE(completePermissionDecision(
                 &deniedPermission, &deniedPermissionDeferral,
                 gui::BrowserPermissionDecision::Deny),
             S_OK);
    QCOMPARE(deniedPermission.saveCalls, 1);
    QCOMPARE(deniedPermission.savesInProfile, FALSE);
    QCOMPARE(deniedPermission.stateCalls, 1);
    QCOMPARE(deniedPermission.state, COREWEBVIEW2_PERMISSION_STATE_DENY);
    QCOMPARE(deniedPermissionDeferral.calls, 1);

    FakePermissionDecisionArgs allowOnce;
    FakeDeferral allowOnceDeferral;
    QCOMPARE(completePermissionDecision(
                 &allowOnce, &allowOnceDeferral,
                 gui::BrowserPermissionDecision::AllowOnce),
             S_OK);
    QCOMPARE(allowOnce.savesInProfile, FALSE);
    QCOMPARE(allowOnce.state, COREWEBVIEW2_PERMISSION_STATE_ALLOW);
    QCOMPARE(allowOnceDeferral.calls, 1);

    FakeCancelArgs external;
    FakeDeferral externalDeferral;
    QCOMPARE(completeExternalProtocolDecision(&external, &externalDeferral, true),
             S_OK);
    QCOMPARE(external.cancel, FALSE);
    QCOMPARE(externalDeferral.calls, 1);

    FakeCancelArgs deniedExternal;
    FakeDeferral deniedExternalDeferral;
    QCOMPARE(completeExternalProtocolDecision(
                 &deniedExternal, &deniedExternalDeferral, false),
             S_OK);
    QCOMPARE(deniedExternal.cancel, TRUE);
    QCOMPARE(deniedExternalDeferral.calls, 1);

    FakeCertificateArgs certificate;
    FakeDeferral certificateDeferral;
    QCOMPARE(completeCertificateDecision(
                 &certificate, &certificateDeferral,
                 gui::BrowserCertificateDecision::ContinueForSession),
             S_OK);
    QCOMPARE(certificate.action,
             COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_ALWAYS_ALLOW);
    QCOMPARE(certificateDeferral.calls, 1);

    FakeCertificateArgs safeCertificate;
    FakeDeferral safeCertificateDeferral;
    QCOMPARE(completeCertificateDecision(
                 &safeCertificate, &safeCertificateDeferral,
                 gui::BrowserCertificateDecision::ReturnToSafety),
             S_OK);
    QCOMPARE(safeCertificate.action,
             COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL);
    QCOMPARE(safeCertificateDeferral.calls, 1);
}

void WebView2BrowserTest::permissionRejectionCompletesWithoutArgs3() {
    FakePermissionArgs baseArgs;
    FakePermissionDecisionArgs args3;
    FakeDeferral deferral;
    QCOMPARE(completePermissionRejection(&baseArgs, &args3, &deferral), S_OK);
    QCOMPARE(baseArgs.calls, 1);
    QCOMPARE(baseArgs.state, COREWEBVIEW2_PERMISSION_STATE_DENY);
    QCOMPARE(args3.saveCalls, 1);
    QCOMPARE(args3.savesInProfile, FALSE);
    QCOMPARE(deferral.calls, 1);

    FakePermissionArgs noArgs3Base;
    FakeDeferral noArgs3Deferral;
    QCOMPARE(completePermissionRejection(
                 &noArgs3Base,
                 static_cast<FakePermissionDecisionArgs*>(nullptr),
                 &noArgs3Deferral),
             S_OK);
    QCOMPARE(noArgs3Base.calls, 1);
    QCOMPARE(noArgs3Base.state, COREWEBVIEW2_PERMISSION_STATE_DENY);
    QCOMPARE(noArgs3Deferral.calls, 1);

    FakePermissionArgs failedArgs3Base;
    FakePermissionDecisionArgs failedArgs3;
    failedArgs3.saveResult = E_FAIL;
    FakeDeferral failedArgs3Deferral;
    QCOMPARE(completePermissionRejection(
                 &failedArgs3Base, &failedArgs3, &failedArgs3Deferral),
             E_FAIL);
    QCOMPARE(failedArgs3Base.calls, 1);
    QCOMPARE(failedArgs3Base.state, COREWEBVIEW2_PERMISSION_STATE_DENY);
    QCOMPARE(failedArgs3.saveCalls, 1);
    QCOMPARE(failedArgs3Deferral.calls, 1);
}

void WebView2BrowserTest::screenCaptureDecisionsAllowOnlyOnce() {
    FakeDownloadArgs allowOnce;
    FakeDeferral allowOnceDeferral;
    QCOMPARE(completeScreenCaptureDecision(
                 &allowOnce, &allowOnceDeferral,
                 gui::BrowserPermissionDecision::AllowOnce),
             S_OK);
    QCOMPARE(allowOnce.cancelCalls, 1);
    QCOMPARE(allowOnce.cancel, FALSE);
    QCOMPARE(allowOnce.handledCalls, 1);
    QCOMPARE(allowOnce.handled, TRUE);
    QCOMPARE(allowOnceDeferral.calls, 1);

    FakeDownloadArgs denied;
    FakeDeferral deniedDeferral;
    QCOMPARE(completeScreenCaptureDecision(
                 &denied, &deniedDeferral,
                 gui::BrowserPermissionDecision::Deny),
             S_OK);
    QCOMPARE(denied.cancelCalls, 1);
    QCOMPARE(denied.cancel, TRUE);
    QCOMPARE(denied.handledCalls, 1);
    QCOMPARE(denied.handled, TRUE);
    QCOMPARE(deniedDeferral.calls, 1);

    FakeDownloadArgs rememberRejected;
    FakeDeferral rememberRejectedDeferral;
    QCOMPARE(completeScreenCaptureDecision(
                 &rememberRejected, &rememberRejectedDeferral,
                 gui::BrowserPermissionDecision::RememberForOrigin),
             S_OK);
    QCOMPARE(rememberRejected.cancelCalls, 1);
    QCOMPARE(rememberRejected.cancel, TRUE);
    QCOMPARE(rememberRejected.handledCalls, 1);
    QCOMPARE(rememberRejected.handled, TRUE);
    QCOMPARE(rememberRejectedDeferral.calls, 1);
}

void WebView2BrowserTest::downloadPreparationAttemptsEverySafetyAction() {
    const auto verifyFailure = [](const HRESULT cancelResult,
                                  const HRESULT handledResult,
                                  const HRESULT operationResult,
                                  const HRESULT deferralResult) {
        FakeDownloadOperation operation;
        FakeDeferral deferral;
        FakeDownloadPreparationArgs args;
        args.cancelResult = cancelResult;
        args.handledResult = handledResult;
        args.operationResult = operationResult;
        args.deferralResult = deferralResult;
        args.operation = &operation;
        args.deferral = &deferral;
        FakeDownloadOperation* preparedOperation = nullptr;
        FakeDeferral* preparedDeferral = nullptr;

        const HRESULT preparationResult = prepareDownloadRequest(
            &args, &preparedOperation, &preparedDeferral);
        const HRESULT result = firstFailure(
            preparationResult,
            completeDownloadCancellation(&args, preparedOperation,
                                         preparedDeferral));

        QVERIFY(FAILED(result));
        QCOMPARE(args.cancelCalls, 2);
        QCOMPARE(args.cancel, TRUE);
        QCOMPARE(args.handledCalls, 1);
        QCOMPARE(args.handled, TRUE);
        QCOMPARE(args.operationCalls, 1);
        QCOMPARE(args.deferralCalls, 1);
        QCOMPARE(operation.cancelCalls, 1);
        QCOMPARE(deferral.calls, 1);
    };

    verifyFailure(E_FAIL, S_OK, S_OK, S_OK);
    verifyFailure(S_OK, E_FAIL, S_OK, S_OK);
    verifyFailure(S_OK, S_OK, E_FAIL, S_OK);
    verifyFailure(S_OK, S_OK, S_OK, E_FAIL);
    verifyFailure(E_ACCESSDENIED, E_FAIL, E_UNEXPECTED, E_ABORT);
}

void WebView2BrowserTest::securityPromptPreparationCompletesAfterSetterFailure() {
    const auto verifyCertificate = [](const HRESULT actionResult,
                                      const HRESULT deferralResult,
                                      const HRESULT expectedResult) {
        FakeDeferral deferral;
        FakeCertificatePreparationArgs args;
        args.actionResult = actionResult;
        args.deferralResult = deferralResult;
        args.deferral = &deferral;
        FakeDeferral* preparedDeferral = nullptr;
        const HRESULT preparationResult = prepareCertificateRequest(
            &args, &preparedDeferral);
        const HRESULT result = firstFailure(
            preparationResult,
            completeCertificateDecision(
                &args, preparedDeferral,
                gui::BrowserCertificateDecision::ReturnToSafety));
        QCOMPARE(result, expectedResult);
        QCOMPARE(args.actionCalls, 2);
        QCOMPARE(args.deferralCalls, 1);
        QCOMPARE(deferral.calls, 1);
    };
    verifyCertificate(E_ACCESSDENIED, S_OK, E_ACCESSDENIED);
    verifyCertificate(S_OK, E_ABORT, E_ABORT);
    verifyCertificate(E_ACCESSDENIED, E_ABORT, E_ACCESSDENIED);

    const auto verifyExternal = [](const HRESULT cancelResult,
                                   const HRESULT deferralResult,
                                   const HRESULT expectedResult) {
        FakeDeferral deferral;
        FakeExternalPreparationArgs args;
        args.cancelResult = cancelResult;
        args.deferralResult = deferralResult;
        args.deferral = &deferral;
        FakeDeferral* preparedDeferral = nullptr;
        const HRESULT preparationResult = prepareExternalProtocolRequest(
            &args, &preparedDeferral);
        const HRESULT result = firstFailure(
            preparationResult,
            completeExternalProtocolDecision(&args, preparedDeferral, false));
        QCOMPARE(result, expectedResult);
        QCOMPARE(args.cancelCalls, 2);
        QCOMPARE(args.deferralCalls, 1);
        QCOMPARE(deferral.calls, 1);
    };
    verifyExternal(E_FAIL, S_OK, E_FAIL);
    verifyExternal(S_OK, E_ABORT, E_ABORT);
    verifyExternal(E_FAIL, E_ABORT, E_FAIL);
}

void WebView2BrowserTest::downloadDecisionRejectsUnsafeDestination() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString existingPath = directory.filePath(QStringLiteral("existing.bin"));
    QFile existing(existingPath);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.close();

    QVERIFY(!isSafeDownloadDestination(QString{}));
    QVERIFY(!isSafeDownloadDestination(QStringLiteral("relative.bin")));
    QVERIFY(!isSafeDownloadDestination(directory.path()));
    QVERIFY(!isSafeDownloadDestination(existingPath));
    QVERIFY(!isSafeDownloadDestination(
        directory.filePath(QStringLiteral("missing/file.bin"))));
    QVERIFY(!isSafeDownloadDestination(
        directory.filePath(QStringLiteral("CON.foo.bar"))));
    QVERIFY(!isSafeDownloadDestination(
        directory.filePath(QStringLiteral("NUL.anything"))));

    const QString newPath = directory.filePath(QStringLiteral("new.bin"));
    QVERIFY(isSafeDownloadDestination(newPath));
    FakeDownloadDecisionArgs args;
    FakeDeferral deferral;
    FakeDownloadOperation pathOperation;
    QCOMPARE(completeDownloadPathDecision(&args, &pathOperation, &deferral, newPath),
             S_OK);
    QCOMPARE(args.pathCalls, 1);
    QCOMPARE(QDir::fromNativeSeparators(args.path),
             QDir::fromNativeSeparators(newPath));
    QCOMPARE(args.cancelCalls, 1);
    QCOMPARE(args.cancel, FALSE);
    QCOMPARE(deferral.calls, 1);

    FakeDownloadDecisionArgs cancelArgs;
    FakeDownloadOperation operation;
    FakeDeferral cancelDeferral;
    QCOMPARE(completeDownloadCancellation(&cancelArgs, &operation,
                                         &cancelDeferral),
             S_OK);
    QCOMPARE(cancelArgs.cancelCalls, 1);
    QCOMPARE(cancelArgs.cancel, TRUE);
    QCOMPARE(operation.cancelCalls, 1);
    QCOMPARE(cancelDeferral.calls, 1);
}

void WebView2BrowserTest::downloadStartFailuresCompleteAndCleanSubscriptions() {
    struct ProbeResult final {
        HRESULT result{S_OK};
        bool didRemoveStoredDownload{false};
        int argsCancelCalls{0};
        BOOL argsCancel{FALSE};
        int operationCancelCalls{0};
        int deferralCalls{0};
        int subscriptionResetCalls{0};
        bool hasSubscription{false};
    };
    const auto runFailure = [](const HRESULT registrationResult,
                               const bool canStore,
                               const QString& destination) -> ProbeResult {
        FakeDownloadDecisionArgs args;
        FakeDownloadOperation operation;
        FakeDeferral deferral;
        FakeActiveDownload active;
        bool didRemoveStoredDownload = false;

        const HRESULT result = startDownloadTransaction(
            active,
            [registrationResult](FakeActiveDownload& candidate) {
                candidate.hasSubscription = true;
                return registrationResult;
            },
            [canStore](FakeActiveDownload&) { return canStore; },
            [&] {
                return completeDownloadPathDecision(
                    &args, &operation, &deferral, destination);
            },
            [&] {
                return completeDownloadCancellation(&args, &operation, &deferral);
            },
            [&] {
                didRemoveStoredDownload = true;
                active.resetSubscriptions();
            });

        return {result,
                didRemoveStoredDownload,
                args.cancelCalls,
                args.cancel,
                operation.cancelCalls,
                deferral.calls,
                active.resetCalls,
                active.hasSubscription};
    };
    const auto verifyFailure = [](const ProbeResult& probe,
                                  const HRESULT expectedResult,
                                  const bool expectedRemoval) {
        QCOMPARE(probe.result, expectedResult);
        QCOMPARE(probe.didRemoveStoredDownload, expectedRemoval);
        QCOMPARE(probe.argsCancelCalls, 1);
        QCOMPARE(probe.argsCancel, TRUE);
        QCOMPARE(probe.operationCancelCalls, 1);
        QCOMPARE(probe.deferralCalls, 1);
        QCOMPARE(probe.subscriptionResetCalls, 1);
        QVERIFY(!probe.hasSubscription);
    };

    const auto registrationFailure =
        runFailure(E_FAIL, true, QStringLiteral("C:/unused.bin"));
    verifyFailure(registrationFailure, E_FAIL, false);

    const auto storageFailure =
        runFailure(S_OK, false, QStringLiteral("C:/unused.bin"));
    verifyFailure(storageFailure, E_UNEXPECTED, false);

    const auto destinationRace =
        runFailure(S_OK, true, QStringLiteral("relative-after-dialog.bin"));
    verifyFailure(destinationRace, E_INVALIDARG, true);
}

void WebView2BrowserTest::pendingDownloadCancellationAwaitsObservedTerminal() {
    std::vector<QString> order;
    FakeDownloadDecisionArgs args;
    FakeDownloadOperation operation;
    FakeDeferral deferral;
    args.order = &order;
    operation.order = &order;
    deferral.order = &order;
    FakeActiveDownload candidate;
    std::optional<FakeActiveDownload> stored;
    int retainCalls = 0;

    const PendingDownloadCancelOutcome outcome = cancelPendingDownloadTransaction(
        candidate,
        [&](FakeActiveDownload& active) {
            order.push_back(QStringLiteral("register"));
            active.hasSubscription = true;
            return S_OK;
        },
        [&](FakeActiveDownload&& active) -> FakeActiveDownload* {
            order.push_back(QStringLiteral("store"));
            stored.emplace(std::move(active));
            return &stored.value();
        },
        [&] {
            return completeDownloadCancellation(&args, &operation, &deferral);
        },
        [&](FakeActiveDownload&&) {
            ++retainCalls;
            return true;
        });

    QCOMPARE(outcome.action, PendingDownloadCancelAction::AwaitTerminal);
    QCOMPARE(outcome.result, S_OK);
    QVERIFY(stored.has_value());
    QVERIFY(stored->hasSubscription);
    QVERIFY(stored->isCancelRequested);
    QCOMPARE(retainCalls, 0);
    QCOMPARE(args.cancelCalls, 1);
    QCOMPARE(operation.cancelCalls, 1);
    QCOMPARE(deferral.calls, 1);
    QCOMPARE(order.size(), std::size_t{5});
    QCOMPARE(order[0], QStringLiteral("register"));
    QCOMPARE(order[1], QStringLiteral("store"));
    QCOMPARE(order[2], QStringLiteral("args"));
    QCOMPARE(order[3], QStringLiteral("operation"));
    QCOMPARE(order[4], QStringLiteral("deferral"));

    FakeDownloadSnapshotOperation terminalOperation;
    terminalOperation.state = COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED;
    terminalOperation.interruptReason =
        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_CANCELED;
    const DownloadSnapshot terminal = readDownloadSnapshot(
        &terminalOperation, 100, stored->isCancelRequested);
    QCOMPARE(terminal.state, gui::BrowserDownloadState::Cancelled);
    QVERIFY(terminal.isTerminal);
}

void WebView2BrowserTest::pendingDownloadCancellationFailureRemainsRetryable() {
    const auto verifyFailure = [](const HRESULT argsResult,
                                  const HRESULT operationResult,
                                  const HRESULT deferralResult) {
        FakeDownloadDecisionArgs args;
        FakeDownloadOperation operation;
        FakeDeferral deferral;
        args.cancelResult = argsResult;
        operation.result = operationResult;
        deferral.result = deferralResult;
        FakeActiveDownload candidate;
        std::optional<FakeActiveDownload> stored;
        int retainCalls = 0;

        const PendingDownloadCancelOutcome outcome =
            cancelPendingDownloadTransaction(
                candidate,
                [](FakeActiveDownload& active) {
                    active.hasSubscription = true;
                    return S_OK;
                },
                [&](FakeActiveDownload&& active) -> FakeActiveDownload* {
                    stored.emplace(std::move(active));
                    return &stored.value();
                },
                [&] {
                    return completeDownloadCancellation(
                        &args, &operation, &deferral);
                },
                [&](FakeActiveDownload&&) {
                    ++retainCalls;
                    return true;
                });

        QCOMPARE(outcome.action,
                 PendingDownloadCancelAction::ReportCancelFailed);
        QVERIFY(FAILED(outcome.result));
        QVERIFY(stored.has_value());
        QVERIFY(!stored->isCancelRequested);
        QCOMPARE(retainCalls, 0);
        QCOMPARE(args.cancelCalls, 1);
        QCOMPARE(operation.cancelCalls, 1);
        QCOMPARE(deferral.calls, 1);

        operation.result = S_OK;
        int retryFailures = 0;
        QCOMPARE(requestActiveDownloadCancellation(
                     stored.value(), [&] { return operation.Cancel(); },
                     [&] { ++retryFailures; }),
                 S_OK);
        QCOMPARE(args.cancelCalls, 1);
        QCOMPARE(operation.cancelCalls, 2);
        QCOMPARE(deferral.calls, 1);
        QCOMPARE(retryFailures, 0);
        QCOMPARE(cancelActiveDownloadForShutdown(
                     stored.value(), [&] { return operation.Cancel(); }),
                 S_OK);
        QCOMPARE(operation.cancelCalls, 3);
    };

    verifyFailure(E_FAIL, S_OK, S_OK);
    verifyFailure(S_OK, E_ABORT, S_OK);
    verifyFailure(S_OK, S_OK, E_UNEXPECTED);
}

void WebView2BrowserTest::pendingDownloadCancellationFallsBackWithoutSubscriptions() {
    struct Probe final {
        PendingDownloadCancelOutcome outcome;
        int storeCalls{0};
        int retainCalls{0};
        int resetCalls{0};
        int argsCalls{0};
        int operationCalls{0};
        int deferralCalls{0};
        bool retainedIsRetryable{false};
        bool retryAndShutdownSucceeded{false};
    };
    const auto run = [](const HRESULT registrationResult, const bool canStore,
                        const HRESULT operationResult) {
        FakeDownloadDecisionArgs args;
        FakeDownloadOperation operation;
        FakeDeferral deferral;
        operation.result = operationResult;
        FakeActiveDownload candidate;
        std::optional<FakeActiveDownload> stored;
        std::optional<FakeActiveDownload> retained;
        int storeCalls = 0;
        int retainCalls = 0;

        const PendingDownloadCancelOutcome outcome =
            cancelPendingDownloadTransaction(
                candidate,
                [registrationResult](FakeActiveDownload& active) {
                    active.hasSubscription = true;
                    return registrationResult;
                },
                [&](FakeActiveDownload&& active) -> FakeActiveDownload* {
                    ++storeCalls;
                    if (!canStore) {
                        return nullptr;
                    }
                    stored.emplace(std::move(active));
                    return &stored.value();
                },
                [&] {
                    return completeDownloadCancellation(
                        &args, &operation, &deferral);
                },
                [&](FakeActiveDownload&& active) {
                    ++retainCalls;
                    retained.emplace(std::move(active));
                    return true;
                });

        const bool retainedIsRetryable =
            retained.has_value() && !retained->hasSubscription &&
            !retained->isCancelRequested;
        bool retryAndShutdownSucceeded = false;
        if (retained.has_value()) {
            operation.result = S_OK;
            int failureNotifications = 0;
            const HRESULT retryResult = requestActiveDownloadCancellation(
                retained.value(), [&] { return operation.Cancel(); },
                [&] { ++failureNotifications; });
            const HRESULT shutdownResult = cancelActiveDownloadForShutdown(
                retained.value(), [&] { return operation.Cancel(); });
            retryAndShutdownSucceeded = retryResult == S_OK &&
                                        shutdownResult == S_OK &&
                                        failureNotifications == 0;
        }
        const FakeActiveDownload& finalCandidate =
            retained.has_value() ? retained.value() : candidate;
        return Probe{outcome,
                     storeCalls,
                     retainCalls,
                     finalCandidate.resetCalls,
                     args.cancelCalls,
                     operation.cancelCalls,
                     deferral.calls,
                     retainedIsRetryable,
                     retryAndShutdownSucceeded};
    };

    const Probe registrationCancelled = run(E_FAIL, true, S_OK);
    QCOMPARE(registrationCancelled.outcome.action,
             PendingDownloadCancelAction::ReportCancelled);
    QCOMPARE(registrationCancelled.storeCalls, 0);
    QCOMPARE(registrationCancelled.retainCalls, 0);
    QCOMPARE(registrationCancelled.resetCalls, 1);

    const Probe registrationFailed = run(E_FAIL, true, E_ABORT);
    QCOMPARE(registrationFailed.outcome.action,
             PendingDownloadCancelAction::ReportCancelFailed);
    QCOMPARE(registrationFailed.storeCalls, 0);
    QCOMPARE(registrationFailed.retainCalls, 1);
    QVERIFY(registrationFailed.retainedIsRetryable);
    QVERIFY(registrationFailed.retryAndShutdownSucceeded);

    const Probe storageCancelled = run(S_OK, false, S_OK);
    QCOMPARE(storageCancelled.outcome.action,
             PendingDownloadCancelAction::ReportCancelled);
    QCOMPARE(storageCancelled.storeCalls, 1);
    QCOMPARE(storageCancelled.retainCalls, 0);
    QCOMPARE(storageCancelled.resetCalls, 1);

    const Probe storageFailed = run(S_OK, false, E_ABORT);
    QCOMPARE(storageFailed.outcome.action,
             PendingDownloadCancelAction::ReportCancelFailed);
    QCOMPARE(storageFailed.storeCalls, 1);
    QCOMPARE(storageFailed.retainCalls, 1);
    QVERIFY(storageFailed.retainedIsRetryable);
    QVERIFY(storageFailed.retryAndShutdownSucceeded);

    for (const Probe* const probe : {&registrationCancelled, &registrationFailed,
                                     &storageCancelled, &storageFailed}) {
        QCOMPARE(probe->argsCalls, 1);
        QCOMPARE(probe->operationCalls,
                 probe->retainCalls == 0 ? 1 : 3);
        QCOMPARE(probe->deferralCalls, 1);
    }
}

void WebView2BrowserTest::downloadCancellationFailureAllowsRetryAndShutdownRepeats() {
    FakeCancelableDownload active;
    int cancelCalls = 0;
    int failureNotifications = 0;
    HRESULT cancelResult = E_FAIL;
    const auto cancel = [&] {
        ++cancelCalls;
        return cancelResult;
    };
    const auto notifyFailure = [&] { ++failureNotifications; };

    QCOMPARE(requestActiveDownloadCancellation(active, cancel, notifyFailure), E_FAIL);
    QCOMPARE(cancelCalls, 1);
    QCOMPARE(failureNotifications, 1);
    QVERIFY(!active.isCancelRequested);

    cancelResult = S_OK;
    QCOMPARE(requestActiveDownloadCancellation(active, cancel, notifyFailure), S_OK);
    QCOMPARE(cancelCalls, 2);
    QCOMPARE(failureNotifications, 1);
    QVERIFY(active.isCancelRequested);
    QCOMPARE(requestActiveDownloadCancellation(active, cancel, notifyFailure), S_FALSE);
    QCOMPARE(cancelCalls, 2);

    QCOMPARE(cancelActiveDownloadForShutdown(active, cancel), S_OK);
    QCOMPARE(cancelCalls, 3);
    QVERIFY(active.isCancelRequested);
}

void WebView2BrowserTest::downloadTerminalSnapshotSurvivesProgressReadFailures() {
    FakeDownloadSnapshotOperation completedOperation;
    completedOperation.state = COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED;
    completedOperation.bytesResult = E_FAIL;
    completedOperation.totalBytes = 1000;
    const DownloadSnapshot completed = readDownloadSnapshot(
        &completedOperation, 900, false);
    QVERIFY(completed.hasState);
    QVERIFY(completed.isTerminal);
    QCOMPARE(completed.state, gui::BrowserDownloadState::Completed);
    QCOMPARE(completed.receivedBytes, std::int64_t{-1});
    QCOMPARE(completed.totalBytes, std::int64_t{1000});
    QCOMPARE(completedOperation.calls.front(), QStringLiteral("state"));

    FakeDownloadSnapshotOperation interruptedOperation;
    interruptedOperation.state = COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED;
    interruptedOperation.receivedBytes = 25;
    interruptedOperation.totalResult = E_FAIL;
    interruptedOperation.interruptReason =
        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_CANCELED;
    const DownloadSnapshot interrupted = readDownloadSnapshot(
        &interruptedOperation, 800, false);
    QVERIFY(interrupted.hasState);
    QVERIFY(interrupted.isTerminal);
    QCOMPARE(interrupted.state, gui::BrowserDownloadState::Cancelled);
    QCOMPARE(interrupted.receivedBytes, std::int64_t{25});
    QCOMPARE(interrupted.totalBytes, std::int64_t{800});
    QCOMPARE(interruptedOperation.calls.front(), QStringLiteral("state"));

    std::unordered_map<std::uint64_t, int> active{{7, 70}};
    QVERIFY(!eraseTerminalDownloadIfCurrent(active, 7, 11, 12, true));
    QCOMPARE(active.size(), std::size_t{1});
    active.clear();
    QVERIFY(!eraseTerminalDownloadIfCurrent(active, 7, 11, 11, false));
}

void WebView2BrowserTest::shutdownPermanentlyRejectsReinitialization() {
    BrowserLifecycleGate gate;
    QVERIFY(gate.beginInitialization());
    gate.beginShutdown();
    QVERIFY(!gate.beginInitialization());

    WebView2BrowserBackend backend;
    backend.shutdown();
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    backend.initialize(nullptr, QString{}, kGeneration);

    QVERIFY(!listener.reachedTerminalState());
}

void WebView2BrowserTest::controllerCompletionClosesOnlyStaleController() {
    FakeClosableController staleController;
    QVERIFY(!acceptControllerCompletion(false, &staleController));
    QCOMPARE(staleController.closeCalls, 1);

    FakeClosableController currentController;
    QVERIFY(acceptControllerCompletion(true, &currentController));
    QCOMPARE(currentController.closeCalls, 0);

    QVERIFY(!acceptControllerCompletion<FakeClosableController>(false, nullptr));
}

void WebView2BrowserTest::shutdownFullScreenExitRemainsReachableAndOrdered() {
    int requestCalls = 0;
    const HRESULT result = submitShutdownFullScreenExit([&requestCalls] {
        ++requestCalls;
        return E_ACCESSDENIED;
    });
    QCOMPARE(result, E_ACCESSDENIED);
    QCOMPARE(requestCalls, 1);

    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    const int releaseStart = source.indexOf(
        QStringLiteral("void releaseBrowserResources() noexcept"));
    const int releaseEnd = source.indexOf(
        QStringLiteral("void releaseComApartment() noexcept"), releaseStart);
    QVERIFY(releaseStart >= 0);
    QVERIFY(releaseEnd > releaseStart);
    const QString release = source.mid(releaseStart, releaseEnd - releaseStart);

    const int fullScreenExit = release.indexOf(
        QStringLiteral("requestExitFullScreenForShutdown()"));
    const int popupClose = release.indexOf(QStringLiteral("closePopups()"));
    const int firstTokenReset = release.indexOf(
        QStringLiteral("newWindowRequested_.reset"));
    const int controllerClose = release.indexOf(
        QStringLiteral("controller_->Close()"));
    QVERIFY(fullScreenExit >= 0);
    QVERIFY(popupClose > fullScreenExit);
    QVERIFY(firstTokenReset > popupClose);
    QVERIFY(controllerClose > firstTokenReset);
}

void WebView2BrowserTest::rejectsEmptyAndRelativeProfilePaths() {
    const QStringList invalidProfiles{QString{}, QStringLiteral("relative-profile")};
    for (const QString& profile : invalidProfiles) {
        WebView2BrowserBackend backend;
        RecordingBrowserListener listener;
        backend.setEventListener(&listener);
        backend.initialize(nullptr, profile, kGeneration);

        QVERIFY(listener.hasError());
        QCOMPARE(listener.generation(), kGeneration);
        QCOMPARE(listener.errorKind(), gui::BrowserErrorKind::ProfileUnavailable);
        QCOMPARE(listener.errorCode(), static_cast<long>(E_INVALIDARG));
        backend.shutdown();
    }
}

void WebView2BrowserTest::profileDirectoryRequiresAbsoluteApplicationData() {
    QCOMPARE(gui::makeBrowserProfileDirectory(QString{}), QString{});
    QCOMPARE(gui::makeBrowserProfileDirectory(QStringLiteral("relative-app-data")),
             QString{});

    const QString profile = gui::makeBrowserProfileDirectory(QDir::tempPath());
    QVERIFY(QFileInfo(profile).isAbsolute());
    QVERIFY(QDir::fromNativeSeparators(profile).endsWith(
        QStringLiteral("/WebView2/Profile-v1")));
}

void WebView2BrowserTest::requiresEverySensitiveHandlerBeforeReady() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    QFile popupSourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_popup_window.cpp"));
    QVERIFY2(popupSourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(popupSourceFile.errorString()));
    const QString popupSource = QString::fromUtf8(popupSourceFile.readAll());

    const int popupCreateStart = popupSource.indexOf(
        QStringLiteral("HRESULT WebView2PopupWindow::createFor("));
    const int popupCreateEnd = popupSource.indexOf(
        QStringLiteral("void WebView2PopupWindow::close()"));
    QVERIFY(popupCreateStart >= 0);
    QVERIFY(popupCreateEnd > popupCreateStart);
    const QString popupCreate = popupSource.mid(
        popupCreateStart, popupCreateEnd - popupCreateStart);
    const int preparePopup =
        popupCreate.indexOf(QStringLiteral("preparePopupRequest"));
    const int storePopupDeferral =
        popupCreate.indexOf(QStringLiteral("pendingDeferral_ = deferral"));
    const int rejectFailedPreparation =
        popupCreate.indexOf(QStringLiteral("if (FAILED(result)"));
    QVERIFY(preparePopup >= 0);
    QVERIFY(storePopupDeferral > preparePopup);
    QVERIFY(rejectFailedPreparation > storePopupDeferral);
    QVERIFY(popupCreate.contains(QStringLiteral("completePendingRequest(false)")));
    QVERIFY(popupSource.contains(
        QStringLiteral("CreateCoreWebView2ControllerWithOptions")));
    QVERIFY(popupSource.contains(QStringLiteral("options->put_ProfileName")));

    const int registerEventsStart = source.indexOf(QStringLiteral("HRESULT registerEvents()"));
    const int registerEventsEnd =
        source.indexOf(QStringLiteral("HRESULT registerNavigationStarting()"));
    QVERIFY(registerEventsStart >= 0);
    QVERIFY(registerEventsEnd > registerEventsStart);
    const QString registerEvents =
        source.mid(registerEventsStart, registerEventsEnd - registerEventsStart);

    const QStringList requiredRegistrations{
        QStringLiteral("registerPermissionRequested()"),
        QStringLiteral("registerScreenCaptureStarting()"),
        QStringLiteral("registerDownloadStarting()"),
        QStringLiteral("registerServerCertificateErrorDetected()"),
        QStringLiteral("registerLaunchingExternalUriScheme()"),
        QStringLiteral("registerNewWindowRequested()"),
    };
    for (const QString& registration : requiredRegistrations) {
        QVERIFY2(registerEvents.contains(registration), qPrintable(registration));
    }

    const auto handlerSegment = [&source](const QString& start,
                                          const QString& end) {
        const int startIndex = source.indexOf(start);
        const int endIndex = source.indexOf(end, startIndex + start.size());
        return startIndex >= 0 && endIndex > startIndex
                   ? source.mid(startIndex, endIndex - startIndex)
                   : QString{};
    };
    const QList<QPair<QString, QStringList>> handlerPolicies{
        {handlerSegment(QStringLiteral("HRESULT registerPermissionRequested()"),
                        QStringLiteral("HRESULT registerScreenCaptureStarting()")),
         {QStringLiteral("GetDeferral"),
          QStringLiteral("rejectPermissionRequest(args)"),
          QStringLiteral("pendingPermissions_.insert"),
          QStringLiteral("onPermissionRequested")}},
        {handlerSegment(QStringLiteral("HRESULT registerScreenCaptureStarting()"),
                        QStringLiteral("HRESULT registerDownloadStarting()")),
         {QStringLiteral("ICoreWebView2_27"),
          QStringLiteral("add_ScreenCaptureStarting"),
          QStringLiteral("put_Cancel(TRUE)"),
          QStringLiteral("put_Handled(TRUE)"),
          QStringLiteral("get_OriginalSourceFrameInfo"),
          QStringLiteral("get_Source"),
          QStringLiteral("GetDeferral"),
          QStringLiteral("pendingScreenCaptures_.insert"),
          QStringLiteral("BrowserPermissionKind::ScreenCapture"),
          QStringLiteral("onPermissionRequested")}},
        {handlerSegment(QStringLiteral("HRESULT registerDownloadStarting()"),
                        QStringLiteral(
                            "HRESULT registerServerCertificateErrorDetected()")),
         {QStringLiteral("prepareDownloadRequest"),
          QStringLiteral("completeDownloadCancellation"),
          QStringLiteral("pendingDownloads_.insert"),
          QStringLiteral("onDownloadRequested")}},
        {handlerSegment(
             QStringLiteral("HRESULT registerServerCertificateErrorDetected()"),
             QStringLiteral("HRESULT registerLaunchingExternalUriScheme()")),
         {QStringLiteral("prepareCertificateRequest"),
          QStringLiteral("completeCertificateDecision"),
          QStringLiteral("pendingCertificates_.insert"),
          QStringLiteral("onCertificateErrorRequested")}},
        {handlerSegment(QStringLiteral("HRESULT registerLaunchingExternalUriScheme()"),
                        QStringLiteral("HRESULT registerNewWindowRequested()")),
         {QStringLiteral("get_IsUserInitiated"),
          QStringLiteral("prepareExternalProtocolRequest"),
          QStringLiteral("completeExternalProtocolDecision"),
          QStringLiteral("pendingExternalProtocols_.insert"),
          QStringLiteral("onExternalProtocolRequested")}},
        {handlerSegment(QStringLiteral("HRESULT registerNewWindowRequested()"),
                        QStringLiteral("HRESULT registerNavigationStarting()")),
         {QStringLiteral("rejectNewWindow(args)")}},
    };
    for (const auto& [segment, policies] : handlerPolicies) {
        QVERIFY(!segment.isEmpty());
        for (const QString& policy : policies) {
            QVERIFY2(segment.contains(policy), qPrintable(policy));
        }
    }
    const QString permissionHandler = handlerSegment(
        QStringLiteral("HRESULT registerPermissionRequested()"),
        QStringLiteral("HRESULT registerScreenCaptureStarting()"));
    const int denyIndex = permissionHandler.indexOf(
        QStringLiteral("args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY)"));
    const int baseDeferralIndex =
        permissionHandler.indexOf(QStringLiteral("baseArgs->GetDeferral"));
    const int args2Index =
        permissionHandler.indexOf(QStringLiteral("baseArgs.As(&args2)"));
    const int args3Index =
        permissionHandler.indexOf(QStringLiteral("baseArgs.As(&args3)"));
    const int validationIndex =
        permissionHandler.indexOf(QStringLiteral("origin.isEmpty()"));
    const int rejectionIndex = permissionHandler.indexOf(
        QStringLiteral("completePermissionRejection"));
    QVERIFY(denyIndex >= 0);
    QVERIFY(baseDeferralIndex > denyIndex);
    QVERIFY(args2Index > baseDeferralIndex);
    QVERIFY(args3Index > args2Index);
    QVERIFY(validationIndex > args3Index);
    QVERIFY(rejectionIndex > validationIndex);
    QVERIFY(permissionHandler.contains(QStringLiteral("listener_ == nullptr")));
    QVERIFY(!permissionHandler.contains(
        QStringLiteral("FAILED(deferralStatus) || deferral == nullptr")));
    const QString screenCaptureHandler = handlerSegment(
        QStringLiteral("HRESULT registerScreenCaptureStarting()"),
        QStringLiteral("HRESULT registerDownloadStarting()"));
    QVERIFY(!screenCaptureHandler.contains(
        QStringLiteral("FAILED(deferralResult) || deferral == nullptr")));
    QVERIFY(screenCaptureHandler.contains(
        QStringLiteral("pendingScreenCaptures_.insert")));
    QVERIFY(source.contains(
        QStringLiteral("pendingScreenCaptures_.take(requestId)")));
    QVERIFY(source.contains(QStringLiteral("pendingScreenCaptures_.takeAll()")));
    const QString newWindowHandler =
        handlerSegment(QStringLiteral("HRESULT registerNewWindowRequested()"),
                       QStringLiteral("HRESULT registerNavigationStarting()"));
    QVERIFY(newWindowHandler.contains(
        QStringLiteral("handleNewWindowRequest(args)")));
    QVERIFY(source.contains(QStringLiteral("popupCoordinator_.tryReserve()")));
    QVERIFY(source.contains(QStringLiteral("std::make_unique<WebView2PopupWindow>")));
    QVERIFY(source.contains(QStringLiteral("listener.onPopupRejected()")));
    QVERIFY(source.contains(
        QStringLiteral("return FAILED(result) ? result : S_OK;")));

    const QStringList requiredBindMarkers{
        QStringLiteral("permissionRequested_.bind"),
        QStringLiteral("screenCaptureStarting_.bind"),
        QStringLiteral("downloadStarting_.bind"),
        QStringLiteral("serverCertificateErrorDetected_.bind"),
        QStringLiteral("launchingExternalUriScheme_.bind"),
        QStringLiteral("newWindowRequested_.bind"),
    };
    for (const QString& marker : requiredBindMarkers) {
        QVERIFY2(source.contains(marker), qPrintable(marker));
    }

    const int finishControllerStart =
        source.indexOf(QStringLiteral("void finishController("));
    const int finishControllerEnd =
        source.indexOf(QStringLiteral("HRESULT configureSettings()"));
    QVERIFY(finishControllerStart >= 0);
    QVERIFY(finishControllerEnd > finishControllerStart);
    const QString finishController = source.mid(
        finishControllerStart, finishControllerEnd - finishControllerStart);
    const int registerGate =
        finishController.indexOf(QStringLiteral("result = registerEvents();"));
    const int readyAssignment =
        finishController.indexOf(QStringLiteral("isReady_ = true;"));
    QVERIFY(registerGate >= 0);
    QVERIFY(readyAssignment > registerGate);

    const int releaseStart =
        source.indexOf(QStringLiteral("void releaseBrowserResources()"));
    const int releaseEnd =
        source.indexOf(QStringLiteral("void releaseComApartment()"));
    QVERIFY(releaseStart >= 0);
    QVERIFY(releaseEnd > releaseStart);
    const QString releaseResources =
        source.mid(releaseStart, releaseEnd - releaseStart);
    QVERIFY(releaseResources.contains(QStringLiteral("cancelPendingSensitiveRequests()")));
    QVERIFY(!source.contains(QStringLiteral("ShellExecute")));
    QVERIFY(source.contains(
        QStringLiteral("COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_PROFILE")));
    QVERIFY(source.contains(QStringLiteral("ClearServerCertificateErrorActions")));
    const QStringList requiredResetMarkers{
        QStringLiteral("permissionRequested_.reset"),
        QStringLiteral("screenCaptureStarting_.reset"),
        QStringLiteral("downloadStarting_.reset"),
        QStringLiteral("serverCertificateErrorDetected_.reset"),
        QStringLiteral("launchingExternalUriScheme_.reset"),
        QStringLiteral("newWindowRequested_.reset"),
    };
    const int controllerClose =
        releaseResources.indexOf(QStringLiteral("controller_->Close()"));
    QVERIFY(controllerClose >= 0);
    const int popupClose =
        releaseResources.indexOf(QStringLiteral("closePopups()"));
    const int firstTokenReset = releaseResources.indexOf(
        QStringLiteral("newWindowRequested_.reset"));
    QVERIFY(popupClose >= 0);
    QVERIFY(firstTokenReset > popupClose);
    for (const QString& marker : requiredResetMarkers) {
        const int reset = releaseResources.indexOf(marker);
        QVERIFY2(reset >= 0, qPrintable(marker));
        QVERIFY2(reset < controllerClose, qPrintable(marker));
    }
}

void WebView2BrowserTest::reportsRuntimeStatusWithoutBlockingGuiThread() {
    LPWSTR runtimeVersion = nullptr;
    const HRESULT runtimeStatus =
        GetAvailableCoreWebView2BrowserVersionString(nullptr, &runtimeVersion);
    const bool isRuntimeAvailable =
        SUCCEEDED(runtimeStatus) && runtimeVersion != nullptr;
    CoTaskMemFree(runtimeVersion);

    QWidget testWindow;
    testWindow.resize(640, 360);
    testWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&testWindow, 5000));

    QTemporaryDir testProfile;
    QVERIFY(testProfile.isValid());

    std::ostringstream browserLog;
    logging::Logger logger(browserLog);
    WebView2BrowserBackend backend(&logger);
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    backend.initialize(reinterpret_cast<void*>(testWindow.winId()), testProfile.path(),
                       kGeneration);

    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QCOMPARE(listener.generation(), kGeneration);
    QVERIFY(!listener.wasCalledFromWrongThread());
    if (isRuntimeAvailable) {
        QVERIFY2(listener.isReady(),
                 qPrintable(QStringLiteral("初始化错误 kind=%1 HRESULT=%2 log=%3")
                                .arg(static_cast<int>(listener.errorKind()))
                                .arg(listener.errorCode())
                                .arg(QString::fromStdString(browserLog.str()))));
        QVERIFY(!listener.hasError());
    } else {
        QVERIFY(listener.hasError());
        QCOMPARE(listener.errorKind(), gui::BrowserErrorKind::RuntimeUnavailable);
        QVERIFY(listener.errorCode() < 0);
    }

    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(removeTemporaryProfile(testProfile.path(), 5000));
}

}  // namespace
}  // namespace mediahub::browser_webview2

QTEST_MAIN(mediahub::browser_webview2::WebView2BrowserTest)

#include "webview2_browser_test.moc"
