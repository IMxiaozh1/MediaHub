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

#include "browser_event_listener.h"
#include "browser_profile_directory.h"
#include "mediahub/browser_webview2/webview2_browser_backend.h"
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

class FakeDeferral final {
 public:
    HRESULT Complete() noexcept {
        ++calls;
        return result;
    }

    int calls{0};
    HRESULT result{S_OK};
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
};

class FakeDownloadOperation final {
 public:
    HRESULT Cancel() noexcept {
        ++cancelCalls;
        return result;
    }

    int cancelCalls{0};
    HRESULT result{S_OK};
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

    void onExternalProtocolRequested(std::uint64_t, const QString&) override {
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
    void navigationBindsExplicitGenerationsInOrder();
    void navigationStopsBeforeStartingAndIgnoresOldCompletion();
    void navigationUsesCurrentGenerationForHistory();
    void defaultDenyPoliciesApplyExactArguments();
    void pendingSensitiveDecisionsCompleteExactlyOnce();
    void downloadDecisionRejectsUnsafeDestination();
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

    FakeCertificateArgs certificate;
    FakeDeferral certificateDeferral;
    QCOMPARE(completeCertificateDecision(
                 &certificate, &certificateDeferral,
                 gui::BrowserCertificateDecision::ContinueForSession),
             S_OK);
    QCOMPARE(certificate.action,
             COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_ALWAYS_ALLOW);
    QCOMPARE(certificateDeferral.calls, 1);
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

    const QString newPath = directory.filePath(QStringLiteral("new.bin"));
    QVERIFY(isSafeDownloadDestination(newPath));
    FakeDownloadDecisionArgs args;
    FakeDeferral deferral;
    QCOMPARE(completeDownloadPathDecision(&args, &deferral, newPath), S_OK);
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

    const int registerEventsStart = source.indexOf(QStringLiteral("HRESULT registerEvents()"));
    const int registerEventsEnd =
        source.indexOf(QStringLiteral("HRESULT registerNavigationStarting()"));
    QVERIFY(registerEventsStart >= 0);
    QVERIFY(registerEventsEnd > registerEventsStart);
    const QString registerEvents =
        source.mid(registerEventsStart, registerEventsEnd - registerEventsStart);

    const QStringList requiredRegistrations{
        QStringLiteral("registerPermissionRequested()"),
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
                        QStringLiteral("HRESULT registerDownloadStarting()")),
         {QStringLiteral("GetDeferral"),
          QStringLiteral("denyPermission(args)"),
          QStringLiteral("pendingPermissions_.insert"),
          QStringLiteral("onPermissionRequested")}},
        {handlerSegment(QStringLiteral("HRESULT registerDownloadStarting()"),
                        QStringLiteral(
                            "HRESULT registerServerCertificateErrorDetected()")),
         {QStringLiteral("put_Handled(TRUE)"), QStringLiteral("GetDeferral"),
          QStringLiteral("cancelDownload(args)"),
          QStringLiteral("pendingDownloads_.insert"),
          QStringLiteral("onDownloadRequested")}},
        {handlerSegment(
             QStringLiteral("HRESULT registerServerCertificateErrorDetected()"),
             QStringLiteral("HRESULT registerLaunchingExternalUriScheme()")),
         {QStringLiteral("GetDeferral"),
          QStringLiteral("cancelCertificateError(args)"),
          QStringLiteral("pendingCertificates_.insert"),
          QStringLiteral("onCertificateErrorRequested")}},
        {handlerSegment(QStringLiteral("HRESULT registerLaunchingExternalUriScheme()"),
                        QStringLiteral("HRESULT registerNewWindowRequested()")),
         {QStringLiteral("get_IsUserInitiated"), QStringLiteral("GetDeferral"),
          QStringLiteral("cancelExternalUri(args)"),
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
    const QString newWindowHandler =
        handlerSegment(QStringLiteral("HRESULT registerNewWindowRequested()"),
                       QStringLiteral("HRESULT registerNavigationStarting()"));
    QVERIFY(!newWindowHandler.contains(QStringLiteral("onPopupRejected")));

    const QStringList requiredBindMarkers{
        QStringLiteral("permissionRequested_.bind"),
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
        QStringLiteral("downloadStarting_.reset"),
        QStringLiteral("serverCertificateErrorDetected_.reset"),
        QStringLiteral("launchingExternalUriScheme_.reset"),
        QStringLiteral("newWindowRequested_.reset"),
    };
    const int controllerClose =
        releaseResources.indexOf(QStringLiteral("controller_->Close()"));
    QVERIFY(controllerClose >= 0);
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

    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    backend.initialize(reinterpret_cast<void*>(testWindow.winId()), testProfile.path(),
                       kGeneration);

    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QCOMPARE(listener.generation(), kGeneration);
    QVERIFY(!listener.wasCalledFromWrongThread());
    if (isRuntimeAvailable) {
        QVERIFY(listener.isReady());
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
