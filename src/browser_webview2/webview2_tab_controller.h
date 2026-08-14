#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>

#include <QByteArray>
#include <QRect>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

#include "browser_types.h"
#include "webview2_handles.h"
#include "webview2_state.h"

namespace mediahub::browser_webview2 {

// 调用线程：WebView2 Favicon 完成回调所在的 GUI STA；只接受不超过 1 MiB 的 PNG。
[[nodiscard]] HRESULT readFaviconPngStream(IStream* stream,
                                           QByteArray& pngBytes) noexcept;

// 将 WebView2 进程类别收敛为 GUI 可长期依赖的稳定语义。
[[nodiscard]] gui::BrowserProcessFailureKind classifyProcessFailureKind(
    COREWEBVIEW2_PROCESS_FAILED_KIND kind) noexcept;

// 在现有网页宿主内承载一个独立 WebView，并与其他标签共享 Environment/Profile。
class WebView2TabController final {
 public:
    using ReadyCallback = std::function<void(std::uint64_t, std::uint64_t)>;
    using NavigationStartedCallback =
        std::function<void(std::uint64_t, std::uint64_t)>;
    using NavigationCompletedCallback = std::function<void(
        std::uint64_t, std::uint64_t, const QString&, const QString&, bool, bool)>;
    using DocumentStateChangedCallback = NavigationCompletedCallback;
    using NavigationStoppedCallback = NavigationCompletedCallback;
    using ErrorCallback = std::function<void(std::uint64_t, std::uint64_t,
                                             gui::BrowserErrorKind, HRESULT)>;
    using ProcessFailedCallback = std::function<void(
        std::uint64_t, std::uint64_t, gui::BrowserProcessFailureKind)>;
    using NewWindowCallback =
        std::function<HRESULT(ICoreWebView2NewWindowRequestedEventArgs*)>;
    using ClosedCallback = std::function<void(std::uint64_t)>;
    using FullScreenCallback =
        std::function<void(std::uint64_t, std::uint64_t, bool)>;
    using AudioStateCallback =
        std::function<void(std::uint64_t, std::uint64_t, bool)>;
    using FaviconCallback = std::function<void(
        std::uint64_t, std::uint64_t, std::uint64_t, const QByteArray&)>;
    using FaviconFailedCallback =
        std::function<void(std::uint64_t, std::uint64_t, HRESULT)>;
    using ZoomFactorCallback =
        std::function<void(std::uint64_t, std::uint64_t, double)>;
    using AcceleratorCallback = std::function<void(
        std::uint64_t, std::uint64_t, gui::BrowserAccelerator)>;
    using FindResultCallback = std::function<void(
        std::uint64_t, std::uint64_t, std::uint64_t, int, int)>;
    using FindFailedCallback = std::function<void(
        std::uint64_t, std::uint64_t, std::uint64_t, HRESULT)>;
    using PermissionCallback = std::function<HRESULT(
        std::uint64_t, std::uint64_t,
        ICoreWebView2PermissionRequestedEventArgs*)>;
    using ScreenCaptureCallback = std::function<HRESULT(
        std::uint64_t, std::uint64_t,
        ICoreWebView2ScreenCaptureStartingEventArgs*)>;
    using DownloadCallback = std::function<HRESULT(
        std::uint64_t, std::uint64_t,
        ICoreWebView2DownloadStartingEventArgs*)>;
    using CertificateCallback = std::function<HRESULT(
        std::uint64_t, std::uint64_t,
        ICoreWebView2ServerCertificateErrorDetectedEventArgs*)>;
    using ExternalProtocolCallback = std::function<HRESULT(
        std::uint64_t, std::uint64_t,
        ICoreWebView2LaunchingExternalUriSchemeEventArgs*)>;
    using ClearDataCallback = std::function<void(HRESULT)>;

    WebView2TabController(
        Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment,
        Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile, HWND parentWindow,
        std::uint64_t tabId, ReadyCallback readyCallback,
        NavigationStartedCallback navigationStartedCallback,
        NavigationCompletedCallback navigationCompletedCallback,
        DocumentStateChangedCallback documentStateChangedCallback,
        NavigationStoppedCallback navigationStoppedCallback,
        ErrorCallback errorCallback, ProcessFailedCallback processFailedCallback,
        NewWindowCallback newWindowCallback, ClosedCallback closedCallback,
        FullScreenCallback fullScreenCallback, AudioStateCallback audioStateCallback,
        FaviconCallback faviconCallback,
        FaviconFailedCallback faviconFailedCallback,
        ZoomFactorCallback zoomFactorCallback,
        AcceleratorCallback acceleratorCallback, FindResultCallback findResultCallback,
        FindFailedCallback findFailedCallback,
        PermissionCallback permissionCallback,
        ScreenCaptureCallback screenCaptureCallback,
        DownloadCallback downloadCallback,
        CertificateCallback certificateCallback,
        ExternalProtocolCallback externalProtocolCallback);
    ~WebView2TabController();

    WebView2TabController(const WebView2TabController&) = delete;
    WebView2TabController& operator=(const WebView2TabController&) = delete;

    // 调用线程：GUI 主线程。pendingArgs 非空时完成原 window.open 请求。
    [[nodiscard]] HRESULT create(
        const QString& initialUrl, std::uint64_t generation,
        ICoreWebView2NewWindowRequestedEventArgs* pendingArgs = nullptr,
        ICoreWebView2Deferral* pendingDeferral = nullptr);
    void navigate(const QString& url, std::uint64_t generation);
    // 调用线程：GUI 主线程。成功回调前会等待专用 Profile 清除和空白页导航完成。
    void clearBrowsingData(std::uint64_t generation,
                           ClearDataCallback callback);
    void goBack() noexcept;
    void goForward() noexcept;
    void reloadOrStop() noexcept;
    // 调用线程：GUI 主线程。恢复崩溃标签时始终重新加载，不受导航状态影响。
    [[nodiscard]] bool reload(std::uint64_t generation) noexcept;
    // 调用线程：GUI 主线程。查询文本只交给当前 WebView2 Find 对象。
    void findInPage(const QString& text, bool forward) noexcept;
    // 调用线程：GUI 主线程。WebView2 Stop 会清除查找高亮；参数保留统一后端语义。
    void stopFinding(bool clearSelection) noexcept;
    void setBounds(const QRect& bounds) noexcept;
    void setVisible(bool isVisible) noexcept;
    void setAudioMuted(bool isMuted) noexcept;
    // 调用线程：GUI 主线程。比例会限制在 25% 至 500%。
    void setZoomFactor(double zoomFactor) noexcept;
    void exitFullScreen() noexcept;
    void close() noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }
    // 调用线程：GUI 主线程。供排队事件在交付前复核查找请求身份。
    [[nodiscard]] bool isCurrentFindRequest(
        std::uint64_t generation, std::uint64_t requestSerial) const noexcept;
    // 调用线程：GUI 主线程。供排队事件在交付前复核图标请求身份。
    [[nodiscard]] bool isCurrentFaviconRequest(
        std::uint64_t generation, std::uint64_t requestSerial) const noexcept;

 private:
    [[nodiscard]] HRESULT createController();
    void finishController(HRESULT status, ICoreWebView2Controller* controller);
    [[nodiscard]] HRESULT configureSettings();
    [[nodiscard]] HRESULT registerEvents();
    [[nodiscard]] HRESULT registerFaviconChanged();
    [[nodiscard]] HRESULT registerZoomFactorChanged();
    void requestFavicon(std::uint64_t generation) noexcept;
    void clearFavicon(std::uint64_t generation) noexcept;
    [[nodiscard]] std::uint64_t nextFaviconRequestSerial() noexcept;
    [[nodiscard]] HRESULT ensureFindController();
    [[nodiscard]] HRESULT observeFindResults(
        std::uint64_t generation, std::uint64_t requestSerial);
    void emitFindResult(std::uint64_t generation,
                        std::uint64_t requestSerial);
    [[nodiscard]] std::uint64_t nextFindRequestSerial() noexcept;
    void releaseFindController() noexcept;
    enum class SnapshotKind {
        NavigationCompleted,
        NavigationStopped,
        DocumentStateChanged,
    };
    void emitNavigationSnapshot(std::uint64_t generation, SnapshotKind kind);
    void reportError(gui::BrowserErrorKind kind, HRESULT result,
                     std::uint64_t generation);
    void completeClearData(HRESULT result);
    [[nodiscard]] HRESULT completePendingRequest(bool attachWebView) noexcept;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webView_;
    Microsoft::WRL::ComPtr<ICoreWebView2NewWindowRequestedEventArgs> pendingArgs_;
    Microsoft::WRL::ComPtr<ICoreWebView2Deferral> pendingDeferral_;
    EventRegistration navigationStarting_;
    EventRegistration navigationCompleted_;
    EventRegistration documentTitleChanged_;
    EventRegistration processFailed_;
    EventRegistration newWindowRequested_;
    EventRegistration windowCloseRequested_;
    EventRegistration fullScreenChanged_;
    EventRegistration documentPlayingAudioChanged_;
    EventRegistration faviconChanged_;
    EventRegistration zoomFactorChanged_;
    EventRegistration acceleratorKeyPressed_;
    EventRegistration findActiveMatchIndexChanged_;
    EventRegistration findMatchCountChanged_;
    EventRegistration permissionRequested_;
    EventRegistration screenCaptureStarting_;
    EventRegistration downloadStarting_;
    EventRegistration certificateErrorRequested_;
    EventRegistration externalProtocolRequested_;
    std::shared_ptr<int> lifetime_;
    HWND parentWindow_{nullptr};
    std::uint64_t tabId_{0};
    std::uint64_t generation_{0};
    QRect bounds_;
    QString initialUrl_;
    QString findText_;
    Microsoft::WRL::ComPtr<ICoreWebView2Find> find_;
    std::uint64_t findRequestSerial_{0};
    std::uint64_t faviconRequestSerial_{0};
    ReadyCallback readyCallback_;
    NavigationStartedCallback navigationStartedCallback_;
    NavigationCompletedCallback navigationCompletedCallback_;
    DocumentStateChangedCallback documentStateChangedCallback_;
    NavigationStoppedCallback navigationStoppedCallback_;
    ErrorCallback errorCallback_;
    ProcessFailedCallback processFailedCallback_;
    NewWindowCallback newWindowCallback_;
    ClosedCallback closedCallback_;
    FullScreenCallback fullScreenCallback_;
    AudioStateCallback audioStateCallback_;
    FaviconCallback faviconCallback_;
    FaviconFailedCallback faviconFailedCallback_;
    ZoomFactorCallback zoomFactorCallback_;
    AcceleratorCallback acceleratorCallback_;
    FindResultCallback findResultCallback_;
    FindFailedCallback findFailedCallback_;
    PermissionCallback permissionCallback_;
    ScreenCaptureCallback screenCaptureCallback_;
    DownloadCallback downloadCallback_;
    CertificateCallback certificateCallback_;
    ExternalProtocolCallback externalProtocolCallback_;
    ClearDataCallback clearDataCallback_;
    NavigationTracker navigation_;
    ClearDataNavigationCoordinator clearDataNavigation_;
    bool isVisible_{false};
    bool isAudioMuted_{false};
    double zoomFactor_{1.0};
    bool isFullScreen_{false};
    bool isPopupRequest_{false};
    bool isClearedBlankSnapshotSuppressed_{false};
    bool isClosed_{false};
};

}  // namespace mediahub::browser_webview2
