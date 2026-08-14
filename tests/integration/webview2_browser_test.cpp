#include <WebView2.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QWidget>
#include <QtTest>

#include <cstdint>
#include <cstring>
#include <functional>
#include <sstream>
#include <tuple>

#include "browser_event_listener.h"
#include "browser_profile_directory.h"
#include "mediahub/browser_webview2/webview2_browser_backend.h"
#include "mediahub/logging/logger.h"
#include "support/local_web_test_server.h"
#include "webview2_default_deny.h"
#include "webview2_accelerator.h"
#include "webview2_handles.h"
#include "webview2_pending_request.h"
#include "webview2_state.h"
#include "webview2_tab_controller.h"

namespace mediahub::browser_webview2 {
namespace {

constexpr std::uint64_t kGeneration = 7;
constexpr qint64 kInitializationTimeoutMilliseconds = 10000;
constexpr qint64 kRuntimeBehaviorTimeoutMilliseconds = 10000;
constexpr qint64 kProfileCleanupTimeoutMilliseconds = 15000;

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

class FakeAcceleratorArgs final {
 public:
    HRESULT get_KeyEventKind(COREWEBVIEW2_KEY_EVENT_KIND* const value) noexcept {
        ++kindCalls;
        *value = kind;
        return kindResult;
    }

    HRESULT get_VirtualKey(UINT* const value) noexcept {
        ++keyCalls;
        *value = virtualKey;
        return keyResult;
    }

    HRESULT get_PhysicalKeyStatus(
        COREWEBVIEW2_PHYSICAL_KEY_STATUS* const value) noexcept {
        ++physicalCalls;
        *value = physicalStatus;
        return physicalResult;
    }

    HRESULT put_Handled(const BOOL value) noexcept {
        ++handledCalls;
        handled = value;
        return handledResult;
    }

    COREWEBVIEW2_KEY_EVENT_KIND kind{COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN};
    UINT virtualKey{0};
    COREWEBVIEW2_PHYSICAL_KEY_STATUS physicalStatus{};
    HRESULT kindResult{S_OK};
    HRESULT keyResult{S_OK};
    HRESULT physicalResult{S_OK};
    HRESULT handledResult{S_OK};
    int kindCalls{0};
    int keyCalls{0};
    int physicalCalls{0};
    int handledCalls{0};
    BOOL handled{FALSE};
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

class FakeResumableDownload final {
 public:
    void resetSubscriptions() noexcept {
        ++resetCalls;
        hasSubscription = false;
    }

    int resetCalls{0};
    bool hasSubscription{false};
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

    HRESULT get_CanResume(BOOL* const value) {
        calls.push_back(QStringLiteral("can_resume"));
        *value = canResume;
        return canResumeResult;
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
    BOOL canResume{FALSE};
    HRESULT canResumeResult{S_OK};
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

QSet<QString>& pendingTemporaryProfiles() {
    static QSet<QString> profiles;
    return profiles;
}

class TemporaryWebView2Profile final {
 public:
    TemporaryWebView2Profile()
        : directory_(QDir(QDir::tempPath())
                         .filePath(QStringLiteral("mediahub_webview2_tests-XXXXXX"))) {
        directory_.setAutoRemove(false);
        if (directory_.isValid()) {
            pendingTemporaryProfiles().insert(directory_.path());
        }
    }

    ~TemporaryWebView2Profile() {
        if (!isCleaned_ && directory_.isValid()) {
            static_cast<void>(cleanup());
        }
    }

    [[nodiscard]] bool isValid() const noexcept { return directory_.isValid(); }
    [[nodiscard]] QString path() const { return directory_.path(); }

    // 调用线程：GUI 主线程，后端关闭后有界等待测试 Profile 释放。
    bool cleanup() {
        isCleaned_ = removeTemporaryProfile(directory_.path(),
                                            kProfileCleanupTimeoutMilliseconds);
        if (isCleaned_) {
            pendingTemporaryProfiles().remove(directory_.path());
        }
        return isCleaned_;
    }

 private:
    QTemporaryDir directory_;
    bool isCleaned_{false};
};

bool isWebView2RuntimeAvailable() {
    LPWSTR runtimeVersion = nullptr;
    const HRESULT status =
        GetAvailableCoreWebView2BrowserVersionString(nullptr, &runtimeVersion);
    const bool isAvailable = SUCCEEDED(status) && runtimeVersion != nullptr;
    CoTaskMemFree(runtimeVersion);
    return isAvailable;
}

// 调用线程：GUI 主线程，模拟用户进入网页模式后的可见活动状态。
void activateBackend(WebView2BrowserBackend& backend) {
    backend.setBounds(QRect(0, 0, 640, 360));
    backend.setVisible(true);
    backend.setSuspended(false);
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

    void onNavigationStarted(std::uint64_t) override {
        recordCallbackThread();
        ++navigationStartedCount_;
    }

    void onNavigationCompleted(const std::uint64_t generation,
                               const QString& visibleUrl, const QString& title,
                               bool, bool) override {
        recordCallbackThread();
        generation_ = generation;
        visibleUrl_ = visibleUrl;
        title_ = title;
        ++navigationCompletedCount_;
    }

    void onFullScreenChanged(std::uint64_t, const bool isFullScreen) override {
        recordCallbackThread();
        isFullScreen_ = isFullScreen;
        ++fullScreenChangedCount_;
    }

    void onAcceleratorRequested(std::uint64_t,
                                gui::BrowserAccelerator) override {
        recordCallbackThread();
    }

    void onPermissionRequested(const std::uint64_t requestId,
                               const QString& origin,
                               const gui::BrowserPermissionKind kind) override {
        recordCallbackThread();
        permissionRequestId_ = requestId;
        permissionOrigin_ = origin;
        permissionKind_ = kind;
        ++permissionRequestCount_;
    }

    void onExternalProtocolRequested(std::uint64_t, const QString&,
                                     const QString&) override {
        recordCallbackThread();
    }

    void onCertificateErrorRequested(std::uint64_t, const QString&,
                                     const QString&) override {
        recordCallbackThread();
    }

    void onDownloadRequested(const std::uint64_t requestId,
                             const QString& origin,
                             const QString& suggestedFileName,
                             const std::int64_t totalBytes) override {
        recordCallbackThread();
        downloadRequestId_ = requestId;
        downloadOrigin_ = origin;
        suggestedFileName_ = suggestedFileName;
        downloadTotalBytes_ = totalBytes;
        ++downloadRequestCount_;
    }

    void onDownloadUpdated(const std::uint64_t requestId,
                           const gui::BrowserDownloadState state,
                           const std::int64_t receivedBytes,
                           const std::int64_t totalBytes) override {
        recordCallbackThread();
        downloadUpdateRequestId_ = requestId;
        downloadState_ = state;
        downloadReceivedBytes_ = receivedBytes;
        downloadTotalBytes_ = totalBytes;
    }

    void onBrowsingDataCleared(const std::uint64_t generation) override {
        recordCallbackThread();
        clearedGeneration_ = generation;
        ++browsingDataClearedCount_;
    }
    void onPopupRejected() override {
        recordCallbackThread();
        ++popupRejectedCount_;
    }

    bool onNewTabRequested(const std::uint64_t newWindowRequestId,
                           const QString& url) override {
        recordCallbackThread();
        requestedTabUrl_ = url;
        if (backend_ == nullptr) {
            return false;
        }
        const bool didCreate = backend_->createTab(
            parentWindowHandle_, 2, url, tabGeneration_, newWindowRequestId);
        if (didCreate) {
            backend_->activateTab(2);
            ++newTabRequestCount_;
        }
        return didCreate;
    }

    void onTabReady(const std::uint64_t tabId,
                    const std::uint64_t generation) override {
        recordCallbackThread();
        if (tabId == 1) {
            onBrowserReady(generation);
            return;
        }
        readyTabId_ = tabId;
        tabGeneration_ = generation;
        isTabReady_ = true;
    }

    void onTabNavigationStarted(const std::uint64_t tabId,
                                const std::uint64_t generation) override {
        recordCallbackThread();
        if (tabId == 1) {
            onNavigationStarted(generation);
            return;
        }
        startedTabId_ = tabId;
        tabGeneration_ = generation;
        ++tabNavigationStartedCount_;
    }

    void onTabNavigationCompleted(const std::uint64_t tabId,
                                  const std::uint64_t generation,
                                  const QString& visibleUrl,
                                  const QString& title, bool, bool) override {
        recordCallbackThread();
        if (tabId == 1) {
            onNavigationCompleted(generation, visibleUrl, title, false, false);
            return;
        }
        completedTabId_ = tabId;
        tabGeneration_ = generation;
        tabVisibleUrl_ = visibleUrl;
        tabTitle_ = title;
        ++tabNavigationCompletedCount_;
    }

    void configureTabCreation(WebView2BrowserBackend* const backend,
                              void* const parentWindowHandle,
                              const std::uint64_t generation) noexcept {
        backend_ = backend;
        parentWindowHandle_ = parentWindowHandle;
        tabGeneration_ = generation;
    }

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
    [[nodiscard]] const QString& visibleUrl() const noexcept { return visibleUrl_; }
    [[nodiscard]] const QString& title() const noexcept { return title_; }
    [[nodiscard]] int navigationCompletedCount() const noexcept {
        return navigationCompletedCount_;
    }
    [[nodiscard]] int navigationStartedCount() const noexcept {
        return navigationStartedCount_;
    }
    [[nodiscard]] std::uint64_t permissionRequestId() const noexcept {
        return permissionRequestId_;
    }
    [[nodiscard]] const QString& permissionOrigin() const noexcept {
        return permissionOrigin_;
    }
    [[nodiscard]] gui::BrowserPermissionKind permissionKind() const noexcept {
        return permissionKind_;
    }
    [[nodiscard]] int permissionRequestCount() const noexcept {
        return permissionRequestCount_;
    }
    [[nodiscard]] std::uint64_t downloadRequestId() const noexcept {
        return downloadRequestId_;
    }
    [[nodiscard]] const QString& downloadOrigin() const noexcept {
        return downloadOrigin_;
    }
    [[nodiscard]] const QString& suggestedFileName() const noexcept {
        return suggestedFileName_;
    }
    [[nodiscard]] int downloadRequestCount() const noexcept {
        return downloadRequestCount_;
    }
    [[nodiscard]] std::uint64_t downloadUpdateRequestId() const noexcept {
        return downloadUpdateRequestId_;
    }
    [[nodiscard]] gui::BrowserDownloadState downloadState() const noexcept {
        return downloadState_;
    }
    [[nodiscard]] std::int64_t downloadReceivedBytes() const noexcept {
        return downloadReceivedBytes_;
    }
    [[nodiscard]] std::int64_t downloadTotalBytes() const noexcept {
        return downloadTotalBytes_;
    }
    [[nodiscard]] std::uint64_t clearedGeneration() const noexcept {
        return clearedGeneration_;
    }
    [[nodiscard]] int browsingDataClearedCount() const noexcept {
        return browsingDataClearedCount_;
    }
    [[nodiscard]] bool isFullScreen() const noexcept { return isFullScreen_; }
    [[nodiscard]] int fullScreenChangedCount() const noexcept {
        return fullScreenChangedCount_;
    }
    [[nodiscard]] int popupRejectedCount() const noexcept {
        return popupRejectedCount_;
    }
    [[nodiscard]] bool isTabReady() const noexcept { return isTabReady_; }
    [[nodiscard]] std::uint64_t readyTabId() const noexcept {
        return readyTabId_;
    }
    [[nodiscard]] const QString& tabTitle() const noexcept { return tabTitle_; }
    [[nodiscard]] int tabNavigationCompletedCount() const noexcept {
        return tabNavigationCompletedCount_;
    }
    [[nodiscard]] int newTabRequestCount() const noexcept {
        return newTabRequestCount_;
    }

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
    QString visibleUrl_;
    QString title_;
    int navigationStartedCount_{0};
    int navigationCompletedCount_{0};
    std::uint64_t permissionRequestId_{0};
    QString permissionOrigin_;
    gui::BrowserPermissionKind permissionKind_{gui::BrowserPermissionKind::Other};
    int permissionRequestCount_{0};
    std::uint64_t downloadRequestId_{0};
    QString downloadOrigin_;
    QString suggestedFileName_;
    std::uint64_t downloadUpdateRequestId_{0};
    gui::BrowserDownloadState downloadState_{gui::BrowserDownloadState::InProgress};
    std::int64_t downloadReceivedBytes_{0};
    std::int64_t downloadTotalBytes_{-1};
    int downloadRequestCount_{0};
    std::uint64_t clearedGeneration_{0};
    int browsingDataClearedCount_{0};
    bool isFullScreen_{false};
    int fullScreenChangedCount_{0};
    int popupRejectedCount_{0};
    WebView2BrowserBackend* backend_{nullptr};
    void* parentWindowHandle_{nullptr};
    std::uint64_t tabGeneration_{0};
    std::uint64_t readyTabId_{0};
    std::uint64_t startedTabId_{0};
    std::uint64_t completedTabId_{0};
    QString requestedTabUrl_;
    QString tabVisibleUrl_;
    QString tabTitle_;
    int tabNavigationStartedCount_{0};
    int tabNavigationCompletedCount_{0};
    int newTabRequestCount_{0};
    bool isTabReady_{false};
};

class WebView2BrowserTest final : public QObject {
    Q_OBJECT

 private slots:
    void cleanupTestCase();
    void suspensionDefaultsToActiveForBackgroundPlayback();
    void suspensionCoordinatesPendingResume();
    void suspensionHandlesFailureAndStaleCompletion();
    void suspensionIgnoresCompletionAfterInvalidation();
    void popupRequestCompletesEverySafetyActionExactlyOnce();
    void navigationBindsExplicitGenerationsInOrder();
    void secondaryTabBindsNavigationIdsToGenerations();
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
    void rejectedDownloadPathsAlwaysReportTerminalFailure();
    void downloadEventsOutliveTheirOriginTabGeneration();
    void downloadStartFailuresCompleteAndCleanSubscriptions();
    void pendingDownloadCancellationAwaitsObservedTerminal();
    void pendingDownloadCancellationFailureRemainsRetryable();
    void pendingDownloadCancellationFallsBackWithoutSubscriptions();
    void downloadCancellationFailureAllowsRetryAndShutdownRepeats();
    void interruptedDownloadResumeTransactionRollsBackSafely();
    void downloadTerminalSnapshotSurvivesProgressReadFailures();
    void resumableDownloadsUseDedicatedBackendPath();
    void shutdownPermanentlyRejectsReinitialization();
    void controllerCompletionAdoptsOnlyCurrentSuccess();
    void shutdownFullScreenExitRemainsReachableAndOrdered();
    void rejectsEmptyAndRelativeProfilePaths();
    void profileDirectoryRequiresAbsoluteApplicationData();
    void requiresEverySensitiveHandlerBeforeReady();
    void mapsAndHandlesControllerAccelerators();
    void acceleratorFailuresDoNotConsumeWebInput();
    void registersAcceleratorBeforeReadyAndRevokesBeforeClose();
    void usesNativeFindForCurrentTabAndReleasesItBeforeClose();
    void registersAudioStateForEveryTabAndKeepsMuteIndependent();
    void faviconStreamReaderAcceptsPngAndRejectsUnsafePayloads();
    void registersFaviconAndZoomForEveryTab();
    void mapsAndRevokesProcessFailureForEveryTab();
    void exposesUserInitiatedTabRecoveryWithoutSensitiveData();
    void secondaryTabsUseCompleteProfileClearSequence();
    void rejectsSensitiveRequestsFromInactiveTabs();
    void servesControlledPagesOverIpv4Loopback();
    void persistsAndClearsOnlyTemporaryProfileData();
    void clearsSharedProfileFromSecondaryTab();
    void followsControlledLoopbackRedirect();
    void createsSharedProfileTabAndClosesIt();
    void popupSharesTheTemporaryProfile();
    void deniesControlledMicrophonePermission();
    void downloadsOnlyToTheChosenTemporaryPath();
    void cancelsPendingControlledDownload();
    void loadsControlledUploadMediaAndFullScreenPages();
    void reportsRuntimeStatusWithoutBlockingGuiThread();
};

void WebView2BrowserTest::cleanupTestCase() {
    int failedCleanupCount = 0;
    const QSet<QString> profiles = pendingTemporaryProfiles();
    for (const QString& profile : profiles) {
        if (removeTemporaryProfile(profile, kProfileCleanupTimeoutMilliseconds)) {
            pendingTemporaryProfiles().remove(profile);
        } else {
            ++failedCleanupCount;
        }
    }
    QVERIFY2(failedCleanupCount == 0,
             qPrintable(QStringLiteral("未能清理 %1 个测试 Profile")
                            .arg(failedCleanupCount)));
}

void WebView2BrowserTest::suspensionDefaultsToActiveForBackgroundPlayback() {
    SuspensionCoordinator coordinator;

    QCOMPARE(coordinator.controllerReady().action, SuspensionAction::None);
    QVERIFY(!coordinator.mustMute());
}

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

void WebView2BrowserTest::secondaryTabBindsNavigationIdsToGenerations() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_tab_controller.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());

    QVERIFY(source.contains(QStringLiteral("navigation_.reset(generation)")));
    QVERIFY(source.contains(QStringLiteral("navigation_.acceptNavigate(generation)")));
    QVERIFY(source.contains(QStringLiteral("navigation_.start(navigationId)")));
    QVERIFY(source.contains(QStringLiteral("navigation_.complete(navigationId)")));
    QVERIFY(source.contains(QStringLiteral(
        "SnapshotKind::NavigationCompleted")));
    QVERIFY(source.contains(QStringLiteral("SnapshotKind::NavigationStopped")));
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

void WebView2BrowserTest::rejectedDownloadPathsAlwaysReportTerminalFailure() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    const int chooseStart = source.indexOf(
        QStringLiteral("void chooseDownloadPath("));
    const int chooseEnd = source.indexOf(
        QStringLiteral("void cancelDownload("), chooseStart);
    QVERIFY(chooseStart >= 0);
    QVERIFY(chooseEnd > chooseStart);
    const QString choose = source.mid(chooseStart, chooseEnd - chooseStart);

    QVERIFY(choose.contains(QStringLiteral("reportRejectedDownload();")));
    QCOMPARE(choose.count(QStringLiteral("reportRejectedDownload();")), 2);
    QVERIFY(choose.contains(QStringLiteral("BrowserDownloadState::Failed")));
    QVERIFY(choose.contains(QStringLiteral("if (FAILED(result))")));
}

void WebView2BrowserTest::downloadEventsOutliveTheirOriginTabGeneration() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    const auto segment = [&source](const QString& start, const QString& end) {
        const int startIndex = source.indexOf(start);
        const int endIndex = source.indexOf(end, startIndex);
        return startIndex >= 0 && endIndex > startIndex
                   ? source.mid(startIndex, endIndex - startIndex)
                   : QString{};
    };

    const QString choose = segment(QStringLiteral("void chooseDownloadPath("),
                                   QStringLiteral("void cancelDownload("));
    const QString cancel = segment(QStringLiteral("void cancelDownload("),
                                   QStringLiteral("void answerExternalProtocol("));
    const QString emitSegment = segment(
        QStringLiteral("void emitDownloadUpdate("),
        QStringLiteral("void cancelPendingSensitiveRequests("));
    const QString mainRequest = segment(
        QStringLiteral("HRESULT registerDownloadStarting("),
        QStringLiteral("HRESULT registerServerCertificateErrorDetected("));
    const QString dispatcher = segment(
        QStringLiteral("void dispatchDownloadListener("),
        QStringLiteral("void reportError("));
    QVERIFY(!choose.isEmpty());
    QVERIFY(!cancel.isEmpty());
    QVERIFY(!emitSegment.isEmpty());
    QVERIFY(!mainRequest.isEmpty());
    QVERIFY(!dispatcher.isEmpty());

    QVERIFY(choose.contains(
        QStringLiteral("active.generation = pending->generation")));
    QVERIFY(cancel.contains(
        QStringLiteral("candidate.generation = generation")));
    QVERIFY(cancel.contains(QStringLiteral("active.generation")));
    QVERIFY(cancel.contains(QStringLiteral("dispatchDownloadListener(")));
    QVERIFY(!cancel.contains(QStringLiteral("dispatchListener(")));
    QVERIFY(emitSegment.contains(QStringLiteral("active.generation")));
    QVERIFY(emitSegment.contains(QStringLiteral("dispatchDownloadListener(")));
    QVERIFY(!emitSegment.contains(QStringLiteral("generation_")));
    QVERIFY(mainRequest.contains(QStringLiteral("dispatchDownloadListener(")));
    QVERIFY(!mainRequest.contains(QStringLiteral("dispatchListener(")));
    QVERIFY(dispatcher.contains(QStringLiteral("lifecycleSerial == lifecycleSerial_")));
    QVERIFY(!dispatcher.contains(QStringLiteral("isActive(")));
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

void WebView2BrowserTest::interruptedDownloadResumeTransactionRollsBackSafely() {
    const auto run = [](const HRESULT validationResult,
                        const HRESULT registrationResult, const bool canStore,
                        const HRESULT resumeResult) {
        FakeResumableDownload candidate;
        bool didStore = false;
        int resumeCalls = 0;
        int rollbackCalls = 0;
        const DownloadResumeOutcome outcome =
            resumeInterruptedDownloadTransaction(
                candidate,
                [validationResult](FakeResumableDownload&) {
                    return validationResult;
                },
                [registrationResult](FakeResumableDownload& active) {
                    active.hasSubscription = true;
                    return registrationResult;
                },
                [canStore, &didStore](FakeResumableDownload&) {
                    didStore = canStore;
                    return canStore;
                },
                [resumeResult, &resumeCalls] {
                    ++resumeCalls;
                    return resumeResult;
                },
                [&rollbackCalls, &candidate] {
                    ++rollbackCalls;
                    candidate.resetSubscriptions();
                });
        return std::tuple{outcome, candidate.resetCalls,
                          candidate.hasSubscription, didStore, resumeCalls,
                          rollbackCalls};
    };

    const auto [invalid, invalidResets, invalidSubscription, invalidStored,
                invalidResumeCalls, invalidRollbackCalls] =
        run(S_FALSE, S_OK, true, S_OK);
    QCOMPARE(invalid.action, DownloadResumeAction::ReportFailed);
    QCOMPARE(invalid.result, S_FALSE);
    QCOMPARE(invalidResets, 0);
    QVERIFY(!invalidSubscription);
    QVERIFY(!invalidStored);
    QCOMPARE(invalidResumeCalls, 0);
    QCOMPARE(invalidRollbackCalls, 0);

    const auto [validationFailed, validationResets,
                validationSubscription, validationStored,
                validationResumeCalls, validationRollbackCalls] =
        run(E_ACCESSDENIED, S_OK, true, S_OK);
    QCOMPARE(validationFailed.action,
             DownloadResumeAction::RemainRetryable);
    QVERIFY(FAILED(validationFailed.result));
    QCOMPARE(validationResets, 0);
    QVERIFY(!validationSubscription);
    QVERIFY(!validationStored);
    QCOMPARE(validationResumeCalls, 0);
    QCOMPARE(validationRollbackCalls, 0);

    const auto [registrationFailed, registrationResets,
                registrationSubscription, registrationStored,
                registrationResumeCalls, registrationRollbackCalls] =
        run(S_OK, E_FAIL, true, S_OK);
    QCOMPARE(registrationFailed.action,
             DownloadResumeAction::RemainRetryable);
    QCOMPARE(registrationResets, 1);
    QVERIFY(!registrationSubscription);
    QVERIFY(!registrationStored);
    QCOMPARE(registrationResumeCalls, 0);
    QCOMPARE(registrationRollbackCalls, 0);

    const auto [storageFailed, storageResets, storageSubscription,
                storageStored, storageResumeCalls, storageRollbackCalls] =
        run(S_OK, S_OK, false, S_OK);
    QCOMPARE(storageFailed.action, DownloadResumeAction::RemainRetryable);
    QCOMPARE(storageFailed.result, E_UNEXPECTED);
    QCOMPARE(storageResets, 1);
    QVERIFY(!storageSubscription);
    QVERIFY(!storageStored);
    QCOMPARE(storageResumeCalls, 0);
    QCOMPARE(storageRollbackCalls, 0);

    const auto [resumeFailed, resumeResets, resumeSubscription, resumeStored,
                resumeCalls, resumeRollbackCalls] =
        run(S_OK, S_OK, true, E_ABORT);
    QCOMPARE(resumeFailed.action, DownloadResumeAction::RemainRetryable);
    QVERIFY(FAILED(resumeFailed.result));
    QCOMPARE(resumeResets, 1);
    QVERIFY(!resumeSubscription);
    QVERIFY(resumeStored);
    QCOMPARE(resumeCalls, 1);
    QCOMPARE(resumeRollbackCalls, 1);

    const auto [resumed, resumedResets, resumedSubscription, resumedStored,
                resumedCalls, resumedRollbackCalls] =
        run(S_OK, S_OK, true, S_OK);
    QCOMPARE(resumed.action, DownloadResumeAction::Resumed);
    QCOMPARE(resumed.result, S_OK);
    QCOMPARE(resumedResets, 0);
    QVERIFY(resumedSubscription);
    QVERIFY(resumedStored);
    QCOMPARE(resumedCalls, 1);
    QCOMPARE(resumedRollbackCalls, 0);
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

    FakeDownloadSnapshotOperation resumableOperation;
    resumableOperation.state = COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED;
    resumableOperation.interruptReason =
        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_DISCONNECTED;
    resumableOperation.canResume = TRUE;
    const DownloadSnapshot resumable = readDownloadSnapshot(
        &resumableOperation, 500, false);
    QVERIFY(resumable.hasState);
    QVERIFY(resumable.isTerminal);
    QVERIFY(resumable.canResume);
    QCOMPARE(resumable.state,
             gui::BrowserDownloadState::RetryableFailure);
    QVERIFY(std::find(resumableOperation.calls.cbegin(),
                      resumableOperation.calls.cend(),
                      QStringLiteral("can_resume")) !=
            resumableOperation.calls.cend());

    FakeDownloadSnapshotOperation nonResumableOperation;
    nonResumableOperation.state = COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED;
    nonResumableOperation.interruptReason =
        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED;
    nonResumableOperation.canResume = FALSE;
    const DownloadSnapshot nonResumable = readDownloadSnapshot(
        &nonResumableOperation, 500, false);
    QCOMPARE(nonResumable.state, gui::BrowserDownloadState::Failed);
    QVERIFY(!nonResumable.canResume);

    FakeDownloadSnapshotOperation canResumeReadFailed;
    canResumeReadFailed.state = COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED;
    canResumeReadFailed.interruptReason =
        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NETWORK_FAILED;
    canResumeReadFailed.canResumeResult = E_FAIL;
    const DownloadSnapshot readFailed = readDownloadSnapshot(
        &canResumeReadFailed, 500, false);
    QCOMPARE(readFailed.state, gui::BrowserDownloadState::Failed);
    QVERIFY(!readFailed.canResume);

    std::unordered_map<std::uint64_t, int> active{{7, 70}};
    QVERIFY(!eraseTerminalDownloadIfCurrent(active, 7, 11, 12, true));
    QCOMPARE(active.size(), std::size_t{1});
    active.clear();
    QVERIFY(!eraseTerminalDownloadIfCurrent(active, 7, 11, 11, false));
}

void WebView2BrowserTest::resumableDownloadsUseDedicatedBackendPath() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    const int retryStart = source.indexOf(
        QStringLiteral("void retryDownload("));
    const int retryEnd = source.indexOf(
        QStringLiteral("void answerExternalProtocol("), retryStart);
    QVERIFY(retryStart >= 0);
    QVERIFY(retryEnd > retryStart);
    const QString retry = source.mid(retryStart, retryEnd - retryStart);

    QVERIFY(source.contains(QStringLiteral("resumableDownloads_")));
    QVERIFY(retry.contains(QStringLiteral("get_CanResume")));
    QVERIFY(retry.contains(QStringLiteral("->Resume()")));
    QVERIFY(retry.contains(QStringLiteral("resumableDownloads_.extract(found)")));
    QVERIFY(retry.contains(QStringLiteral("previousUpdateSerial")));
    QVERIFY(retry.contains(QStringLiteral("didSynchronouslyUpdate")));
    QVERIFY(retry.contains(QStringLiteral("BrowserDownloadState::InProgress")));
    QVERIFY(retry.contains(
        QStringLiteral("BrowserDownloadState::RetryableFailure")));
    QVERIFY(retry.contains(QStringLiteral("BrowserDownloadState::Failed")));
    QVERIFY(source.contains(QStringLiteral(
        "void WebView2BrowserBackend::retryDownload(")));

    const int emitStart = source.indexOf(
        QStringLiteral("void emitDownloadUpdate("));
    const int emitEnd = source.indexOf(
        QStringLiteral("void cancelPendingSensitiveRequests("), emitStart);
    QVERIFY(emitStart >= 0);
    QVERIFY(emitEnd > emitStart);
    const QString emitBlock = source.mid(emitStart, emitEnd - emitStart);
    QVERIFY(emitBlock.contains(QStringLiteral("snapshot.canResume")));
    QVERIFY(emitBlock.contains(QStringLiteral("resumableDownloads_")));
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

void WebView2BrowserTest::controllerCompletionAdoptsOnlyCurrentSuccess() {
    FakeClosableController staleController;
    {
        ControllerAdoptionTransaction<FakeClosableController> transaction(
            &staleController);
        QVERIFY(!transaction.canAdopt(false, S_OK));
    }
    QCOMPARE(staleController.closeCalls, 1);

    FakeClosableController failedController;
    {
        ControllerAdoptionTransaction<FakeClosableController> transaction(
            &failedController);
        QVERIFY(!transaction.canAdopt(true, E_ACCESSDENIED));
    }
    QCOMPARE(failedController.closeCalls, 1);

    FakeClosableController acceptedController;
    {
        ControllerAdoptionTransaction<FakeClosableController> transaction(
            &acceptedController);
        QVERIFY(transaction.canAdopt(true, S_OK));
        QCOMPARE(transaction.adopt(), &acceptedController);
    }
    QCOMPARE(acceptedController.closeCalls, 0);

    ControllerAdoptionTransaction<FakeClosableController> nullTransaction(nullptr);
    QVERIFY(!nullTransaction.canAdopt(true, S_OK));

    QFile mainSourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QFile tabSourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_tab_controller.cpp"));
    QVERIFY(mainSourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(tabSourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString marker =
        QStringLiteral("ControllerAdoptionTransaction<ICoreWebView2Controller>");
    QVERIFY(QString::fromUtf8(mainSourceFile.readAll()).contains(marker));
    QVERIFY(QString::fromUtf8(tabSourceFile.readAll()).contains(marker));
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
    const int firstTokenReset = release.indexOf(
        QStringLiteral("newWindowRequested_.reset"));
    const int controllerClose = release.indexOf(
        QStringLiteral("controller_->Close()"));
    QVERIFY(fullScreenExit >= 0);
    QVERIFY(firstTokenReset > fullScreenExit);
    QVERIFY(controllerClose > firstTokenReset);
}

void WebView2BrowserTest::rejectsEmptyAndRelativeProfilePaths() {
    const QStringList invalidProfiles{QString{}, QStringLiteral("relative-profile")};
    for (const QString& profile : invalidProfiles) {
        WebView2BrowserBackend backend;
        RecordingBrowserListener listener;
        backend.setEventListener(&listener);
        backend.initialize(nullptr, profile, kGeneration);

        QVERIFY(!listener.hasError());
        QVERIFY(waitUntil([&listener] { return listener.hasError(); }, 1000));
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

void WebView2BrowserTest::mapsAndHandlesControllerAccelerators() {
    struct Case {
        UINT virtualKey;
        bool isControlDown;
        bool isAltDown;
        bool isShiftDown;
        gui::BrowserAccelerator accelerator;
    };
    const QList<Case> cases{
        {static_cast<UINT>('L'), true, false, false,
         gui::BrowserAccelerator::FocusAddress},
        {static_cast<UINT>('H'), true, false, false,
         gui::BrowserAccelerator::ShowHistory},
        {static_cast<UINT>('J'), true, false, false,
         gui::BrowserAccelerator::ShowDownloads},
        {static_cast<UINT>('O'), true, false, true,
         gui::BrowserAccelerator::ShowFavorites},
        {static_cast<UINT>('T'), true, false, false,
         gui::BrowserAccelerator::NewTab},
        {static_cast<UINT>('W'), true, false, false,
         gui::BrowserAccelerator::CloseTab},
        {VK_TAB, true, false, false, gui::BrowserAccelerator::NextTab},
        {VK_TAB, true, false, true, gui::BrowserAccelerator::PreviousTab},
        {static_cast<UINT>('F'), true, false, false,
         gui::BrowserAccelerator::FindInPage},
        {static_cast<UINT>('T'), true, false, true,
         gui::BrowserAccelerator::ReopenClosedTab},
        {VK_LEFT, false, true, false, gui::BrowserAccelerator::Back},
        {VK_RIGHT, false, true, false, gui::BrowserAccelerator::Forward},
        {static_cast<UINT>('R'), true, false, false,
         gui::BrowserAccelerator::Reload},
        {VK_OEM_PLUS, true, false, false,
         gui::BrowserAccelerator::ZoomIn},
        {VK_OEM_PLUS, true, false, true,
         gui::BrowserAccelerator::ZoomIn},
        {VK_ADD, true, false, false, gui::BrowserAccelerator::ZoomIn},
        {VK_OEM_MINUS, true, false, false,
         gui::BrowserAccelerator::ZoomOut},
        {VK_SUBTRACT, true, false, false,
         gui::BrowserAccelerator::ZoomOut},
        {static_cast<UINT>('0'), true, false, false,
         gui::BrowserAccelerator::ResetZoom},
        {VK_F5, false, false, false, gui::BrowserAccelerator::Reload},
        {VK_F6, false, false, false, gui::BrowserAccelerator::FocusCycle},
        {VK_ESCAPE, false, false, false,
         gui::BrowserAccelerator::ExitFullScreen},
    };
    for (const Case& item : cases) {
        FakeAcceleratorArgs args;
        args.virtualKey = item.virtualKey;
        if (item.isAltDown) {
            args.kind = COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN;
        }
        const AcceleratorDispatch result = handleAcceleratorKey(
            args, item.isControlDown, item.isAltDown, item.isShiftDown, false,
            item.accelerator == gui::BrowserAccelerator::ExitFullScreen);
        QVERIFY(result.accelerator.has_value());
        QCOMPARE(*result.accelerator, item.accelerator);
        QCOMPARE(args.handledCalls, 1);
        QCOMPARE(args.handled, TRUE);
        QVERIFY(SUCCEEDED(result.status));
    }

    FakeAcceleratorArgs repeat;
    repeat.virtualKey = VK_F5;
    repeat.physicalStatus.WasKeyDown = TRUE;
    const AcceleratorDispatch repeatResult =
        handleAcceleratorKey(repeat, false, false, false, false, false);
    QVERIFY(!repeatResult.accelerator.has_value());
    QCOMPARE(repeat.handledCalls, 0);

    FakeAcceleratorArgs keyUp;
    keyUp.kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_UP;
    keyUp.virtualKey = VK_F5;
    const AcceleratorDispatch keyUpResult =
        handleAcceleratorKey(keyUp, false, false, false, false, false);
    QVERIFY(!keyUpResult.accelerator.has_value());
    QCOMPARE(keyUp.handledCalls, 0);

    FakeAcceleratorArgs shiftedWithoutControl;
    shiftedWithoutControl.virtualKey = static_cast<UINT>('T');
    const AcceleratorDispatch shiftedWithoutControlResult =
        handleAcceleratorKey(shiftedWithoutControl, false, false, true, false,
                             false);
    QVERIFY(!shiftedWithoutControlResult.accelerator.has_value());
    QCOMPARE(shiftedWithoutControl.handledCalls, 0);
}

void WebView2BrowserTest::acceleratorFailuresDoNotConsumeWebInput() {
    FakeAcceleratorArgs unknown;
    unknown.virtualKey = static_cast<UINT>('X');
    const AcceleratorDispatch unknownResult =
        handleAcceleratorKey(unknown, true, false, false, false, false);
    QVERIFY(!unknownResult.accelerator.has_value());
    QCOMPARE(unknown.handledCalls, 0);

    FakeAcceleratorArgs getterFailure;
    getterFailure.virtualKey = static_cast<UINT>('L');
    getterFailure.keyResult = E_FAIL;
    const AcceleratorDispatch getterResult =
        handleAcceleratorKey(getterFailure, true, false, false, false, false);
    QVERIFY(!getterResult.accelerator.has_value());
    QVERIFY(FAILED(getterResult.status));
    QCOMPARE(getterFailure.handledCalls, 0);

    FakeAcceleratorArgs setterFailure;
    setterFailure.virtualKey = VK_F5;
    setterFailure.handledResult = E_ACCESSDENIED;
    const AcceleratorDispatch setterResult =
        handleAcceleratorKey(setterFailure, false, false, false, false, false);
    QVERIFY(!setterResult.accelerator.has_value());
    QVERIFY(FAILED(setterResult.status));
    QCOMPARE(setterFailure.handledCalls, 2);

    FakeAcceleratorArgs escapeOutsideFullScreen;
    escapeOutsideFullScreen.virtualKey = VK_ESCAPE;
    const AcceleratorDispatch escapeResult = handleAcceleratorKey(
        escapeOutsideFullScreen, false, false, false, false, false);
    QVERIFY(!escapeResult.accelerator.has_value());
    QCOMPARE(escapeOutsideFullScreen.handledCalls, 0);

    FakeAcceleratorArgs shiftedReload;
    shiftedReload.virtualKey = VK_F5;
    const AcceleratorDispatch shiftedResult = handleAcceleratorKey(
        shiftedReload, false, false, true, false, false);
    QVERIFY(!shiftedResult.accelerator.has_value());
    QCOMPARE(shiftedReload.handledCalls, 0);
}

void WebView2BrowserTest::registersAcceleratorBeforeReadyAndRevokesBeforeClose() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    const int finishStart = source.indexOf(QStringLiteral("void finishController("));
    const int finishEnd = source.indexOf(
        QStringLiteral("HRESULT configureDefaultDownloadDirectory()"), finishStart);
    const QString finish = source.mid(finishStart, finishEnd - finishStart);
    const int registerEvents =
        finish.indexOf(QStringLiteral("result = registerEvents();"));
    const int ready = finish.indexOf(QStringLiteral("isReady_ = true;"));
    QVERIFY(registerEvents >= 0);
    QVERIFY(ready > registerEvents);
    QVERIFY(source.contains(
        QStringLiteral("result = registerAcceleratorKeyPressed();")));
    const int handlerStart = source.indexOf(
        QStringLiteral("HRESULT registerAcceleratorKeyPressed()"));
    const int handlerEnd = source.indexOf(
        QStringLiteral("void emitNavigationSnapshot"), handlerStart);
    const QString handler = source.mid(handlerStart, handlerEnd - handlerStart);
    QVERIFY(handler.contains(QStringLiteral("add_AcceleratorKeyPressed")));
    QVERIFY(handler.contains(QStringLiteral("handleAcceleratorKey")));
    QVERIFY(handler.contains(QStringLiteral("onAcceleratorRequested")));
    QVERIFY(handler.contains(QStringLiteral("remove_AcceleratorKeyPressed")));
    QFile policyFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_accelerator.h"));
    QVERIFY(policyFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString policy = QString::fromUtf8(policyFile.readAll());
    QVERIFY(policy.contains(QStringLiteral("put_Handled(TRUE)")));
    QVERIFY(policy.contains(QStringLiteral("put_Handled(FALSE)")));

    const int releaseStart = source.indexOf(
        QStringLiteral("void releaseBrowserResources() noexcept"));
    const int releaseEnd = source.indexOf(
        QStringLiteral("void releaseComApartment() noexcept"), releaseStart);
    const QString release = source.mid(releaseStart, releaseEnd - releaseStart);
    const int revoke = release.indexOf(
        QStringLiteral("acceleratorKeyPressed_.reset();"));
    const int close = release.indexOf(QStringLiteral("controller_->Close()"));
    QVERIFY(revoke >= 0);
    QVERIFY(close > revoke);
}

void WebView2BrowserTest::usesNativeFindForCurrentTabAndReleasesItBeforeClose() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    QFile tabSourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_tab_controller.cpp"));
    QVERIFY2(tabSourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(tabSourceFile.errorString()));
    const QString tabSource = QString::fromUtf8(tabSourceFile.readAll());

    const QStringList nativeFindMarkers{
        QStringLiteral("ICoreWebView2_28"),
        QStringLiteral("get_Find"),
        QStringLiteral("ICoreWebView2Environment15"),
        QStringLiteral("CreateFindOptions"),
        QStringLiteral("put_FindTerm"),
        QStringLiteral("put_IsCaseSensitive(FALSE)"),
        QStringLiteral("put_ShouldHighlightAllMatches(TRUE)"),
        QStringLiteral("put_ShouldMatchWord(FALSE)"),
        QStringLiteral("put_SuppressDefaultFindDialog(TRUE)"),
        QStringLiteral("add_ActiveMatchIndexChanged"),
        QStringLiteral("add_MatchCountChanged"),
        QStringLiteral("Start"),
        QStringLiteral("FindNext"),
        QStringLiteral("FindPrevious"),
        QStringLiteral("Stop"),
    };
    for (const QString& marker : nativeFindMarkers) {
        QVERIFY2(source.contains(marker), qPrintable(marker));
        QVERIFY2(tabSource.contains(marker), qPrintable(marker));
    }
    QVERIFY(source.contains(QStringLiteral("found->second->findInPage")));
    QVERIFY(source.contains(QStringLiteral("found->second->stopFinding")));
    QVERIFY(!source.contains(QStringLiteral("ExecuteScript") +
                             QStringLiteral("(find")));
    QVERIFY(!tabSource.contains(QStringLiteral("window.find")));

    const int mainReleaseStart = source.indexOf(
        QStringLiteral("void releaseBrowserResources() noexcept"));
    const int mainReleaseEnd = source.indexOf(
        QStringLiteral("void releaseComApartment() noexcept"), mainReleaseStart);
    const QString mainRelease =
        source.mid(mainReleaseStart, mainReleaseEnd - mainReleaseStart);
    const int mainFindRelease = mainRelease.indexOf(
        QStringLiteral("releaseMainFindController()"));
    const int mainControllerClose =
        mainRelease.indexOf(QStringLiteral("controller_->Close()"));
    QVERIFY(mainFindRelease >= 0);
    QVERIFY(mainControllerClose > mainFindRelease);

    const int releaseHelperStart = source.indexOf(
        QStringLiteral("void releaseMainFindController() noexcept"));
    const int releaseHelperEnd = source.indexOf(
        QStringLiteral("bool isActive("), releaseHelperStart);
    const QString releaseHelper =
        source.mid(releaseHelperStart, releaseHelperEnd - releaseHelperStart);
    QVERIFY(releaseHelper.contains(
        QStringLiteral("findActiveMatchIndexChanged_.reset()")));
    QVERIFY(releaseHelper.contains(
        QStringLiteral("findMatchCountChanged_.reset()")));

    const int tabCloseStart = tabSource.indexOf(
        QStringLiteral("void WebView2TabController::close() noexcept"));
    const QString tabClose = tabSource.mid(tabCloseStart);
    const int tabFindRelease = tabClose.indexOf(
        QStringLiteral("releaseFindController()"));
    const int tabControllerClose =
        tabClose.indexOf(QStringLiteral("controller_->Close()"));
    QVERIFY(tabFindRelease >= 0);
    QVERIFY(tabControllerClose > tabFindRelease);

    const int tabReleaseHelperStart = tabSource.indexOf(
        QStringLiteral("void WebView2TabController::releaseFindController() noexcept"));
    const int tabReleaseHelperEnd = tabSource.indexOf(
        QStringLiteral("void WebView2TabController::setBounds"),
        tabReleaseHelperStart);
    const QString tabReleaseHelper = tabSource.mid(
        tabReleaseHelperStart, tabReleaseHelperEnd - tabReleaseHelperStart);
    QVERIFY(tabReleaseHelper.contains(
        QStringLiteral("findActiveMatchIndexChanged_.reset()")));
    QVERIFY(tabReleaseHelper.contains(
        QStringLiteral("findMatchCountChanged_.reset()")));

    const QStringList raceSafetyMarkers{
        QStringLiteral("mainFindRequestSerial_"),
        QStringLiteral("nextMainFindRequestSerial"),
        QStringLiteral("isCurrentMainFindRequest"),
        QStringLiteral("observeMainFindResults"),
        QStringLiteral("generation != generation_ ||"),
        QStringLiteral("requestSerial != mainFindRequestSerial_"),
        QStringLiteral("stopMainFinding"),
    };
    for (const QString& marker : raceSafetyMarkers) {
        QVERIFY2(source.contains(marker), qPrintable(marker));
    }

    const QStringList tabRaceSafetyMarkers{
        QStringLiteral("findRequestSerial_"),
        QStringLiteral("nextFindRequestSerial"),
        QStringLiteral("isCurrentFindRequest"),
        QStringLiteral("observeFindResults"),
        QStringLiteral("generation != generation_ ||"),
        QStringLiteral("requestSerial != findRequestSerial_"),
        QStringLiteral("stopFinding(true)"),
    };
    for (const QString& marker : tabRaceSafetyMarkers) {
        QVERIFY2(tabSource.contains(marker), qPrintable(marker));
    }
}

void WebView2BrowserTest::registersAudioStateForEveryTabAndKeepsMuteIndependent() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    QFile tabSourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_tab_controller.cpp"));
    QVERIFY2(tabSourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(tabSourceFile.errorString()));
    const QString tabSource = QString::fromUtf8(tabSourceFile.readAll());
    QFile tabHeaderFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_tab_controller.h"));
    QVERIFY2(tabHeaderFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(tabHeaderFile.errorString()));
    const QString tabHeader = QString::fromUtf8(tabHeaderFile.readAll());

    const auto segment = [](const QString& content, const QString& start,
                            const QString& end) {
        const int startIndex = content.indexOf(start);
        const int endIndex = content.indexOf(end, startIndex + start.size());
        return startIndex >= 0 && endIndex > startIndex
                   ? content.mid(startIndex, endIndex - startIndex)
                   : QString{};
    };

    const QString registerEvents = segment(
        source, QStringLiteral("HRESULT registerEvents()"),
        QStringLiteral("HRESULT handlePermissionRequest("));
    QVERIFY(!registerEvents.isEmpty());
    QVERIFY(registerEvents.contains(
        QStringLiteral("registerDocumentPlayingAudioChanged()")));
    const QString mainAudioHandler = segment(
        source, QStringLiteral("HRESULT registerDocumentPlayingAudioChanged()"),
        QStringLiteral("HRESULT registerAcceleratorKeyPressed()"));
    const QStringList mainAudioMarkers{
        QStringLiteral("ICoreWebView2_8"),
        QStringLiteral("add_IsDocumentPlayingAudioChanged"),
        QStringLiteral("get_IsDocumentPlayingAudio"),
        QStringLiteral("onTabAudioStateChanged"),
        QStringLiteral("remove_IsDocumentPlayingAudioChanged"),
    };
    QVERIFY(!mainAudioHandler.isEmpty());
    for (const QString& marker : mainAudioMarkers) {
        QVERIFY2(mainAudioHandler.contains(marker), qPrintable(marker));
    }
    const QString release = segment(
        source, QStringLiteral("void releaseBrowserResources() noexcept"),
        QStringLiteral("void releaseComApartment() noexcept"));
    const int mainAudioReset =
        release.indexOf(QStringLiteral("documentPlayingAudioChanged_.reset()"));
    const int mainControllerClose =
        release.indexOf(QStringLiteral("controller_->Close()"));
    QVERIFY(mainAudioReset >= 0);
    QVERIFY(mainControllerClose > mainAudioReset);

    const QString tabEvents = segment(
        tabSource, QStringLiteral("HRESULT WebView2TabController::registerEvents()"),
        QStringLiteral("void WebView2TabController::emitNavigationSnapshot("));
    const QStringList tabAudioMarkers{
        QStringLiteral("add_IsDocumentPlayingAudioChanged"),
        QStringLiteral("get_IsDocumentPlayingAudio"),
        QStringLiteral("audioStateCallback_"),
        QStringLiteral("remove_IsDocumentPlayingAudioChanged"),
    };
    QVERIFY(!tabEvents.isEmpty());
    for (const QString& marker : tabAudioMarkers) {
        QVERIFY2(tabEvents.contains(marker), qPrintable(marker));
    }
    const QString tabClose = segment(
        tabSource, QStringLiteral("void WebView2TabController::close() noexcept"),
        QStringLiteral("}  // namespace mediahub::browser_webview2"));
    const int tabAudioReset =
        tabClose.indexOf(QStringLiteral("documentPlayingAudioChanged_.reset()"));
    const int tabControllerClose =
        tabClose.indexOf(QStringLiteral("controller_->Close()"));
    QVERIFY(tabAudioReset >= 0);
    QVERIFY(tabControllerClose > tabAudioReset);

    const QString activateTab = segment(
        source, QStringLiteral("void activateTab("),
        QStringLiteral("void goBack() noexcept"));
    QVERIFY(activateTab.contains(QStringLiteral("put_IsVisible")));
    QVERIFY(activateTab.contains(QStringLiteral("setVisible")));
    QVERIFY(!activateTab.contains(QStringLiteral("setAudioMuted")));

    const QString createTab = segment(
        source, QStringLiteral("[[nodiscard]] bool createTab("),
        QStringLiteral("void closeTab("));
    QVERIFY(createTab.contains(QStringLiteral(
        "isAudioMutedDesired_ || isTabAudioMuted(tabId)")));
    const QString setGlobalMute = segment(
        source, QStringLiteral("void setAudioMuted("),
        QStringLiteral("void setTabAudioMuted("));
    QVERIFY(setGlobalMute.contains(QStringLiteral(
        "isMuted || isTabAudioMuted(tabId)")));
    const QString setTabMute = segment(
        source, QStringLiteral("void setTabAudioMuted("),
        QStringLiteral("[[nodiscard]] bool isTabAudioMuted("));
    QVERIFY(setTabMute.contains(QStringLiteral("tabAudioMuted_[tabId] = isMuted")));
    QVERIFY(setTabMute.contains(QStringLiteral(
        "isAudioMutedDesired_ || isMuted")));
    const QString effectiveMute = segment(
        source, QStringLiteral("void applyEffectiveAudioMute() noexcept"),
        QStringLiteral("void setSuspended("));
    QVERIFY(effectiveMute.contains(QStringLiteral("isAudioMutedDesired_")));
    QVERIFY(effectiveMute.contains(QStringLiteral("isTabAudioMuted(1)")));
    QVERIFY(effectiveMute.contains(QStringLiteral("suspension_.mustMute()")));
    QVERIFY(!effectiveMute.contains(QStringLiteral("activeTabId_")));

    QVERIFY(source.contains(QStringLiteral("bool isAudioMutedDesired_{false}")));
    QVERIFY(tabHeader.contains(QStringLiteral("bool isAudioMuted_{false}")));
}

void WebView2BrowserTest::faviconStreamReaderAcceptsPngAndRejectsUnsafePayloads() {
    const auto streamForBytes = [](const QByteArray& bytes) {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        if (memory == nullptr) {
            return Microsoft::WRL::ComPtr<IStream>{};
        }
        void* const destination = GlobalLock(memory);
        if (destination == nullptr) {
            GlobalFree(memory);
            return Microsoft::WRL::ComPtr<IStream>{};
        }
        if (!bytes.isEmpty()) {
            std::memcpy(destination, bytes.constData(), bytes.size());
        }
        GlobalUnlock(memory);
        Microsoft::WRL::ComPtr<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
            GlobalFree(memory);
            return Microsoft::WRL::ComPtr<IStream>{};
        }
        return stream;
    };

    QByteArray valid("\x89PNG\r\n\x1a\n", 8);
    valid.append("test-payload");
    auto validStream = streamForBytes(valid);
    QVERIFY(validStream != nullptr);
    QByteArray output;
    QCOMPARE(readFaviconPngStream(validStream.Get(), output), S_OK);
    QCOMPARE(output, valid);

    auto invalidStream = streamForBytes(QByteArrayLiteral("not-a-png"));
    QVERIFY(invalidStream != nullptr);
    QCOMPARE(readFaviconPngStream(invalidStream.Get(), output), E_INVALIDARG);
    QVERIFY(output.isEmpty());

    QByteArray oversized(1024 * 1024 + 1, '\0');
    oversized.replace(0, 8, QByteArray("\x89PNG\r\n\x1a\n", 8));
    auto oversizedStream = streamForBytes(oversized);
    QVERIFY(oversizedStream != nullptr);
    QCOMPARE(readFaviconPngStream(oversizedStream.Get(), output), E_INVALIDARG);
    QVERIFY(output.isEmpty());
    QCOMPARE(readFaviconPngStream(nullptr, output), E_POINTER);
}

void WebView2BrowserTest::registersFaviconAndZoomForEveryTab() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    QFile tabSourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_tab_controller.cpp"));
    QVERIFY2(tabSourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(tabSourceFile.errorString()));
    const QString tabSource = QString::fromUtf8(tabSourceFile.readAll());

    const QStringList markers{
        QStringLiteral("ICoreWebView2_15"),
        QStringLiteral("add_FaviconChanged"),
        QStringLiteral("GetFavicon("),
        QStringLiteral("COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG"),
        QStringLiteral("remove_FaviconChanged"),
        QStringLiteral("add_ZoomFactorChanged"),
        QStringLiteral("get_ZoomFactor"),
        QStringLiteral("put_ZoomFactor"),
        QStringLiteral("remove_ZoomFactorChanged"),
        QStringLiteral("std::clamp(zoomFactor, 0.25, 5.0)"),
    };
    for (const QString& marker : markers) {
        QVERIFY2(source.contains(marker), qPrintable(marker));
        QVERIFY2(tabSource.contains(marker), qPrintable(marker));
    }
    QVERIFY(source.contains(QStringLiteral("mainFaviconRequestSerial_")));
    QVERIFY(source.contains(QStringLiteral("isCurrentMainFaviconRequest")));
    QVERIFY(tabSource.contains(QStringLiteral("faviconRequestSerial_")));
    QVERIFY(tabSource.contains(QStringLiteral("isCurrentFaviconRequest")));
    QVERIFY(source.contains(QStringLiteral("dispatchTabListener(")));
    QVERIFY(!source.contains(QStringLiteral("get_FaviconUri")));
    QVERIFY(!tabSource.contains(QStringLiteral("get_FaviconUri")));
    QVERIFY(!source.contains(QStringLiteral("QNetworkAccessManager")));
    QVERIFY(!tabSource.contains(QStringLiteral("QNetworkAccessManager")));

    const int mainCloseStart = source.indexOf(
        QStringLiteral("void closeTab(const std::uint64_t tabId)"));
    const int mainCloseEnd = source.indexOf(
        QStringLiteral("void activateTab("), mainCloseStart);
    const QString mainClose =
        source.mid(mainCloseStart, mainCloseEnd - mainCloseStart);
    const int faviconReset =
        mainClose.indexOf(QStringLiteral("faviconChanged_.reset()"));
    const int zoomReset =
        mainClose.indexOf(QStringLiteral("zoomFactorChanged_.reset()"));
    const int controllerClose =
        mainClose.indexOf(QStringLiteral("controller_->Close()"));
    QVERIFY(faviconReset >= 0);
    QVERIFY(zoomReset >= 0);
    QVERIFY(controllerClose > faviconReset);
    QVERIFY(controllerClose > zoomReset);
    QVERIFY(mainClose.contains(QStringLiteral("tabZoomFactors_.erase(tabId)")));

    const int tabCloseStart = tabSource.indexOf(
        QStringLiteral("void WebView2TabController::close() noexcept"));
    const QString tabClose = tabSource.mid(tabCloseStart);
    QVERIFY(tabClose.indexOf(QStringLiteral("faviconChanged_.reset()")) >= 0);
    QVERIFY(tabClose.indexOf(QStringLiteral("zoomFactorChanged_.reset()")) >= 0);
    QVERIFY(tabClose.indexOf(QStringLiteral("controller_->Close()")) >
            tabClose.indexOf(QStringLiteral("faviconChanged_.reset()")));

    const int activateStart = source.indexOf(QStringLiteral("void activateTab("));
    const int activateEnd = source.indexOf(QStringLiteral("void goBack()"),
                                           activateStart);
    const QString activate =
        source.mid(activateStart, activateEnd - activateStart);
    QVERIFY(!activate.contains(QStringLiteral("put_ZoomFactor")));
    QVERIFY(!activate.contains(QStringLiteral("setZoomFactor")));
}

void WebView2BrowserTest::mapsAndRevokesProcessFailureForEveryTab() {
    QCOMPARE(classifyProcessFailureKind(
                 COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED),
             gui::BrowserProcessFailureKind::BrowserProcessExited);
    QCOMPARE(classifyProcessFailureKind(
                 COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED),
             gui::BrowserProcessFailureKind::RenderProcessExited);
    QCOMPARE(classifyProcessFailureKind(
                 COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED),
             gui::BrowserProcessFailureKind::RenderProcessExited);
    QCOMPARE(classifyProcessFailureKind(
                 COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE),
             gui::BrowserProcessFailureKind::RenderProcessUnresponsive);
    QCOMPARE(classifyProcessFailureKind(
                 COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED),
             gui::BrowserProcessFailureKind::OtherProcessExited);

    QFile typesFile(
        QStringLiteral(MEDIAHUB_SOURCE_DIR "/apps/gui/browser_types.h"));
    QVERIFY2(typesFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(typesFile.errorString()));
    const QString types = QString::fromUtf8(typesFile.readAll());
    QFile listenerFile(
        QStringLiteral(MEDIAHUB_SOURCE_DIR "/apps/gui/browser_event_listener.h"));
    QVERIFY2(listenerFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(listenerFile.errorString()));
    const QString listener = QString::fromUtf8(listenerFile.readAll());
    QVERIFY(types.contains(QStringLiteral("enum class BrowserProcessFailureKind")));
    QVERIFY(!types.contains(QStringLiteral("COREWEBVIEW2_PROCESS_FAILED_KIND")));
    QVERIFY(listener.contains(QStringLiteral("onTabProcessFailed")));
    QVERIFY(!listener.contains(QStringLiteral("ICoreWebView2ProcessFailedEventArgs")));

    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    QFile tabSourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_tab_controller.cpp"));
    QVERIFY2(tabSourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(tabSourceFile.errorString()));
    const QString tabSource = QString::fromUtf8(tabSourceFile.readAll());

    const QStringList requiredMarkers{
        QStringLiteral("add_ProcessFailed"),
        QStringLiteral("get_ProcessFailedKind"),
        QStringLiteral("classifyProcessFailureKind"),
        QStringLiteral("remove_ProcessFailed"),
    };
    for (const QString& marker : requiredMarkers) {
        QVERIFY2(source.contains(marker), qPrintable(marker));
        QVERIFY2(tabSource.contains(marker), qPrintable(marker));
    }
    QVERIFY(source.contains(QStringLiteral("onTabProcessFailed")));
    QVERIFY(tabSource.contains(QStringLiteral("processFailedCallback_")));
    QVERIFY(source.contains(QStringLiteral("dispatchListener(")));
    QVERIFY(source.contains(QStringLiteral("dispatchTabListener(")));
    QVERIFY(source.contains(QStringLiteral(
        "found->second->generation() != failedGeneration")));
    QVERIFY(source.contains(QStringLiteral("hasReportedBrowserProcessFailure_")));
    QVERIFY(source.contains(QStringLiteral("const std::uint64_t reportedTabId")));
    QVERIFY(source.contains(QStringLiteral("const std::uint64_t reportedGeneration")));
    QVERIFY(source.contains(QStringLiteral("generation_ != reportedGeneration")));
    QVERIFY(source.contains(
        QStringLiteral("controller_ == nullptr || webView_ == nullptr")));

    const int mainReleaseStart = source.indexOf(
        QStringLiteral("void releaseBrowserResources() noexcept"));
    const int mainReleaseEnd = source.indexOf(
        QStringLiteral("void releaseComApartment() noexcept"), mainReleaseStart);
    const QString mainRelease =
        source.mid(mainReleaseStart, mainReleaseEnd - mainReleaseStart);
    QVERIFY(mainRelease.indexOf(QStringLiteral("processFailed_.reset()")) >= 0);
    QVERIFY(mainRelease.indexOf(QStringLiteral("controller_->Close()")) >
            mainRelease.indexOf(QStringLiteral("processFailed_.reset()")));

    const int tabCloseStart = tabSource.indexOf(
        QStringLiteral("void WebView2TabController::close() noexcept"));
    const QString tabClose = tabSource.mid(tabCloseStart);
    QVERIFY(tabClose.indexOf(QStringLiteral("processFailed_.reset()")) >= 0);
    QVERIFY(tabClose.indexOf(QStringLiteral("controller_->Close()")) >
            tabClose.indexOf(QStringLiteral("processFailed_.reset()")));
}

void WebView2BrowserTest::exposesUserInitiatedTabRecoveryWithoutSensitiveData() {
    QFile backendHeader(
        QStringLiteral(MEDIAHUB_SOURCE_DIR "/apps/gui/browser_backend.h"));
    QVERIFY2(backendHeader.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(backendHeader.errorString()));
    const QString interfaceSource = QString::fromUtf8(backendHeader.readAll());
    QVERIFY(interfaceSource.contains(QStringLiteral("bool recoverTab(std::uint64_t tabId,")));
    QVERIFY(!interfaceSource.contains(QStringLiteral("ICoreWebView2")));

    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    const int recoveryStart = source.indexOf(
        QStringLiteral("bool recoverTab(const std::uint64_t tabId,"));
    const int recoveryEnd = source.indexOf(
        QStringLiteral("void findInPage("), recoveryStart);
    QVERIFY(recoveryStart >= 0);
    QVERIFY(recoveryEnd > recoveryStart);
    const QString recovery = source.mid(recoveryStart,
                                        recoveryEnd - recoveryStart);
    QVERIFY(recovery.contains(QStringLiteral("webView_->Reload()")));
    QVERIFY(recovery.contains(QStringLiteral("found->second->reload(generation)")));
    QVERIFY(!recovery.contains(QStringLiteral("reloadOrStop()")));
    QVERIFY(recovery.contains(QStringLiteral("navigation_.acceptNavigate(generation)")));
    QVERIFY(recovery.contains(QStringLiteral("return SUCCEEDED(result)")));
    QVERIFY(!recovery.contains(QStringLiteral("webView_->Navigate(")));
    QVERIFY(!recovery.contains(QStringLiteral("normalizedUrl")));
}

void WebView2BrowserTest::requiresEverySensitiveHandlerBeforeReady() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    QFile tabSourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_tab_controller.cpp"));
    QVERIFY2(tabSourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(tabSourceFile.errorString()));
    const QString tabSource = QString::fromUtf8(tabSourceFile.readAll());

    const int popupCreateStart = tabSource.indexOf(
        QStringLiteral("HRESULT WebView2TabController::create("));
    const int popupCreateEnd = tabSource.indexOf(
        QStringLiteral("HRESULT WebView2TabController::createController()"));
    QVERIFY(popupCreateStart >= 0);
    QVERIFY(popupCreateEnd > popupCreateStart);
    const QString popupCreate = tabSource.mid(
        popupCreateStart, popupCreateEnd - popupCreateStart);
    const int storePopupDeferral =
        popupCreate.indexOf(QStringLiteral("pendingDeferral_ = pendingDeferral"));
    QVERIFY(storePopupDeferral >= 0);
    QVERIFY(popupCreate.contains(QStringLiteral("completePendingRequest(false)")));
    QVERIFY(tabSource.contains(
        QStringLiteral("CreateCoreWebView2ControllerWithOptions")));
    QVERIFY(tabSource.contains(QStringLiteral("options->put_ProfileName")));
    const QStringList tabSensitiveCallbacks{
        QStringLiteral("permissionCallback_"),
        QStringLiteral("screenCaptureCallback_"),
        QStringLiteral("downloadCallback_"),
        QStringLiteral("certificateCallback_"),
        QStringLiteral("externalProtocolCallback_"),
    };
    for (const QString& callback : tabSensitiveCallbacks) {
        QVERIFY2(tabSource.contains(callback), qPrintable(callback));
    }
    const QStringList sharedSensitiveHandlers{
        QStringLiteral("handlePermissionRequest(tabId, tabGeneration, args)"),
        QStringLiteral("handleScreenCaptureRequest(tabId, tabGeneration, args)"),
        QStringLiteral("handleDownloadRequest(tabId, tabGeneration, args)"),
        QStringLiteral("handleCertificateRequest(tabId, tabGeneration, args)"),
        QStringLiteral("handleExternalProtocolRequest(tabId, tabGeneration"),
    };
    for (const QString& handler : sharedSensitiveHandlers) {
        QVERIFY2(source.contains(handler), qPrintable(handler));
    }

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
          QStringLiteral("onTabDownloadRequested(")}},
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
    QVERIFY(source.contains(
        QStringLiteral("listener.onNewTabRequested(requestId, url)")));
    QVERIFY(source.contains(QStringLiteral("listener.onTabDocumentStateChanged")));
    QVERIFY(source.contains(QStringLiteral("preparePopupRequest(args")));
    QVERIFY(source.contains(QStringLiteral("pendingNewWindows_.insert")));
    QVERIFY(source.contains(QStringLiteral("pendingNewWindows_.take(requestId)")));
    QVERIFY(source.contains(QStringLiteral("pendingNewWindows_.takeAll()")));
    QVERIFY(!source.contains(QStringLiteral("pendingNewWindowArgs_")));
    const int dispatchStart = source.indexOf(
        QStringLiteral("void dispatchListener("));
    const int dispatchEnd = source.indexOf(
        QStringLiteral("void reportError("), dispatchStart);
    QVERIFY(dispatchStart >= 0);
    QVERIFY(dispatchEnd > dispatchStart);
    const QString dispatchers =
        source.mid(dispatchStart, dispatchEnd - dispatchStart);
    QCOMPARE(dispatchers.count(QStringLiteral("Qt::QueuedConnection")), 3);
    QVERIFY(!dispatchers.contains(QStringLiteral("QThread::currentThread")));
    QVERIFY(source.contains(QStringLiteral("std::make_unique<WebView2TabController>")));
    QVERIFY(!source.contains(QStringLiteral("std::make_unique<WebView2PopupWindow>")));
    QVERIFY(source.contains(QStringLiteral("listener.onPopupRejected()")));
    QVERIFY(source.contains(QStringLiteral("return rejectNewWindow(args);")));

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
    QVERIFY(source.contains(QStringLiteral("configureDefaultDownloadDirectory")));
    QVERIFY(source.contains(QStringLiteral("put_DefaultDownloadFolderPath")));
    const int downloadDirectoryGate = finishController.indexOf(
        QStringLiteral("result = configureDefaultDownloadDirectory();"));
    QVERIFY(downloadDirectoryGate >= 0);
    QVERIFY(readyAssignment > downloadDirectoryGate);
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
    const int firstTokenReset = releaseResources.indexOf(
        QStringLiteral("newWindowRequested_.reset"));
    QVERIFY(firstTokenReset >= 0);
    for (const QString& marker : requiredResetMarkers) {
        const int reset = releaseResources.indexOf(marker);
        QVERIFY2(reset >= 0, qPrintable(marker));
        QVERIFY2(reset < controllerClose, qPrintable(marker));
    }
}

void WebView2BrowserTest::secondaryTabsUseCompleteProfileClearSequence() {
    QFile tabSourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_tab_controller.cpp"));
    QVERIFY2(tabSourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(tabSourceFile.errorString()));
    const QString tabSource = QString::fromUtf8(tabSourceFile.readAll());
    const int eventsStart = tabSource.indexOf(QStringLiteral(
        "HRESULT WebView2TabController::registerEvents()"));
    const int eventsEnd = tabSource.indexOf(QStringLiteral(
        "void WebView2TabController::emitNavigationSnapshot("), eventsStart);
    QVERIFY(eventsStart >= 0);
    QVERIFY(eventsEnd > eventsStart);
    const QString events = tabSource.mid(eventsStart, eventsEnd - eventsStart);
    QVERIFY(events.count(QStringLiteral("completeClearData(E_POINTER)")) >= 2);
    QVERIFY(events.contains(QStringLiteral("if (FAILED(uriStatus))")));
    QVERIFY(events.contains(QStringLiteral("completeClearData(uriStatus)")));

    const int clearStart = tabSource.indexOf(QStringLiteral(
        "void WebView2TabController::clearBrowsingData("));
    const int clearEnd = tabSource.indexOf(QStringLiteral(
        "void WebView2TabController::completeClearData("), clearStart);
    QVERIFY(clearStart >= 0);
    QVERIFY(clearEnd > clearStart);
    const QString clear = tabSource.mid(clearStart, clearEnd - clearStart);
    const int allProfile = clear.indexOf(QStringLiteral(
        "COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_PROFILE"));
    const int certificates = clear.indexOf(
        QStringLiteral("ClearServerCertificateErrorActions"));
    const int blank = clear.indexOf(QStringLiteral("Navigate(L\"about:blank\")"));
    QVERIFY(allProfile >= 0);
    QVERIFY(certificates > allProfile);
    QVERIFY(blank > certificates);
    QVERIFY(clear.contains(QStringLiteral("dataAndCertificatesCleared")));
    QVERIFY(clear.contains(QStringLiteral("blankRequestFailed")));
    QVERIFY(clear.contains(
        QStringLiteral("isClearedBlankSnapshotSuppressed_ = true")));

    const int completionStart = tabSource.indexOf(QStringLiteral(
        "ICoreWebView2NavigationCompletedEventArgs* args"));
    const int completionEnd = tabSource.indexOf(
        QStringLiteral("DocumentTitleChanged"), completionStart);
    QVERIFY(completionStart >= 0);
    QVERIFY(completionEnd > completionStart);
    const QString completion =
        tabSource.mid(completionStart, completionEnd - completionStart);
    QVERIFY(completion.contains(QStringLiteral("ownsNavigation")));
    QVERIFY(completion.contains(QStringLiteral("ClearDataNavigationOutcome::Succeeded")));
    QVERIFY(completion.contains(QStringLiteral("completeClearData")));
    QVERIFY(tabSource.contains(
        QStringLiteral("!isClearedBlankSnapshotSuppressed_")));
}

void WebView2BrowserTest::rejectsSensitiveRequestsFromInactiveTabs() {
    QFile sourceFile(QStringLiteral(
        MEDIAHUB_SOURCE_DIR "/src/browser_webview2/webview2_browser_backend.cpp"));
    QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(sourceFile.errorString()));
    const QString source = QString::fromUtf8(sourceFile.readAll());
    const QStringList handlers{
        QStringLiteral("HRESULT handlePermissionRequest("),
        QStringLiteral("HRESULT handleScreenCaptureRequest("),
        QStringLiteral("HRESULT handleDownloadRequest("),
        QStringLiteral("HRESULT handleCertificateRequest("),
        QStringLiteral("HRESULT handleExternalProtocolRequest("),
    };
    for (const QString& handler : handlers) {
        const int start = source.indexOf(handler);
        QVERIFY2(start >= 0, qPrintable(handler));
        const int end = source.indexOf(QStringLiteral("\n    HRESULT "), start + 1);
        const QString segment =
            source.mid(start, end > start ? end - start : source.size() - start);
        const bool hasInactiveRejection =
            segment.contains(QStringLiteral("tabId != activeTabId_"));
        const bool hasActiveOnlyGate =
            segment.contains(QStringLiteral("tabId == activeTabId_"));
        QVERIFY2(hasInactiveRejection || hasActiveOnlyGate,
                 qPrintable(handler));
    }

    const QStringList mainHandlers{
        QStringLiteral("HRESULT registerPermissionRequested()"),
        QStringLiteral("HRESULT registerScreenCaptureStarting()"),
        QStringLiteral("HRESULT registerDownloadStarting()"),
        QStringLiteral("HRESULT registerServerCertificateErrorDetected()"),
        QStringLiteral("HRESULT registerLaunchingExternalUriScheme()"),
        QStringLiteral("HRESULT registerFullScreenChanged()"),
        QStringLiteral("HRESULT registerAcceleratorKeyPressed()"),
    };
    for (const QString& handler : mainHandlers) {
        const int start = source.indexOf(handler);
        QVERIFY2(start >= 0, qPrintable(handler));
        const int end = source.indexOf(QStringLiteral("\n    HRESULT "), start + 1);
        const QString segment =
            source.mid(start, end > start ? end - start : source.size() - start);
        QVERIFY2(segment.contains(QStringLiteral("activeTabId_ != 1")),
                 qPrintable(handler));
    }

    const int navigationStart = source.indexOf(
        QStringLiteral("HRESULT registerNavigationStarting()"));
    const int navigationEnd = source.indexOf(
        QStringLiteral("HRESULT registerNavigationCompleted()"),
        navigationStart + 1);
    QVERIFY(navigationStart >= 0);
    QVERIFY(navigationEnd > navigationStart);
    const QString navigationHandler =
        source.mid(navigationStart, navigationEnd - navigationStart);
    QVERIFY2(!navigationHandler.contains(QStringLiteral("activeTabId_ != 1")),
             "后台主标签的普通导航必须继续更新独立状态");
}

void WebView2BrowserTest::servesControlledPagesOverIpv4Loopback() {
    LocalWebTestServer server;
    QVERIFY(server.start());
    QCOMPARE(server.serverAddress(), QHostAddress::LocalHost);

    QNetworkAccessManager networkManager;
    QNetworkRequest finalRequest(server.url(QStringLiteral("/navigation/final")));
    finalRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                              QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* const finalReply = networkManager.get(finalRequest);
    QVERIFY(waitUntil([finalReply] { return finalReply->isFinished(); }, 5000));
    QCOMPARE(finalReply->error(), QNetworkReply::NoError);
    QCOMPARE(finalReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
             200);
    QVERIFY(finalReply->readAll().contains("<title>redirected</title>"));
    finalReply->deleteLater();

    QNetworkRequest redirectRequest(server.url(QStringLiteral("/redirect")));
    redirectRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                 QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* const redirectReply = networkManager.get(redirectRequest);
    QVERIFY(waitUntil([redirectReply] { return redirectReply->isFinished(); }, 5000));
    QCOMPARE(redirectReply->error(), QNetworkReply::NoError);
    QCOMPARE(redirectReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
             302);
    QCOMPARE(redirectReply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl(),
             QUrl(QStringLiteral("/navigation/final")));
    redirectReply->deleteLater();
}

void WebView2BrowserTest::persistsAndClearsOnlyTemporaryProfileData() {
    if (!isWebView2RuntimeAvailable()) {
        QSKIP("本机没有 WebView2 Runtime，无法执行真实 Profile 集成测试");
    }

    LocalWebTestServer server;
    QVERIFY(server.start());
    QCOMPARE(server.serverAddress(), QHostAddress::LocalHost);
    QCOMPARE(server.url(QStringLiteral("/storage/set")).host(),
             QStringLiteral("127.0.0.1"));

    QWidget window;
    window.resize(640, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));

    TemporaryWebView2Profile profile;
    QVERIFY(profile.isValid());

    WebView2BrowserBackend firstBackend;
    RecordingBrowserListener firstListener;
    firstBackend.setEventListener(&firstListener);
    activateBackend(firstBackend);
    firstBackend.initialize(reinterpret_cast<void*>(window.winId()), profile.path(), 100);
    QVERIFY(waitUntil([&firstListener] { return firstListener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(firstListener.isReady());
    firstBackend.navigate(server.url(QStringLiteral("/storage/set")).toString(), 101);
    const bool isStored = waitUntil(
        [&firstListener] { return firstListener.title() == QStringLiteral("stored"); },
        kRuntimeBehaviorTimeoutMilliseconds);
    QVERIFY2(isStored,
             qPrintable(QStringLiteral("title=%1 request-count=%2 error=%3")
                            .arg(firstListener.title())
                            .arg(server.requestCount(QStringLiteral("/storage/set")))
                            .arg(firstListener.hasError())));
    firstBackend.setEventListener(nullptr);
    firstBackend.shutdown();

    WebView2BrowserBackend secondBackend;
    RecordingBrowserListener secondListener;
    secondBackend.setEventListener(&secondListener);
    activateBackend(secondBackend);
    secondBackend.initialize(reinterpret_cast<void*>(window.winId()), profile.path(), 200);
    QVERIFY(waitUntil([&secondListener] { return secondListener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(secondListener.isReady());
    secondBackend.navigate(server.url(QStringLiteral("/storage/read")).toString(), 201);
    const bool wasPersisted = waitUntil(
        [&secondListener] {
            return secondListener.title() ==
                   QStringLiteral("cookie=1;local=1;indexed=1");
        },
        kRuntimeBehaviorTimeoutMilliseconds);
    QVERIFY2(wasPersisted,
             qPrintable(QStringLiteral(
                            "title=%1 read-count=%2 completed=%3 error=%4")
                            .arg(secondListener.title())
                            .arg(server.requestCount(QStringLiteral("/storage/read")))
                            .arg(secondListener.navigationCompletedCount())
                            .arg(secondListener.hasError())));

    secondBackend.clearBrowsingData(202);
    QVERIFY(waitUntil(
        [&secondListener] {
            return secondListener.browsingDataClearedCount() == 1 &&
                   secondListener.clearedGeneration() == 202;
        },
        kRuntimeBehaviorTimeoutMilliseconds));
    secondBackend.navigate(server.url(QStringLiteral("/storage/read")).toString(), 203);
    QVERIFY(waitUntil(
        [&secondListener] { return secondListener.title() == QStringLiteral("empty"); },
        kRuntimeBehaviorTimeoutMilliseconds));

    secondBackend.setEventListener(nullptr);
    secondBackend.shutdown();
    QVERIFY(profile.cleanup());
}

void WebView2BrowserTest::clearsSharedProfileFromSecondaryTab() {
    if (!isWebView2RuntimeAvailable()) {
        QSKIP("本机没有 WebView2 Runtime，无法执行真实次级标签清除测试");
    }

    LocalWebTestServer server;
    QVERIFY(server.start());
    QWidget window;
    window.resize(640, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
    TemporaryWebView2Profile profile;
    QVERIFY(profile.isValid());

    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    activateBackend(backend);
    void* const parentWindowHandle = reinterpret_cast<void*>(window.winId());
    backend.initialize(parentWindowHandle, profile.path(), 220);
    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(listener.isReady());

    backend.navigate(server.url(QStringLiteral("/storage/set")).toString(), 221);
    QVERIFY(waitUntil(
        [&listener] { return listener.title() == QStringLiteral("stored"); },
        kRuntimeBehaviorTimeoutMilliseconds));
    QVERIFY(backend.createTab(
        parentWindowHandle, 2,
        server.url(QStringLiteral("/storage/read")).toString(), 222));
    backend.activateTab(2);
    const bool didShareProfile = waitUntil(
        [&listener] {
            return listener.isTabReady() &&
                   listener.tabTitle() ==
                       QStringLiteral("cookie=1;local=1;indexed=1");
        },
        kRuntimeBehaviorTimeoutMilliseconds);
    QVERIFY2(didShareProfile,
             qPrintable(QStringLiteral("tab-title=%1 ready=%2 error=%3")
                            .arg(listener.tabTitle())
                            .arg(listener.isTabReady())
                            .arg(listener.hasError())));

    backend.clearBrowsingData(223);
    QVERIFY(waitUntil(
        [&listener] {
            return listener.browsingDataClearedCount() == 1 &&
                   listener.clearedGeneration() == 223;
        },
        kRuntimeBehaviorTimeoutMilliseconds));
    backend.navigate(server.url(QStringLiteral("/storage/read")).toString(), 224);
    QVERIFY(waitUntil(
        [&listener] { return listener.tabTitle() == QStringLiteral("empty"); },
        kRuntimeBehaviorTimeoutMilliseconds));

    backend.closeTab(2);
    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(profile.cleanup());
}

void WebView2BrowserTest::followsControlledLoopbackRedirect() {
    if (!isWebView2RuntimeAvailable()) {
        QSKIP("本机没有 WebView2 Runtime，无法执行真实重定向测试");
    }

    LocalWebTestServer server;
    QVERIFY(server.start());
    QWidget window;
    window.resize(640, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
    TemporaryWebView2Profile profile;
    QVERIFY(profile.isValid());
    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    activateBackend(backend);
    backend.initialize(reinterpret_cast<void*>(window.winId()), profile.path(), 300);
    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(listener.isReady());
    backend.navigate(server.url(QStringLiteral("/redirect")).toString(), 301);
    const bool didRedirect = waitUntil(
        [&listener] { return listener.title() == QStringLiteral("redirected"); },
        kRuntimeBehaviorTimeoutMilliseconds);
    QVERIFY2(didRedirect,
             qPrintable(QStringLiteral(
                            "title=%1 started=%2 completed=%3 redirect=%4 final=%5 "
                            "error=%6")
                            .arg(listener.title())
                            .arg(listener.navigationStartedCount())
                            .arg(listener.navigationCompletedCount())
                            .arg(server.requestCount(QStringLiteral("/redirect")))
                            .arg(server.requestCount(
                                QStringLiteral("/navigation/final")))
                            .arg(listener.hasError())));
    QCOMPARE(QUrl(listener.visibleUrl()).path(), QStringLiteral("/navigation/final"));
    QCOMPARE(server.requestCount(QStringLiteral("/redirect")), 1);
    QCOMPARE(server.requestCount(QStringLiteral("/navigation/final")), 1);

    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(profile.cleanup());
}

void WebView2BrowserTest::createsSharedProfileTabAndClosesIt() {
    if (!isWebView2RuntimeAvailable()) {
        QSKIP("本机没有 WebView2 Runtime，无法执行真实标签集成测试");
    }

    LocalWebTestServer server;
    QVERIFY(server.start());
    QWidget window;
    window.resize(640, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
    TemporaryWebView2Profile profile;
    QVERIFY(profile.isValid());

    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    activateBackend(backend);
    void* const parentWindowHandle = reinterpret_cast<void*>(window.winId());
    backend.initialize(parentWindowHandle, profile.path(), 350);
    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(listener.isReady());

    listener.configureTabCreation(&backend, parentWindowHandle, 351);
    backend.navigate(server.url(QStringLiteral("/popup")).toString(), 351);
    const bool didLoadTab = waitUntil(
        [&listener] {
            return listener.newTabRequestCount() == 1 &&
                   listener.isTabReady() &&
                   listener.tabTitle() == QStringLiteral("child-ready");
        },
        kRuntimeBehaviorTimeoutMilliseconds);
    QVERIFY2(didLoadTab,
             qPrintable(QStringLiteral(
                            "requests=%1 ready=%2 tab-completed=%3 child=%4 "
                            "signal=%5 rejected=%6 main-error=%7")
                            .arg(listener.newTabRequestCount())
                            .arg(listener.isTabReady())
                            .arg(listener.tabNavigationCompletedCount())
                            .arg(server.requestCount(QStringLiteral("/child")))
                            .arg(server.requestCount(
                                QStringLiteral("/signal/child-ready")))
                            .arg(listener.popupRejectedCount())
                            .arg(listener.hasError())));
    QCOMPARE(listener.readyTabId(), std::uint64_t{2});
    QCOMPARE(listener.popupRejectedCount(), 0);
    QCOMPARE(server.requestCount(QStringLiteral("/child")), 1);
    QCOMPARE(server.requestCount(QStringLiteral("/signal/child-ready")), 1);

    backend.activateTab(1);
    backend.activateTab(2);
    QVERIFY(listener.tabNavigationCompletedCount() > 0);
    backend.closeTab(2);
    backend.activateTab(1);
    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(profile.cleanup());
}

void WebView2BrowserTest::popupSharesTheTemporaryProfile() {
    if (!isWebView2RuntimeAvailable()) {
        QSKIP("本机没有 WebView2 Runtime，无法执行真实弹窗测试");
    }

    LocalWebTestServer server;
    QVERIFY(server.start());
    QWidget window;
    window.resize(640, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
    TemporaryWebView2Profile profile;
    QVERIFY(profile.isValid());
    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    activateBackend(backend);
    void* const parentWindowHandle = reinterpret_cast<void*>(window.winId());
    backend.initialize(parentWindowHandle, profile.path(), 400);
    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(listener.isReady());
    listener.configureTabCreation(&backend, parentWindowHandle, 402);
    backend.navigate(server.url(QStringLiteral("/popup")).toString(), 401);
    QVERIFY(waitUntil(
        [&listener] { return listener.title() == QStringLiteral("popup-shared"); },
        kRuntimeBehaviorTimeoutMilliseconds));
    QVERIFY(server.requestCount(QStringLiteral("/child")) >= 1);
    QVERIFY(server.requestCount(QStringLiteral("/signal/child-ready")) >= 1);
    QCOMPARE(listener.newTabRequestCount(), 1);
    QCOMPARE(listener.popupRejectedCount(), 0);

    backend.closeTab(2);
    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(profile.cleanup());
}

void WebView2BrowserTest::deniesControlledMicrophonePermission() {
    if (!isWebView2RuntimeAvailable()) {
        QSKIP("本机没有 WebView2 Runtime，无法执行真实权限测试");
    }

    LocalWebTestServer server;
    QVERIFY(server.start());
    QWidget window;
    window.resize(640, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
    TemporaryWebView2Profile profile;
    QVERIFY(profile.isValid());
    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    activateBackend(backend);
    backend.initialize(reinterpret_cast<void*>(window.winId()), profile.path(), 500);
    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(listener.isReady());
    backend.navigate(server.url(QStringLiteral("/permission")).toString(), 501);
    QVERIFY(waitUntil(
        [&listener] { return listener.permissionRequestCount() == 1; },
        kRuntimeBehaviorTimeoutMilliseconds));
    QCOMPARE(listener.permissionOrigin(), server.origin());
    QCOMPARE(listener.permissionKind(), gui::BrowserPermissionKind::Microphone);
    backend.answerPermission(listener.permissionRequestId(),
                             gui::BrowserPermissionDecision::Deny);
    QVERIFY(waitUntil(
        [&listener] {
            return listener.title() == QStringLiteral("permission=denied");
        },
        kRuntimeBehaviorTimeoutMilliseconds));

    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(profile.cleanup());
}

void WebView2BrowserTest::downloadsOnlyToTheChosenTemporaryPath() {
    if (!isWebView2RuntimeAvailable()) {
        QSKIP("本机没有 WebView2 Runtime，无法执行真实下载测试");
    }

    LocalWebTestServer server;
    QVERIFY(server.start());
    QWidget window;
    window.resize(640, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
    TemporaryWebView2Profile profile;
    QVERIFY(profile.isValid());
    QTemporaryDir downloadDirectory;
    QVERIFY(downloadDirectory.isValid());
    const QString destination =
        downloadDirectory.filePath(QStringLiteral("sample.txt"));
    QVERIFY(!QFileInfo::exists(destination));

    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    activateBackend(backend);
    backend.initialize(reinterpret_cast<void*>(window.winId()), profile.path(), 600);
    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(listener.isReady());
    backend.navigate(server.url(QStringLiteral("/download")).toString(), 601);
    QVERIFY(waitUntil(
        [&listener] { return listener.downloadRequestCount() == 1; },
        kRuntimeBehaviorTimeoutMilliseconds));
    QCOMPARE(listener.downloadOrigin(), server.origin());
    QCOMPARE(listener.suggestedFileName(), QStringLiteral("sample.txt"));
    backend.chooseDownloadPath(listener.downloadRequestId(), destination);
    QVERIFY(waitUntil(
        [&listener] {
            return listener.downloadUpdateRequestId() ==
                       listener.downloadRequestId() &&
                   listener.downloadState() == gui::BrowserDownloadState::Completed;
        },
        kRuntimeBehaviorTimeoutMilliseconds));

    QFile downloadedFile(destination);
    QVERIFY(downloadedFile.open(QIODevice::ReadOnly));
    QCOMPARE(downloadedFile.readAll(), QByteArrayLiteral("MediaHub WebView2 test\n"));
    QVERIFY(listener.downloadReceivedBytes() > 0);
    QCOMPARE(listener.downloadReceivedBytes(), listener.downloadTotalBytes());

    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(profile.cleanup());
}

void WebView2BrowserTest::cancelsPendingControlledDownload() {
    if (!isWebView2RuntimeAvailable()) {
        QSKIP("本机没有 WebView2 Runtime，无法执行真实下载取消测试");
    }

    LocalWebTestServer server;
    QVERIFY(server.start());
    QWidget window;
    window.resize(640, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
    TemporaryWebView2Profile profile;
    QVERIFY(profile.isValid());
    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    activateBackend(backend);
    backend.initialize(reinterpret_cast<void*>(window.winId()), profile.path(), 700);
    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(listener.isReady());
    const QDir isolatedDownloadDirectory(
        QDir(profile.path()).filePath(QStringLiteral("Downloads")));
    QVERIFY(isolatedDownloadDirectory.exists());
    QVERIFY(isolatedDownloadDirectory.isEmpty());
    backend.navigate(server.url(QStringLiteral("/download")).toString(), 701);
    QVERIFY(waitUntil(
        [&listener] { return listener.downloadRequestCount() == 1; },
        kRuntimeBehaviorTimeoutMilliseconds));
    backend.cancelDownload(listener.downloadRequestId());
    QVERIFY(waitUntil(
        [&listener] {
            return listener.downloadUpdateRequestId() ==
                       listener.downloadRequestId() &&
                   listener.downloadState() == gui::BrowserDownloadState::Cancelled;
        },
        kRuntimeBehaviorTimeoutMilliseconds));
    QVERIFY(waitUntil(
        [&isolatedDownloadDirectory] {
            return isolatedDownloadDirectory.isEmpty();
        },
        kRuntimeBehaviorTimeoutMilliseconds));

    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(profile.cleanup());
}

void WebView2BrowserTest::loadsControlledUploadMediaAndFullScreenPages() {
    if (!isWebView2RuntimeAvailable()) {
        QSKIP("本机没有 WebView2 Runtime，无法执行真实受控页面测试");
    }

    LocalWebTestServer server;
    QVERIFY(server.start());
    QWidget window;
    window.resize(640, 360);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window, 5000));
    TemporaryWebView2Profile profile;
    QVERIFY(profile.isValid());
    WebView2BrowserBackend backend;
    RecordingBrowserListener listener;
    backend.setEventListener(&listener);
    activateBackend(backend);
    backend.initialize(reinterpret_cast<void*>(window.winId()), profile.path(), 800);
    QVERIFY(waitUntil([&listener] { return listener.reachedTerminalState(); },
                      kInitializationTimeoutMilliseconds));
    QVERIFY(listener.isReady());
    backend.navigate(server.url(QStringLiteral("/upload")).toString(), 801);
    QVERIFY(waitUntil(
        [&listener] { return listener.title() == QStringLiteral("upload-ready"); },
        kRuntimeBehaviorTimeoutMilliseconds));
    backend.navigate(server.url(QStringLiteral("/media")).toString(), 802);
    QVERIFY(waitUntil(
        [&listener] { return listener.title() == QStringLiteral("media-ready"); },
        kRuntimeBehaviorTimeoutMilliseconds));
    backend.navigate(server.url(QStringLiteral("/fullscreen")).toString(), 803);
    QVERIFY(waitUntil(
        [&listener] {
            return listener.title() == QStringLiteral("fullscreen-ready");
        },
        kRuntimeBehaviorTimeoutMilliseconds));
    backend.navigate(server.url(QStringLiteral("/external")).toString(), 804);
    QVERIFY(waitUntil(
        [&listener] { return listener.title() == QStringLiteral("external-ready"); },
        kRuntimeBehaviorTimeoutMilliseconds));
    QCOMPARE(listener.fullScreenChangedCount(), 0);
    QVERIFY(!listener.isFullScreen());

    backend.setEventListener(nullptr);
    backend.shutdown();
    QVERIFY(profile.cleanup());
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

    TemporaryWebView2Profile testProfile;
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
    QVERIFY(testProfile.cleanup());
}

}  // namespace
}  // namespace mediahub::browser_webview2

QTEST_MAIN(mediahub::browser_webview2::WebView2BrowserTest)

#include "webview2_browser_test.moc"
