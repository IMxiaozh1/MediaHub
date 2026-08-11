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
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "browser_event_listener.h"
#include "mediahub/logging/logger.h"
#include "webview2_default_deny.h"
#include "webview2_handles.h"
#include "webview2_pending_request.h"
#include "webview2_popup_window.h"
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

QString normalizedOriginFromUri(const wchar_t* const uri) {
    if (uri == nullptr) {
        return {};
    }
    const QUrl parsed(QString::fromWCharArray(uri), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || (scheme != QStringLiteral("https") &&
                              scheme != QStringLiteral("http")) ||
        parsed.host().isEmpty() || !parsed.userName().isEmpty() ||
        !parsed.password().isEmpty()) {
        return {};
    }
    QUrl origin;
    origin.setScheme(scheme);
    origin.setHost(parsed.host().toLower());
    origin.setPort(parsed.port(-1));
    return origin.toString(QUrl::FullyEncoded | QUrl::RemovePath |
                           QUrl::RemoveQuery | QUrl::RemoveFragment |
                           QUrl::RemoveUserInfo);
}

bool isValidExternalTarget(const QString& target) {
    const QUrl parsed(target, QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || scheme.isEmpty() || target.contains(QLatin1Char('\r')) ||
        target.contains(QLatin1Char('\n'))) {
        return false;
    }
    return scheme != QStringLiteral("http") && scheme != QStringLiteral("https") &&
           scheme != QStringLiteral("file") && scheme != QStringLiteral("data") &&
           scheme != QStringLiteral("javascript") && scheme != QStringLiteral("about") &&
           scheme != QStringLiteral("blob");
}

std::optional<gui::BrowserPermissionKind> supportedPermissionKind(
    const COREWEBVIEW2_PERMISSION_KIND kind) noexcept {
    switch (kind) {
    case COREWEBVIEW2_PERMISSION_KIND_CAMERA:
        return gui::BrowserPermissionKind::Camera;
    case COREWEBVIEW2_PERMISSION_KIND_MICROPHONE:
        return gui::BrowserPermissionKind::Microphone;
    case COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION:
        return gui::BrowserPermissionKind::Geolocation;
    case COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS:
        return gui::BrowserPermissionKind::Notifications;
    case COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ:
        return gui::BrowserPermissionKind::ClipboardRead;
    default:
        return std::nullopt;
    }
}

QString certificateErrorDescription(const COREWEBVIEW2_WEB_ERROR_STATUS status) {
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT:
        return QStringLiteral("服务器证书名称与网站不匹配");
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
        return QStringLiteral("服务器证书已过期");
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS:
        return QStringLiteral("客户端证书包含错误");
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:
        return QStringLiteral("服务器证书已被吊销");
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID:
        return QStringLiteral("服务器证书无效");
    default:
        return QStringLiteral("服务器证书验证失败");
    }
}

// 调用线程：WebView2 权限事件所在 GUI STA；生命周期失效时不得访问后端实例。
HRESULT rejectPermissionRequest(
    ICoreWebView2PermissionRequestedEventArgs* const args) noexcept {
    if (args == nullptr) {
        return E_POINTER;
    }
    HRESULT result = args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
    ComPtr<ICoreWebView2Deferral> deferral;
    const HRESULT deferralResult = args->GetDeferral(&deferral);
    result = firstFailure(result, deferralResult);
    if (deferral == nullptr) {
        return FAILED(result) ? result : E_POINTER;
    }
    ComPtr<ICoreWebView2PermissionRequestedEventArgs> baseArgs(args);
    ComPtr<ICoreWebView2PermissionRequestedEventArgs3> args3;
    static_cast<void>(baseArgs.As(&args3));
    return firstFailure(
        result,
        completePermissionRejection(baseArgs.Get(), args3.Get(), deferral.Get()));
}

// 调用线程：WebView2 屏幕捕获事件所在 GUI STA；生命周期失效时不得访问后端实例。
HRESULT rejectScreenCaptureRequest(
    ICoreWebView2ScreenCaptureStartingEventArgs* const args) noexcept {
    if (args == nullptr) {
        return E_POINTER;
    }
    HRESULT result = args->put_Cancel(TRUE);
    result = firstFailure(result, args->put_Handled(TRUE));
    ComPtr<ICoreWebView2Deferral> deferral;
    const HRESULT deferralResult = args->GetDeferral(&deferral);
    result = firstFailure(result, deferralResult);
    if (deferral == nullptr) {
        return FAILED(result) ? result : E_POINTER;
    }
    return firstFailure(
        result,
        completeScreenCaptureDecision(
            args, deferral.Get(), gui::BrowserPermissionDecision::Deny));
}

}  // namespace

class WebView2BrowserBackend::Impl final {
    struct PendingPermission final {
        ComPtr<ICoreWebView2PermissionRequestedEventArgs3> args;
        ComPtr<ICoreWebView2Deferral> deferral;
        QString origin;
        gui::BrowserPermissionKind kind{gui::BrowserPermissionKind::Other};
        std::uint64_t lifecycleSerial{0};
        std::uint64_t generation{0};
    };

    struct PendingExternalProtocol final {
        ComPtr<ICoreWebView2LaunchingExternalUriSchemeEventArgs> args;
        ComPtr<ICoreWebView2Deferral> deferral;
        std::uint64_t lifecycleSerial{0};
        std::uint64_t generation{0};
    };

    struct PendingScreenCapture final {
        ComPtr<ICoreWebView2ScreenCaptureStartingEventArgs> args;
        ComPtr<ICoreWebView2Deferral> deferral;
        QString origin;
        std::uint64_t lifecycleSerial{0};
        std::uint64_t generation{0};
    };

    struct PendingCertificate final {
        ComPtr<ICoreWebView2ServerCertificateErrorDetectedEventArgs> args;
        ComPtr<ICoreWebView2Deferral> deferral;
        QString origin;
        std::uint64_t lifecycleSerial{0};
        std::uint64_t generation{0};
    };

    struct PendingDownload final {
        ComPtr<ICoreWebView2DownloadStartingEventArgs> args;
        ComPtr<ICoreWebView2DownloadOperation> operation;
        ComPtr<ICoreWebView2Deferral> deferral;
        std::int64_t totalBytes{-1};
        std::uint64_t lifecycleSerial{0};
        std::uint64_t generation{0};
    };

    struct ActiveDownload final {
        ComPtr<ICoreWebView2DownloadOperation> operation;
        EventRegistration bytesReceivedChanged;
        EventRegistration stateChanged;
        std::int64_t totalBytes{-1};
        std::uint64_t lifecycleSerial{0};
        bool isCancelRequested{false};

        void resetSubscriptions() noexcept {
            stateChanged.reset();
            bytesReceivedChanged.reset();
        }
    };

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
        if (!lifecycleGate_.beginInitialization()) {
            return;
        }
        isShuttingDown_ = true;
        ++lifecycleSerial_;
        lifetime_.reset();
        releaseBrowserResources();
        releaseComApartment();

        isShuttingDown_ = false;
        isReady_ = false;
        popupCoordinator_.reset();
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
        clearDataNavigation_.reset();
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
        navigation_.reset(generation);
        clearDataNavigation_.begin(generation);
        if (webView_ == nullptr) {
            clearDataNavigation_.reset();
            reportError(generation, gui::BrowserErrorKind::ClearDataFailed,
                        E_UNEXPECTED, "clear_data_before_ready");
            return;
        }

        ComPtr<ICoreWebView2_13> webView13;
        ComPtr<ICoreWebView2_14> webView14;
        ComPtr<ICoreWebView2Profile> profile;
        ComPtr<ICoreWebView2Profile2> profile2;
        HRESULT result = webView_.As(&webView13);
        if (SUCCEEDED(result)) {
            result = webView13->get_Profile(&profile);
        }
        if (SUCCEEDED(result)) {
            result = profile.As(&profile2);
        }
        if (SUCCEEDED(result)) {
            result = webView_.As(&webView14);
        }
        if (FAILED(result)) {
            clearDataNavigation_.reset();
            reportError(generation, gui::BrowserErrorKind::ClearDataFailed, result,
                        "clear_data_interface_unavailable");
            return;
        }

        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        result = profile2->ClearBrowsingData(
            COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_PROFILE,
            Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>(
                [this, weakLifetime, lifecycleSerial, webView14,
                 generation](const HRESULT status) -> HRESULT {
                    if (weakLifetime.expired() ||
                        !isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    if (FAILED(status)) {
                        clearDataNavigation_.reset();
                        reportError(generation, gui::BrowserErrorKind::ClearDataFailed,
                                    status, "clear_data_failed");
                    } else {
                        const HRESULT clearCertificateResult =
                            webView14->ClearServerCertificateErrorActions(
                                Callback<
                                    ICoreWebView2ClearServerCertificateErrorActionsCompletedHandler>(
                                    [this, weakLifetime, lifecycleSerial,
                                     generation](const HRESULT certificateStatus)
                                        -> HRESULT {
                                        if (weakLifetime.expired() ||
                                            !isActive(weakLifetime, lifecycleSerial,
                                                      generation)) {
                                            return S_OK;
                                        }
                                        if (FAILED(certificateStatus)) {
                                            clearDataNavigation_.reset();
                                            reportError(
                                                generation,
                                                gui::BrowserErrorKind::ClearDataFailed,
                                                certificateStatus,
                                                "clear_certificate_actions_failed");
                                        } else {
                                            if (!clearDataNavigation_
                                                     .dataAndCertificatesCleared(
                                                         generation)) {
                                                return S_OK;
                                            }
                                            const HRESULT blankResult =
                                                webView_->Navigate(L"about:blank");
                                            if (FAILED(blankResult)) {
                                                static_cast<void>(
                                                    clearDataNavigation_
                                                        .blankRequestFailed(
                                                            generation));
                                                reportError(
                                                    generation,
                                                    gui::BrowserErrorKind::
                                                        ClearDataFailed,
                                                    blankResult,
                                                    "clear_blank_navigation_request_failed");
                                            }
                                        }
                                        return S_OK;
                                    })
                                    .Get());
                        if (FAILED(clearCertificateResult)) {
                            clearDataNavigation_.reset();
                            reportError(
                                generation, gui::BrowserErrorKind::ClearDataFailed,
                                clearCertificateResult,
                                "clear_certificate_actions_request_failed");
                        }
                    }
                    return S_OK;
                })
                .Get());
        if (FAILED(result)) {
            clearDataNavigation_.reset();
            reportError(generation, gui::BrowserErrorKind::ClearDataFailed, result,
                        "clear_data_request_failed");
        }
    }

    // 调用线程：GUI 主线程。每个 requestId 只从待决定集合取出并完成一次。
    void answerPermission(const std::uint64_t requestId,
                          gui::BrowserPermissionDecision decision) noexcept {
        std::optional<PendingPermission> pending = pendingPermissions_.take(requestId);
        if (pending.has_value()) {
            if (isShuttingDown_ || pending->lifecycleSerial != lifecycleSerial_ ||
                pending->generation != generation_ ||
                pending->kind == gui::BrowserPermissionKind::Other) {
                decision = gui::BrowserPermissionDecision::Deny;
            }
            logOperationFailure(
                "permission_decision_failed",
                completePermissionDecision(pending->args.Get(),
                                           pending->deferral.Get(), decision));
            return;
        }

        std::optional<PendingScreenCapture> screenCapture =
            pendingScreenCaptures_.take(requestId);
        if (!screenCapture.has_value()) {
            return;
        }
        if (isShuttingDown_ ||
            screenCapture->lifecycleSerial != lifecycleSerial_ ||
            screenCapture->generation != generation_ ||
            decision != gui::BrowserPermissionDecision::AllowOnce) {
            decision = gui::BrowserPermissionDecision::Deny;
        }
        logOperationFailure(
            "screen_capture_decision_failed",
            completeScreenCaptureDecision(screenCapture->args.Get(),
                                          screenCapture->deferral.Get(), decision));
    }

    // 调用线程：GUI 主线程。后端再次验证目标，绝不覆盖已有文件。
    void chooseDownloadPath(const std::uint64_t requestId,
                            const QString& destination) noexcept {
        std::optional<PendingDownload> pending = pendingDownloads_.take(requestId);
        if (!pending.has_value()) {
            return;
        }
        if (isShuttingDown_ || pending->lifecycleSerial != lifecycleSerial_ ||
            pending->generation != generation_ ||
            !isSafeDownloadDestination(destination)) {
            logOperationFailure(
                "download_destination_rejected",
                completeDownloadCancellation(pending->args.Get(),
                                             pending->operation.Get(),
                                             pending->deferral.Get()));
            return;
        }

        ActiveDownload active;
        active.operation = pending->operation;
        active.totalBytes = pending->totalBytes;
        active.lifecycleSerial = pending->lifecycleSerial;
        const HRESULT result = startDownloadTransaction(
            active,
            [this, requestId](ActiveDownload& candidate) {
                return registerActiveDownload(requestId, candidate);
            },
            [this, requestId](ActiveDownload& candidate) {
                return activeDownloads_
                    .try_emplace(requestId, std::move(candidate))
                    .second;
            },
            [&pending, &destination] {
                return completeDownloadPathDecision(
                    pending->args.Get(), pending->operation.Get(),
                    pending->deferral.Get(), destination);
            },
            [&pending] {
                return completeDownloadCancellation(
                    pending->args.Get(), pending->operation.Get(),
                    pending->deferral.Get());
            },
            [this, requestId] { activeDownloads_.erase(requestId); });
        logOperationFailure("download_start_failed", result);
    }

    // 调用线程：GUI 主线程。待选路径先接入终态订阅，失败后只重试 operation。
    void cancelDownload(const std::uint64_t requestId) noexcept {
        std::optional<PendingDownload> pending = pendingDownloads_.take(requestId);
        if (pending.has_value()) {
            const std::uint64_t generation = pending->generation;
            const std::int64_t totalBytes = pending->totalBytes;
            ActiveDownload candidate;
            candidate.operation = pending->operation;
            candidate.totalBytes = totalBytes;
            candidate.lifecycleSerial = pending->lifecycleSerial;
            const PendingDownloadCancelOutcome outcome =
                cancelPendingDownloadTransaction(
                    candidate,
                    [this, requestId](ActiveDownload& active) {
                        return registerActiveDownload(requestId, active);
                    },
                    [this, requestId](ActiveDownload&& active)
                        -> ActiveDownload* {
                        auto [stored, didInsert] = activeDownloads_.try_emplace(
                            requestId, std::move(active));
                        return didInsert ? &stored->second : nullptr;
                    },
                    [&pending] {
                        return completeDownloadCancellation(
                            pending->args.Get(), pending->operation.Get(),
                            pending->deferral.Get());
                    },
                    [this, requestId](ActiveDownload&& active) {
                        return retryDownloads_
                            .try_emplace(requestId, std::move(active))
                            .second;
                    });
            logOperationFailure("download_cancel_transaction_failed",
                                outcome.result);
            if (outcome.action ==
                PendingDownloadCancelAction::ReportCancelled) {
                dispatchListener(
                    generation,
                    [requestId, totalBytes](gui::BrowserEventListener& listener) {
                        listener.onDownloadUpdated(
                            requestId, gui::BrowserDownloadState::Cancelled,
                            -1, totalBytes);
                    });
            } else if (outcome.action ==
                       PendingDownloadCancelAction::ReportCancelFailed) {
                dispatchListener(
                    generation,
                    [requestId, totalBytes](gui::BrowserEventListener& listener) {
                        listener.onDownloadUpdated(
                            requestId, gui::BrowserDownloadState::CancelFailed,
                            -1, totalBytes);
                    });
            }
            return;
        }
        const auto retryFound = retryDownloads_.find(requestId);
        if (retryFound != retryDownloads_.end()) {
            ActiveDownload& active = retryFound->second;
            const std::uint64_t generation = generation_;
            const HRESULT result = requestActiveDownloadCancellation(
                active, [&active] { return active.operation->Cancel(); },
                [this, generation, requestId, totalBytes = active.totalBytes] {
                    dispatchListener(
                        generation,
                        [requestId, totalBytes](
                            gui::BrowserEventListener& listener) {
                            listener.onDownloadUpdated(
                                requestId,
                                gui::BrowserDownloadState::CancelFailed, -1,
                                totalBytes);
                        });
                });
            logOperationFailure("download_cancel_failed", result);
            if (SUCCEEDED(result)) {
                const std::int64_t totalBytes = active.totalBytes;
                dispatchListener(
                    generation,
                    [requestId, totalBytes](
                        gui::BrowserEventListener& listener) {
                        listener.onDownloadUpdated(
                            requestId, gui::BrowserDownloadState::Cancelled,
                            -1, totalBytes);
                    });
                retryDownloads_.erase(retryFound);
            }
            return;
        }

        const auto found = activeDownloads_.find(requestId);
        if (found == activeDownloads_.end()) {
            return;
        }
        ActiveDownload& active = found->second;
        const std::uint64_t generation = generation_;
        const HRESULT result = requestActiveDownloadCancellation(
            active, [&active] { return active.operation->Cancel(); },
            [this, generation, requestId, totalBytes = active.totalBytes] {
                dispatchListener(
                    generation,
                    [requestId, totalBytes](gui::BrowserEventListener& listener) {
                        listener.onDownloadUpdated(
                            requestId, gui::BrowserDownloadState::CancelFailed,
                            -1, totalBytes);
                    });
            });
        logOperationFailure("download_cancel_failed", result);
    }

    // 调用线程：GUI 主线程。只有当前存活请求的显式允许会解除 Cancel。
    void answerExternalProtocol(const std::uint64_t requestId,
                                bool isAllowed) noexcept {
        std::optional<PendingExternalProtocol> pending =
            pendingExternalProtocols_.take(requestId);
        if (!pending.has_value()) {
            return;
        }
        if (isShuttingDown_ || pending->lifecycleSerial != lifecycleSerial_ ||
            pending->generation != generation_) {
            isAllowed = false;
        }
        logOperationFailure(
            "external_protocol_decision_failed",
            completeExternalProtocolDecision(pending->args.Get(),
                                             pending->deferral.Get(), isAllowed));
    }

    // 调用线程：GUI 主线程。会话例外只应用于当前 requestId 保存的来源。
    void answerCertificateError(
        const std::uint64_t requestId,
        gui::BrowserCertificateDecision decision) noexcept {
        std::optional<PendingCertificate> pending =
            pendingCertificates_.take(requestId);
        if (!pending.has_value()) {
            return;
        }
        if (isShuttingDown_ || pending->lifecycleSerial != lifecycleSerial_ ||
            pending->generation != generation_ ||
            pending->origin.isEmpty()) {
            decision = gui::BrowserCertificateDecision::ReturnToSafety;
        }
        logOperationFailure(
            "certificate_decision_failed",
            completeCertificateDecision(pending->args.Get(), pending->deferral.Get(),
                                        decision));
    }

    // 调用线程：GUI 主线程。只请求当前文档退出标准 Fullscreen API。
    void exitFullScreen() noexcept {
        if (isShuttingDown_ || !isReady_ || webView_ == nullptr) {
            return;
        }
        logOperationFailure("fullscreen_exit_request_failed",
                            submitFullScreenExitRequest());
    }

    // 调用线程：GUI 主线程。标准 exitFullscreen 脚本可重复提交且不会等待完成。
    HRESULT submitFullScreenExitRequest() noexcept {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        return webView_->ExecuteScript(
            L"document.fullscreenElement && document.exitFullscreen();",
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [this, weakLifetime, lifecycleSerial](const HRESULT status,
                                                      LPCWSTR) -> HRESULT {
                    if (!weakLifetime.expired() && !isShuttingDown_ &&
                        lifecycleSerial == lifecycleSerial_) {
                        logOperationFailure("fullscreen_exit_failed", status);
                    }
                    return S_OK;
                })
                .Get());
    }

    // 调用线程：GUI 主线程。关闭门禁生效后仍提交一次退出请求，不等待脚本完成。
    void requestExitFullScreenForShutdown() noexcept {
        if (webView_ == nullptr) {
            return;
        }
        const HRESULT result = submitShutdownFullScreenExit(
            [this] { return submitFullScreenExitRequest(); });
        logOperationFailure("fullscreen_shutdown_exit_request_failed", result);
    }

    // 调用线程：GUI 主线程。不等待浏览器子进程。
    void closePopups() noexcept {
        popupCoordinator_.beginShutdown();
        for (const auto& popup : popupWindows_) {
            popup->close();
        }
        popupWindows_.clear();
    }

    // 调用线程：GUI 主线程。只释放本适配器持有的 COM 对象，不等待子进程。
    void shutdown() noexcept {
        lifecycleGate_.beginShutdown();
        if (isShuttingDown_ && lifetime_ == nullptr && controller_ == nullptr &&
            environment_ == nullptr && !ownsComApartmentReference_) {
            listener_ = nullptr;
            return;
        }
        isShuttingDown_ = true;
        isReady_ = false;
        ++lifecycleSerial_;
        listener_ = nullptr;
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

    // 调用线程：GUI STA。requestId 在对象生命周期内单调递增且不使用零值。
    std::uint64_t nextSensitiveRequestId() noexcept {
        const std::uint64_t result = nextSensitiveRequestId_++;
        if (nextSensitiveRequestId_ == 0) {
            nextSensitiveRequestId_ = 1;
        }
        return result == 0 ? nextSensitiveRequestId_++ : result;
    }

    // 回调线程：GUI 主线程；超时只拒绝仍属于同一生命周期的权限请求。
    void schedulePermissionTimeout(const std::uint64_t requestId,
                                   const std::uint64_t lifecycleSerial) {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        QTimer::singleShot(30000, QApplication::instance(),
                           [this, weakLifetime, lifecycleSerial, requestId] {
                               if (weakLifetime.expired()) {
                                   return;
                               }
                               if (!isShuttingDown_ &&
                                   lifecycleSerial == lifecycleSerial_) {
                                   answerPermission(
                                       requestId,
                                       gui::BrowserPermissionDecision::Deny);
                               }
                           });
    }

    // 回调线程：GUI 主线程；超时不允许外部协议。
    void scheduleExternalProtocolTimeout(const std::uint64_t requestId,
                                         const std::uint64_t lifecycleSerial) {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        QTimer::singleShot(30000, QApplication::instance(),
                           [this, weakLifetime, lifecycleSerial, requestId] {
                               if (weakLifetime.expired()) {
                                   return;
                               }
                               if (!isShuttingDown_ &&
                                   lifecycleSerial == lifecycleSerial_) {
                                   answerExternalProtocol(requestId, false);
                               }
                           });
    }

    // 回调线程：GUI 主线程；证书请求超时返回安全页面。
    void scheduleCertificateTimeout(const std::uint64_t requestId,
                                    const std::uint64_t lifecycleSerial) {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        QTimer::singleShot(30000, QApplication::instance(),
                           [this, weakLifetime, lifecycleSerial, requestId] {
                               if (weakLifetime.expired()) {
                                   return;
                               }
                               if (!isShuttingDown_ &&
                                   lifecycleSerial == lifecycleSerial_) {
                                   answerCertificateError(
                                       requestId,
                                       gui::BrowserCertificateDecision::ReturnToSafety);
                               }
                           });
    }

    // 回调线程：GUI 主线程；未选择路径的下载超时会被取消并完成 deferral。
    void scheduleDownloadTimeout(const std::uint64_t requestId,
                                 const std::uint64_t lifecycleSerial) {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        QTimer::singleShot(30000, QApplication::instance(),
                           [this, weakLifetime, lifecycleSerial, requestId] {
                               if (weakLifetime.expired()) {
                                   return;
                               }
                               if (!isShuttingDown_ &&
                                   lifecycleSerial == lifecycleSerial_) {
                                   cancelDownload(requestId);
                               }
                           });
    }

    // 调用线程：GUI STA；事件回调只读取 operation 数值并投递稳定下载状态。
    HRESULT registerActiveDownload(const std::uint64_t requestId,
                                   ActiveDownload& active) {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = active.lifecycleSerial;
        const ComPtr<ICoreWebView2DownloadOperation> operation = active.operation;
        EventRegistrationToken bytesToken{};
        HRESULT result = operation->add_BytesReceivedChanged(
            Callback<ICoreWebView2BytesReceivedChangedEventHandler>(
                [this, weakLifetime, lifecycleSerial, requestId](
                    ICoreWebView2DownloadOperation*, IUnknown*) -> HRESULT {
                    if (weakLifetime.expired()) {
                        return S_OK;
                    }
                    if (!isShuttingDown_ && lifecycleSerial == lifecycleSerial_) {
                        emitDownloadUpdate(requestId);
                    }
                    return S_OK;
                })
                .Get(),
            &bytesToken);
        if (FAILED(result)) {
            return result;
        }
        active.bytesReceivedChanged.bind(
            bytesToken, [operation](const EventRegistrationToken token) {
                return operation->remove_BytesReceivedChanged(token);
            });

        EventRegistrationToken stateToken{};
        result = operation->add_StateChanged(
            Callback<ICoreWebView2StateChangedEventHandler>(
                [this, weakLifetime, lifecycleSerial, requestId](
                    ICoreWebView2DownloadOperation*, IUnknown*) -> HRESULT {
                    if (weakLifetime.expired()) {
                        return S_OK;
                    }
                    if (!isShuttingDown_ && lifecycleSerial == lifecycleSerial_) {
                        emitDownloadUpdate(requestId);
                    }
                    return S_OK;
                })
                .Get(),
            &stateToken);
        if (FAILED(result)) {
            active.bytesReceivedChanged.reset();
            return result;
        }
        active.stateChanged.bind(
            stateToken, [operation](const EventRegistrationToken token) {
                return operation->remove_StateChanged(token);
            });
        return S_OK;
    }

    // 调用线程：WebView2 下载事件所在 GUI STA；不得记录 URI 或目标路径。
    void emitDownloadUpdate(const std::uint64_t requestId) {
        const auto found = activeDownloads_.find(requestId);
        if (found == activeDownloads_.end()) {
            return;
        }
        ActiveDownload& active = found->second;
        const DownloadSnapshot snapshot = readDownloadSnapshot(
            active.operation.Get(), active.totalBytes, active.isCancelRequested);
        if (!snapshot.hasState) {
            logOperationFailure("download_state_read_failed", snapshot.stateResult);
            return;
        }
        const std::uint64_t generation = generation_;
        dispatchListener(
            generation,
            [requestId, snapshot](gui::BrowserEventListener& listener) {
                listener.onDownloadUpdated(
                    requestId, snapshot.state, snapshot.receivedBytes,
                    snapshot.totalBytes);
            });
        if (snapshot.isTerminal) {
            const std::weak_ptr<int> weakLifetime = lifetime_;
            const std::uint64_t lifecycleSerial = lifecycleSerial_;
            QMetaObject::invokeMethod(
                QApplication::instance(),
                [this, weakLifetime, lifecycleSerial, requestId] {
                    if (weakLifetime.expired()) {
                        return;
                    }
                    static_cast<void>(eraseTerminalDownloadIfCurrent(
                        activeDownloads_, requestId, lifecycleSerial,
                        lifecycleSerial_, isShuttingDown_));
                },
                Qt::QueuedConnection);
        }
    }

    // 调用线程：创建 COM 对象的 GUI STA；关闭时逐项拒绝并完成全部 deferral。
    void cancelPendingSensitiveRequests() noexcept {
        for (PendingPermission& pending : pendingPermissions_.takeAll()) {
            logOperationFailure(
                "permission_shutdown_reject_failed",
                completePermissionDecision(
                    pending.args.Get(), pending.deferral.Get(),
                    gui::BrowserPermissionDecision::Deny));
        }
        for (PendingScreenCapture& pending : pendingScreenCaptures_.takeAll()) {
            logOperationFailure(
                "screen_capture_shutdown_reject_failed",
                completeScreenCaptureDecision(
                    pending.args.Get(), pending.deferral.Get(),
                    gui::BrowserPermissionDecision::Deny));
        }
        for (PendingExternalProtocol& pending :
             pendingExternalProtocols_.takeAll()) {
            logOperationFailure(
                "external_uri_shutdown_reject_failed",
                completeExternalProtocolDecision(pending.args.Get(),
                                                 pending.deferral.Get(), false));
        }
        for (PendingCertificate& pending : pendingCertificates_.takeAll()) {
            logOperationFailure(
                "certificate_shutdown_reject_failed",
                completeCertificateDecision(
                    pending.args.Get(), pending.deferral.Get(),
                    gui::BrowserCertificateDecision::ReturnToSafety));
        }
        for (PendingDownload& pending : pendingDownloads_.takeAll()) {
            logOperationFailure(
                "download_shutdown_cancel_failed",
                completeDownloadCancellation(pending.args.Get(),
                                             pending.operation.Get(),
                                             pending.deferral.Get()));
        }
        for (auto& [requestId, active] : activeDownloads_) {
            static_cast<void>(requestId);
            logOperationFailure(
                "download_shutdown_cancel_failed",
                cancelActiveDownloadForShutdown(
                    active, [&active] { return active.operation->Cancel(); }));
        }
        activeDownloads_.clear();
        for (auto& [requestId, active] : retryDownloads_) {
            static_cast<void>(requestId);
            logOperationFailure(
                "download_shutdown_cancel_failed",
                cancelActiveDownloadForShutdown(
                    active, [&active] { return active.operation->Cancel(); }));
        }
        retryDownloads_.clear();
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
                    const bool isCurrentLifecycle =
                        !weakLifetime.expired() &&
                        isActive(weakLifetime, lifecycleSerial, generation);
                    if (!acceptControllerCompletion(isCurrentLifecycle, controller)) {
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
        ComPtr<ICoreWebView2_13> webView13;
        if (SUCCEEDED(result)) {
            result = webView_.As(&webView13);
        }
        if (SUCCEEDED(result)) {
            result = webView13->get_Profile(&profile_);
        }
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
            result = registerScreenCaptureStarting();
        }
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

    // 调用线程：GUI STA；回调持有 deferral，经监听器等待用户决定。
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
                        static_cast<void>(rejectPermissionRequest(args));
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        return S_OK;
                    }
                    if (args == nullptr) {
                        logOperationFailure("permission_args_missing", E_POINTER);
                        return S_OK;
                    }
                    HRESULT status =
                        args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                    ComPtr<ICoreWebView2PermissionRequestedEventArgs> baseArgs(args);
                    ComPtr<ICoreWebView2Deferral> deferral;
                    const HRESULT deferralStatus =
                        baseArgs->GetDeferral(&deferral);
                    status = firstFailure(status, deferralStatus);
                    if (deferral == nullptr) {
                        logOperationFailure(
                            "permission_deferral_failed",
                            FAILED(status) ? status : E_POINTER);
                        return S_OK;
                    }

                    ComPtr<ICoreWebView2PermissionRequestedEventArgs2> args2;
                    ComPtr<ICoreWebView2PermissionRequestedEventArgs3> args3;
                    HRESULT args2Status = baseArgs.As(&args2);
                    status = firstFailure(status, args2Status);
                    if (SUCCEEDED(args2Status)) {
                        status = firstFailure(status, args2->put_Handled(TRUE));
                    }
                    const HRESULT args3Status = baseArgs.As(&args3);
                    status = firstFailure(status, args3Status);

                    COREWEBVIEW2_PERMISSION_KIND rawKind =
                        COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
                    LPWSTR rawUri = nullptr;
                    status = firstFailure(
                        status, args->get_PermissionKind(&rawKind));
                    status = firstFailure(status, args->get_Uri(&rawUri));
                    CoTaskMemString uri(rawUri);
                    const std::optional<gui::BrowserPermissionKind> kind =
                        supportedPermissionKind(rawKind);
                    const QString origin = normalizedOriginFromUri(uri.get());
                    if (FAILED(status) || !kind.has_value() || origin.isEmpty() ||
                        listener_ == nullptr) {
                        const HRESULT rejectionResult =
                            completePermissionRejection(
                                baseArgs.Get(), args3.Get(), deferral.Get());
                        logOperationFailure(
                            "permission_request_rejected",
                            firstFailure(status, rejectionResult));
                        return S_OK;
                    }

                    const std::uint64_t requestId = nextSensitiveRequestId();
                    PendingPermission pending{args3, deferral, origin, *kind,
                                              lifecycleSerial, generation};
                    if (!pendingPermissions_.insert(requestId, std::move(pending))) {
                        logOperationFailure(
                            "permission_store_failed",
                            completePermissionDecision(
                                args3.Get(), deferral.Get(),
                                gui::BrowserPermissionDecision::Deny));
                        return S_OK;
                    }
                    dispatchListener(
                        generation,
                        [requestId, origin,
                         kind = *kind](gui::BrowserEventListener& listener) {
                            listener.onPermissionRequested(requestId, origin, kind);
                        });
                    schedulePermissionTimeout(requestId, lifecycleSerial);
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

    // 调用线程：GUI STA；只注册主 WebView 事件，默认拒绝后等待一次性用户决定。
    HRESULT registerScreenCaptureStarting() {
        ComPtr<ICoreWebView2_27> webView27;
        HRESULT result = webView_.As(&webView27);
        if (FAILED(result)) {
            logOperationFailure("screen_capture_interface_unavailable", result);
            return result;
        }
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        result = webView27->add_ScreenCaptureStarting(
            Callback<ICoreWebView2ScreenCaptureStartingEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2*,
                    ICoreWebView2ScreenCaptureStartingEventArgs* const args)
                    -> HRESULT {
                    if (weakLifetime.expired()) {
                        static_cast<void>(rejectScreenCaptureRequest(args));
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        static_cast<void>(rejectScreenCaptureRequest(args));
                        return S_OK;
                    }
                    if (args == nullptr) {
                        logOperationFailure("screen_capture_args_missing", E_POINTER);
                        return S_OK;
                    }

                    HRESULT status = args->put_Cancel(TRUE);
                    status = firstFailure(status, args->put_Handled(TRUE));
                    ComPtr<ICoreWebView2FrameInfo> frameInfo;
                    const HRESULT frameResult =
                        args->get_OriginalSourceFrameInfo(&frameInfo);
                    status = firstFailure(status, frameResult);
                    LPWSTR rawSource = nullptr;
                    HRESULT sourceResult = E_POINTER;
                    if (frameInfo != nullptr) {
                        sourceResult = frameInfo->get_Source(&rawSource);
                    }
                    status = firstFailure(status, sourceResult);
                    CoTaskMemString source(rawSource);
                    const QString origin = normalizedOriginFromUri(source.get());

                    ComPtr<ICoreWebView2Deferral> deferral;
                    const HRESULT deferralResult = args->GetDeferral(&deferral);
                    status = firstFailure(status, deferralResult);
                    if (deferral == nullptr) {
                        logOperationFailure(
                            "screen_capture_deferral_failed",
                            FAILED(status) ? status : E_POINTER);
                        return S_OK;
                    }
                    if (FAILED(status) || origin.isEmpty() || listener_ == nullptr) {
                        const HRESULT rejectionResult =
                            completeScreenCaptureDecision(
                                args, deferral.Get(),
                                gui::BrowserPermissionDecision::Deny);
                        logOperationFailure(
                            "screen_capture_request_rejected",
                            firstFailure(status, rejectionResult));
                        return S_OK;
                    }

                    const std::uint64_t requestId = nextSensitiveRequestId();
                    PendingScreenCapture pending{args, deferral, origin,
                                                 lifecycleSerial, generation};
                    if (!pendingScreenCaptures_.insert(requestId,
                                                       std::move(pending))) {
                        logOperationFailure(
                            "screen_capture_store_failed",
                            completeScreenCaptureDecision(
                                args, deferral.Get(),
                                gui::BrowserPermissionDecision::Deny));
                        return S_OK;
                    }
                    dispatchListener(
                        generation,
                        [requestId, origin](gui::BrowserEventListener& listener) {
                            listener.onPermissionRequested(
                                requestId, origin,
                                gui::BrowserPermissionKind::ScreenCapture);
                        });
                    schedulePermissionTimeout(requestId, lifecycleSerial);
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            screenCaptureStarting_.bind(
                token, [webView27](const EventRegistrationToken value) {
                    return webView27->remove_ScreenCaptureStarting(value);
                });
        } else {
            logOperationFailure("screen_capture_registration_failed", result);
        }
        return result;
    }

    // 调用线程：GUI STA；回调先接管下载，再等待用户选择不覆盖的新路径。
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
                        ComPtr<ICoreWebView2DownloadOperation> operation;
                        ComPtr<ICoreWebView2Deferral> deferral;
                        const HRESULT status = prepareDownloadRequest(
                            args, operation.GetAddressOf(), deferral.GetAddressOf());
                        static_cast<void>(firstFailure(
                            status,
                            completeDownloadCancellation(
                                args, operation.Get(), deferral.Get())));
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        ComPtr<ICoreWebView2DownloadOperation> operation;
                        ComPtr<ICoreWebView2Deferral> deferral;
                        const HRESULT status = prepareDownloadRequest(
                            args, operation.GetAddressOf(), deferral.GetAddressOf());
                        logOperationFailure(
                            "download_inactive_reject_failed",
                            firstFailure(
                                status,
                                completeDownloadCancellation(
                                    args, operation.Get(), deferral.Get())));
                        return S_OK;
                    }
                    if (args == nullptr) {
                        logOperationFailure("download_args_missing", E_POINTER);
                        return S_OK;
                    }
                    ComPtr<ICoreWebView2DownloadOperation> operation;
                    ComPtr<ICoreWebView2Deferral> deferral;
                    HRESULT status = prepareDownloadRequest(
                        args, operation.GetAddressOf(), deferral.GetAddressOf());
                    LPWSTR rawSuggestedPath = nullptr;
                    LPWSTR rawUri = nullptr;
                    status = firstFailure(
                        status, args->get_ResultFilePath(&rawSuggestedPath));
                    status = firstFailure(
                        status, operation != nullptr ? operation->get_Uri(&rawUri)
                                                     : E_POINTER);
                    CoTaskMemString suggestedPath(rawSuggestedPath);
                    CoTaskMemString uri(rawUri);
                    const QString suggestedFileName =
                        suggestedPath != nullptr
                            ? QFileInfo(QString::fromWCharArray(suggestedPath.get()))
                                  .fileName()
                            : QString{};
                    const QString origin = normalizedOriginFromUri(uri.get());
                    INT64 rawTotalBytes = -1;
                    if (operation == nullptr ||
                        FAILED(operation->get_TotalBytesToReceive(&rawTotalBytes))) {
                        rawTotalBytes = -1;
                    }
                    if (FAILED(status) || operation == nullptr || deferral == nullptr ||
                        suggestedFileName.isEmpty() || origin.isEmpty() ||
                        listener_ == nullptr) {
                        status = firstFailure(
                            status,
                            completeDownloadCancellation(
                                args, operation.Get(), deferral.Get()));
                        logOperationFailure("download_request_rejected",
                                            FAILED(status) ? status : E_INVALIDARG);
                        return S_OK;
                    }

                    const std::uint64_t requestId = nextSensitiveRequestId();
                    PendingDownload pending{args, operation, deferral,
                                            static_cast<std::int64_t>(rawTotalBytes),
                                            lifecycleSerial, generation};
                    if (!pendingDownloads_.insert(requestId, std::move(pending))) {
                        logOperationFailure(
                            "download_store_failed",
                            completeDownloadCancellation(args, operation.Get(),
                                                         deferral.Get()));
                        return S_OK;
                    }
                    dispatchListener(
                        generation,
                        [requestId, origin, suggestedFileName,
                         totalBytes = static_cast<std::int64_t>(rawTotalBytes)](
                            gui::BrowserEventListener& listener) {
                            listener.onDownloadRequested(requestId, origin,
                                                         suggestedFileName,
                                                         totalBytes);
                        });
                    scheduleDownloadTimeout(requestId, lifecycleSerial);
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

    // 调用线程：GUI STA；回调默认取消，并只把规范化来源和稳定说明交给界面。
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
                        ComPtr<ICoreWebView2Deferral> deferral;
                        const HRESULT status = prepareCertificateRequest(
                            args, deferral.GetAddressOf());
                        static_cast<void>(firstFailure(
                            status,
                            completeCertificateDecision(
                                args, deferral.Get(),
                                gui::BrowserCertificateDecision::ReturnToSafety)));
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        ComPtr<ICoreWebView2Deferral> deferral;
                        const HRESULT status = prepareCertificateRequest(
                            args, deferral.GetAddressOf());
                        logOperationFailure(
                            "certificate_inactive_reject_failed",
                            firstFailure(
                                status,
                                completeCertificateDecision(
                                    args, deferral.Get(),
                                    gui::BrowserCertificateDecision::ReturnToSafety)));
                        return S_OK;
                    }
                    if (args == nullptr) {
                        logOperationFailure("certificate_args_missing", E_POINTER);
                        return S_OK;
                    }
                    ComPtr<ICoreWebView2Deferral> deferral;
                    HRESULT status = prepareCertificateRequest(
                        args, deferral.GetAddressOf());
                    COREWEBVIEW2_WEB_ERROR_STATUS errorStatus =
                        COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                    LPWSTR rawRequestUri = nullptr;
                    status = firstFailure(
                        status, args->get_ErrorStatus(&errorStatus));
                    status = firstFailure(
                        status, args->get_RequestUri(&rawRequestUri));
                    CoTaskMemString requestUri(rawRequestUri);
                    const QString origin = normalizedOriginFromUri(requestUri.get());
                    if (FAILED(status) || deferral == nullptr || origin.isEmpty() ||
                        listener_ == nullptr) {
                        if (deferral != nullptr) {
                            status = firstFailure(
                                status,
                                completeCertificateDecision(
                                    args, deferral.Get(),
                                    gui::BrowserCertificateDecision::ReturnToSafety));
                        }
                        logOperationFailure("certificate_request_rejected",
                                            FAILED(status) ? status : E_INVALIDARG);
                        return S_OK;
                    }

                    const std::uint64_t requestId = nextSensitiveRequestId();
                    PendingCertificate pending{args, deferral, origin,
                                               lifecycleSerial, generation};
                    if (!pendingCertificates_.insert(requestId,
                                                     std::move(pending))) {
                        logOperationFailure(
                            "certificate_store_failed",
                            completeCertificateDecision(
                                args, deferral.Get(),
                                gui::BrowserCertificateDecision::ReturnToSafety));
                        return S_OK;
                    }
                    const QString description =
                        certificateErrorDescription(errorStatus);
                    dispatchListener(
                        generation,
                        [requestId, origin,
                         description](gui::BrowserEventListener& listener) {
                            listener.onCertificateErrorRequested(
                                requestId, origin, description);
                        });
                    scheduleCertificateTimeout(requestId, lifecycleSerial);
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

    // 调用线程：GUI STA；非用户触发或无效目标保持 Cancel，不启动宿主外部进程。
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
                        ComPtr<ICoreWebView2Deferral> deferral;
                        const HRESULT status = prepareExternalProtocolRequest(
                            args, deferral.GetAddressOf());
                        static_cast<void>(firstFailure(
                            status,
                            completeExternalProtocolDecision(
                                args, deferral.Get(), false)));
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        ComPtr<ICoreWebView2Deferral> deferral;
                        const HRESULT status = prepareExternalProtocolRequest(
                            args, deferral.GetAddressOf());
                        logOperationFailure(
                            "external_uri_inactive_reject_failed",
                            firstFailure(
                                status,
                                completeExternalProtocolDecision(
                                    args, deferral.Get(), false)));
                        return S_OK;
                    }
                    if (args == nullptr) {
                        logOperationFailure("external_uri_args_missing", E_POINTER);
                        return S_OK;
                    }
                    ComPtr<ICoreWebView2Deferral> deferral;
                    HRESULT status = prepareExternalProtocolRequest(
                        args, deferral.GetAddressOf());
                    BOOL isUserInitiated = FALSE;
                    LPWSTR rawUri = nullptr;
                    LPWSTR rawInitiatingOrigin = nullptr;
                    status = firstFailure(
                        status, args->get_IsUserInitiated(&isUserInitiated));
                    status = firstFailure(status, args->get_Uri(&rawUri));
                    status = firstFailure(
                        status,
                        args->get_InitiatingOrigin(&rawInitiatingOrigin));
                    CoTaskMemString uri(rawUri);
                    CoTaskMemString initiatingOrigin(rawInitiatingOrigin);
                    const QString target =
                        uri != nullptr ? QString::fromWCharArray(uri.get()) : QString{};
                    const QString origin =
                        normalizedOriginFromUri(initiatingOrigin.get());
                    if (FAILED(status) || deferral == nullptr ||
                        isUserInitiated == FALSE || !isValidExternalTarget(target) ||
                        origin.isEmpty() || listener_ == nullptr) {
                        if (deferral != nullptr) {
                            status = firstFailure(
                                status,
                                completeExternalProtocolDecision(
                                    args, deferral.Get(), false));
                        }
                        logOperationFailure("external_uri_request_rejected",
                                            FAILED(status) ? status : E_INVALIDARG);
                        return S_OK;
                    }

                    const std::uint64_t requestId = nextSensitiveRequestId();
                    PendingExternalProtocol pending{args, deferral, lifecycleSerial,
                                                     generation};
                    if (!pendingExternalProtocols_.insert(requestId,
                                                          std::move(pending))) {
                        logOperationFailure(
                            "external_uri_store_failed",
                            completeExternalProtocolDecision(args, deferral.Get(),
                                                             false));
                        return S_OK;
                    }
                    dispatchListener(
                        generation,
                        [requestId, origin,
                         target](gui::BrowserEventListener& listener) {
                            listener.onExternalProtocolRequested(requestId, origin,
                                                                 target);
                        });
                    scheduleExternalProtocolTimeout(requestId, lifecycleSerial);
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

    // 调用线程：GUI STA；事件回调创建共享 Profile 的受控登录子窗口。
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
                        return rejectNewWindow(args);
                    }
                    const std::uint64_t generation = generation_;
                    if (!isActive(weakLifetime, lifecycleSerial, generation)) {
                        return rejectNewWindow(args);
                    }
                    return handleNewWindowRequest(args);
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

    // 调用线程：主页面或登录子窗口 NewWindowRequested 所在的 GUI STA。
    HRESULT handleNewWindowRequest(
        ICoreWebView2NewWindowRequestedEventArgs* const args) {
        if (args == nullptr || isShuttingDown_ || !isReady_ ||
            environment_ == nullptr || profile_ == nullptr) {
            return rejectNewWindow(args);
        }
        if (!popupCoordinator_.tryReserve()) {
            const HRESULT result = rejectNewWindow(args);
            const std::uint64_t generation = generation_;
            dispatchListener(generation, [](gui::BrowserEventListener& listener) {
                listener.onPopupRejected();
            });
            return result;
        }

        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        auto popup = std::make_unique<WebView2PopupWindow>(
            environment_, profile_, parentWindow_,
            [this, weakLifetime, lifecycleSerial](WebView2PopupWindow* closedPopup) {
                if (weakLifetime.expired()) {
                    return;
                }
                QTimer::singleShot(
                    0, QApplication::instance(),
                    [this, weakLifetime, lifecycleSerial, closedPopup] {
                        if (weakLifetime.expired() || isShuttingDown_ ||
                            lifecycleSerial != lifecycleSerial_) {
                            return;
                        }
                        const auto found = std::find_if(
                            popupWindows_.begin(), popupWindows_.end(),
                            [closedPopup](const auto& candidate) {
                                return candidate.get() == closedPopup;
                            });
                        if (found != popupWindows_.end()) {
                            popupWindows_.erase(found);
                            popupCoordinator_.release();
                        }
                    });
            },
            [this, weakLifetime, lifecycleSerial](
                ICoreWebView2NewWindowRequestedEventArgs* nestedArgs) {
                if (weakLifetime.expired() || isShuttingDown_ ||
                    lifecycleSerial != lifecycleSerial_) {
                    return rejectNewWindow(nestedArgs);
                }
                return handleNewWindowRequest(nestedArgs);
            });
        WebView2PopupWindow* const popupPointer = popup.get();
        popupWindows_.push_back(std::move(popup));
        const HRESULT result = popupPointer->createFor(args);
        if (FAILED(result)) {
            const auto found = std::find_if(
                popupWindows_.begin(), popupWindows_.end(),
                [popupPointer](const auto& candidate) {
                    return candidate.get() == popupPointer;
                });
            if (found != popupWindows_.end()) {
                popupWindows_.erase(found);
                popupCoordinator_.release();
            }
            logOperationFailure("popup_creation_failed", result);
        }
        return FAILED(result) ? result : S_OK;
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
                        LPWSTR rawUri = nullptr;
                        const HRESULT uriStatus = args->get_Uri(&rawUri);
                        if (clearDataNavigation_.isBusy() && FAILED(uriStatus)) {
                            clearDataNavigation_.reset();
                            reportError(
                                generation_, gui::BrowserErrorKind::ClearDataFailed,
                                uriStatus, "clear_blank_navigation_uri_failed");
                            return S_OK;
                        }
                        CoTaskMemString uri(rawUri);
                        const bool isInternalBlank =
                            SUCCEEDED(uriStatus) && uri != nullptr &&
                            QString::fromWCharArray(uri.get()).compare(
                                QStringLiteral("about:blank"),
                                Qt::CaseInsensitive) == 0;
                        if (clearDataNavigation_.start(navigationId,
                                                       isInternalBlank)) {
                            return S_OK;
                        }
                        if (clearDataNavigation_.isBusy()) {
                            return S_OK;
                        }
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
                        if (clearDataNavigation_.isBusy()) {
                            clearDataNavigation_.reset();
                            reportError(
                                generation_, gui::BrowserErrorKind::ClearDataFailed,
                                status, "clear_blank_navigation_id_failed");
                        } else {
                            logOperationFailure("navigation_id_failed", status);
                        }
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
                        if (clearDataNavigation_.isBusy()) {
                            clearDataNavigation_.reset();
                            reportError(
                                generation_, gui::BrowserErrorKind::ClearDataFailed,
                                result, "clear_blank_navigation_id_failed");
                        } else {
                            reportError(
                                generation_, gui::BrowserErrorKind::NavigationFailed,
                                result, "navigation_id_failed");
                        }
                        return S_OK;
                    }
                    if (clearDataNavigation_.isBusy()) {
                        if (!clearDataNavigation_.ownsNavigation(navigationId)) {
                            return S_OK;
                        }
                        BOOL isSuccess = FALSE;
                        const HRESULT status = args->get_IsSuccess(&isSuccess);
                        const ClearDataNavigationCompletion completion =
                            clearDataNavigation_.complete(
                                navigationId,
                                SUCCEEDED(status) && isSuccess != FALSE);
                        if (completion.outcome ==
                            ClearDataNavigationOutcome::Succeeded) {
                            dispatchListener(
                                completion.generation,
                                [generation = completion.generation](
                                    gui::BrowserEventListener& listener) {
                                    listener.onBrowsingDataCleared(generation);
                                });
                        } else if (completion.outcome ==
                                   ClearDataNavigationOutcome::Failed) {
                            reportError(
                                completion.generation,
                                gui::BrowserErrorKind::ClearDataFailed,
                                FAILED(status) ? status : E_FAIL,
                                "clear_blank_navigation_failed");
                        }
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
                        navigation_.isNavigating() || clearDataNavigation_.isBusy() ||
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
        requestExitFullScreenForShutdown();
        closePopups();
        cancelPendingSensitiveRequests();
        newWindowRequested_.reset();
        launchingExternalUriScheme_.reset();
        serverCertificateErrorDetected_.reset();
        downloadStarting_.reset();
        screenCaptureStarting_.reset();
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
        profile_.Reset();
        environment_.Reset();
        suspension_.invalidate();
        navigation_.reset(generation_);
        clearDataNavigation_.reset();
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
    ComPtr<ICoreWebView2Profile> profile_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webView_;
    EventRegistration navigationStarting_;
    EventRegistration navigationCompleted_;
    EventRegistration documentTitleChanged_;
    EventRegistration fullScreenChanged_;
    EventRegistration permissionRequested_;
    EventRegistration screenCaptureStarting_;
    EventRegistration downloadStarting_;
    EventRegistration serverCertificateErrorDetected_;
    EventRegistration launchingExternalUriScheme_;
    EventRegistration newWindowRequested_;
    PendingRequestStore<PendingPermission> pendingPermissions_;
    PendingRequestStore<PendingScreenCapture> pendingScreenCaptures_;
    PendingRequestStore<PendingExternalProtocol> pendingExternalProtocols_;
    PendingRequestStore<PendingCertificate> pendingCertificates_;
    PendingRequestStore<PendingDownload> pendingDownloads_;
    std::unordered_map<std::uint64_t, ActiveDownload> activeDownloads_;
    std::unordered_map<std::uint64_t, ActiveDownload> retryDownloads_;
    std::vector<std::unique_ptr<WebView2PopupWindow>> popupWindows_;
    PopupCoordinator popupCoordinator_;
    BrowserLifecycleGate lifecycleGate_;
    std::shared_ptr<int> lifetime_;
    std::uint64_t nextSensitiveRequestId_{1};
    std::uint64_t lifecycleSerial_{0};
    std::uint64_t generation_{0};
    NavigationTracker navigation_;
    ClearDataNavigationCoordinator clearDataNavigation_;
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

void WebView2BrowserBackend::closePopups() noexcept { impl_->closePopups(); }

void WebView2BrowserBackend::shutdown() noexcept { impl_->shutdown(); }

}  // namespace mediahub::browser_webview2
