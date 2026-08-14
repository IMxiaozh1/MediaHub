#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "mediahub/browser_webview2/webview2_browser_backend.h"

#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "browser_event_listener.h"
#include "mediahub/logging/logger.h"
#include "webview2_default_deny.h"
#include "webview2_accelerator.h"
#include "webview2_handles.h"
#include "webview2_pending_request.h"
#include "webview2_state.h"
#include "webview2_tab_controller.h"

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
    enum class SnapshotKind {
        NavigationCompleted,
        NavigationStopped,
        DocumentStateChanged,
    };

    struct PendingNewWindow final {
        ComPtr<ICoreWebView2NewWindowRequestedEventArgs> args;
        ComPtr<ICoreWebView2Deferral> deferral;
        std::uint64_t lifecycleSerial{0};
    };

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
        std::uint64_t tabId{0};
    };

    struct ActiveDownload final {
        ComPtr<ICoreWebView2DownloadOperation> operation;
        EventRegistration bytesReceivedChanged;
        EventRegistration stateChanged;
        std::int64_t totalBytes{-1};
        std::uint64_t lifecycleSerial{0};
        std::uint64_t generation{0};
        std::uint64_t tabId{0};
        std::uint64_t updateSerial{0};
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
        hasReportedBrowserProcessFailure_ = false;
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
        userDataDirectory_ = QDir::cleanPath(userDataDirectory);

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
        if (activeTabId_ != 1) {
            const auto found = tabControllers_.find(activeTabId_);
            if (found != tabControllers_.end()) {
                found->second->navigate(normalizedUrl, generation);
                return;
            }
        }
        stopMainFinding(true);
        clearMainFavicon(generation);
        isClearedBlankSnapshotSuppressed_ = false;
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

    // 调用线程：GUI 主线程。同一 Environment/Profile 为标签创建独立 Controller。
    [[nodiscard]] bool createTab(void* const parentWindowHandle,
                                 const std::uint64_t tabId,
                                 const QString& initialUrl,
                                 const std::uint64_t generation,
                                 const std::uint64_t newWindowRequestId) {
        if (isShuttingDown_ || !isReady_ || tabId <= 1 ||
            tabControllers_.find(tabId) != tabControllers_.end()) {
            return false;
        }
        std::optional<PendingNewWindow> pendingNewWindow;
        if (newWindowRequestId != 0) {
            pendingNewWindow = pendingNewWindows_.take(newWindowRequestId);
            if (!pendingNewWindow.has_value() ||
                pendingNewWindow->lifecycleSerial != lifecycleSerial_) {
                if (pendingNewWindow.has_value()) {
                    static_cast<void>(completePopupRequest(
                        pendingNewWindow->args.Get(),
                        pendingNewWindow->deferral.Get(),
                        static_cast<ICoreWebView2*>(nullptr)));
                }
                return false;
            }
        }
        const HWND parent = reinterpret_cast<HWND>(parentWindowHandle);
        auto tab = std::make_unique<WebView2TabController>(
            environment_, profile_, parent, tabId,
            [this](const std::uint64_t readyTabId,
                   const std::uint64_t readyGeneration) {
                dispatchTabListener(
                    [readyTabId, readyGeneration](
                        gui::BrowserEventListener& listener) {
                        listener.onTabReady(readyTabId, readyGeneration);
                    });
            },
            [this](const std::uint64_t startedTabId,
                   const std::uint64_t startedGeneration) {
                dispatchTabListener(
                    [startedTabId, startedGeneration](
                        gui::BrowserEventListener& listener) {
                        listener.onTabNavigationStarted(startedTabId,
                                                        startedGeneration);
                    });
            },
            [this](const std::uint64_t completedTabId,
                   const std::uint64_t completedGeneration,
                   const QString& url, const QString& title,
                   const bool canGoBack, const bool canGoForward) {
                dispatchTabListener(
                    [completedTabId, completedGeneration, url, title,
                     canGoBack, canGoForward](
                        gui::BrowserEventListener& listener) {
                        listener.onTabNavigationCompleted(
                            completedTabId, completedGeneration, url, title,
                            canGoBack, canGoForward);
                    });
            },
            [this](const std::uint64_t changedTabId,
                   const std::uint64_t changedGeneration,
                   const QString& url, const QString& title,
                   const bool canGoBack, const bool canGoForward) {
                dispatchTabListener(
                    [changedTabId, changedGeneration, url, title,
                     canGoBack, canGoForward](
                        gui::BrowserEventListener& listener) {
                        listener.onTabDocumentStateChanged(
                            changedTabId, changedGeneration, url, title,
                            canGoBack, canGoForward);
                    });
            },
            [this](const std::uint64_t stoppedTabId,
                   const std::uint64_t stoppedGeneration,
                   const QString& url, const QString& title,
                   const bool canGoBack, const bool canGoForward) {
                dispatchTabListener(
                    [stoppedTabId, stoppedGeneration, url, title,
                     canGoBack, canGoForward](
                        gui::BrowserEventListener& listener) {
                        listener.onTabNavigationStopped(
                            stoppedTabId, stoppedGeneration, url, title,
                            canGoBack, canGoForward);
                    });
            },
            [this](const std::uint64_t errorTabId,
                   const std::uint64_t errorGeneration,
                   const gui::BrowserErrorKind kind,
                   const HRESULT result) {
                logOperationFailure("tab_operation_failed", result);
                dispatchTabListener(
                    [errorTabId, errorGeneration, kind, result](
                        gui::BrowserEventListener& listener) {
                        listener.onTabError(errorTabId, errorGeneration, kind,
                                            static_cast<long>(result));
                    });
            },
            [this](const std::uint64_t failedTabId,
                   const std::uint64_t failedGeneration,
                   const gui::BrowserProcessFailureKind kind) {
                const std::uint64_t reportedGeneration =
                    kind == gui::BrowserProcessFailureKind::BrowserProcessExited
                        ? generation_
                        : failedGeneration;
                dispatchTabListener(
                    [this, failedTabId, failedGeneration, reportedGeneration,
                     kind](
                        gui::BrowserEventListener& listener) {
                        const auto found = tabControllers_.find(failedTabId);
                        if (found == tabControllers_.end() ||
                            found->second->generation() != failedGeneration) {
                            return;
                        }
                        if (kind == gui::BrowserProcessFailureKind::BrowserProcessExited) {
                            if (controller_ == nullptr || webView_ == nullptr ||
                                generation_ != reportedGeneration ||
                                hasReportedBrowserProcessFailure_) {
                                return;
                            }
                            hasReportedBrowserProcessFailure_ = true;
                        }
                        const std::uint64_t reportedTabId =
                            kind == gui::BrowserProcessFailureKind::BrowserProcessExited
                                ? 1
                                : failedTabId;
                        listener.onTabProcessFailed(reportedTabId,
                                                    reportedGeneration, kind);
                    });
            },
            [this](ICoreWebView2NewWindowRequestedEventArgs* args) {
                return handleNewWindowRequest(args);
            },
            [this](const std::uint64_t closedTabId) {
                dispatchTabListener(
                    [closedTabId](gui::BrowserEventListener& listener) {
                        listener.onTabCloseRequested(closedTabId);
                    });
            },
            [this](const std::uint64_t fullScreenTabId,
                   const std::uint64_t fullScreenGeneration,
                   const bool isFullScreen) {
                if (fullScreenTabId == activeTabId_) {
                    dispatchTabListener(
                        [fullScreenGeneration, isFullScreen](
                            gui::BrowserEventListener& listener) {
                            listener.onFullScreenChanged(fullScreenGeneration,
                                                         isFullScreen);
                    });
                }
            },
            [this](const std::uint64_t audioTabId,
                   const std::uint64_t audioGeneration,
                   const bool isPlayingAudio) {
                dispatchTabListener(
                    [audioTabId, audioGeneration, isPlayingAudio](
                        gui::BrowserEventListener& listener) {
                        listener.onTabAudioStateChanged(
                            audioTabId, audioGeneration, isPlayingAudio);
                    });
            },
            [this](const std::uint64_t faviconTabId,
                   const std::uint64_t faviconGeneration,
                   const std::uint64_t faviconRequestSerial,
                   const QByteArray& pngBytes) {
                dispatchTabListener(
                    [this, faviconTabId, faviconGeneration,
                     faviconRequestSerial, pngBytes](
                        gui::BrowserEventListener& listener) {
                        const auto found = tabControllers_.find(faviconTabId);
                        if (found == tabControllers_.end() ||
                            !found->second->isCurrentFaviconRequest(
                                faviconGeneration, faviconRequestSerial)) {
                            return;
                        }
                        listener.onTabFaviconChanged(
                            faviconTabId, faviconGeneration, pngBytes);
                    });
            },
            [this](const std::uint64_t faviconTabId,
                   const std::uint64_t faviconGeneration,
                   const HRESULT result) {
                Q_UNUSED(faviconTabId);
                Q_UNUSED(faviconGeneration);
                logOperationFailure("tab_favicon_failed", result);
            },
            [this](const std::uint64_t zoomTabId,
                   const std::uint64_t zoomGeneration,
                   const double zoomFactor) {
                dispatchTabListener(
                    [this, zoomTabId, zoomGeneration, zoomFactor](
                        gui::BrowserEventListener& listener) {
                        const auto found = tabControllers_.find(zoomTabId);
                        if (found == tabControllers_.end() ||
                            found->second->generation() != zoomGeneration) {
                            return;
                        }
                        listener.onTabZoomFactorChanged(
                            zoomTabId, zoomGeneration, zoomFactor);
                    });
            },
            [this](const std::uint64_t acceleratorTabId,
                   const std::uint64_t acceleratorGeneration,
                   const gui::BrowserAccelerator accelerator) {
                if (acceleratorTabId == activeTabId_) {
                    dispatchTabListener(
                        [acceleratorGeneration, accelerator](
                            gui::BrowserEventListener& listener) {
                            listener.onAcceleratorRequested(
                                acceleratorGeneration, accelerator);
                    });
                }
            },
            [this](const std::uint64_t findTabId,
                   const std::uint64_t findGeneration,
                   const std::uint64_t findRequestSerial,
                   const int activeMatchIndex, const int matchCount) {
                dispatchTabListener(
                    [this, findTabId, findGeneration, findRequestSerial,
                     activeMatchIndex, matchCount](
                        gui::BrowserEventListener& listener) {
                        const auto found = tabControllers_.find(findTabId);
                        if (found == tabControllers_.end() ||
                            !found->second->isCurrentFindRequest(
                                findGeneration, findRequestSerial)) {
                            return;
                        }
                        listener.onFindResultChanged(
                            findTabId, findGeneration, activeMatchIndex,
                            matchCount);
                    });
            },
            [this](const std::uint64_t findTabId,
                   const std::uint64_t findGeneration,
                   const std::uint64_t findRequestSerial,
                   const HRESULT result) {
                logOperationFailure("tab_find_failed", result);
                dispatchTabListener(
                    [this, findTabId, findGeneration, findRequestSerial,
                     result](
                        gui::BrowserEventListener& listener) {
                        const auto found = tabControllers_.find(findTabId);
                        if (found == tabControllers_.end() ||
                            !found->second->isCurrentFindRequest(
                                findGeneration, findRequestSerial)) {
                            return;
                        }
                        listener.onFindFailed(findTabId, findGeneration,
                                              static_cast<long>(result));
                    });
            },
            [this](const std::uint64_t tabId,
                   const std::uint64_t tabGeneration,
                   ICoreWebView2PermissionRequestedEventArgs* args) {
                return handlePermissionRequest(tabId, tabGeneration, args);
            },
            [this](const std::uint64_t tabId,
                   const std::uint64_t tabGeneration,
                   ICoreWebView2ScreenCaptureStartingEventArgs* args) {
                return handleScreenCaptureRequest(tabId, tabGeneration, args);
            },
            [this](const std::uint64_t tabId,
                   const std::uint64_t tabGeneration,
                   ICoreWebView2DownloadStartingEventArgs* args) {
                return handleDownloadRequest(tabId, tabGeneration, args);
            },
            [this](const std::uint64_t tabId,
                   const std::uint64_t tabGeneration,
                   ICoreWebView2ServerCertificateErrorDetectedEventArgs* args) {
                return handleCertificateRequest(tabId, tabGeneration, args);
            },
            [this](const std::uint64_t tabId,
                   const std::uint64_t tabGeneration,
                   ICoreWebView2LaunchingExternalUriSchemeEventArgs* args) {
                return handleExternalProtocolRequest(tabId, tabGeneration,
                                                     args);
            });
        tab->setBounds(bounds_);
        tab->setVisible(isVisible_ && activeTabId_ == tabId);
        tab->setAudioMuted(isAudioMutedDesired_ || isTabAudioMuted(tabId));
        tab->setZoomFactor(tabZoomFactor(tabId));
        const HRESULT result = tab->create(
            initialUrl, generation,
            pendingNewWindow.has_value() ? pendingNewWindow->args.Get() : nullptr,
            pendingNewWindow.has_value() ? pendingNewWindow->deferral.Get()
                                         : nullptr);
        if (FAILED(result)) {
            logOperationFailure("tab_creation_failed", result);
            return false;
        }
        tabControllers_.emplace(tabId, std::move(tab));
        return true;
    }

    void closeTab(const std::uint64_t tabId) noexcept {
        if (tabId == 1) {
            if (controller_ == nullptr) {
                return;
            }
            newWindowRequested_.reset();
            acceleratorKeyPressed_.reset();
            launchingExternalUriScheme_.reset();
            serverCertificateErrorDetected_.reset();
            downloadStarting_.reset();
            screenCaptureStarting_.reset();
            permissionRequested_.reset();
            fullScreenChanged_.reset();
            processFailed_.reset();
            releaseMainFindController();
            static_cast<void>(nextMainFaviconRequestSerial());
            zoomFactorChanged_.reset();
            faviconChanged_.reset();
            documentPlayingAudioChanged_.reset();
            documentTitleChanged_.reset();
            navigationCompleted_.reset();
            navigationStarting_.reset();
            static_cast<void>(controller_->Close());
            webView_.Reset();
            controller_.Reset();
            tabAudioMuted_.erase(tabId);
            tabZoomFactors_.erase(tabId);
            navigation_.reset(generation_);
            return;
        }
        const auto found = tabControllers_.find(tabId);
        if (found != tabControllers_.end()) {
            found->second->close();
            tabControllers_.erase(found);
            tabAudioMuted_.erase(tabId);
            tabZoomFactors_.erase(tabId);
        }
    }

    void activateTab(const std::uint64_t tabId) noexcept {
        if (tabId != 1 && tabControllers_.find(tabId) == tabControllers_.end()) {
            return;
        }
        activeTabId_ = tabId;
        if (controller_ != nullptr) {
            static_cast<void>(
                controller_->put_IsVisible(isVisible_ && tabId == 1 ? TRUE : FALSE));
        }
        applyEffectiveAudioMute();
        for (auto& [candidateId, controller] : tabControllers_) {
            const bool isActive = candidateId == tabId;
            controller->setVisible(isVisible_ && isActive);
        }
    }

    // 调用线程：GUI 主线程。
    void goBack() noexcept {
        if (activeTabId_ != 1) {
            const auto found = tabControllers_.find(activeTabId_);
            if (found != tabControllers_.end()) found->second->goBack();
            return;
        }
        if (webView_ != nullptr) {
            logOperationFailure("go_back_failed", webView_->GoBack());
        }
    }

    // 调用线程：GUI 主线程。
    void goForward() noexcept {
        if (activeTabId_ != 1) {
            const auto found = tabControllers_.find(activeTabId_);
            if (found != tabControllers_.end()) found->second->goForward();
            return;
        }
        if (webView_ != nullptr) {
            logOperationFailure("go_forward_failed", webView_->GoForward());
        }
    }

    // 调用线程：GUI 主线程。
    void reloadOrStop() noexcept {
        if (activeTabId_ != 1) {
            const auto found = tabControllers_.find(activeTabId_);
            if (found != tabControllers_.end()) found->second->reloadOrStop();
            return;
        }
        if (webView_ == nullptr) {
            return;
        }
        const bool isNavigating = navigation_.isNavigating();
        const HRESULT result = isNavigating ? webView_->Stop() : webView_->Reload();
        logOperationFailure(isNavigating ? "stop_failed" : "reload_failed", result);
    }

    // 调用线程：GUI 主线程。渲染进程失败时 WebView2 建议重新加载对应网页实例。
    [[nodiscard]] bool recoverTab(const std::uint64_t tabId,
                                  const std::uint64_t generation) noexcept {
        if (tabId == 1) {
            if (webView_ == nullptr) {
                return false;
            }
            const HRESULT result = webView_->Reload();
            logOperationFailure("tab_recovery_failed", result);
            if (SUCCEEDED(result)) {
                generation_ = generation;
                navigation_.acceptNavigate(generation);
            }
            return SUCCEEDED(result);
        }
        const auto found = tabControllers_.find(tabId);
        return found != tabControllers_.end() &&
               found->second->reload(generation);
    }

    // 调用线程：GUI 主线程。只将查询传给当前标签的 WebView2 原生 Find 对象。
    void findInPage(const QString& text, const bool forward) noexcept {
        if (activeTabId_ != 1) {
            const auto found = tabControllers_.find(activeTabId_);
            if (found != tabControllers_.end()) {
                found->second->findInPage(text, forward);
                return;
            }
        }
        findInMainPage(text, forward);
    }

    // 调用线程：GUI 主线程。
    void stopFinding(const bool clearSelection) noexcept {
        if (activeTabId_ != 1) {
            const auto found = tabControllers_.find(activeTabId_);
            if (found != tabControllers_.end()) {
                found->second->stopFinding(clearSelection);
                return;
            }
        }
        stopMainFinding(clearSelection);
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
        for (auto& [tabId, tab] : tabControllers_) {
            Q_UNUSED(tabId);
            tab->setBounds(bounds_);
        }
    }

    // 调用线程：GUI 主线程。
    void setVisible(const bool isVisible) noexcept {
        isVisible_ = isVisible;
        if (controller_ != nullptr) {
            logOperationFailure("set_visibility_failed",
                                controller_->put_IsVisible(
                                    isVisible && activeTabId_ == 1 ? TRUE : FALSE));
        }
        for (auto& [tabId, tab] : tabControllers_) {
            tab->setVisible(isVisible && tabId == activeTabId_);
        }
    }

    // 调用线程：GUI 主线程。
    void setAudioMuted(const bool isMuted) noexcept {
        isAudioMutedDesired_ = isMuted;
        applyEffectiveAudioMute();
        for (auto& [tabId, tab] : tabControllers_) {
            tab->setAudioMuted(isMuted || isTabAudioMuted(tabId));
        }
    }

    // 调用线程：GUI 主线程。切换标签只改变可见性，不重写独立静音状态。
    void setTabAudioMuted(const std::uint64_t tabId, const bool isMuted) {
        if (tabId == 0) {
            return;
        }
        tabAudioMuted_[tabId] = isMuted;
        if (tabId == 1) {
            applyEffectiveAudioMute();
            return;
        }
        const auto found = tabControllers_.find(tabId);
        if (found != tabControllers_.end()) {
            found->second->setAudioMuted(isAudioMutedDesired_ || isMuted);
        }
    }

    [[nodiscard]] bool isTabAudioMuted(const std::uint64_t tabId) const noexcept {
        const auto found = tabAudioMuted_.find(tabId);
        return found != tabAudioMuted_.end() && found->second;
    }

    // 调用线程：GUI 主线程。切换标签不改变已保存的缩放比例。
    void setTabZoomFactor(const std::uint64_t tabId,
                          const double zoomFactor) noexcept {
        if (tabId == 0) {
            return;
        }
        const double clampedZoom = std::clamp(zoomFactor, 0.25, 5.0);
        tabZoomFactors_[tabId] = clampedZoom;
        if (tabId == 1) {
            if (controller_ != nullptr) {
                logOperationFailure("set_zoom_failed",
                                    controller_->put_ZoomFactor(clampedZoom));
            }
            return;
        }
        const auto found = tabControllers_.find(tabId);
        if (found != tabControllers_.end()) {
            found->second->setZoomFactor(clampedZoom);
        }
    }

    [[nodiscard]] double tabZoomFactor(
        const std::uint64_t tabId) const noexcept {
        const auto found = tabZoomFactors_.find(tabId);
        return found == tabZoomFactors_.end() ? 1.0 : found->second;
    }

    // 调用线程：GUI 主线程；挂起期间强制静音，只有确认恢复成功后才解除。
    void applyEffectiveAudioMute() noexcept {
        if (webView_ == nullptr) {
            return;
        }
        ComPtr<ICoreWebView2_8> webView8;
        HRESULT result = webView_.As(&webView8);
        if (SUCCEEDED(result)) {
            const bool isMuted = isAudioMutedDesired_ || isTabAudioMuted(1) ||
                                 suspension_.mustMute();
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
        if (activeTabId_ == 1) {
            stopMainFinding(true);
        }
        generation_ = generation;
        if (activeTabId_ != 1) {
            const auto found = tabControllers_.find(activeTabId_);
            if (found == tabControllers_.end()) {
                reportError(generation, gui::BrowserErrorKind::ClearDataFailed,
                            E_UNEXPECTED, "clear_data_active_tab_missing");
                return;
            }
            found->second->clearBrowsingData(
                generation, [this, generation](const HRESULT status) {
                    if (FAILED(status)) {
                        reportError(generation,
                                    gui::BrowserErrorKind::ClearDataFailed,
                                    status, "tab_clear_data_failed");
                        return;
                    }
                    dispatchListener(
                        generation,
                        [generation](gui::BrowserEventListener& listener) {
                            listener.onBrowsingDataCleared(generation);
                        });
                });
            return;
        }
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
                                    ICoreWebView2ClearServerCertificateErrorActionsCompletedHandler>
                                (
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
                                            isClearedBlankSnapshotSuppressed_ =
                                                true;
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
                !isCurrentTabGeneration(pending->generation) ||
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
            !isCurrentTabGeneration(screenCapture->generation) ||
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
        const std::int64_t totalBytes = pending->totalBytes;
        const std::uint64_t generation = pending->generation;
        const std::uint64_t tabId = pending->tabId;
        const auto reportRejectedDownload = [this, tabId, requestId, totalBytes,
                                             generation] {
            dispatchDownloadListener(
                generation,
                [tabId, requestId, totalBytes](gui::BrowserEventListener& listener) {
                    listener.onTabDownloadUpdated(
                        tabId, requestId, gui::BrowserDownloadState::Failed,
                        -1, totalBytes);
                });
        };
        if (isShuttingDown_ || pending->lifecycleSerial != lifecycleSerial_ ||
            !isCurrentTabGeneration(pending->generation) ||
            !isSafeDownloadDestination(destination)) {
            const HRESULT result = completeDownloadCancellation(
                pending->args.Get(), pending->operation.Get(),
                pending->deferral.Get());
            logOperationFailure("download_destination_rejected", result);
            reportRejectedDownload();
            return;
        }

        ActiveDownload active;
        active.operation = pending->operation;
        active.totalBytes = pending->totalBytes;
        active.lifecycleSerial = pending->lifecycleSerial;
        active.generation = pending->generation;
        active.tabId = pending->tabId;
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
        if (FAILED(result)) {
            reportRejectedDownload();
        }
    }

    // 调用线程：GUI 主线程。待选路径先接入终态订阅，失败后只重试 operation。
    void cancelDownload(const std::uint64_t requestId) noexcept {
        std::optional<PendingDownload> pending = pendingDownloads_.take(requestId);
        if (pending.has_value()) {
            const std::uint64_t generation = pending->generation;
            const std::uint64_t tabId = pending->tabId;
            const std::int64_t totalBytes = pending->totalBytes;
            ActiveDownload candidate;
            candidate.operation = pending->operation;
            candidate.totalBytes = totalBytes;
            candidate.lifecycleSerial = pending->lifecycleSerial;
            candidate.generation = generation;
            candidate.tabId = pending->tabId;
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
                dispatchDownloadListener(
                    generation,
                    [tabId, requestId, totalBytes](gui::BrowserEventListener& listener) {
                        listener.onTabDownloadUpdated(
                            tabId, requestId, gui::BrowserDownloadState::Cancelled,
                            -1, totalBytes);
                    });
            } else if (outcome.action ==
                       PendingDownloadCancelAction::ReportCancelFailed) {
                dispatchDownloadListener(
                    generation,
                    [tabId, requestId, totalBytes](gui::BrowserEventListener& listener) {
                        listener.onTabDownloadUpdated(
                            tabId, requestId, gui::BrowserDownloadState::CancelFailed,
                            -1, totalBytes);
                    });
            }
            return;
        }
        const auto retryFound = retryDownloads_.find(requestId);
        if (retryFound != retryDownloads_.end()) {
            ActiveDownload& active = retryFound->second;
            const std::uint64_t generation = active.generation;
            const std::uint64_t tabId = active.tabId;
            const HRESULT result = requestActiveDownloadCancellation(
                active, [&active] { return active.operation->Cancel(); },
                [this, tabId, generation, requestId, totalBytes = active.totalBytes] {
                    dispatchDownloadListener(
                        generation,
                        [tabId, requestId, totalBytes](
                            gui::BrowserEventListener& listener) {
                            listener.onTabDownloadUpdated(
                                tabId, requestId,
                                gui::BrowserDownloadState::CancelFailed, -1,
                                totalBytes);
                        });
                });
            logOperationFailure("download_cancel_failed", result);
            if (SUCCEEDED(result)) {
                const std::int64_t totalBytes = active.totalBytes;
                dispatchDownloadListener(
                    generation,
                    [tabId, requestId, totalBytes](
                        gui::BrowserEventListener& listener) {
                        listener.onTabDownloadUpdated(
                            tabId, requestId, gui::BrowserDownloadState::Cancelled,
                            -1, totalBytes);
                    });
                retryDownloads_.erase(retryFound);
            }
            return;
        }

        const auto resumableFound = resumableDownloads_.find(requestId);
        if (resumableFound != resumableDownloads_.end()) {
            ActiveDownload& active = resumableFound->second;
            const std::uint64_t generation = active.generation;
            const std::uint64_t tabId = active.tabId;
            const std::int64_t totalBytes = active.totalBytes;
            const HRESULT result = requestActiveDownloadCancellation(
                active, [&active] { return active.operation->Cancel(); },
                [this, tabId, generation, requestId, totalBytes] {
                    dispatchDownloadListener(
                        generation,
                        [tabId, requestId, totalBytes](
                            gui::BrowserEventListener& listener) {
                            listener.onTabDownloadUpdated(
                                tabId, requestId,
                                gui::BrowserDownloadState::CancelFailed, -1,
                                totalBytes);
                        });
                });
            logOperationFailure("download_cancel_failed", result);
            if (SUCCEEDED(result)) {
                dispatchDownloadListener(
                    generation,
                    [tabId, requestId, totalBytes](
                        gui::BrowserEventListener& listener) {
                        listener.onTabDownloadUpdated(
                            tabId, requestId,
                            gui::BrowserDownloadState::Cancelled, -1,
                            totalBytes);
                    });
                resumableDownloads_.erase(resumableFound);
            }
            return;
        }

        const auto found = activeDownloads_.find(requestId);
        if (found == activeDownloads_.end()) {
            return;
        }
        ActiveDownload& active = found->second;
        const std::uint64_t generation = active.generation;
        const std::uint64_t tabId = active.tabId;
        const HRESULT result = requestActiveDownloadCancellation(
            active, [&active] { return active.operation->Cancel(); },
            [this, tabId, generation, requestId, totalBytes = active.totalBytes] {
                dispatchDownloadListener(
                    generation,
                    [tabId, requestId, totalBytes](gui::BrowserEventListener& listener) {
                        listener.onTabDownloadUpdated(
                            tabId, requestId, gui::BrowserDownloadState::CancelFailed,
                            -1, totalBytes);
                    });
            });
        logOperationFailure("download_cancel_failed", result);
    }

    // 调用线程：GUI 主线程。只恢复 WebView2 明确标记可继续的中断任务。
    void retryDownload(const std::uint64_t requestId) noexcept {
        const auto found = resumableDownloads_.find(requestId);
        if (found == resumableDownloads_.end()) {
            return;
        }
        auto retained = resumableDownloads_.extract(found);
        ActiveDownload candidate = std::move(retained.mapped());
        const std::uint64_t generation = candidate.generation;
        const std::uint64_t tabId = candidate.tabId;
        const std::int64_t totalBytes = candidate.totalBytes;
        const std::uint64_t previousUpdateSerial = candidate.updateSerial;
        if (isShuttingDown_ || candidate.lifecycleSerial != lifecycleSerial_ ||
            candidate.operation == nullptr) {
            return;
        }

        const ComPtr<ICoreWebView2DownloadOperation> operation =
            candidate.operation;
        const DownloadResumeOutcome outcome =
            resumeInterruptedDownloadTransaction(
                candidate,
                [](ActiveDownload& active) {
                    COREWEBVIEW2_DOWNLOAD_STATE state =
                        COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
                    HRESULT result = active.operation->get_State(&state);
                    if (FAILED(result)) {
                        return result;
                    }
                    BOOL canResume = FALSE;
                    result = active.operation->get_CanResume(&canResume);
                    if (FAILED(result)) {
                        return result;
                    }
                    return state == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED &&
                                   canResume == TRUE
                               ? S_OK
                               : S_FALSE;
                },
                [this, requestId](ActiveDownload& active) {
                    return registerActiveDownload(requestId, active);
                },
                [this, requestId](ActiveDownload& active) {
                    return activeDownloads_
                        .try_emplace(requestId, std::move(active))
                        .second;
                },
                [operation] { return operation->Resume(); },
                [this, requestId, &candidate] {
                    const auto active = activeDownloads_.find(requestId);
                    if (active != activeDownloads_.end()) {
                        candidate = std::move(active->second);
                        activeDownloads_.erase(active);
                        candidate.resetSubscriptions();
                        return;
                    }
                    const auto retryable = resumableDownloads_.find(requestId);
                    if (retryable != resumableDownloads_.end()) {
                        candidate = std::move(retryable->second);
                        resumableDownloads_.erase(retryable);
                    }
                });
        logOperationFailure("download_resume_failed", outcome.result);
        if (outcome.action == DownloadResumeAction::Resumed) {
            const auto active = activeDownloads_.find(requestId);
            if (active != activeDownloads_.end() &&
                active->second.updateSerial == previousUpdateSerial) {
                dispatchDownloadListener(
                    generation,
                    [tabId, requestId,
                     totalBytes](gui::BrowserEventListener& listener) {
                        listener.onTabDownloadUpdated(
                            tabId, requestId,
                            gui::BrowserDownloadState::InProgress, -1,
                            totalBytes);
                    });
            }
            return;
        }
        if (outcome.action == DownloadResumeAction::ReportFailed) {
            dispatchDownloadListener(
                generation,
                [tabId, requestId, totalBytes](
                    gui::BrowserEventListener& listener) {
                    listener.onTabDownloadUpdated(
                        tabId, requestId, gui::BrowserDownloadState::Failed,
                        -1, totalBytes);
                });
            return;
        }
        const DownloadSnapshot snapshot = readDownloadSnapshot(
            operation.Get(), totalBytes, candidate.isCancelRequested);
        const bool didSynchronouslyUpdate =
            candidate.updateSerial != previousUpdateSerial;
        if (snapshot.hasState && snapshot.isTerminal && !snapshot.canResume) {
            if (!didSynchronouslyUpdate) {
                dispatchDownloadListener(
                    generation,
                    [tabId, requestId, snapshot](
                        gui::BrowserEventListener& listener) {
                        listener.onTabDownloadUpdated(
                            tabId, requestId, snapshot.state,
                            snapshot.receivedBytes, snapshot.totalBytes);
                    });
            }
            return;
        }
        if (snapshot.hasState && !snapshot.isTerminal) {
            const HRESULT registrationResult =
                registerActiveDownload(requestId, candidate);
            if (SUCCEEDED(registrationResult) &&
                activeDownloads_
                    .try_emplace(requestId, std::move(candidate))
                    .second) {
                if (!didSynchronouslyUpdate) {
                    dispatchDownloadListener(
                        generation,
                        [tabId, requestId, snapshot](
                            gui::BrowserEventListener& listener) {
                            listener.onTabDownloadUpdated(
                                tabId, requestId, snapshot.state,
                                snapshot.receivedBytes, snapshot.totalBytes);
                        });
                }
                return;
            }
            candidate.resetSubscriptions();
            logOperationFailure("download_resume_reactivation_failed",
                                registrationResult);
        }
        resumableDownloads_.insert_or_assign(requestId, std::move(candidate));
        if (!didSynchronouslyUpdate) {
            dispatchDownloadListener(
                generation,
                [tabId, requestId, totalBytes](
                    gui::BrowserEventListener& listener) {
                    listener.onTabDownloadUpdated(
                        tabId, requestId,
                        gui::BrowserDownloadState::RetryableFailure, -1,
                        totalBytes);
                });
        }
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
            !isCurrentTabGeneration(pending->generation)) {
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
            !isCurrentTabGeneration(pending->generation) ||
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
        if (activeTabId_ != 1) {
            const auto found = tabControllers_.find(activeTabId_);
            if (found != tabControllers_.end()) found->second->exitFullScreen();
            return;
        }
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
    // 调用线程：GUI 主线程。按需取得首标签的原生 Find 对象。
    [[nodiscard]] HRESULT ensureMainFindController() {
        if (find_ != nullptr) {
            return S_OK;
        }
        if (webView_ == nullptr) {
            return E_UNEXPECTED;
        }

        ComPtr<ICoreWebView2_28> webView28;
        HRESULT result = webView_.As(&webView28);
        if (SUCCEEDED(result)) {
            result = webView28->get_Find(&find_);
        }
        if (FAILED(result) || find_ == nullptr) {
            find_.Reset();
            return FAILED(result) ? result : E_NOINTERFACE;
        }

        return S_OK;
    }

    std::uint64_t nextMainFindRequestSerial() noexcept {
        ++mainFindRequestSerial_;
        if (mainFindRequestSerial_ == 0) {
            ++mainFindRequestSerial_;
        }
        return mainFindRequestSerial_;
    }

    bool isCurrentMainFindRequest(
        const std::uint64_t generation,
        const std::uint64_t requestSerial) const noexcept {
        return !isShuttingDown_ && generation == generation_ &&
               requestSerial != 0 && requestSerial == mainFindRequestSerial_;
    }

    [[nodiscard]] HRESULT observeMainFindResults(
        const std::uint64_t generation,
        const std::uint64_t requestSerial) {
        findActiveMatchIndexChanged_.reset();
        findMatchCountChanged_.reset();
        if (find_ == nullptr) {
            return E_UNEXPECTED;
        }
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        HRESULT result = find_->add_ActiveMatchIndexChanged(
            Callback<ICoreWebView2FindActiveMatchIndexChangedEventHandler>(
                [this, weakLifetime, lifecycleSerial, generation,
                 requestSerial](ICoreWebView2Find*, IUnknown*) -> HRESULT {
                    if (!weakLifetime.expired() && !isShuttingDown_ &&
                        lifecycleSerial == lifecycleSerial_ &&
                        isCurrentMainFindRequest(generation, requestSerial)) {
                        emitMainFindResult(generation, requestSerial);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            const ComPtr<ICoreWebView2Find> source = find_;
            findActiveMatchIndexChanged_.bind(token, [source](const auto value) {
                return source->remove_ActiveMatchIndexChanged(value);
            });
        }
        if (SUCCEEDED(result)) {
            result = find_->add_MatchCountChanged(
                Callback<ICoreWebView2FindMatchCountChangedEventHandler>(
                    [this, weakLifetime, lifecycleSerial, generation,
                     requestSerial](ICoreWebView2Find*, IUnknown*) -> HRESULT {
                        if (!weakLifetime.expired() && !isShuttingDown_ &&
                            lifecycleSerial == lifecycleSerial_ &&
                            isCurrentMainFindRequest(generation, requestSerial)) {
                            emitMainFindResult(generation, requestSerial);
                        }
                        return S_OK;
                    })
                    .Get(),
                &token);
        }
        if (SUCCEEDED(result)) {
            const ComPtr<ICoreWebView2Find> source = find_;
            findMatchCountChanged_.bind(token, [source](const auto value) {
                return source->remove_MatchCountChanged(value);
            });
        }
        if (FAILED(result)) {
            findActiveMatchIndexChanged_.reset();
            findMatchCountChanged_.reset();
        }
        return result;
    }

    void emitMainFindResult(const std::uint64_t generation,
                            const std::uint64_t requestSerial) {
        if (!isCurrentMainFindRequest(generation, requestSerial) ||
            find_ == nullptr) {
            return;
        }
        INT32 activeMatchIndex = -1;
        INT32 matchCount = 0;
        HRESULT result = find_->get_ActiveMatchIndex(&activeMatchIndex);
        if (SUCCEEDED(result)) {
            result = find_->get_MatchCount(&matchCount);
        }
        if (FAILED(result)) {
            dispatchFindFailure(1, generation, requestSerial, result);
            return;
        }
        dispatchTabListener(
            [this, generation, requestSerial, activeMatchIndex, matchCount](
                gui::BrowserEventListener& listener) {
                if (!isCurrentMainFindRequest(generation, requestSerial)) {
                    return;
                }
                listener.onFindResultChanged(1, generation, activeMatchIndex,
                                             matchCount);
            });
    }

    void dispatchFindFailure(const std::uint64_t tabId,
                             const std::uint64_t generation,
                             const std::uint64_t requestSerial,
                             const HRESULT result) {
        logOperationFailure("find_failed", result);
        dispatchTabListener(
            [this, tabId, generation, requestSerial, result](
                gui::BrowserEventListener& listener) {
                if (tabId == 1 &&
                    !isCurrentMainFindRequest(generation, requestSerial)) {
                    return;
                }
                listener.onFindFailed(tabId, generation,
                                      static_cast<long>(result));
            });
    }

    void findInMainPage(const QString& text, const bool forward) noexcept {
        if (text.isEmpty()) {
            stopMainFinding(true);
            return;
        }
        HRESULT result = ensureMainFindController();
        if (FAILED(result)) {
            const std::uint64_t requestSerial = nextMainFindRequestSerial();
            dispatchFindFailure(1, generation_, requestSerial, result);
            return;
        }
        const std::uint64_t generation = generation_;
        const std::uint64_t requestSerial = nextMainFindRequestSerial();
        result = observeMainFindResults(generation, requestSerial);
        if (FAILED(result)) {
            dispatchFindFailure(1, generation, requestSerial, result);
            return;
        }
        if (mainFindText_ == text) {
            result = forward ? find_->FindNext() : find_->FindPrevious();
            if (FAILED(result)) {
                dispatchFindFailure(1, generation, requestSerial, result);
            }
            return;
        }

        ComPtr<ICoreWebView2Environment15> environment15;
        ComPtr<ICoreWebView2FindOptions> options;
        result = environment_.As(&environment15);
        if (SUCCEEDED(result)) {
            result = environment15->CreateFindOptions(&options);
        }
        const std::wstring term = text.toStdWString();
        if (SUCCEEDED(result)) {
            result = options->put_FindTerm(term.c_str());
        }
        if (SUCCEEDED(result)) {
            result = options->put_IsCaseSensitive(FALSE);
        }
        if (SUCCEEDED(result)) {
            result = options->put_ShouldHighlightAllMatches(TRUE);
        }
        if (SUCCEEDED(result)) {
            result = options->put_ShouldMatchWord(FALSE);
        }
        if (SUCCEEDED(result)) {
            result = options->put_SuppressDefaultFindDialog(TRUE);
        }
        if (FAILED(result)) {
            dispatchFindFailure(1, generation, requestSerial, result);
            return;
        }

        mainFindText_ = text;
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        result = find_->Start(
            options.Get(),
            Callback<ICoreWebView2FindStartCompletedHandler>(
                [this, weakLifetime, lifecycleSerial, generation, requestSerial,
                 forward](const HRESULT status) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_ ||
                        generation != generation_ ||
                        requestSerial != mainFindRequestSerial_) {
                        return S_OK;
                    }
                    if (FAILED(status)) {
                        dispatchFindFailure(1, generation, requestSerial, status);
                        return S_OK;
                    }
                    if (!forward && find_ != nullptr) {
                        const HRESULT previousResult = find_->FindPrevious();
                        if (FAILED(previousResult)) {
                            dispatchFindFailure(1, generation, requestSerial,
                                                previousResult);
                        }
                    }
                    emitMainFindResult(generation, requestSerial);
                    return S_OK;
                })
                .Get());
        if (FAILED(result)) {
            dispatchFindFailure(1, generation, requestSerial, result);
        }
    }

    void stopMainFinding(const bool clearSelection) noexcept {
        Q_UNUSED(clearSelection);
        const std::uint64_t generation = generation_;
        const std::uint64_t requestSerial = nextMainFindRequestSerial();
        findActiveMatchIndexChanged_.reset();
        findMatchCountChanged_.reset();
        mainFindText_.clear();
        if (find_ != nullptr) {
            const HRESULT result = find_->Stop();
            if (FAILED(result)) {
                dispatchFindFailure(1, generation, requestSerial, result);
            }
        }
    }

    void releaseMainFindController() noexcept {
        static_cast<void>(nextMainFindRequestSerial());
        findActiveMatchIndexChanged_.reset();
        findMatchCountChanged_.reset();
        if (find_ != nullptr) {
            static_cast<void>(find_->Stop());
        }
        find_.Reset();
        mainFindText_.clear();
    }

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

    // 调用线程：GUI STA。敏感请求只接受仍属于存活标签的代次。
    [[nodiscard]] bool isCurrentTabGeneration(
        const std::uint64_t generation) const noexcept {
        if (generation == generation_) {
            return true;
        }
        return std::any_of(
            tabControllers_.begin(), tabControllers_.end(),
            [generation](const auto& entry) {
                return entry.second->generation() == generation;
            });
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
        ++active.updateSerial;
        dispatchDownloadListener(
            active.generation,
            [tabId = active.tabId, requestId, snapshot](gui::BrowserEventListener& listener) {
                listener.onTabDownloadUpdated(
                    tabId, requestId, snapshot.state, snapshot.receivedBytes,
                    snapshot.totalBytes);
            });
        if (snapshot.canResume) {
            active.resetSubscriptions();
            resumableDownloads_.insert_or_assign(requestId, std::move(active));
            activeDownloads_.erase(found);
        } else if (snapshot.isTerminal) {
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
        for (PendingNewWindow& pending : pendingNewWindows_.takeAll()) {
            logOperationFailure(
                "popup_shutdown_reject_failed",
                completePopupRequest(pending.args.Get(), pending.deferral.Get(),
                                     static_cast<ICoreWebView2*>(nullptr)));
        }
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
        for (auto& [requestId, active] : resumableDownloads_) {
            static_cast<void>(requestId);
            logOperationFailure(
                "download_shutdown_cancel_failed",
                cancelActiveDownloadForShutdown(
                    active, [&active] { return active.operation->Cancel(); }));
        }
        resumableDownloads_.clear();
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
                    ControllerAdoptionTransaction<ICoreWebView2Controller>
                        controllerTransaction(controller);
                    const bool isCurrentLifecycle =
                        !weakLifetime.expired() &&
                        isActive(weakLifetime, lifecycleSerial, generation);
                    if (!controllerTransaction.canAdopt(isCurrentLifecycle, status)) {
                        if (!isCurrentLifecycle) {
                            return S_OK;
                        }
                        const HRESULT error = FAILED(status) ? status : E_POINTER;
                        reportError(generation, classifyInitializationError(error), error,
                                    "controller_creation_failed");
                        return S_OK;
                    }
                    finishController(controllerTransaction.adopt(), generation);
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
            result = configureDefaultDownloadDirectory();
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
        logOperationFailure("initial_zoom_failed",
                            controller_->put_ZoomFactor(tabZoomFactor(1)));
        isReady_ = true;
        executeSuspensionStep(suspension_.controllerReady());
        dispatchTabListener([generation](gui::BrowserEventListener& listener) {
            listener.onTabReady(1, generation);
        });
    }

    // 调用线程：Controller 完成回调所在的 GUI STA；失败时阻止进入 Ready。
    HRESULT configureDefaultDownloadDirectory() {
        if (profile_ == nullptr || userDataDirectory_.isEmpty()) {
            return E_UNEXPECTED;
        }
        const QString downloadDirectory =
            QDir(userDataDirectory_).filePath(QStringLiteral("Downloads"));
        if (!QDir().mkpath(downloadDirectory)) {
            return HRESULT_FROM_WIN32(ERROR_CANNOT_MAKE);
        }
        const std::wstring nativeDownloadDirectory =
            QDir::toNativeSeparators(downloadDirectory).toStdWString();
        return profile_->put_DefaultDownloadFolderPath(
            nativeDownloadDirectory.c_str());
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
        if (FAILED(result = settings4->put_IsPasswordAutosaveEnabled(TRUE))) {
            return result;
        }
        return settings4->put_IsGeneralAutofillEnabled(TRUE);
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
            result = registerProcessFailed();
        }
        if (SUCCEEDED(result)) {
            result = registerFullScreenChanged();
        }
        if (SUCCEEDED(result)) {
            result = registerDocumentPlayingAudioChanged();
        }
        if (SUCCEEDED(result)) {
            result = registerFaviconChanged();
        }
        if (SUCCEEDED(result)) {
            result = registerZoomFactorChanged();
        }
        if (SUCCEEDED(result)) {
            result = registerAcceleratorKeyPressed();
        }
        return result;
    }

    // 调用线程：主 Controller 的 GUI STA；只投递稳定类别并由生命周期门禁排除迟到事件。
    HRESULT registerProcessFailed() {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = webView_->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2*,
                    ICoreWebView2ProcessFailedEventArgs* const args) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_ || args == nullptr) {
                        return S_OK;
                    }
                    COREWEBVIEW2_PROCESS_FAILED_KIND rawKind =
                        COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED;
                    if (FAILED(args->get_ProcessFailedKind(&rawKind))) {
                        return S_OK;
                    }
                    const std::uint64_t failedGeneration = generation_;
                    const gui::BrowserProcessFailureKind kind =
                        classifyProcessFailureKind(rawKind);
                    dispatchListener(
                        failedGeneration,
                        [this, failedGeneration, kind](
                            gui::BrowserEventListener& listener) {
                            if (controller_ == nullptr || webView_ == nullptr) {
                                return;
                            }
                            if (kind == gui::BrowserProcessFailureKind::BrowserProcessExited) {
                                if (hasReportedBrowserProcessFailure_) {
                                    return;
                                }
                                hasReportedBrowserProcessFailure_ = true;
                            }
                            listener.onTabProcessFailed(1, failedGeneration, kind);
                        });
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            const ComPtr<ICoreWebView2> source = webView_;
            processFailed_.bind(token, [source](const auto value) {
                return source->remove_ProcessFailed(value);
            });
        }
        return result;
    }

    // 调用线程：次级标签 WebView2 事件所在 GUI STA；与首标签共享宿主确认存储。
    HRESULT handlePermissionRequest(
        const std::uint64_t tabId, const std::uint64_t generation,
        ICoreWebView2PermissionRequestedEventArgs* const args) {
        if (isShuttingDown_ || tabId != activeTabId_ ||
            tabControllers_.find(tabId) == tabControllers_.end() ||
            args == nullptr) {
            static_cast<void>(rejectPermissionRequest(args));
            return S_OK;
        }
        HRESULT status = args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
        ComPtr<ICoreWebView2PermissionRequestedEventArgs> baseArgs(args);
        ComPtr<ICoreWebView2Deferral> deferral;
        status = firstFailure(status, baseArgs->GetDeferral(&deferral));
        ComPtr<ICoreWebView2PermissionRequestedEventArgs2> args2;
        ComPtr<ICoreWebView2PermissionRequestedEventArgs3> args3;
        const HRESULT args2Status = baseArgs.As(&args2);
        status = firstFailure(status, args2Status);
        if (SUCCEEDED(args2Status)) {
            status = firstFailure(status, args2->put_Handled(TRUE));
        }
        status = firstFailure(status, baseArgs.As(&args3));
        COREWEBVIEW2_PERMISSION_KIND rawKind =
            COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
        LPWSTR rawUri = nullptr;
        status = firstFailure(status, args->get_PermissionKind(&rawKind));
        status = firstFailure(status, args->get_Uri(&rawUri));
        CoTaskMemString uri(rawUri);
        const auto kind = supportedPermissionKind(rawKind);
        const QString origin = normalizedOriginFromUri(uri.get());
        if (FAILED(status) || deferral == nullptr || !kind.has_value() ||
            origin.isEmpty() || listener_ == nullptr) {
            logOperationFailure(
                "tab_permission_request_rejected",
                firstFailure(status, completePermissionRejection(
                                         baseArgs.Get(), args3.Get(),
                                         deferral.Get())));
            return S_OK;
        }
        const std::uint64_t requestId = nextSensitiveRequestId();
        PendingPermission pending{args3, deferral, origin, *kind,
                                  lifecycleSerial_, generation};
        if (!pendingPermissions_.insert(requestId, std::move(pending))) {
            logOperationFailure(
                "tab_permission_store_failed",
                completePermissionDecision(args3.Get(), deferral.Get(),
                                           gui::BrowserPermissionDecision::Deny));
            return S_OK;
        }
        dispatchTabListener(
            [requestId, origin, kind = *kind](gui::BrowserEventListener& listener) {
                listener.onPermissionRequested(requestId, origin, kind);
            });
        schedulePermissionTimeout(requestId, lifecycleSerial_);
        return S_OK;
    }

    // 调用线程：次级标签 WebView2 事件所在 GUI STA；默认取消并等待宿主确认。
    HRESULT handleScreenCaptureRequest(
        const std::uint64_t tabId, const std::uint64_t generation,
        ICoreWebView2ScreenCaptureStartingEventArgs* const args) {
        if (isShuttingDown_ || tabId != activeTabId_ ||
            tabControllers_.find(tabId) == tabControllers_.end() ||
            args == nullptr) {
            static_cast<void>(rejectScreenCaptureRequest(args));
            return S_OK;
        }
        HRESULT status = args->put_Cancel(TRUE);
        status = firstFailure(status, args->put_Handled(TRUE));
        ComPtr<ICoreWebView2FrameInfo> frameInfo;
        status = firstFailure(status,
                              args->get_OriginalSourceFrameInfo(&frameInfo));
        LPWSTR rawSource = nullptr;
        status = firstFailure(
            status, frameInfo != nullptr ? frameInfo->get_Source(&rawSource)
                                         : E_POINTER);
        CoTaskMemString source(rawSource);
        const QString origin = normalizedOriginFromUri(source.get());
        ComPtr<ICoreWebView2Deferral> deferral;
        status = firstFailure(status, args->GetDeferral(&deferral));
        if (FAILED(status) || deferral == nullptr || origin.isEmpty() ||
            listener_ == nullptr) {
            logOperationFailure(
                "tab_screen_capture_request_rejected",
                firstFailure(status, completeScreenCaptureDecision(
                                         args, deferral.Get(),
                                         gui::BrowserPermissionDecision::Deny)));
            return S_OK;
        }
        const std::uint64_t requestId = nextSensitiveRequestId();
        PendingScreenCapture pending{args, deferral, origin, lifecycleSerial_,
                                     generation};
        if (!pendingScreenCaptures_.insert(requestId, std::move(pending))) {
            static_cast<void>(completeScreenCaptureDecision(
                args, deferral.Get(), gui::BrowserPermissionDecision::Deny));
            return S_OK;
        }
        dispatchTabListener(
            [requestId, origin](gui::BrowserEventListener& listener) {
                listener.onPermissionRequested(
                    requestId, origin,
                    gui::BrowserPermissionKind::ScreenCapture);
            });
        schedulePermissionTimeout(requestId, lifecycleSerial_);
        return S_OK;
    }

    // 调用线程：次级标签 WebView2 事件所在 GUI STA；下载仍使用统一路径确认。
    HRESULT handleDownloadRequest(
        const std::uint64_t tabId, const std::uint64_t generation,
        ICoreWebView2DownloadStartingEventArgs* const args) {
        if (args == nullptr) {
            return S_OK;
        }
        ComPtr<ICoreWebView2DownloadOperation> operation;
        ComPtr<ICoreWebView2Deferral> deferral;
        HRESULT status = prepareDownloadRequest(
            args, operation.GetAddressOf(), deferral.GetAddressOf());
        if (isShuttingDown_ || tabId != activeTabId_ ||
            tabControllers_.find(tabId) == tabControllers_.end()) {
            static_cast<void>(firstFailure(
                status, completeDownloadCancellation(args, operation.Get(),
                                                     deferral.Get())));
            return S_OK;
        }
        LPWSTR rawSuggestedPath = nullptr;
        LPWSTR rawUri = nullptr;
        status = firstFailure(status, args->get_ResultFilePath(&rawSuggestedPath));
        status = firstFailure(
            status, operation != nullptr ? operation->get_Uri(&rawUri) : E_POINTER);
        CoTaskMemString suggestedPath(rawSuggestedPath);
        CoTaskMemString uri(rawUri);
        const QString suggestedFileName =
            suggestedPath != nullptr
                ? QFileInfo(QString::fromWCharArray(suggestedPath.get())).fileName()
                : QString{};
        const QString origin = normalizedOriginFromUri(uri.get());
        INT64 totalBytes = -1;
        if (operation == nullptr ||
            FAILED(operation->get_TotalBytesToReceive(&totalBytes))) {
            totalBytes = -1;
        }
        if (FAILED(status) || operation == nullptr || deferral == nullptr ||
            suggestedFileName.isEmpty() || origin.isEmpty() || listener_ == nullptr) {
            logOperationFailure(
                "tab_download_request_rejected",
                firstFailure(status, completeDownloadCancellation(
                                         args, operation.Get(), deferral.Get())));
            return S_OK;
        }
        const std::uint64_t requestId = nextSensitiveRequestId();
        PendingDownload pending{args, operation, deferral,
                                static_cast<std::int64_t>(totalBytes),
                                lifecycleSerial_, generation, tabId};
        if (!pendingDownloads_.insert(requestId, std::move(pending))) {
            static_cast<void>(completeDownloadCancellation(
                args, operation.Get(), deferral.Get()));
            return S_OK;
        }
        dispatchTabListener(
            [tabId, requestId, origin, suggestedFileName,
             totalBytes = static_cast<std::int64_t>(totalBytes)](
                gui::BrowserEventListener& listener) {
                listener.onTabDownloadRequested(tabId, requestId, origin,
                                                suggestedFileName, totalBytes);
            });
        scheduleDownloadTimeout(requestId, lifecycleSerial_);
        return S_OK;
    }

    // 调用线程：次级标签 WebView2 事件所在 GUI STA；证书例外仍仅限当前会话。
    HRESULT handleCertificateRequest(
        const std::uint64_t tabId, const std::uint64_t generation,
        ICoreWebView2ServerCertificateErrorDetectedEventArgs* const args) {
        if (args == nullptr) {
            return S_OK;
        }
        ComPtr<ICoreWebView2Deferral> deferral;
        HRESULT status = prepareCertificateRequest(args, deferral.GetAddressOf());
        COREWEBVIEW2_WEB_ERROR_STATUS errorStatus =
            COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
        LPWSTR rawRequestUri = nullptr;
        if (!isShuttingDown_ && tabId == activeTabId_ &&
            tabControllers_.find(tabId) != tabControllers_.end()) {
            status = firstFailure(status, args->get_ErrorStatus(&errorStatus));
            status = firstFailure(status, args->get_RequestUri(&rawRequestUri));
        } else {
            status = firstFailure(status, E_ABORT);
        }
        CoTaskMemString requestUri(rawRequestUri);
        const QString origin = normalizedOriginFromUri(requestUri.get());
        if (FAILED(status) || deferral == nullptr || origin.isEmpty() ||
            listener_ == nullptr) {
            static_cast<void>(completeCertificateDecision(
                args, deferral.Get(),
                gui::BrowserCertificateDecision::ReturnToSafety));
            return S_OK;
        }
        const std::uint64_t requestId = nextSensitiveRequestId();
        PendingCertificate pending{args, deferral, origin, lifecycleSerial_,
                                   generation};
        if (!pendingCertificates_.insert(requestId, std::move(pending))) {
            static_cast<void>(completeCertificateDecision(
                args, deferral.Get(),
                gui::BrowserCertificateDecision::ReturnToSafety));
            return S_OK;
        }
        const QString description = certificateErrorDescription(errorStatus);
        dispatchTabListener(
            [requestId, origin,
             description](gui::BrowserEventListener& listener) {
                listener.onCertificateErrorRequested(requestId, origin,
                                                     description);
            });
        scheduleCertificateTimeout(requestId, lifecycleSerial_);
        return S_OK;
    }

    // 调用线程：次级标签 WebView2 事件所在 GUI STA；外部协议必须由用户确认。
    HRESULT handleExternalProtocolRequest(
        const std::uint64_t tabId, const std::uint64_t generation,
        ICoreWebView2LaunchingExternalUriSchemeEventArgs* const args) {
        if (args == nullptr) {
            return S_OK;
        }
        ComPtr<ICoreWebView2Deferral> deferral;
        HRESULT status =
            prepareExternalProtocolRequest(args, deferral.GetAddressOf());
        BOOL isUserInitiated = FALSE;
        LPWSTR rawUri = nullptr;
        LPWSTR rawOrigin = nullptr;
        if (!isShuttingDown_ && tabId == activeTabId_ &&
            tabControllers_.find(tabId) != tabControllers_.end()) {
            status = firstFailure(status,
                                  args->get_IsUserInitiated(&isUserInitiated));
            status = firstFailure(status, args->get_Uri(&rawUri));
            status = firstFailure(status,
                                  args->get_InitiatingOrigin(&rawOrigin));
        } else {
            status = firstFailure(status, E_ABORT);
        }
        CoTaskMemString uri(rawUri);
        CoTaskMemString initiatingOrigin(rawOrigin);
        const QString target =
            uri != nullptr ? QString::fromWCharArray(uri.get()) : QString{};
        const QString origin = normalizedOriginFromUri(initiatingOrigin.get());
        if (FAILED(status) || deferral == nullptr || isUserInitiated == FALSE ||
            !isValidExternalTarget(target) || origin.isEmpty() ||
            listener_ == nullptr) {
            static_cast<void>(
                completeExternalProtocolDecision(args, deferral.Get(), false));
            return S_OK;
        }
        const std::uint64_t requestId = nextSensitiveRequestId();
        PendingExternalProtocol pending{args, deferral, lifecycleSerial_,
                                        generation};
        if (!pendingExternalProtocols_.insert(requestId, std::move(pending))) {
            static_cast<void>(
                completeExternalProtocolDecision(args, deferral.Get(), false));
            return S_OK;
        }
        dispatchTabListener(
            [requestId, origin, target](gui::BrowserEventListener& listener) {
                listener.onExternalProtocolRequested(requestId, origin, target);
            });
        scheduleExternalProtocolTimeout(requestId, lifecycleSerial_);
        return S_OK;
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
                    if (activeTabId_ != 1) {
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
                    if (activeTabId_ != 1) {
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
                    if (activeTabId_ != 1) {
                        ComPtr<ICoreWebView2DownloadOperation> operation;
                        ComPtr<ICoreWebView2Deferral> deferral;
                        const HRESULT status = prepareDownloadRequest(
                            args, operation.GetAddressOf(), deferral.GetAddressOf());
                        static_cast<void>(firstFailure(
                            status, completeDownloadCancellation(
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
                                            lifecycleSerial, generation, 1};
                    if (!pendingDownloads_.insert(requestId, std::move(pending))) {
                        logOperationFailure(
                            "download_store_failed",
                            completeDownloadCancellation(args, operation.Get(),
                                                         deferral.Get()));
                        return S_OK;
                    }
                    dispatchDownloadListener(
                        generation,
                        [requestId, origin, suggestedFileName,
                         totalBytes = static_cast<std::int64_t>(rawTotalBytes)](
                            gui::BrowserEventListener& listener) {
                            listener.onTabDownloadRequested(
                                1, requestId, origin, suggestedFileName,
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
                    if (activeTabId_ != 1) {
                        ComPtr<ICoreWebView2Deferral> deferral;
                        const HRESULT status = prepareCertificateRequest(
                            args, deferral.GetAddressOf());
                        static_cast<void>(firstFailure(
                            status, completeCertificateDecision(
                                        args, deferral.Get(),
                                        gui::BrowserCertificateDecision::
                                            ReturnToSafety)));
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
                    if (activeTabId_ != 1) {
                        ComPtr<ICoreWebView2Deferral> deferral;
                        const HRESULT status = prepareExternalProtocolRequest(
                            args, deferral.GetAddressOf());
                        static_cast<void>(firstFailure(
                            status, completeExternalProtocolDecision(
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

        // 新窗口请求先取得 deferral，再把稳定请求 ID 和 URL 排队到 Qt 主循环。
        LPWSTR rawUri = nullptr;
        const HRESULT uriResult = args->get_Uri(&rawUri);
        CoTaskMemString uri(rawUri);
        if (FAILED(uriResult) || uri == nullptr || listener_ == nullptr) {
            const HRESULT result = rejectNewWindow(args);
            dispatchTabListener([](gui::BrowserEventListener& listener) {
                listener.onPopupRejected();
            });
            return FAILED(uriResult) ? uriResult : result;
        }

        ComPtr<ICoreWebView2Deferral> deferral;
        const HRESULT prepareResult =
            preparePopupRequest(args, deferral.GetAddressOf());
        if (FAILED(prepareResult) || deferral == nullptr) {
            static_cast<void>(completePopupRequest(
                args, deferral.Get(), static_cast<ICoreWebView2*>(nullptr)));
            dispatchTabListener([](gui::BrowserEventListener& listener) {
                listener.onPopupRejected();
            });
            return FAILED(prepareResult) ? prepareResult : E_POINTER;
        }

        const std::uint64_t requestId = nextSensitiveRequestId();
        if (!pendingNewWindows_.insert(
                requestId, PendingNewWindow{args, deferral, lifecycleSerial_})) {
            static_cast<void>(completePopupRequest(
                args, deferral.Get(), static_cast<ICoreWebView2*>(nullptr)));
            dispatchTabListener([](gui::BrowserEventListener& listener) {
                listener.onPopupRejected();
            });
            return E_UNEXPECTED;
        }
        const QString url = QString::fromWCharArray(uri.get());
        dispatchTabListener(
            [this, requestId, url](gui::BrowserEventListener& listener) {
                if (listener.onNewTabRequested(requestId, url)) {
                    return;
                }
                std::optional<PendingNewWindow> pending =
                    pendingNewWindows_.take(requestId);
                if (pending.has_value()) {
                    logOperationFailure(
                        "popup_rejection_failed",
                        completePopupRequest(pending->args.Get(),
                                             pending->deferral.Get(),
                                             static_cast<ICoreWebView2*>(nullptr)));
                }
                listener.onPopupRejected();
            });
        return S_OK;
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
                    stopMainFinding(true);
                    clearMainFavicon(generation_);
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
                            dispatchTabListener(
                                [generation = start.generation](
                                    gui::BrowserEventListener& listener) {
                                    listener.onTabNavigationStarted(1, generation);
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
                            emitNavigationSnapshot(
                                eventGeneration, SnapshotKind::NavigationStopped);
                        } else {
                            reportError(eventGeneration,
                                        gui::BrowserErrorKind::NavigationFailed, E_FAIL,
                                        "navigation_completed_with_error");
                        }
                    } else {
                        emitNavigationSnapshot(
                            eventGeneration,
                            SnapshotKind::NavigationCompleted);
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
                        isClearedBlankSnapshotSuppressed_ ||
                        lifecycleSerial != lifecycleSerial_) {
                        return S_OK;
                    }
                    emitNavigationSnapshot(
                        generation_, SnapshotKind::DocumentStateChanged);
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
                        lifecycleSerial != lifecycleSerial_ || activeTabId_ != 1) {
                        return S_OK;
                    }
                    BOOL isFullScreen = FALSE;
                    const HRESULT status =
                        webView_->get_ContainsFullScreenElement(&isFullScreen);
                    if (SUCCEEDED(status)) {
                        isWebFullScreen_ = isFullScreen != FALSE;
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

    // 调用线程：GUI STA；事件只上报当前文档是否播放音频，不直接操作 Qt 控件。
    HRESULT registerDocumentPlayingAudioChanged() {
        ComPtr<ICoreWebView2_8> webView8;
        HRESULT result = webView_.As(&webView8);
        if (FAILED(result)) {
            return result;
        }
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        result = webView8->add_IsDocumentPlayingAudioChanged(
            Callback<ICoreWebView2IsDocumentPlayingAudioChangedEventHandler>(
                [this, weakLifetime, lifecycleSerial, webView8](
                    ICoreWebView2*, IUnknown*) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_) {
                        return S_OK;
                    }
                    BOOL isPlayingAudio = FALSE;
                    const HRESULT status =
                        webView8->get_IsDocumentPlayingAudio(&isPlayingAudio);
                    if (SUCCEEDED(status)) {
                        const std::uint64_t generation = generation_;
                        dispatchTabListener(
                            [generation, isPlayingAudio](
                                gui::BrowserEventListener& listener) {
                                listener.onTabAudioStateChanged(
                                    1, generation, isPlayingAudio != FALSE);
                            });
                    } else {
                        logOperationFailure("audio_state_failed", status);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            documentPlayingAudioChanged_.bind(
                token, [webView8](const EventRegistrationToken value) {
                    return webView8->remove_IsDocumentPlayingAudioChanged(value);
                });
        }
        return result;
    }

    // 调用线程：主 Controller 的 GUI STA；旧 Runtime 不支持时安全降级。
    HRESULT registerFaviconChanged() {
        ComPtr<ICoreWebView2_15> webView15;
        HRESULT result = webView_.As(&webView15);
        if (result == E_NOINTERFACE) {
            return S_OK;
        }
        if (FAILED(result)) {
            return result;
        }
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        result = webView15->add_FaviconChanged(
            Callback<ICoreWebView2FaviconChangedEventHandler>(
                [this, weakLifetime, lifecycleSerial](ICoreWebView2*, IUnknown*)
                    -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_) {
                        return S_OK;
                    }
                    requestMainFavicon(generation_);
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            faviconChanged_.bind(token, [webView15](const auto value) {
                return webView15->remove_FaviconChanged(value);
            });
        }
        return result;
    }

    // 调用线程：主 Controller 的 GUI STA；事件最终排队投递到监听器。
    HRESULT registerZoomFactorChanged() {
        if (controller_ == nullptr) {
            return E_UNEXPECTED;
        }
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = controller_->add_ZoomFactorChanged(
            Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
                [this, weakLifetime, lifecycleSerial](ICoreWebView2Controller*,
                                                      IUnknown*) -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_ ||
                        controller_ == nullptr) {
                        return S_OK;
                    }
                    double zoomFactor = 1.0;
                    const HRESULT status =
                        controller_->get_ZoomFactor(&zoomFactor);
                    if (SUCCEEDED(status)) {
                        const double clampedZoom =
                            std::clamp(zoomFactor, 0.25, 5.0);
                        tabZoomFactors_[1] = clampedZoom;
                        const std::uint64_t generation = generation_;
                        dispatchTabListener(
                            [this, generation, clampedZoom](
                                gui::BrowserEventListener& listener) {
                                if (controller_ != nullptr &&
                                    generation == generation_) {
                                    listener.onTabZoomFactorChanged(
                                        1, generation, clampedZoom);
                                }
                            });
                    } else {
                        logOperationFailure("zoom_state_failed", status);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            const ComPtr<ICoreWebView2Controller> source = controller_;
            zoomFactorChanged_.bind(token, [source](const auto value) {
                return source->remove_ZoomFactorChanged(value);
            });
        }
        return result;
    }

    // 调用线程：WebView2 Favicon 事件所在 GUI STA；完成回调不直接操作 Qt 控件。
    void requestMainFavicon(const std::uint64_t generation) noexcept {
        ComPtr<ICoreWebView2_15> webView15;
        const HRESULT queryResult = webView_.As(&webView15);
        if (FAILED(queryResult)) {
            logOperationFailure("favicon_api_failed", queryResult);
            return;
        }
        const std::uint64_t requestSerial = nextMainFaviconRequestSerial();
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        const HRESULT result = webView15->GetFavicon(
            COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG,
            Callback<ICoreWebView2GetFaviconCompletedHandler>(
                [this, weakLifetime, lifecycleSerial, generation,
                 requestSerial](const HRESULT status,
                                IStream* const stream) -> HRESULT {
                    if (!isCurrentMainFaviconRequest(
                            weakLifetime, lifecycleSerial, generation,
                            requestSerial)) {
                        return S_OK;
                    }
                    QByteArray pngBytes;
                    const HRESULT readResult =
                        SUCCEEDED(status)
                            ? readFaviconPngStream(stream, pngBytes)
                            : status;
                    if (FAILED(readResult)) {
                        logOperationFailure("favicon_read_failed", readResult);
                        return S_OK;
                    }
                    if (!isCurrentMainFaviconRequest(
                            weakLifetime, lifecycleSerial, generation,
                            requestSerial)) {
                        return S_OK;
                    }
                    dispatchTabListener(
                        [this, generation, requestSerial, pngBytes](
                            gui::BrowserEventListener& listener) {
                            if (controller_ != nullptr &&
                                generation == generation_ &&
                                requestSerial == mainFaviconRequestSerial_) {
                                listener.onTabFaviconChanged(
                                    1, generation, pngBytes);
                            }
                        });
                    return S_OK;
                })
                .Get());
        logOperationFailure("favicon_request_failed", result);
    }

    // 调用线程：GUI STA；导航时先清除旧图标并使旧请求失效。
    void clearMainFavicon(const std::uint64_t generation) noexcept {
        const std::uint64_t requestSerial = nextMainFaviconRequestSerial();
        dispatchTabListener(
            [this, generation, requestSerial](
                gui::BrowserEventListener& listener) {
                if (controller_ != nullptr && generation == generation_ &&
                    requestSerial == mainFaviconRequestSerial_) {
                    listener.onTabFaviconChanged(1, generation, QByteArray{});
                }
            });
    }

    std::uint64_t nextMainFaviconRequestSerial() noexcept {
        ++mainFaviconRequestSerial_;
        if (mainFaviconRequestSerial_ == 0) {
            ++mainFaviconRequestSerial_;
        }
        return mainFaviconRequestSerial_;
    }

    bool isCurrentMainFaviconRequest(
        const std::weak_ptr<int>& weakLifetime,
        const std::uint64_t lifecycleSerial,
        const std::uint64_t generation,
        const std::uint64_t requestSerial) const noexcept {
        return !weakLifetime.expired() && !isShuttingDown_ &&
               lifecycleSerial == lifecycleSerial_ && controller_ != nullptr &&
               generation == generation_ && requestSerial != 0 &&
               requestSerial == mainFaviconRequestSerial_;
    }

    // 调用线程：Controller 的 GUI STA；原生子窗口按键只投递稳定动作。
    HRESULT registerAcceleratorKeyPressed() {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        EventRegistrationToken token{};
        const HRESULT result = controller_->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [this, weakLifetime, lifecycleSerial](
                    ICoreWebView2Controller*,
                    ICoreWebView2AcceleratorKeyPressedEventArgs* const args)
                    -> HRESULT {
                    if (weakLifetime.expired() || isShuttingDown_ ||
                        lifecycleSerial != lifecycleSerial_ || activeTabId_ != 1 ||
                        args == nullptr) {
                        return S_OK;
                    }
                    const bool isControlDown =
                        (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    const bool isAltDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
                    const bool isShiftDown =
                        (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    const bool isWindowsDown =
                        (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                        (GetKeyState(VK_RWIN) & 0x8000) != 0;
                    const AcceleratorDispatch dispatch =
                        handleAcceleratorKey(*args, isControlDown, isAltDown,
                                             isShiftDown, isWindowsDown,
                                             isWebFullScreen_);
                    if (FAILED(dispatch.status)) {
                        logOperationFailure("accelerator_handling_failed",
                                            dispatch.status);
                        return S_OK;
                    }
                    if (!dispatch.accelerator.has_value()) {
                        return S_OK;
                    }
                    const std::uint64_t generation = generation_;
                    dispatchListener(
                        generation,
                        [generation, accelerator = *dispatch.accelerator](
                            gui::BrowserEventListener& listener) {
                            listener.onAcceleratorRequested(generation,
                                                            accelerator);
                        });
                    return S_OK;
                })
                .Get(),
            &token);
        if (SUCCEEDED(result)) {
            const ComPtr<ICoreWebView2Controller> source = controller_;
            acceleratorKeyPressed_.bind(
                token, [source](const EventRegistrationToken value) {
                    return source->remove_AcceleratorKeyPressed(value);
                });
        }
        return result;
    }

    // 调用线程：WebView2 导航或标题事件所在的 GUI STA，界面更新统一经监听器投递。
    void emitNavigationSnapshot(const std::uint64_t generation,
                                const SnapshotKind kind) {
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
        dispatchTabListener(
            [generation, visibleUrl, documentTitle, canGoBack, canGoForward,
             kind](gui::BrowserEventListener& listener) {
                if (kind == SnapshotKind::NavigationCompleted) {
                    listener.onTabNavigationCompleted(
                        1, generation, visibleUrl, documentTitle,
                        canGoBack != FALSE, canGoForward != FALSE);
                } else if (kind == SnapshotKind::NavigationStopped) {
                    listener.onTabNavigationStopped(
                        1, generation, visibleUrl, documentTitle,
                        canGoBack != FALSE, canGoForward != FALSE);
                } else {
                    listener.onTabDocumentStateChanged(
                        1, generation, visibleUrl, documentTitle,
                        canGoBack != FALSE, canGoForward != FALSE);
                }
            });
    }

    template <typename CallbackType>
    // 调用线程：GUI STA 或 WebView2 回调线程；监听器最终只在 GUI 主线程调用。
    void dispatchListener(const std::uint64_t generation, CallbackType callback) {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
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

    template <typename CallbackType>
    // 调用线程：GUI STA 或 WebView2 回调线程；标签自身代次由 GUI 标签模型验证。
    void dispatchTabListener(CallbackType callback) {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        QMetaObject::invokeMethod(
            QApplication::instance(),
            [this, weakLifetime, lifecycleSerial,
             callback = std::move(callback)]() mutable {
                if (!weakLifetime.expired() && !isShuttingDown_ &&
                    lifecycleSerial == lifecycleSerial_ && listener_ != nullptr) {
                    callback(*listener_);
                }
            },
            Qt::QueuedConnection);
    }

    template <typename CallbackType>
    // 调用线程：GUI STA 或 WebView2 下载回调线程；任务在所属标签关闭后仍可继续。
    void dispatchDownloadListener(const std::uint64_t generation,
                                  CallbackType callback) {
        const std::weak_ptr<int> weakLifetime = lifetime_;
        const std::uint64_t lifecycleSerial = lifecycleSerial_;
        QMetaObject::invokeMethod(
            QApplication::instance(),
            [this, weakLifetime, lifecycleSerial, generation,
             callback = std::move(callback)]() mutable {
                if (!weakLifetime.expired() && !isShuttingDown_ &&
                    lifecycleSerial == lifecycleSerial_ && generation != 0 &&
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
        for (auto& [tabId, tab] : tabControllers_) {
            Q_UNUSED(tabId);
            tab->close();
        }
        tabControllers_.clear();
        tabAudioMuted_.clear();
        tabZoomFactors_.clear();
        activeTabId_ = 1;
        cancelPendingSensitiveRequests();
        newWindowRequested_.reset();
        acceleratorKeyPressed_.reset();
        releaseMainFindController();
        static_cast<void>(nextMainFaviconRequestSerial());
        zoomFactorChanged_.reset();
        faviconChanged_.reset();
        launchingExternalUriScheme_.reset();
        serverCertificateErrorDetected_.reset();
        downloadStarting_.reset();
        screenCaptureStarting_.reset();
        permissionRequested_.reset();
        fullScreenChanged_.reset();
        documentPlayingAudioChanged_.reset();
        processFailed_.reset();
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
        isWebFullScreen_ = false;
        parentWindow_ = nullptr;
        userDataDirectory_.clear();
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
    QString userDataDirectory_;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Profile> profile_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webView_;
    ComPtr<ICoreWebView2Find> find_;
    EventRegistration navigationStarting_;
    EventRegistration navigationCompleted_;
    EventRegistration documentTitleChanged_;
    EventRegistration processFailed_;
    EventRegistration fullScreenChanged_;
    EventRegistration documentPlayingAudioChanged_;
    EventRegistration faviconChanged_;
    EventRegistration zoomFactorChanged_;
    EventRegistration permissionRequested_;
    EventRegistration screenCaptureStarting_;
    EventRegistration downloadStarting_;
    EventRegistration serverCertificateErrorDetected_;
    EventRegistration launchingExternalUriScheme_;
    EventRegistration newWindowRequested_;
    EventRegistration acceleratorKeyPressed_;
    EventRegistration findActiveMatchIndexChanged_;
    EventRegistration findMatchCountChanged_;
    PendingRequestStore<PendingPermission> pendingPermissions_;
    PendingRequestStore<PendingNewWindow> pendingNewWindows_;
    PendingRequestStore<PendingScreenCapture> pendingScreenCaptures_;
    PendingRequestStore<PendingExternalProtocol> pendingExternalProtocols_;
    PendingRequestStore<PendingCertificate> pendingCertificates_;
    PendingRequestStore<PendingDownload> pendingDownloads_;
    std::unordered_map<std::uint64_t, ActiveDownload> activeDownloads_;
    std::unordered_map<std::uint64_t, ActiveDownload> retryDownloads_;
    std::unordered_map<std::uint64_t, ActiveDownload> resumableDownloads_;
    std::unordered_map<std::uint64_t, std::unique_ptr<WebView2TabController>>
        tabControllers_;
    std::unordered_map<std::uint64_t, bool> tabAudioMuted_;
    std::unordered_map<std::uint64_t, double> tabZoomFactors_;
    std::uint64_t activeTabId_{1};
    BrowserLifecycleGate lifecycleGate_;
    std::shared_ptr<int> lifetime_;
    std::uint64_t nextSensitiveRequestId_{1};
    std::uint64_t lifecycleSerial_{0};
    std::uint64_t generation_{0};
    std::uint64_t mainFindRequestSerial_{0};
    std::uint64_t mainFaviconRequestSerial_{0};
    QString mainFindText_;
    NavigationTracker navigation_;
    ClearDataNavigationCoordinator clearDataNavigation_;
    QRect bounds_;
    bool ownsComApartmentReference_{false};
    bool isReady_{false};
    bool isVisible_{false};
    bool isAudioMutedDesired_{false};
    bool isWebFullScreen_{false};
    bool isClearedBlankSnapshotSuppressed_{false};
    bool hasReportedBrowserProcessFailure_{false};
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

bool WebView2BrowserBackend::createTab(void* const parentWindowHandle,
                                       const std::uint64_t tabId,
                                       const QString& initialUrl,
                                       const std::uint64_t generation,
                                       const std::uint64_t newWindowRequestId) {
    return impl_->createTab(parentWindowHandle, tabId, initialUrl, generation,
                            newWindowRequestId);
}

void WebView2BrowserBackend::closeTab(const std::uint64_t tabId) {
    impl_->closeTab(tabId);
}

void WebView2BrowserBackend::activateTab(const std::uint64_t tabId) {
    impl_->activateTab(tabId);
}

void WebView2BrowserBackend::goBack() { impl_->goBack(); }

void WebView2BrowserBackend::goForward() { impl_->goForward(); }

void WebView2BrowserBackend::reloadOrStop() { impl_->reloadOrStop(); }

bool WebView2BrowserBackend::recoverTab(const std::uint64_t tabId,
                                        const std::uint64_t generation) {
    return impl_->recoverTab(tabId, generation);
}

void WebView2BrowserBackend::findInPage(const QString& text,
                                        const bool forward) {
    impl_->findInPage(text, forward);
}

void WebView2BrowserBackend::stopFinding(const bool clearSelection) {
    impl_->stopFinding(clearSelection);
}

void WebView2BrowserBackend::setBounds(const QRect& pixelBounds) {
    impl_->setBounds(pixelBounds);
}

void WebView2BrowserBackend::setVisible(const bool isVisible) {
    impl_->setVisible(isVisible);
}

void WebView2BrowserBackend::setAudioMuted(const bool isMuted) {
    impl_->setAudioMuted(isMuted);
}

void WebView2BrowserBackend::setTabAudioMuted(const std::uint64_t tabId,
                                               const bool isMuted) {
    impl_->setTabAudioMuted(tabId, isMuted);
}

void WebView2BrowserBackend::setTabZoomFactor(const std::uint64_t tabId,
                                              const double zoomFactor) {
    impl_->setTabZoomFactor(tabId, zoomFactor);
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

void WebView2BrowserBackend::retryDownload(const std::uint64_t requestId) {
    impl_->retryDownload(requestId);
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
