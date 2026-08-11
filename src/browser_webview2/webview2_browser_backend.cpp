#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "mediahub/browser_webview2/webview2_browser_backend.h"

#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>

#include <QApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "browser_event_listener.h"
#include "mediahub/logging/logger.h"
#include "webview2_default_deny.h"
#include "webview2_handles.h"
#include "webview2_state.h"

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
        generation_ = generation;
        navigation_.reset(generation);
        lifetime_ = std::make_shared<int>(0);
        parentWindow_ = reinterpret_cast<HWND>(parentWindowHandle);

        if (userDataDirectory.isEmpty() ||
            !QFileInfo(userDataDirectory).isAbsolute()) {
            reportError(generation, gui::BrowserErrorKind::ProfileUnavailable,
                        E_INVALIDARG, "invalid_profile_input");
            return;
        }
        if (parentWindow_ == nullptr || !IsWindow(parentWindow_)) {
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
        navigation_.setCurrentGeneration(generation);
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
        } else {
            navigation_.acceptNavigate(generation);
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
        const bool isNavigating = navigation_.isNavigating();
        const HRESULT result = isNavigating ? webView_->Stop() : webView_->Reload();
        logOperationFailure(isNavigating ? "stop_failed" : "reload_failed", result);
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
        isAudioMutedDesired_ = isMuted;
        applyEffectiveAudioMute();
    }

    // 调用线程：GUI 主线程；挂起期间强制静音，只有确认恢复成功后才解除。
    void applyEffectiveAudioMute() noexcept {
        if (webView_ == nullptr) {
            return;
        }
        ComPtr<ICoreWebView2_8> webView8;
        HRESULT result = webView_.As(&webView8);
        if (SUCCEEDED(result)) {
            const bool isMuted = isAudioMutedDesired_ || suspension_.mustMute();
            result = webView8->put_IsMuted(isMuted ? TRUE : FALSE);
        }
        logOperationFailure("set_audio_muted_failed", result);
    }

    // 调用线程：GUI 主线程。异步完成处理器不得触碰界面对象。
    void setSuspended(const bool isSuspended) noexcept {
        if (webView_ == nullptr) {
            suspension_.setDesired(isSuspended);
            return;
        }
        executeSuspensionStep(suspension_.request(isSuspended));
    }

    // 调用线程：GUI STA；TrySuspend 回调先验证生命周期，再推进纯状态协调器。
    void executeSuspensionStep(const SuspensionStep step) noexcept {
        applyEffectiveAudioMute();
        if (step.action == SuspensionAction::None || webView_ == nullptr) {
            return;
        }
        ComPtr<ICoreWebView2_3> webView3;
        HRESULT result = webView_.As(&webView3);
        if (FAILED(result)) {
            logOperationFailure("suspension_interface_unavailable", result);
            const SuspensionStep next =
                step.action == SuspensionAction::TrySuspend
                    ? suspension_.completeTrySuspend(step.requestSerial, false, false)
                    : suspension_.completeResume(step.requestSerial, false);
            executeSuspensionStep(next);
            return;
        }

        if (step.action == SuspensionAction::Resume) {
            result = webView3->Resume();
            logOperationFailure("resume_failed", result);
            executeSuspensionStep(
                suspension_.completeResume(step.requestSerial, SUCCEEDED(result)));
            return;
        }

        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        result = webView3->TrySuspend(
            Callback<ICoreWebView2TrySuspendCompletedHandler>(
                [this, weakLifetime, lifecycleSerial,
                 requestSerial = step.requestSerial](const HRESULT status,
                                                     const BOOL didSuspend) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_) {
                        return S_OK;
                    }
                    const HRESULT completionResult =
                        FAILED(status) || didSuspend != FALSE ? status : E_FAIL;
                    logOperationFailure("suspend_failed", completionResult);
                    executeSuspensionStep(suspension_.completeTrySuspend(
                        requestSerial, SUCCEEDED(status), didSuspend != FALSE));
                    return S_OK;
                })
                .Get());
        logOperationFailure("suspend_request_failed", result);
        if (FAILED(result)) {
            executeSuspensionStep(
                suspension_.completeTrySuspend(step.requestSerial, false, false));
        }
    }

    // 调用线程：GUI 主线程。清除回调只投递稳定成功或错误事件。
    void clearBrowsingData(const std::uint64_t generation) {
        generation_ = generation;
        navigation_.setCurrentGeneration(generation);
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
        listener_ = nullptr;
        ++lifecycleSerial_;
        lifetime_.reset();
        releaseBrowserResources();
        releaseComApartment();
    }

 private:
    // 调用线程：GUI STA 或待投递到 GUI 的回调线程，只读取生命周期状态。
    bool isActive(const std::weak_ptr<int>& weakLifetime,
                  const std::uint64_t lifecycleSerial,
                  const std::uint64_t generation) const noexcept {
        return !weakLifetime.expired() && !isShuttingDown_ &&
               lifecycleSerial == lifecycleSerial_ && generation == generation_;
    }

    // 调用线程：WebView2 Environment 完成回调所在的 GUI STA，禁止操作 Qt 控件。
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

    // 调用线程：WebView2 Controller 完成回调所在的 GUI STA，禁止操作 Qt 控件。
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
        isReady_ = true;
        executeSuspensionStep(suspension_.controllerReady());
        dispatchListener(generation, [generation](gui::BrowserEventListener& listener) {
            listener.onBrowserReady(generation);
        });
    }

    // 调用线程：Controller 完成回调所在的 GUI STA。
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

    // 调用线程：创建 WebView2 的 GUI STA；任一安全事件注册失败都阻止进入 ready。
    HRESULT registerEvents() {
        HRESULT result = registerPermissionRequested();
        if (SUCCEEDED(result)) {
            result = registerDownloadStarting();
        }
        if (SUCCEEDED(result)) {
            result = registerServerCertificateErrorDetected();
        }
        if (SUCCEEDED(result)) {
            result = registerLaunchingExternalUriScheme();
        }
        if (SUCCEEDED(result)) {
            result = registerNewWindowRequested();
        }
        if (SUCCEEDED(result)) {
            result = registerNavigationStarting();
        }
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

    // 调用线程：GUI STA；事件回调只拒绝权限，不直接操作 Qt 控件。
    HRESULT registerPermissionRequested() {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = webView_->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2*,
                    ICoreWebView2PermissionRequestedEventArgs* const args) -> HRESULT {
                    if (weakLifetime.expired()) {
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    const HRESULT status = denyPermission(args);
                    logOperationFailure("permission_default_deny_failed", status);
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

    // 调用线程：GUI STA；事件回调取消下载并禁止默认下载界面，不接触文件系统。
    HRESULT registerDownloadStarting() {
        ComPtr<ICoreWebView2_4> webView4;
        HRESULT result = webView_.As(&webView4);
        if (FAILED(result)) {
            return result;
        }
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        result = webView4->add_DownloadStarting(
            Callback<ICoreWebView2DownloadStartingEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2*,
                    ICoreWebView2DownloadStartingEventArgs* const args) -> HRESULT {
                    if (weakLifetime.expired()) {
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    const HRESULT status =
                        ::mediahub::browser_webview2::cancelDownload(args);
                    logOperationFailure("download_default_cancel_failed", status);
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

    // 调用线程：GUI STA；事件回调只取消证书错误导航，不读取证书或请求地址。
    HRESULT registerServerCertificateErrorDetected() {
        ComPtr<ICoreWebView2_14> webView14;
        HRESULT result = webView_.As(&webView14);
        if (FAILED(result)) {
            return result;
        }
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        result = webView14->add_ServerCertificateErrorDetected(
            Callback<ICoreWebView2ServerCertificateErrorDetectedEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2*,
                    ICoreWebView2ServerCertificateErrorDetectedEventArgs* const args)
                    -> HRESULT {
                    if (weakLifetime.expired()) {
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    const HRESULT status = cancelCertificateError(args);
                    logOperationFailure("certificate_default_cancel_failed", status);
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            serverCertificateErrorDetected_.bind(
                token, [webView14](const EventRegistrationToken value) {
                    return webView14->remove_ServerCertificateErrorDetected(value);
                });
        }
        return result;
    }

    // 调用线程：GUI STA；事件回调禁止启动外部应用，不读取或记录目标 URI。
    HRESULT registerLaunchingExternalUriScheme() {
        ComPtr<ICoreWebView2_18> webView18;
        HRESULT result = webView_.As(&webView18);
        if (FAILED(result)) {
            return result;
        }
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        result = webView18->add_LaunchingExternalUriScheme(
            Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2*,
                    ICoreWebView2LaunchingExternalUriSchemeEventArgs* const args)
                    -> HRESULT {
                    if (weakLifetime.expired()) {
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    const HRESULT status = cancelExternalUri(args);
                    logOperationFailure("external_uri_default_cancel_failed", status);
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            launchingExternalUriScheme_.bind(
                token, [webView18](const EventRegistrationToken value) {
                    return webView18->remove_LaunchingExternalUriScheme(value);
                });
        }
        return result;
    }

    // 调用线程：GUI STA；事件回调静默拒绝弹窗，不复用后续阶段的容量错误提示。
    HRESULT registerNewWindowRequested() {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = webView_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2*,
                    ICoreWebView2NewWindowRequestedEventArgs* const args) -> HRESULT {
                    if (weakLifetime.expired()) {
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    const HRESULT status = rejectNewWindow(args);
                    logOperationFailure("popup_default_reject_failed", status);
                    return S_OK;
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

    // 调用线程：GUI STA；事件回调只更新稳定导航状态，禁止操作 Qt 控件。
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
                        const NavigationStart start = navigation_.start(navigationId);
                        if (start.shouldReport) {
                            dispatchListener(
                                start.generation,
                                [generation = start.generation](
                                    gui::BrowserEventListener& listener) {
                                    listener.onNavigationStarted(generation);
                                });
                        }
                    } else {
                        logOperationFailure("navigation_id_failed", status);
                    }
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

    // 调用线程：GUI STA；事件回调通过 dispatchListener 投递，不直接操作 Qt 控件。
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
                    const NavigationCompletion completion =
                        navigation_.complete(navigationId);
                    if (!completion.shouldReport) {
                        return S_OK;
                    }
                    const std::uint64_t eventGeneration = completion.generation;
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

    // 调用线程：GUI STA；事件回调不记录标题，并经 GUI 线程监听器更新界面。
    HRESULT registerDocumentTitleChanged() {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = webView_->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                [this, weakLifetime, lifecycleSerial](ICoreWebView2*, IUnknown*) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        navigation_.isNavigating() ||
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

    // 调用线程：GUI STA；事件回调仅投递布尔状态，不直接操作 Qt 控件。
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

    // 调用线程：WebView2 导航或标题事件所在的 GUI STA，界面更新统一经监听器投递。
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
    // 调用线程：GUI STA 或 WebView2 回调线程；监听器最终只在 GUI 主线程调用。
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

    // 调用线程：GUI STA 或 WebView2 回调线程，监听器最终只在 GUI 主线程调用。
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

    // 调用线程：任意调用线程；只记录事件类别和 HRESULT。
    void logOperationFailure(const char* const event, const HRESULT result) noexcept {
        if (FAILED(result)) {
            logFailure(event, result);
        }
    }

    // 调用线程：任意调用线程；不得添加 URL、Profile、标题或凭据字段。
    void logFailure(const char* const event, const HRESULT result) noexcept {
        if (logger_ != nullptr) {
            logger_->log(logging::LogLevel::Error, "browser_webview2", event,
                         {{"hresult", std::to_string(static_cast<long>(result))}});
        }
    }

    // 调用线程：创建 COM 对象的 GUI STA；必须先撤销全部事件，再关闭 Controller。
    void releaseBrowserResources() noexcept {
        newWindowRequested_.reset();
        launchingExternalUriScheme_.reset();
        serverCertificateErrorDetected_.reset();
        downloadStarting_.reset();
        permissionRequested_.reset();
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
        suspension_.invalidate();
        navigation_.reset(generation_);
        parentWindow_ = nullptr;
    }

    // 调用线程：初始化 COM 的 GUI STA，严格配对当前对象持有的初始化引用。
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
    EventRegistration permissionRequested_;
    EventRegistration downloadStarting_;
    EventRegistration serverCertificateErrorDetected_;
    EventRegistration launchingExternalUriScheme_;
    EventRegistration newWindowRequested_;
    std::shared_ptr<int> lifetime_;
    std::uint64_t lifecycleSerial_{0};
    std::uint64_t generation_{0};
    NavigationTracker navigation_;
    QRect bounds_;
    bool ownsComApartmentReference_{false};
    bool isReady_{false};
    bool isVisible_{false};
    bool isAudioMutedDesired_{true};
    SuspensionCoordinator suspension_;
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
