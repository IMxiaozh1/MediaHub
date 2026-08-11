#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "mediahub/browser_webview2/webview2_browser_backend.h"

#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>

#include <QApplication>
#include <QMetaObject>
#include <QThread>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "browser_event_listener.h"
#include "mediahub/logging/logger.h"
#include "webview2_handles.h"

namespace mediahub::browser_webview2 {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

struct CoTaskMemStringDeleter {
    void operator()(wchar_t* value) const noexcept { CoTaskMemFree(value); }
};

using CoTaskMemString = std::unique_ptr<wchar_t, CoTaskMemStringDeleter>;

gui::BrowserErrorKind classifyInitializationError(const HRESULT result) noexcept {
    if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
        result == HRESULT_FROM_WIN32(ERROR_PRODUCT_UNINSTALLED) ||
        result == REGDB_E_CLASSNOTREG) {
        return gui::BrowserErrorKind::RuntimeUnavailable;
    }
    if (result == E_ACCESSDENIED || result == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) ||
        result == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) ||
        result == HRESULT_FROM_WIN32(ERROR_DISK_FULL)) {
        return gui::BrowserErrorKind::ProfileUnavailable;
    }
    return gui::BrowserErrorKind::InitializationFailed;
}

RECT toNativeRect(const QRect& bounds) noexcept {
    return RECT{bounds.x(), bounds.y(), bounds.x() + bounds.width(),
                bounds.y() + bounds.height()};
}

}  // namespace

class WebView2BrowserBackend::Impl final {
 public:
    explicit Impl(logging::Logger* const logger) noexcept : logger_(logger) {}

    ~Impl() { shutdown(); }

    // 调用线程：GUI 主线程。
    void setEventListener(gui::BrowserEventListener* const listener) noexcept {
        listener_ = listener;
    }

    // 调用线程：GUI 主线程。回调由创建环境的同一 STA 消息循环投递。
    void initialize(void* const parentWindowHandle,
                    const QString& userDataDirectory,
                    const std::uint64_t generation) {
        isShuttingDown_ = true;
        ++lifecycleSerial_;
        lifetime_.reset();
        releaseBrowserResources();
        releaseComApartment();

        isShuttingDown_ = false;
        isReady_ = false;
        isNavigating_ = false;
        generation_ = generation;
        lifetime_ = std::make_shared<int>(0);
        parentWindow_ = reinterpret_cast<HWND>(parentWindowHandle);

        if (parentWindow_ == nullptr || !IsWindow(parentWindow_) ||
            userDataDirectory.isEmpty()) {
            reportError(generation, gui::BrowserErrorKind::InitializationFailed,
                        E_INVALIDARG, "invalid_initialization_input");
            return;
        }

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(comResult)) {
            reportError(generation, gui::BrowserErrorKind::InitializationFailed,
                        comResult, "sta_initialization_failed");
            return;
        }
        ownsComApartmentReference_ = true;

        const std::wstring profilePath = userDataDirectory.toStdWString();
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, profilePath.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, weakLifetime, lifecycleSerial,
                 generation](const HRESULT status,
                             ICoreWebView2Environment* const environment) -> HRESULT {
                    if (weakLifetime.expired() ||
                        !isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    if (FAILED(status) || environment == nullptr) {
                        const HRESULT error = FAILED(status) ? status : E_POINTER;
                        reportError(generation, classifyInitializationError(error), error,
                                    "environment_creation_failed");
                        return S_OK;
                    }
                    environment_ = environment;
                    createController(weakLifetime, lifecycleSerial, generation);
                    return S_OK;
                })
                .Get());

        if (FAILED(result)) {
            reportError(generation, classifyInitializationError(result), result,
                        "environment_request_failed");
        }
    }

    // 调用线程：GUI 主线程。
    void navigate(const QString& normalizedUrl, const std::uint64_t generation) {
        generation_ = generation;
        if (!isReady_ || webView_ == nullptr) {
            reportError(generation, gui::BrowserErrorKind::InitializationFailed,
                        E_UNEXPECTED, "navigate_before_ready");
            return;
        }
        const std::wstring url = normalizedUrl.toStdWString();
        const HRESULT result = webView_->Navigate(url.c_str());
        if (FAILED(result)) {
            reportError(generation, gui::BrowserErrorKind::NavigationFailed, result,
                        "navigation_request_failed");
        }
    }

    // 调用线程：GUI 主线程。
    void goBack() noexcept {
        if (webView_ != nullptr) {
            logOperationFailure("go_back_failed", webView_->GoBack());
        }
    }

    // 调用线程：GUI 主线程。
    void goForward() noexcept {
        if (webView_ != nullptr) {
            logOperationFailure("go_forward_failed", webView_->GoForward());
        }
    }

    // 调用线程：GUI 主线程。
    void reloadOrStop() noexcept {
        if (webView_ == nullptr) {
            return;
        }
        const HRESULT result = isNavigating_ ? webView_->Stop() : webView_->Reload();
        logOperationFailure(isNavigating_ ? "stop_failed" : "reload_failed", result);
    }

    // 调用线程：GUI 主线程。
    void setBounds(const QRect& pixelBounds) noexcept {
        if (!pixelBounds.isValid() || pixelBounds.width() <= 0 ||
            pixelBounds.height() <= 0) {
            return;
        }
        bounds_ = pixelBounds;
        if (controller_ != nullptr) {
            const RECT bounds = toNativeRect(bounds_);
            logOperationFailure("set_bounds_failed", controller_->put_Bounds(bounds));
        }
    }

    // 调用线程：GUI 主线程。
    void setVisible(const bool isVisible) noexcept {
        isVisible_ = isVisible;
        if (controller_ != nullptr) {
            logOperationFailure("set_visibility_failed",
                                controller_->put_IsVisible(isVisible ? TRUE : FALSE));
        }
    }

    // 调用线程：GUI 主线程。
    void setAudioMuted(const bool isMuted) noexcept {
        isMuted_ = isMuted;
        if (webView_ == nullptr) {
            return;
        }
        ComPtr<ICoreWebView2_8> webView8;
        HRESULT result = webView_.As(&webView8);
        if (SUCCEEDED(result)) {
            result = webView8->put_IsMuted(isMuted ? TRUE : FALSE);
        }
        logOperationFailure("set_audio_muted_failed", result);
    }

    // 调用线程：GUI 主线程。异步完成处理器不得触碰界面对象。
    void setSuspended(const bool isSuspended) noexcept {
        isSuspended_ = isSuspended;
        if (webView_ == nullptr) {
            return;
        }

        ComPtr<ICoreWebView2_3> webView3;
        HRESULT result = webView_.As(&webView3);
        if (FAILED(result)) {
            if (isSuspended) {
                setAudioMuted(true);
            }
            logOperationFailure("suspension_interface_unavailable", result);
            return;
        }
        if (!isSuspended) {
            logOperationFailure("resume_failed", webView3->Resume());
            return;
        }

        setAudioMuted(true);
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        const std::uint64_t generation = generation_;
        result = webView3->TrySuspend(
            Callback<ICoreWebView2TrySuspendCompletedHandler>(
                [this, weakLifetime, lifecycleSerial,
                 generation](const HRESULT status, BOOL) -> HRESULT {
                    if (weakLifetime.expired() ||
                        lifecycleSerial != lifecycleSerial_ ||
                        generation != generation_) {
                        return S_OK;
                    }
                    logOperationFailure("suspend_failed", status);
                    return S_OK;
                })
                .Get());
        logOperationFailure("suspend_request_failed", result);
    }

    // 调用线程：GUI 主线程。清除回调只投递稳定成功或错误事件。
    void clearBrowsingData(const std::uint64_t generation) {
        generation_ = generation;
        if (webView_ == nullptr) {
            reportError(generation, gui::BrowserErrorKind::ClearDataFailed,
                        E_UNEXPECTED, "clear_data_before_ready");
            return;
        }

        ComPtr<ICoreWebView2_13> webView13;
        ComPtr<ICoreWebView2Profile> profile;
        ComPtr<ICoreWebView2Profile2> profile2;
        HRESULT result = webView_.As(&webView13);
        if (SUCCEEDED(result)) {
            result = webView13->get_Profile(&profile);
        }
        if (SUCCEEDED(result)) {
            result = profile.As(&profile2);
        }
        if (FAILED(result)) {
            reportError(generation, gui::BrowserErrorKind::ClearDataFailed, result,
                        "clear_data_interface_unavailable");
            return;
        }

        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        result = profile2->ClearBrowsingData(
            COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_PROFILE,
            Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>(
                [this, weakLifetime, lifecycleSerial,
                 generation](const HRESULT status) -> HRESULT {
                    if (weakLifetime.expired() ||
                        !isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    if (FAILED(status)) {
                        reportError(generation, gui::BrowserErrorKind::ClearDataFailed,
                                    status, "clear_data_failed");
                    } else {
                        dispatchListener(generation,
                                         [generation](gui::BrowserEventListener& listener) {
                                             listener.onBrowsingDataCleared(generation);
                                         });
                    }
                    return S_OK;
                })
                .Get());
        if (FAILED(result)) {
            reportError(generation, gui::BrowserErrorKind::ClearDataFailed, result,
                        "clear_data_request_failed");
        }
    }

    // 调用线程：GUI 主线程。后续安全阶段实现挂起请求；当前默认拒绝且无副作用。
    void answerPermission(std::uint64_t,
                          gui::BrowserPermissionDecision) noexcept {}

    // 调用线程：GUI 主线程。后续安全阶段实现下载；当前不接触文件系统。
    void chooseDownloadPath(std::uint64_t, const QString&) noexcept {}

    // 调用线程：GUI 主线程。后续安全阶段实现下载；当前没有待取消操作。
    void cancelDownload(std::uint64_t) noexcept {}

    // 调用线程：GUI 主线程。后续安全阶段实现外部协议；当前不启动外部应用。
    void answerExternalProtocol(std::uint64_t, bool) noexcept {}

    // 调用线程：GUI 主线程。后续安全阶段实现证书例外；当前不放行证书错误。
    void answerCertificateError(std::uint64_t,
                                gui::BrowserCertificateDecision) noexcept {}

    // 调用线程：GUI 主线程。不执行脚本注入；退出语义由后续全屏阶段接入。
    void exitFullScreen() noexcept {}

    // 调用线程：GUI 主线程。只释放本适配器持有的 COM 对象，不等待子进程。
    void shutdown() noexcept {
        if (isShuttingDown_ && lifetime_ == nullptr && controller_ == nullptr &&
            environment_ == nullptr && !ownsComApartmentReference_) {
            listener_ = nullptr;
            return;
        }
        isShuttingDown_ = true;
        isReady_ = false;
        isNavigating_ = false;
        listener_ = nullptr;
        ++lifecycleSerial_;
        lifetime_.reset();
        releaseBrowserResources();
        releaseComApartment();
    }

 private:
    bool isActive(const std::weak_ptr<int>& weakLifetime,
                  const std::uint64_t lifecycleSerial,
                  const std::uint64_t generation) const noexcept {
        return !weakLifetime.expired() && !isShuttingDown_ &&
               lifecycleSerial == lifecycleSerial_ && generation == generation_;
    }

    void createController(const std::weak_ptr<int>& weakLifetime,
                          const std::uint64_t lifecycleSerial,
                          const std::uint64_t generation) {
        const HRESULT result = environment_->CreateCoreWebView2Controller(
            parentWindow_,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this, weakLifetime, lifecycleSerial,
                 generation](const HRESULT status,
                             ICoreWebView2Controller* const controller) -> HRESULT {
                    if (weakLifetime.expired() ||
                        !isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    if (FAILED(status) || controller == nullptr) {
                        const HRESULT error = FAILED(status) ? status : E_POINTER;
                        reportError(generation, classifyInitializationError(error), error,
                                    "controller_creation_failed");
                        return S_OK;
                    }
                    finishController(controller, generation);
                    return S_OK;
                })
                .Get());
        if (FAILED(result)) {
            reportError(generation, classifyInitializationError(result), result,
                        "controller_request_failed");
        }
    }

    void finishController(ICoreWebView2Controller* const controller,
                          const std::uint64_t generation) {
        controller_ = controller;
        HRESULT result = controller_->get_CoreWebView2(&webView_);
        if (SUCCEEDED(result)) {
            result = configureSettings();
        }
        if (SUCCEEDED(result)) {
            result = registerEvents();
        }
        if (FAILED(result)) {
            reportError(generation, gui::BrowserErrorKind::InitializationFailed, result,
                        "controller_configuration_failed");
            releaseBrowserResources();
            return;
        }

        if (bounds_.isValid() && bounds_.width() > 0 && bounds_.height() > 0) {
            const RECT bounds = toNativeRect(bounds_);
            logOperationFailure("initial_bounds_failed", controller_->put_Bounds(bounds));
        }
        logOperationFailure("initial_visibility_failed",
                            controller_->put_IsVisible(isVisible_ ? TRUE : FALSE));
        setAudioMuted(isMuted_);

        isReady_ = true;
        if (isSuspended_) {
            setSuspended(true);
        }
        dispatchListener(generation, [generation](gui::BrowserEventListener& listener) {
            listener.onBrowserReady(generation);
        });
    }

    HRESULT configureSettings() {
        ComPtr<ICoreWebView2Settings> settings;
        HRESULT result = webView_->get_Settings(&settings);
        if (FAILED(result)) {
            return result;
        }
        if (FAILED(result = settings->put_IsScriptEnabled(TRUE))) {
            return result;
        }
        if (FAILED(result = settings->put_AreDefaultContextMenusEnabled(TRUE))) {
            return result;
        }
        if (FAILED(result = settings->put_AreDevToolsEnabled(FALSE))) {
            return result;
        }

        ComPtr<ICoreWebView2Settings4> settings4;
        result = settings.As(&settings4);
        if (FAILED(result)) {
            return result;
        }
        if (FAILED(result = settings4->put_IsPasswordAutosaveEnabled(FALSE))) {
            return result;
        }
        return settings4->put_IsGeneralAutofillEnabled(FALSE);
    }

    HRESULT registerEvents() {
        HRESULT result = registerNavigationStarting();
        if (SUCCEEDED(result)) {
            result = registerNavigationCompleted();
        }
        if (SUCCEEDED(result)) {
            result = registerDocumentTitleChanged();
        }
        if (SUCCEEDED(result)) {
            result = registerFullScreenChanged();
        }
        return result;
    }

    HRESULT registerNavigationStarting() {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = webView_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2*,
                    ICoreWebView2NavigationStartingEventArgs* const args) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_) {
                        return S_OK;
                    }
                    UINT64 navigationId = 0;
                    const HRESULT status =
                        args != nullptr ? args->get_NavigationId(&navigationId) : E_POINTER;
                    if (SUCCEEDED(status)) {
                        navigationGenerations_[navigationId] = generation_;
                        activeNavigationId_ = navigationId;
                    } else {
                        logOperationFailure("navigation_id_failed", status);
                    }
                    isNavigating_ = true;
                    const std::uint64_t generation = generation_;
                    dispatchListener(
                        generation, [generation](gui::BrowserEventListener& listener) {
                            listener.onNavigationStarted(generation);
                        });
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            const ComPtr<ICoreWebView2> source = webView_;
            navigationStarting_.bind(token, [source](const EventRegistrationToken value) {
                return source->remove_NavigationStarting(value);
            });
        }
        return result;
    }

    HRESULT registerNavigationCompleted() {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = webView_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2*,
                    ICoreWebView2NavigationCompletedEventArgs* const args) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_) {
                        return S_OK;
                    }
                    UINT64 navigationId = 0;
                    HRESULT result =
                        args != nullptr ? args->get_NavigationId(&navigationId) : E_POINTER;
                    if (FAILED(result)) {
                        reportError(generation_, gui::BrowserErrorKind::NavigationFailed,
                                    result, "navigation_id_failed");
                        return S_OK;
                    }
                    const auto generationIterator =
                        navigationGenerations_.find(navigationId);
                    const std::uint64_t eventGeneration =
                        generationIterator != navigationGenerations_.end()
                            ? generationIterator->second
                            : generation_;
                    if (generationIterator != navigationGenerations_.end()) {
                        navigationGenerations_.erase(generationIterator);
                    }
                    if (activeNavigationId_ == navigationId) {
                        activeNavigationId_ = 0;
                        isNavigating_ = false;
                    }
                    BOOL isSuccess = FALSE;
                    result = args->get_IsSuccess(&isSuccess);
                    if (FAILED(result)) {
                        reportError(eventGeneration,
                                    gui::BrowserErrorKind::NavigationFailed,
                                    result, "navigation_status_failed");
                    } else if (isSuccess == FALSE) {
                        COREWEBVIEW2_WEB_ERROR_STATUS status =
                            COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                        static_cast<void>(args->get_WebErrorStatus(&status));
                        if (status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) {
                            emitNavigationSnapshot(eventGeneration);
                        } else {
                            reportError(eventGeneration,
                                        gui::BrowserErrorKind::NavigationFailed, E_FAIL,
                                        "navigation_completed_with_error");
                        }
                    } else {
                        emitNavigationSnapshot(eventGeneration);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            const ComPtr<ICoreWebView2> source = webView_;
            navigationCompleted_.bind(token, [source](const EventRegistrationToken value) {
                return source->remove_NavigationCompleted(value);
            });
        }
        return result;
    }

    HRESULT registerDocumentTitleChanged() {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = webView_->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                [this, weakLifetime, lifecycleSerial](ICoreWebView2*, IUnknown*) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ || isNavigating_ ||
                        lifecycleSerial != lifecycleSerial_) {
                        return S_OK;
                    }
                    emitNavigationSnapshot(generation_);
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            const ComPtr<ICoreWebView2> source = webView_;
            documentTitleChanged_.bind(token, [source](const EventRegistrationToken value) {
                return source->remove_DocumentTitleChanged(value);
            });
        }
        return result;
    }

    HRESULT registerFullScreenChanged() {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = webView_->add_ContainsFullScreenElementChanged(
            Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
                [this, weakLifetime, lifecycleSerial](ICoreWebView2*, IUnknown*) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_) {
                        return S_OK;
                    }
                    BOOL isFullScreen = FALSE;
                    const HRESULT status =
                        webView_->get_ContainsFullScreenElement(&isFullScreen);
                    if (SUCCEEDED(status)) {
                        const std::uint64_t generation = generation_;
                        dispatchListener(
                            generation,
                            [generation, isFullScreen](gui::BrowserEventListener& listener) {
                                listener.onFullScreenChanged(generation,
                                                             isFullScreen != FALSE);
                            });
                    } else {
                        logOperationFailure("fullscreen_state_failed", status);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            const ComPtr<ICoreWebView2> source = webView_;
            fullScreenChanged_.bind(token, [source](const EventRegistrationToken value) {
                return source->remove_ContainsFullScreenElementChanged(value);
            });
        }
        return result;
    }

    void emitNavigationSnapshot(const std::uint64_t generation) {
        if (webView_ == nullptr) {
            return;
        }
        LPWSTR rawSource = nullptr;
        LPWSTR rawTitle = nullptr;
        BOOL canGoBack = FALSE;
        BOOL canGoForward = FALSE;
        const HRESULT sourceResult = webView_->get_Source(&rawSource);
        const HRESULT titleResult = webView_->get_DocumentTitle(&rawTitle);
        const HRESULT backResult = webView_->get_CanGoBack(&canGoBack);
        const HRESULT forwardResult = webView_->get_CanGoForward(&canGoForward);
        CoTaskMemString source(rawSource);
        CoTaskMemString title(rawTitle);
        if (FAILED(sourceResult) || FAILED(titleResult) || FAILED(backResult) ||
            FAILED(forwardResult)) {
            const HRESULT error = FAILED(sourceResult)   ? sourceResult
                                  : FAILED(titleResult)  ? titleResult
                                  : FAILED(backResult)   ? backResult
                                                         : forwardResult;
            logOperationFailure("navigation_snapshot_failed", error);
            return;
        }

        const QString visibleUrl = source != nullptr ? QString::fromWCharArray(source.get())
                                                     : QString{};
        const QString documentTitle = title != nullptr ? QString::fromWCharArray(title.get())
                                                       : QString{};
        dispatchListener(
            generation,
            [generation, visibleUrl, documentTitle, canGoBack,
             canGoForward](gui::BrowserEventListener& listener) {
                listener.onNavigationCompleted(generation, visibleUrl, documentTitle,
                                               canGoBack != FALSE,
                                               canGoForward != FALSE);
            });
    }

    template <typename CallbackType>
    void dispatchListener(const std::uint64_t generation, CallbackType callback) {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        if (QThread::currentThread() == QApplication::instance()->thread()) {
            if (isActive(weakLifetime, lifecycleSerial, generation) &&
                listener_ != nullptr) {
                callback(*listener_);
            }
            return;
        }

        QMetaObject::invokeMethod(
            QApplication::instance(),
            [this, weakLifetime, lifecycleSerial, generation,
             callback = std::move(callback)]() mutable {
                if (!weakLifetime.expired() &&
                    isActive(weakLifetime, lifecycleSerial, generation) &&
                    listener_ != nullptr) {
                    callback(*listener_);
                }
            },
            Qt::QueuedConnection);
    }

    void reportError(const std::uint64_t generation,
                     const gui::BrowserErrorKind kind,
                     const HRESULT result,
                     const char* const event) {
        logFailure(event, result);
        dispatchListener(
            generation, [generation, kind, result](gui::BrowserEventListener& listener) {
                listener.onBrowserError(generation, kind, static_cast<long>(result));
            });
    }

    void logOperationFailure(const char* const event, const HRESULT result) noexcept {
        if (FAILED(result)) {
            logFailure(event, result);
        }
    }

    void logFailure(const char* const event, const HRESULT result) noexcept {
        if (logger_ != nullptr) {
            logger_->log(logging::LogLevel::Error, "browser_webview2", event,
                         {{"hresult", std::to_string(static_cast<long>(result))}});
        }
    }

    void releaseBrowserResources() noexcept {
        fullScreenChanged_.reset();
        documentTitleChanged_.reset();
        navigationCompleted_.reset();
        navigationStarting_.reset();
        if (controller_ != nullptr) {
            static_cast<void>(controller_->Close());
        }
        webView_.Reset();
        controller_.Reset();
        environment_.Reset();
        navigationGenerations_.clear();
        activeNavigationId_ = 0;
        parentWindow_ = nullptr;
    }

    void releaseComApartment() noexcept {
        if (ownsComApartmentReference_) {
            CoUninitialize();
            ownsComApartmentReference_ = false;
        }
    }

    logging::Logger* logger_{nullptr};
    gui::BrowserEventListener* listener_{nullptr};
    HWND parentWindow_{nullptr};
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webView_;
    EventRegistration navigationStarting_;
    EventRegistration navigationCompleted_;
    EventRegistration documentTitleChanged_;
    EventRegistration fullScreenChanged_;
    std::shared_ptr<int> lifetime_;
    std::uint64_t lifecycleSerial_{0};
    std::uint64_t generation_{0};
    std::unordered_map<std::uint64_t, std::uint64_t> navigationGenerations_;
    std::uint64_t activeNavigationId_{0};
    QRect bounds_;
    bool ownsComApartmentReference_{false};
    bool isReady_{false};
    bool isNavigating_{false};
    bool isVisible_{false};
    bool isMuted_{true};
    bool isSuspended_{true};
    bool isShuttingDown_{true};
};

WebView2BrowserBackend::WebView2BrowserBackend(logging::Logger* const logger)
    : impl_(std::make_unique<Impl>(logger)) {}

WebView2BrowserBackend::~WebView2BrowserBackend() = default;

void WebView2BrowserBackend::setEventListener(
    gui::BrowserEventListener* const listener) {
    impl_->setEventListener(listener);
}

void WebView2BrowserBackend::initialize(void* const parentWindowHandle,
                                        const QString& userDataDirectory,
                                        const std::uint64_t generation) {
    impl_->initialize(parentWindowHandle, userDataDirectory, generation);
}

void WebView2BrowserBackend::navigate(const QString& normalizedUrl,
                                      const std::uint64_t generation) {
    impl_->navigate(normalizedUrl, generation);
}

void WebView2BrowserBackend::goBack() { impl_->goBack(); }

void WebView2BrowserBackend::goForward() { impl_->goForward(); }

void WebView2BrowserBackend::reloadOrStop() { impl_->reloadOrStop(); }

void WebView2BrowserBackend::setBounds(const QRect& pixelBounds) {
    impl_->setBounds(pixelBounds);
}

void WebView2BrowserBackend::setVisible(const bool isVisible) {
    impl_->setVisible(isVisible);
}

void WebView2BrowserBackend::setAudioMuted(const bool isMuted) {
    impl_->setAudioMuted(isMuted);
}

void WebView2BrowserBackend::setSuspended(const bool isSuspended) {
    impl_->setSuspended(isSuspended);
}

void WebView2BrowserBackend::clearBrowsingData(const std::uint64_t generation) {
    impl_->clearBrowsingData(generation);
}

void WebView2BrowserBackend::answerPermission(
    const std::uint64_t requestId,
    const gui::BrowserPermissionDecision decision) {
    impl_->answerPermission(requestId, decision);
}

void WebView2BrowserBackend::chooseDownloadPath(
    const std::uint64_t requestId, const QString& destination) {
    impl_->chooseDownloadPath(requestId, destination);
}

void WebView2BrowserBackend::cancelDownload(const std::uint64_t requestId) {
    impl_->cancelDownload(requestId);
}

void WebView2BrowserBackend::answerExternalProtocol(const std::uint64_t requestId,
                                                    const bool isAllowed) {
    impl_->answerExternalProtocol(requestId, isAllowed);
}

void WebView2BrowserBackend::answerCertificateError(
    const std::uint64_t requestId,
    const gui::BrowserCertificateDecision decision) {
    impl_->answerCertificateError(requestId, decision);
}

void WebView2BrowserBackend::exitFullScreen() { impl_->exitFullScreen(); }

void WebView2BrowserBackend::shutdown() noexcept { impl_->shutdown(); }

}  // namespace mediahub::browser_webview2
