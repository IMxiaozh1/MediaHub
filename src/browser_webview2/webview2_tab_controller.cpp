#include "webview2_tab_controller.h"

#include <utility>

#include "webview2_accelerator.h"
#include "webview2_default_deny.h"
#include "webview2_pending_request.h"

namespace mediahub::browser_webview2 {
namespace {

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

struct CoTaskMemStringDeleter {
    void operator()(wchar_t* value) const noexcept { CoTaskMemFree(value); }
};
using CoTaskMemString = std::unique_ptr<wchar_t, CoTaskMemStringDeleter>;

RECT toNativeRect(const QRect& bounds) noexcept {
    return RECT{bounds.x(), bounds.y(), bounds.x() + bounds.width(),
                bounds.y() + bounds.height()};
}

}  // namespace

WebView2TabController::WebView2TabController(
    ComPtr<ICoreWebView2Environment> environment,
    ComPtr<ICoreWebView2Profile> profile, const HWND parentWindow,
    const std::uint64_t tabId, ReadyCallback readyCallback,
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
    ExternalProtocolCallback externalProtocolCallback)
    : environment_(std::move(environment)),
      profile_(std::move(profile)),
      lifetime_(std::make_shared<int>(0)),
      parentWindow_(parentWindow),
      tabId_(tabId),
      readyCallback_(std::move(readyCallback)),
      navigationStartedCallback_(std::move(navigationStartedCallback)),
      navigationCompletedCallback_(std::move(navigationCompletedCallback)),
      documentStateChangedCallback_(std::move(documentStateChangedCallback)),
      navigationStoppedCallback_(std::move(navigationStoppedCallback)),
      errorCallback_(std::move(errorCallback)),
      newWindowCallback_(std::move(newWindowCallback)),
      closedCallback_(std::move(closedCallback)),
      fullScreenCallback_(std::move(fullScreenCallback)),
      acceleratorCallback_(std::move(acceleratorCallback)),
      permissionCallback_(std::move(permissionCallback)),
      screenCaptureCallback_(std::move(screenCaptureCallback)),
      downloadCallback_(std::move(downloadCallback)),
      certificateCallback_(std::move(certificateCallback)),
      externalProtocolCallback_(std::move(externalProtocolCallback)) {}

WebView2TabController::~WebView2TabController() { close(); }

HRESULT WebView2TabController::create(
    const QString& initialUrl, const std::uint64_t generation,
    ICoreWebView2NewWindowRequestedEventArgs* const pendingArgs,
    ICoreWebView2Deferral* const pendingDeferral) {
    const bool hasIncompletePopupRequest =
        pendingArgs != nullptr && pendingDeferral != nullptr;
    if (environment_ == nullptr || profile_ == nullptr || parentWindow_ == nullptr ||
        tabId_ == 0 || isClosed_ ||
        ((pendingArgs == nullptr) != (pendingDeferral == nullptr))) {
        if (hasIncompletePopupRequest) {
            static_cast<void>(
                completePopupRequest(pendingArgs, pendingDeferral,
                                     static_cast<ICoreWebView2*>(nullptr)));
        }
        return E_INVALIDARG;
    }
    generation_ = generation;
    navigation_.reset(generation);
    initialUrl_ = initialUrl;
    isPopupRequest_ = pendingArgs != nullptr;
    if (pendingArgs != nullptr) {
        pendingArgs_ = pendingArgs;
        pendingDeferral_ = pendingDeferral;
    }
    const HRESULT result = createController();
    if (FAILED(result)) {
        static_cast<void>(completePendingRequest(false));
    }
    return result;
}

HRESULT WebView2TabController::createController() {
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
    CoTaskMemString profileName(rawProfileName);
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
        parentWindow_, options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, weakLifetime](const HRESULT status,
                                 ICoreWebView2Controller* const controller) {
                if (weakLifetime.expired()) {
                    ControllerAdoptionTransaction<ICoreWebView2Controller> guard(
                        controller);
                    return S_OK;
                }
                finishController(status, controller);
                return S_OK;
            })
            .Get());
}

void WebView2TabController::finishController(
    const HRESULT status, ICoreWebView2Controller* const controller) {
    HRESULT result = status;
    {
        ControllerAdoptionTransaction<ICoreWebView2Controller> guard(controller);
        if (guard.canAdopt(true, status)) {
            controller_ = guard.adopt();
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
    if (SUCCEEDED(result) && bounds_.isValid()) {
        result = controller_->put_Bounds(toNativeRect(bounds_));
    }
    if (SUCCEEDED(result)) {
        result = controller_->put_IsVisible(isVisible_ ? TRUE : FALSE);
    }
    if (SUCCEEDED(result)) {
        setAudioMuted(isAudioMuted_);
        result = completePendingRequest(true);
    }
    if (SUCCEEDED(result) && !isPopupRequest_ && !initialUrl_.isEmpty()) {
        const std::wstring url = initialUrl_.toStdWString();
        result = webView_->Navigate(url.c_str());
        if (SUCCEEDED(result)) {
            navigation_.acceptNavigate(generation_);
        }
    }
    if (FAILED(result)) {
        static_cast<void>(completePendingRequest(false));
        reportError(gui::BrowserErrorKind::InitializationFailed, result,
                    generation_);
        close();
        return;
    }
    if (readyCallback_) {
        readyCallback_(tabId_, generation_);
    }
}

HRESULT WebView2TabController::configureSettings() {
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
        result = settings4->put_IsPasswordAutosaveEnabled(TRUE);
    }
    if (SUCCEEDED(result)) {
        result = settings4->put_IsGeneralAutofillEnabled(TRUE);
    }
    return result;
}

HRESULT WebView2TabController::registerEvents() {
    const std::weak_ptr<int> weakLifetime = lifetime_;
    EventRegistrationToken token{};
    HRESULT result = webView_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this, weakLifetime](ICoreWebView2*,
                                 ICoreWebView2NavigationStartingEventArgs* args) {
                if (!weakLifetime.expired()) {
                    if (args == nullptr) {
                        if (clearDataNavigation_.isBusy()) {
                            completeClearData(E_POINTER);
                        } else {
                            reportError(gui::BrowserErrorKind::NavigationFailed,
                                        E_POINTER, generation_);
                        }
                        return S_OK;
                    }
                    if (clearDataNavigation_.isBusy()) {
                        UINT64 navigationId = 0;
                        LPWSTR rawUri = nullptr;
                        const HRESULT idStatus = args->get_NavigationId(&navigationId);
                        const HRESULT uriStatus = args->get_Uri(&rawUri);
                        CoTaskMemString uri(rawUri);
                        const bool isInternalBlank =
                            SUCCEEDED(uriStatus) && uri != nullptr &&
                            QString::fromWCharArray(uri.get()).compare(
                                QStringLiteral("about:blank"),
                                Qt::CaseInsensitive) == 0;
                        if (FAILED(idStatus)) {
                            completeClearData(idStatus);
                            return S_OK;
                        }
                        if (FAILED(uriStatus)) {
                            completeClearData(uriStatus);
                            return S_OK;
                        }
                        if (clearDataNavigation_.start(navigationId,
                                                       isInternalBlank)) {
                            return S_OK;
                        }
                        if (clearDataNavigation_.isBusy()) {
                            return S_OK;
                        }
                        return S_OK;
                    }
                    UINT64 navigationId = 0;
                    const HRESULT status = args->get_NavigationId(&navigationId);
                    if (FAILED(status)) {
                        reportError(gui::BrowserErrorKind::NavigationFailed,
                                    status, generation_);
                        return S_OK;
                    }
                    const NavigationStart start = navigation_.start(navigationId);
                    if (start.shouldReport && navigationStartedCallback_) {
                        navigationStartedCallback_(tabId_, start.generation);
                    }
                }
                return S_OK;
            })
            .Get(),
        &token);
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
        navigationStarting_.bind(token, [source](const auto value) {
            return source->remove_NavigationStarting(value);
        });
    }
    if (SUCCEEDED(result)) {
        result = webView_->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this, weakLifetime](ICoreWebView2*,
                                     ICoreWebView2NavigationCompletedEventArgs* args) {
                    if (!weakLifetime.expired()) {
                        if (args == nullptr) {
                            if (clearDataNavigation_.isBusy()) {
                                completeClearData(E_POINTER);
                            } else {
                                reportError(
                                    gui::BrowserErrorKind::NavigationFailed,
                                    E_POINTER, generation_);
                            }
                            return S_OK;
                        }
                        if (clearDataNavigation_.isBusy()) {
                            UINT64 navigationId = 0;
                            HRESULT status = args->get_NavigationId(&navigationId);
                            if (FAILED(status)) {
                                completeClearData(status);
                                return S_OK;
                            }
                            if (!clearDataNavigation_.ownsNavigation(navigationId)) {
                                return S_OK;
                            }
                            BOOL isSuccess = FALSE;
                            status = args->get_IsSuccess(&isSuccess);
                            const ClearDataNavigationCompletion completion =
                                clearDataNavigation_.complete(
                                    navigationId, SUCCEEDED(status) &&
                                                      isSuccess != FALSE);
                            completeClearData(
                                completion.outcome ==
                                        ClearDataNavigationOutcome::Succeeded
                                    ? S_OK
                                    : (FAILED(status) ? status : E_FAIL));
                            return S_OK;
                        }
                        UINT64 navigationId = 0;
                        HRESULT status = args->get_NavigationId(&navigationId);
                        if (FAILED(status)) {
                            reportError(gui::BrowserErrorKind::NavigationFailed,
                                        status, generation_);
                            return S_OK;
                        }
                        const NavigationCompletion completion =
                            navigation_.complete(navigationId);
                        if (!completion.shouldReport) {
                            return S_OK;
                        }
                        BOOL isSuccess = FALSE;
                        status = args->get_IsSuccess(&isSuccess);
                        if (SUCCEEDED(status) && isSuccess != FALSE) {
                            emitNavigationSnapshot(
                                completion.generation,
                                SnapshotKind::NavigationCompleted);
                        } else if (SUCCEEDED(status) &&
                                   isSuccess == FALSE) {
                            COREWEBVIEW2_WEB_ERROR_STATUS webError =
                                COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                            static_cast<void>(args->get_WebErrorStatus(&webError));
                            if (webError ==
                                COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED) {
                                emitNavigationSnapshot(
                                    completion.generation,
                                    SnapshotKind::NavigationStopped);
                            } else {
                                reportError(
                                    gui::BrowserErrorKind::NavigationFailed,
                                    E_FAIL, completion.generation);
                            }
                        } else {
                            reportError(gui::BrowserErrorKind::NavigationFailed,
                                        FAILED(status) ? status : E_FAIL,
                                        completion.generation);
                        }
                    }
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
        navigationCompleted_.bind(token, [source](const auto value) {
            return source->remove_NavigationCompleted(value);
        });
    }
    if (SUCCEEDED(result)) {
        result = webView_->add_DocumentTitleChanged(
            Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                [this, weakLifetime](ICoreWebView2*, IUnknown*) {
                    if (!weakLifetime.expired() && !navigation_.isNavigating() &&
                        !clearDataNavigation_.isBusy() &&
                        !isClearedBlankSnapshotSuppressed_) {
                        emitNavigationSnapshot(
                            generation_, SnapshotKind::DocumentStateChanged);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
        documentTitleChanged_.bind(token, [source](const auto value) {
            return source->remove_DocumentTitleChanged(value);
        });
    }
    if (SUCCEEDED(result)) {
        result = webView_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [this, weakLifetime](ICoreWebView2*,
                                     ICoreWebView2NewWindowRequestedEventArgs* args) {
                    if (!weakLifetime.expired() && newWindowCallback_) {
                        return newWindowCallback_(args);
                    }
                    return rejectNewWindow(args);
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
        newWindowRequested_.bind(token, [source](const auto value) {
            return source->remove_NewWindowRequested(value);
        });
    }
    if (SUCCEEDED(result)) {
        result = webView_->add_WindowCloseRequested(
            Callback<ICoreWebView2WindowCloseRequestedEventHandler>(
                [this, weakLifetime](ICoreWebView2*, IUnknown*) {
                    if (!weakLifetime.expired() && closedCallback_) {
                        closedCallback_(tabId_);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
        windowCloseRequested_.bind(token, [source](const auto value) {
            return source->remove_WindowCloseRequested(value);
        });
    }
    if (SUCCEEDED(result)) {
        result = webView_->add_ContainsFullScreenElementChanged(
            Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
                [this, weakLifetime](ICoreWebView2*, IUnknown*) {
                    BOOL fullScreen = FALSE;
                    if (!weakLifetime.expired() &&
                        SUCCEEDED(webView_->get_ContainsFullScreenElement(&fullScreen))) {
                        isFullScreen_ = fullScreen != FALSE;
                        if (fullScreenCallback_) {
                            fullScreenCallback_(tabId_, generation_, isFullScreen_);
                        }
                    }
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
        fullScreenChanged_.bind(token, [source](const auto value) {
            return source->remove_ContainsFullScreenElementChanged(value);
        });
    }
    if (SUCCEEDED(result)) {
        result = controller_->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [this, weakLifetime](ICoreWebView2Controller*,
                                     ICoreWebView2AcceleratorKeyPressedEventArgs* args) {
                    if (weakLifetime.expired() || args == nullptr) {
                        return S_OK;
                    }
                    const AcceleratorDispatch dispatch = handleAcceleratorKey(
                        *args, (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                        (GetKeyState(VK_MENU) & 0x8000) != 0,
                        (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                        (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
                            (GetKeyState(VK_RWIN) & 0x8000) != 0,
                        isFullScreen_);
                    if (dispatch.accelerator.has_value() && acceleratorCallback_) {
                        acceleratorCallback_(tabId_, generation_,
                                             *dispatch.accelerator);
                    }
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2Controller> source = controller_;
        acceleratorKeyPressed_.bind(token, [source](const auto value) {
            return source->remove_AcceleratorKeyPressed(value);
        });
    }

    if (SUCCEEDED(result)) {
        result = webView_->add_PermissionRequested(
            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                [this, weakLifetime](
                    ICoreWebView2*,
                    ICoreWebView2PermissionRequestedEventArgs* args) {
                    if (!weakLifetime.expired() && permissionCallback_) {
                        return permissionCallback_(tabId_, generation_, args);
                    }
                    static_cast<void>(denyPermission(args));
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        const ComPtr<ICoreWebView2> source = webView_;
            permissionRequested_.bind(token, [source](const auto value) {
                return source->remove_PermissionRequested(value);
            });
    }
    ComPtr<ICoreWebView2_27> webView27;
    if (SUCCEEDED(result)) {
        result = webView_.As(&webView27);
    }
    if (SUCCEEDED(result)) {
        result = webView27->add_ScreenCaptureStarting(
            Callback<ICoreWebView2ScreenCaptureStartingEventHandler>(
                [this, weakLifetime](
                    ICoreWebView2*,
                    ICoreWebView2ScreenCaptureStartingEventArgs* args) {
                    if (!weakLifetime.expired() && screenCaptureCallback_) {
                        return screenCaptureCallback_(tabId_, generation_, args);
                    }
                    static_cast<void>(cancelScreenCapture(args));
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        screenCaptureStarting_.bind(token, [webView27](const auto value) {
            return webView27->remove_ScreenCaptureStarting(value);
        });
    }
    ComPtr<ICoreWebView2_4> webView4;
    if (SUCCEEDED(result)) {
        result = webView_.As(&webView4);
    }
    if (SUCCEEDED(result)) {
        result = webView4->add_DownloadStarting(
            Callback<ICoreWebView2DownloadStartingEventHandler>(
                [this, weakLifetime](
                    ICoreWebView2*,
                    ICoreWebView2DownloadStartingEventArgs* args) {
                    if (!weakLifetime.expired() && downloadCallback_) {
                        return downloadCallback_(tabId_, generation_, args);
                    }
                    static_cast<void>(cancelDownload(args));
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        downloadStarting_.bind(token, [webView4](const auto value) {
            return webView4->remove_DownloadStarting(value);
        });
    }
    ComPtr<ICoreWebView2_14> webView14;
    if (SUCCEEDED(result)) {
        result = webView_.As(&webView14);
    }
    if (SUCCEEDED(result)) {
        result = webView14->add_ServerCertificateErrorDetected(
            Callback<ICoreWebView2ServerCertificateErrorDetectedEventHandler>(
                [this, weakLifetime](
                    ICoreWebView2*,
                    ICoreWebView2ServerCertificateErrorDetectedEventArgs* args) {
                    if (!weakLifetime.expired() && certificateCallback_) {
                        return certificateCallback_(tabId_, generation_, args);
                    }
                    static_cast<void>(cancelCertificateError(args));
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        certificateErrorRequested_.bind(token, [webView14](const auto value) {
            return webView14->remove_ServerCertificateErrorDetected(value);
        });
    }
    ComPtr<ICoreWebView2_18> webView18;
    if (SUCCEEDED(result)) {
        result = webView_.As(&webView18);
    }
    if (SUCCEEDED(result)) {
        result = webView18->add_LaunchingExternalUriScheme(
            Callback<ICoreWebView2LaunchingExternalUriSchemeEventHandler>(
                [this, weakLifetime](
                    ICoreWebView2*,
                    ICoreWebView2LaunchingExternalUriSchemeEventArgs* args) {
                    if (!weakLifetime.expired() && externalProtocolCallback_) {
                        return externalProtocolCallback_(tabId_, generation_, args);
                    }
                    static_cast<void>(cancelExternalUri(args));
                    return S_OK;
                })
                .Get(),
            &token);
    }
    if (SUCCEEDED(result)) {
        externalProtocolRequested_.bind(token, [webView18](const auto value) {
            return webView18->remove_LaunchingExternalUriScheme(value);
        });
    }
    return result;
}

void WebView2TabController::emitNavigationSnapshot(
    const std::uint64_t generation, const SnapshotKind kind) {
    const NavigationCompletedCallback& callback =
        kind == SnapshotKind::NavigationCompleted
            ? navigationCompletedCallback_
        : kind == SnapshotKind::NavigationStopped
            ? navigationStoppedCallback_
            : documentStateChangedCallback_;
    if (webView_ == nullptr || !callback) {
        return;
    }
    LPWSTR rawSource = nullptr;
    LPWSTR rawTitle = nullptr;
    BOOL canGoBack = FALSE;
    BOOL canGoForward = FALSE;
    HRESULT result = webView_->get_Source(&rawSource);
    result = firstFailure(result, webView_->get_DocumentTitle(&rawTitle));
    result = firstFailure(result, webView_->get_CanGoBack(&canGoBack));
    result = firstFailure(result, webView_->get_CanGoForward(&canGoForward));
    CoTaskMemString source(rawSource);
    CoTaskMemString title(rawTitle);
    if (FAILED(result)) {
        reportError(gui::BrowserErrorKind::NavigationFailed, result,
                    generation);
        return;
    }
    callback(
        tabId_, generation,
        source != nullptr ? QString::fromWCharArray(source.get()) : QString{},
        title != nullptr ? QString::fromWCharArray(title.get()) : QString{},
        canGoBack != FALSE, canGoForward != FALSE);
}

void WebView2TabController::reportError(const gui::BrowserErrorKind kind,
                                        const HRESULT result,
                                        const std::uint64_t generation) {
    if (errorCallback_) {
        errorCallback_(tabId_, generation, kind, result);
    }
}

void WebView2TabController::navigate(const QString& url,
                                     const std::uint64_t generation) {
    if (clearDataNavigation_.isBusy()) {
        return;
    }
    isClearedBlankSnapshotSuppressed_ = false;
    generation_ = generation;
    navigation_.setCurrentGeneration(generation);
    if (webView_ != nullptr) {
        const std::wstring value = url.toStdWString();
        const HRESULT result = webView_->Navigate(value.c_str());
        if (FAILED(result)) {
            reportError(gui::BrowserErrorKind::NavigationFailed, result,
                        generation);
        } else {
            navigation_.acceptNavigate(generation);
        }
    }
}

void WebView2TabController::clearBrowsingData(
    const std::uint64_t generation, ClearDataCallback callback) {
    generation_ = generation;
    navigation_.reset(generation);
    clearDataCallback_ = std::move(callback);
    clearDataNavigation_.begin(generation);
    if (webView_ == nullptr || profile_ == nullptr) {
        completeClearData(E_UNEXPECTED);
        return;
    }

    ComPtr<ICoreWebView2Profile2> profile2;
    ComPtr<ICoreWebView2_14> webView14;
    HRESULT result = profile_.As(&profile2);
    if (SUCCEEDED(result)) {
        result = webView_.As(&webView14);
    }
    if (FAILED(result)) {
        completeClearData(result);
        return;
    }

    const std::weak_ptr<int> weakLifetime = lifetime_;
    result = profile2->ClearBrowsingData(
        COREWEBVIEW2_BROWSING_DATA_KINDS_ALL_PROFILE,
        Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>(
            [this, weakLifetime, webView14,
             generation](const HRESULT status) -> HRESULT {
                if (weakLifetime.expired() || isClosed_ ||
                    generation != generation_) {
                    return S_OK;
                }
                if (FAILED(status)) {
                    completeClearData(status);
                    return S_OK;
                }
                const HRESULT requestStatus =
                    webView14->ClearServerCertificateErrorActions(
                        Callback<
                            ICoreWebView2ClearServerCertificateErrorActionsCompletedHandler>(
                            [this, weakLifetime,
                             generation](const HRESULT certificateStatus) -> HRESULT {
                                if (weakLifetime.expired() || isClosed_ ||
                                    generation != generation_) {
                                    return S_OK;
                                }
                                if (FAILED(certificateStatus)) {
                                    completeClearData(certificateStatus);
                                    return S_OK;
                                }
                                if (!clearDataNavigation_.dataAndCertificatesCleared(
                                        generation)) {
                                    return S_OK;
                                }
                                isClearedBlankSnapshotSuppressed_ = true;
                                const HRESULT blankStatus =
                                    webView_->Navigate(L"about:blank");
                                if (FAILED(blankStatus)) {
                                    static_cast<void>(
                                        clearDataNavigation_.blankRequestFailed(
                                            generation));
                                    completeClearData(blankStatus);
                                }
                                return S_OK;
                            })
                            .Get());
                if (FAILED(requestStatus)) {
                    completeClearData(requestStatus);
                }
                return S_OK;
            })
            .Get());
    if (FAILED(result)) {
        completeClearData(result);
    }
}

void WebView2TabController::completeClearData(const HRESULT result) {
    clearDataNavigation_.reset();
    ClearDataCallback callback = std::move(clearDataCallback_);
    clearDataCallback_ = {};
    if (callback) {
        callback(result);
    }
}

void WebView2TabController::goBack() noexcept {
    if (webView_ != nullptr) static_cast<void>(webView_->GoBack());
}

void WebView2TabController::goForward() noexcept {
    if (webView_ != nullptr) static_cast<void>(webView_->GoForward());
}

void WebView2TabController::reloadOrStop() noexcept {
    if (webView_ != nullptr) {
        static_cast<void>(navigation_.isNavigating() ? webView_->Stop()
                                                     : webView_->Reload());
    }
}

void WebView2TabController::setBounds(const QRect& bounds) noexcept {
    bounds_ = bounds;
    if (controller_ != nullptr && bounds_.isValid()) {
        static_cast<void>(controller_->put_Bounds(toNativeRect(bounds_)));
    }
}

void WebView2TabController::setVisible(const bool isVisible) noexcept {
    isVisible_ = isVisible;
    if (controller_ != nullptr) {
        static_cast<void>(controller_->put_IsVisible(isVisible ? TRUE : FALSE));
    }
}

void WebView2TabController::setAudioMuted(const bool isMuted) noexcept {
    isAudioMuted_ = isMuted;
    if (webView_ == nullptr) return;
    ComPtr<ICoreWebView2_8> webView8;
    if (SUCCEEDED(webView_.As(&webView8))) {
        static_cast<void>(webView8->put_IsMuted(isMuted ? TRUE : FALSE));
    }
}

void WebView2TabController::exitFullScreen() noexcept {
    if (webView_ != nullptr) {
        static_cast<void>(webView_->ExecuteScript(
            L"document.fullscreenElement && document.exitFullscreen();", nullptr));
    }
}

HRESULT WebView2TabController::completePendingRequest(
    const bool attachWebView) noexcept {
    if (pendingArgs_ == nullptr && pendingDeferral_ == nullptr) {
        return S_OK;
    }
    const HRESULT result = completePopupRequest(
        pendingArgs_.Get(), pendingDeferral_.Get(),
        attachWebView ? webView_.Get() : nullptr);
    pendingDeferral_.Reset();
    pendingArgs_.Reset();
    return result;
}

void WebView2TabController::close() noexcept {
    if (isClosed_) return;
    isClosed_ = true;
    clearDataNavigation_.reset();
    navigation_.reset(generation_);
    clearDataCallback_ = {};
    lifetime_.reset();
    static_cast<void>(completePendingRequest(false));
    externalProtocolRequested_.reset();
    certificateErrorRequested_.reset();
    downloadStarting_.reset();
    screenCaptureStarting_.reset();
    permissionRequested_.reset();
    acceleratorKeyPressed_.reset();
    fullScreenChanged_.reset();
    windowCloseRequested_.reset();
    newWindowRequested_.reset();
    documentTitleChanged_.reset();
    navigationCompleted_.reset();
    navigationStarting_.reset();
    if (controller_ != nullptr) static_cast<void>(controller_->Close());
    webView_.Reset();
    controller_.Reset();
    profile_.Reset();
    environment_.Reset();
}

}  // namespace mediahub::browser_webview2
