#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>

#include <functional>
#include <memory>

#include "webview2_handles.h"

namespace mediahub::browser_webview2 {

// 登录子窗口与主页面共享 Environment/Profile，只承载网页和原生最小标题栏。
class WebView2PopupWindow final {
 public:
    using ClosedCallback = std::function<void(WebView2PopupWindow*)>;
    using NewWindowCallback = std::function<HRESULT(
        ICoreWebView2NewWindowRequestedEventArgs*)>;

    WebView2PopupWindow(
        Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment,
        Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile,
        HWND ownerWindow,
        ClosedCallback closedCallback,
        NewWindowCallback newWindowCallback);
    ~WebView2PopupWindow();

    WebView2PopupWindow(const WebView2PopupWindow&) = delete;
    WebView2PopupWindow& operator=(const WebView2PopupWindow&) = delete;

    // 调用线程：NewWindowRequested 所在的 GUI STA；异步完成 deferral。
    [[nodiscard]] HRESULT createFor(
        ICoreWebView2NewWindowRequestedEventArgs* args);
    // 调用线程：创建 Environment 的 GUI STA；不等待浏览器子进程。
    void close() noexcept;

 private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message,
                                            WPARAM wParam, LPARAM lParam);
    [[nodiscard]] HRESULT createNativeWindow();
    [[nodiscard]] HRESULT createController();
    // 回调线程：创建 Environment 的 GUI STA，禁止等待浏览器进程。
    void finishController(HRESULT status, ICoreWebView2Controller* controller);
    [[nodiscard]] HRESULT configureSettings();
    [[nodiscard]] HRESULT registerEvents();
    [[nodiscard]] HRESULT registerPermissionRequested();
    [[nodiscard]] HRESULT registerScreenCaptureStarting();
    [[nodiscard]] HRESULT registerDownloadStarting();
    [[nodiscard]] HRESULT registerCertificateError();
    [[nodiscard]] HRESULT registerExternalProtocol();
    [[nodiscard]] HRESULT registerNewWindowRequested();
    [[nodiscard]] HRESULT registerWindowCloseRequested();
    [[nodiscard]] HRESULT completePendingRequest(bool attachWebView) noexcept;
    void resizeController() noexcept;
    void closeInternal(bool notifyOwner) noexcept;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
    Microsoft::WRL::ComPtr<ICoreWebView2Profile> profile_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webView_;
    Microsoft::WRL::ComPtr<ICoreWebView2NewWindowRequestedEventArgs>
        pendingArgs_;
    Microsoft::WRL::ComPtr<ICoreWebView2Deferral> pendingDeferral_;
    EventRegistration windowCloseRequested_;
    EventRegistration newWindowRequested_;
    EventRegistration externalProtocolRequested_;
    EventRegistration certificateErrorRequested_;
    EventRegistration downloadStarting_;
    EventRegistration screenCaptureStarting_;
    EventRegistration permissionRequested_;
    std::shared_ptr<int> lifetime_;
    HWND ownerWindow_{nullptr};
    HWND window_{nullptr};
    ClosedCallback closedCallback_;
    NewWindowCallback newWindowCallback_;
    bool isClosing_{false};
    bool isClosed_{false};
};

}  // namespace mediahub::browser_webview2
