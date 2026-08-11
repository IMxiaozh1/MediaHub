#include "webview2_popup_window.h"

#include <utility>

#include "webview2_default_deny.h"
#include "webview2_pending_request.h"
#include "webview2_state.h"

namespace mediahub::browser_webview2 {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

constexpr wchar_t kPopupWindowClass[] = L"MediaHubWebView2PopupWindow";

}  // namespace

WebView2PopupWindow::WebView2PopupWindow(
    ComPtr<ICoreWebView2Environment> environment,
    ComPtr<ICoreWebView2Profile> profile,
    const HWND ownerWindow,
    ClosedCallback closedCallback,
    NewWindowCallback newWindowCallback)
    : environment_(std::move(environment)),
      profile_(std::move(profile)),
      lifetime_(std::make_shared<int>(0)),
      ownerWindow_(ownerWindow),
      closedCallback_(std::move(closedCallback)),
      newWindowCallback_(std::move(newWindowCallback)) {}

WebView2PopupWindow::~WebView2PopupWindow() {
    close();
}

HRESULT WebView2PopupWindow::createFor(
    ICoreWebView2NewWindowRequestedEventArgs* const args) {
    if (args == nullptr || environment_ == nullptr || profile_ == nullptr ||
        ownerWindow_ == nullptr || isClosing_ || isClosed_) {
        return E_INVALIDARG;
    }

    ComPtr<ICoreWebView2Deferral> deferral;
    HRESULT result = preparePopupRequest(args, deferral.GetAddressOf());
    pendingArgs_ = args;
    pendingDeferral_ = deferral;
    if (FAILED(result) || deferral == nullptr) {
        const HRESULT completionResult = completePendingRequest(false);
        result = firstFailure(result, completionResult);
        return FAILED(result) ? result : E_POINTER;
    }

    result = createNativeWindow();
    if (SUCCEEDED(result)) {
        result = createController();
    }
    if (FAILED(result)) {
        static_cast<void>(completePendingRequest(false));
        closeAfterPopupFailure(PopupFailureTiming::SynchronousCreate,
                               [this](const bool notifyOwner) {
                                   closeInternal(notifyOwner);
                               });
    }
    return result;
}

void WebView2PopupWindow::close() noexcept {
    closeInternal(false);
}

LRESULT CALLBACK WebView2PopupWindow::windowProcedure(
    const HWND window, const UINT message, const WPARAM wParam,
    const LPARAM lParam) {
    WebView2PopupWindow* self = reinterpret_cast<WebView2PopupWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<WebView2PopupWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (self != nullptr) {
        if (message == WM_SIZE) {
            self->resizeController();
            return 0;
        }
        if (message == WM_CLOSE) {
            self->closeInternal(true);
            return 0;
        }
        if (message == WM_NCDESTROY) {
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HRESULT WebView2PopupWindow::createNativeWindow() {
    static const bool isWindowClassReady = [] {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &WebView2PopupWindow::windowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kPopupWindowClass;
        return RegisterClassExW(&windowClass) != 0 ||
               GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    if (!isWindowClassReady) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    window_ = CreateWindowExW(
        0, kPopupWindowClass, L"MediaHub 登录", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, ownerWindow_, nullptr,
        GetModuleHandleW(nullptr), this);
    return window_ != nullptr ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

HRESULT WebView2PopupWindow::createController() {
    ComPtr<ICoreWebView2Environment10> environment10;
    HRESULT result = environment_.As(&environment10);
    ComPtr<ICoreWebView2ControllerOptions> options;
    if (SUCCEEDED(result)) {
        result = environment10->CreateCoreWebView2ControllerOptions(&options);
    }

    LPWSTR rawProfileName = nullptr;
    if (SUCCEEDED(result)) {
        result = profile_->get_ProfileName(&rawProfileName);
    }
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> profileName(
        rawProfileName, &CoTaskMemFree);
    if (SUCCEEDED(result) && profileName != nullptr) {
        result = options->put_ProfileName(profileName.get());
    } else if (SUCCEEDED(result)) {
        result = E_POINTER;
    }
    if (SUCCEEDED(result)) {
        result = options->put_IsInPrivateModeEnabled(FALSE);
    }
    if (FAILED(result)) {
        return result;
    }

    const std::weak_ptr<int> weakLifetime = lifetime_;
    return environment10->CreateCoreWebView2ControllerWithOptions(
        window_, options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, weakLifetime](const HRESULT status,
                                 ICoreWebView2Controller* const controller) -> HRESULT {
                if (weakLifetime.expired()) {
                    ControllerAdoptionTransaction<ICoreWebView2Controller>
                        controllerTransaction(controller);
                    return S_OK;
                }
                finishController(status, controller);
                return S_OK;
            })
            .Get());
}

void WebView2PopupWindow::finishController(
    const HRESULT status, ICoreWebView2Controller* const controller) {
    HRESULT result = status;
    {
        ControllerAdoptionTransaction<ICoreWebView2Controller>
            controllerTransaction(controller);
        if (controllerTransaction.canAdopt(true, status)) {
            controller_ = controllerTransaction.adopt();
            result = controller_->get_CoreWebView2(&webView_);
        } else if (SUCCEEDED(result)) {
            result = E_POINTER;
        }
    }
    if (SUCCEEDED(result)) {
        result = configureSettings();
    }
    if (SUCCEEDED(result)) {
        result = registerEvents();
    }
    if (SUCCEEDED(result)) {
        resizeController();
        result = controller_->put_IsVisible(TRUE);
    }
    if (SUCCEEDED(result)) {
        result = completePendingRequest(true);
    } else {
        static_cast<void>(completePendingRequest(false));
    }
    if (FAILED(result)) {
        closeAfterPopupFailure(PopupFailureTiming::AsynchronousCompletion,
                               [this](const bool notifyOwner) {
                                   closeInternal(notifyOwner);
                               });
        return;
    }
    ShowWindow(window_, SW_SHOWNORMAL);
    UpdateWindow(window_);
}

HRESULT WebView2PopupWindow::configureSettings() {
    ComPtr<ICoreWebView2Settings> settings;
    HRESULT result = webView_->get_Settings(&settings);
    if (SUCCEEDED(result)) {
        result = settings->put_IsScriptEnabled(TRUE);
    }
    if (SUCCEEDED(result)) {
        result = settings->put_AreDefaultContextMenusEnabled(TRUE);
    }
    if (SUCCEEDED(result)) {
        result = settings->put_AreDevToolsEnabled(FALSE);
    }

    ComPtr<ICoreWebView2Settings4> settings4;
    if (SUCCEEDED(result)) {
        result = settings.As(&settings4);
    }
    if (SUCCEEDED(result)) {
        result = settings4->put_IsPasswordAutosaveEnabled(FALSE);
    }
    if (SUCCEEDED(result)) {
        result = settings4->put_IsGeneralAutofillEnabled(FALSE);
    }
    return result;
}

HRESULT WebView2PopupWindow::registerEvents() {
    HRESULT result = registerPermissionRequested();
    if (SUCCEEDED(result)) {
        result = registerScreenCaptureStarting();
    }
    if (SUCCEEDED(result)) {
        result = registerDownloadStarting();
    }
    if (SUCCEEDED(result)) {
        result = registerCertificateError();
    }
    if (SUCCEEDED(result)) {
        result = registerExternalProtocol();
    }
    if (SUCCEEDED(result)) {
        result = registerNewWindowRequested();
    }
    if (SUCCEEDED(result)) {
        result = registerWindowCloseRequested();
    }
    return result;
}

HRESULT WebView2PopupWindow::registerPermissionRequested() {
    const std::weak_ptr<int> weakLifetime = lifetime_;
    EventRegistrationToken token{};
    const HRESULT result = webView_->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [this, weakLifetime](
                ICoreWebView2*,
                ICoreWebView2PermissionRequestedEventArgs* const args) {
                if (!weakLifetime.expired()) {
                    return handleSafetyDecision(denyPermission(args));
                }
                return S_OK;
            })
            .Get(),
        &token);
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
        permissionRequested_.bind(token, [source](const EventRegistrationToken value) {
            return source->remove_PermissionRequested(value);
        });
    }
    return result;
}

HRESULT WebView2PopupWindow::registerScreenCaptureStarting() {
    ComPtr<ICoreWebView2_27> webView27;
    HRESULT result = webView_.As(&webView27);
    if (FAILED(result)) {
        return result;
    }
    const std::weak_ptr<int> weakLifetime = lifetime_;
    EventRegistrationToken token{};
    result = webView27->add_ScreenCaptureStarting(
        Callback<ICoreWebView2ScreenCaptureStartingEventHandler>(
            [this, weakLifetime](
                ICoreWebView2*,
                ICoreWebView2ScreenCaptureStartingEventArgs* const args) {
                if (!weakLifetime.expired()) {
                    return handleSafetyDecision(cancelScreenCapture(args));
                }
                return S_OK;
            })
            .Get(),
        &token);
    if (SUCCEEDED(result)) {
        screenCaptureStarting_.bind(
            token, [webView27](const EventRegistrationToken value) {
                return webView27->remove_ScreenCaptureStarting(value);
            });
    }
    return result;
}

HRESULT WebView2PopupWindow::registerDownloadStarting() {
    ComPtr<ICoreWebView2_4> webView4;
    HRESULT result = webView_.As(&webView4);
    if (FAILED(result)) {
        return result;
    }
    const std::weak_ptr<int> weakLifetime = lifetime_;
    EventRegistrationToken token{};
    result = webView4->add_DownloadStarting(
        Callback<ICoreWebView2DownloadStartingEventHandler>(
            [this, weakLifetime](
                ICoreWebView2*,
                ICoreWebView2DownloadStartingEventArgs* const args) {
                if (!weakLifetime.expired()) {
                    return handleSafetyDecision(cancelDownload(args));
                }
                return S_OK;
            })
            .Get(),
        &token);
    if (SUCCEEDED(result)) {
        downloadStarting_.bind(token, [webView4](const EventRegistrationToken value) {
            return webView4->remove_DownloadStarting(value);
        });
    }
    return result;
}

HRESULT WebView2PopupWindow::registerCertificateError() {
    ComPtr<ICoreWebView2_14> webView14;
    HRESULT result = webView_.As(&webView14);
    if (FAILED(result)) {
        return result;
    }
    const std::weak_ptr<int> weakLifetime = lifetime_;
    EventRegistrationToken token{};
    result = webView14->add_ServerCertificateErrorDetected(
        Callback<ICoreWebView2ServerCertificateErrorDetectedEventHandler>(
            [this, weakLifetime](
                ICoreWebView2*,
                ICoreWebView2ServerCertificateErrorDetectedEventArgs* const args) {
                if (!weakLifetime.expired()) {
                    return handleSafetyDecision(cancelCertificateError(args));
                }
                return S_OK;
            })
            .Get(),
        &token);
    if (SUCCEEDED(result)) {
        certificateErrorRequested_.bind(
            token, [webView14](const EventRegistrationToken value) {
                return webView14->remove_ServerCertificateErrorDetected(value);
            });
    }
    return result;
}

HRESULT WebView2PopupWindow::registerExternalProtocol() {
    ComPtr<ICoreWebView2_18> webView18;
    HRESULT result = webView_.As(&webView18);
    if (FAILED(result)) {
        return result;
    }
    const std::weak_ptr<int> weakLifetime = lifetime_;
    EventRegistrationToken token{};
    result = webView18->add_LaunchingExternalUriScheme(
        Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
            [this, weakLifetime](
                ICoreWebView2*,
                ICoreWebView2LaunchingExternalUriSchemeEventArgs* const args) {
                if (!weakLifetime.expired()) {
                    return handleSafetyDecision(cancelExternalUri(args));
                }
                return S_OK;
            })
            .Get(),
        &token);
    if (SUCCEEDED(result)) {
        externalProtocolRequested_.bind(
            token, [webView18](const EventRegistrationToken value) {
                return webView18->remove_LaunchingExternalUriScheme(value);
            });
    }
    return result;
}

HRESULT WebView2PopupWindow::registerNewWindowRequested() {
    const std::weak_ptr<int> weakLifetime = lifetime_;
    EventRegistrationToken token{};
    const HRESULT result = webView_->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this, weakLifetime](
                ICoreWebView2*,
                ICoreWebView2NewWindowRequestedEventArgs* const args) {
                if (weakLifetime.expired() || !newWindowCallback_) {
                    static_cast<void>(rejectNewWindow(args));
                    return S_OK;
                }
                return newWindowCallback_(args);
            })
            .Get(),
        &token);
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
        newWindowRequested_.bind(token, [source](const EventRegistrationToken value) {
            return source->remove_NewWindowRequested(value);
        });
    }
    return result;
}

HRESULT WebView2PopupWindow::registerWindowCloseRequested() {
    const std::weak_ptr<int> weakLifetime = lifetime_;
    EventRegistrationToken token{};
    const HRESULT result = webView_->add_WindowCloseRequested(
        Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
            [this, weakLifetime](ICoreWebView2*, IUnknown*) {
                if (!weakLifetime.expired()) {
                    closeInternal(true);
                }
                return S_OK;
            })
            .Get(),
        &token);
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
        windowCloseRequested_.bind(token, [source](const EventRegistrationToken value) {
            return source->remove_WindowCloseRequested(value);
        });
    }
    return result;
}

HRESULT WebView2PopupWindow::handleSafetyDecision(
    const HRESULT safetyResult) noexcept {
    return completePopupSafetyDecision(
        safetyResult, [this] { closeInternal(true); });
}

HRESULT WebView2PopupWindow::completePendingRequest(
    const bool attachWebView) noexcept {
    const HRESULT result = completePopupRequest(
        pendingArgs_.Get(), pendingDeferral_.Get(),
        attachWebView ? webView_.Get() : nullptr);
    pendingDeferral_.Reset();
    pendingArgs_.Reset();
    return result;
}

void WebView2PopupWindow::resizeController() noexcept {
    if (window_ == nullptr || controller_ == nullptr) {
        return;
    }
    RECT bounds{};
    if (GetClientRect(window_, &bounds) != FALSE) {
        static_cast<void>(controller_->put_Bounds(bounds));
    }
}

void WebView2PopupWindow::closeInternal(const bool notifyOwner) noexcept {
    if (isClosed_ || isClosing_) {
        return;
    }
    isClosing_ = true;
    lifetime_.reset();
    static_cast<void>(completePendingRequest(false));
    windowCloseRequested_.reset();
    newWindowRequested_.reset();
    externalProtocolRequested_.reset();
    certificateErrorRequested_.reset();
    downloadStarting_.reset();
    screenCaptureStarting_.reset();
    permissionRequested_.reset();
    if (controller_ != nullptr) {
        static_cast<void>(controller_->Close());
    }
    webView_.Reset();
    controller_.Reset();
    profile_.Reset();
    environment_.Reset();

    const HWND window = window_;
    window_ = nullptr;
    if (window != nullptr && IsWindow(window) != FALSE) {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
    }
    isClosed_ = true;
    isClosing_ = false;
    if (notifyOwner && closedCallback_) {
        closedCallback_(this);
    }
}

}  // namespace mediahub::browser_webview2
