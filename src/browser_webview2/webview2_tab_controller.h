#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>

#include <QRect>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

#include "browser_types.h"
#include "webview2_handles.h"
#include "webview2_state.h"

namespace mediahub::browser_webview2 {

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
    using NewWindowCallback =
        std::function<HRESULT(ICoreWebView2NewWindowRequestedEventArgs*)>;
    using ClosedCallback = std::function<void(std::uint64_t)>;
    using FullScreenCallback =
        std::function<void(std::uint64_t, std::uint64_t, bool)>;
    using AcceleratorCallback = std::function<void(
        std::uint64_t, std::uint64_t, gui::BrowserAccelerator)>;
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
        ErrorCallback errorCallback,
        NewWindowCallback newWindowCallback, ClosedCallback closedCallback,
        FullScreenCallback fullScreenCallback,
        AcceleratorCallback acceleratorCallback,
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
    void setBounds(const QRect& bounds) noexcept;
    void setVisible(bool isVisible) noexcept;
    void setAudioMuted(bool isMuted) noexcept;
    void exitFullScreen() noexcept;
    void close() noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }

 private:
    [[nodiscard]] HRESULT createController();
    void finishController(HRESULT status, ICoreWebView2Controller* controller);
    [[nodiscard]] HRESULT configureSettings();
    [[nodiscard]] HRESULT registerEvents();
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
    EventRegistration newWindowRequested_;
    EventRegistration windowCloseRequested_;
    EventRegistration fullScreenChanged_;
    EventRegistration acceleratorKeyPressed_;
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
    ReadyCallback readyCallback_;
    NavigationStartedCallback navigationStartedCallback_;
    NavigationCompletedCallback navigationCompletedCallback_;
    DocumentStateChangedCallback documentStateChangedCallback_;
    NavigationStoppedCallback navigationStoppedCallback_;
    ErrorCallback errorCallback_;
    NewWindowCallback newWindowCallback_;
    ClosedCallback closedCallback_;
    FullScreenCallback fullScreenCallback_;
    AcceleratorCallback acceleratorCallback_;
    PermissionCallback permissionCallback_;
    ScreenCaptureCallback screenCaptureCallback_;
    DownloadCallback downloadCallback_;
    CertificateCallback certificateCallback_;
    ExternalProtocolCallback externalProtocolCallback_;
    ClearDataCallback clearDataCallback_;
    NavigationTracker navigation_;
    ClearDataNavigationCoordinator clearDataNavigation_;
    bool isVisible_{false};
    bool isAudioMuted_{true};
    bool isFullScreen_{false};
    bool isPopupRequest_{false};
    bool isClearedBlankSnapshotSuppressed_{false};
    bool isClosed_{false};
};

}  // namespace mediahub::browser_webview2
