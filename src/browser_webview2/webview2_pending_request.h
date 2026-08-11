#pragma once

#include <WebView2.h>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "browser_types.h"

namespace mediahub::browser_webview2 {

// 保存待决定对象；take 会原子移除请求，使每个 requestId 最多完成一次。
template <typename Value>
class PendingRequestStore final {
 public:
    bool insert(const std::uint64_t requestId, Value value) {
        return values_.emplace(requestId, std::move(value)).second;
    }

    [[nodiscard]] std::optional<Value> take(const std::uint64_t requestId) {
        const auto found = values_.find(requestId);
        if (found == values_.end()) {
            return std::nullopt;
        }
        std::optional<Value> result(std::move(found->second));
        values_.erase(found);
        return result;
    }

    [[nodiscard]] std::vector<Value> takeAll() {
        std::vector<Value> result;
        result.reserve(values_.size());
        for (auto& [requestId, value] : values_) {
            static_cast<void>(requestId);
            result.push_back(std::move(value));
        }
        values_.clear();
        return result;
    }

    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }

 private:
    std::unordered_map<std::uint64_t, Value> values_;
};

inline HRESULT firstFailure(const HRESULT current,
                            const HRESULT candidate) noexcept {
    return FAILED(current) ? current : candidate;
}

// 标准权限准备失败时不依赖 Args3，也必须拒绝并完成已取得的 deferral。
template <typename BaseArgs, typename Args3, typename Deferral>
HRESULT completePermissionRejection(BaseArgs* const baseArgs,
                                    Args3* const args3,
                                    Deferral* const deferral) noexcept {
    if (baseArgs == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    HRESULT result =
        baseArgs->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
    if (args3 != nullptr) {
        result = firstFailure(result, args3->put_SavesInProfile(FALSE));
    }
    return firstFailure(result, deferral->Complete());
}

template <typename Args, typename Deferral>
HRESULT completePermissionDecision(
    Args* const args, Deferral* const deferral,
    const gui::BrowserPermissionDecision decision) noexcept {
    if (args == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    const bool wantsAllow = decision != gui::BrowserPermissionDecision::Deny;
    const BOOL savesInProfile =
        decision == gui::BrowserPermissionDecision::RememberForOrigin ? TRUE : FALSE;
    HRESULT result = args->put_SavesInProfile(savesInProfile);
    const COREWEBVIEW2_PERMISSION_STATE state =
        wantsAllow && SUCCEEDED(result) ? COREWEBVIEW2_PERMISSION_STATE_ALLOW
                                       : COREWEBVIEW2_PERMISSION_STATE_DENY;
    result = firstFailure(result, args->put_State(state));
    return firstFailure(result, deferral->Complete());
}

// 屏幕捕获没有 Profile 记忆语义，只有显式“仅本次允许”才解除取消状态。
template <typename Args, typename Deferral>
HRESULT completeScreenCaptureDecision(
    Args* const args, Deferral* const deferral,
    const gui::BrowserPermissionDecision decision) noexcept {
    if (args == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    const BOOL isCancelled =
        decision == gui::BrowserPermissionDecision::AllowOnce ? FALSE : TRUE;
    HRESULT result = args->put_Cancel(isCancelled);
    result = firstFailure(result, args->put_Handled(TRUE));
    return firstFailure(result, deferral->Complete());
}

template <typename Args, typename Deferral>
HRESULT completeExternalProtocolDecision(Args* const args, Deferral* const deferral,
                                         const bool isAllowed) noexcept {
    if (args == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    HRESULT result = args->put_Cancel(isAllowed ? FALSE : TRUE);
    return firstFailure(result, deferral->Complete());
}

template <typename Args, typename Deferral>
HRESULT completeCertificateDecision(
    Args* const args, Deferral* const deferral,
    const gui::BrowserCertificateDecision decision) noexcept {
    if (args == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    const auto action =
        decision == gui::BrowserCertificateDecision::ContinueForSession
            ? COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_ALWAYS_ALLOW
            : COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL;
    HRESULT result = args->put_Action(action);
    return firstFailure(result, deferral->Complete());
}

// 证书安全决定失败也不能阻止取得 deferral，以便调用方完成默认拒绝。
template <typename Args, typename Deferral>
HRESULT prepareCertificateRequest(Args* const args,
                                  Deferral** const deferral) noexcept {
    if (args == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    HRESULT result = args->put_Action(
        COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL);
    return firstFailure(result, args->GetDeferral(deferral));
}

// 外部协议默认取消失败也继续取得 deferral，避免 Runtime 默认处理请求。
template <typename Args, typename Deferral>
HRESULT prepareExternalProtocolRequest(Args* const args,
                                       Deferral** const deferral) noexcept {
    if (args == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    HRESULT result = args->put_Cancel(TRUE);
    return firstFailure(result, args->GetDeferral(deferral));
}

inline bool isSafeDownloadDestination(const QString& destination) {
    if (destination.isEmpty() || destination.trimmed() != destination) {
        return false;
    }
    const QFileInfo destinationInfo(QDir::cleanPath(destination));
    if (!destinationInfo.isAbsolute() || destinationInfo.exists() ||
        destinationInfo.fileName().isEmpty()) {
        return false;
    }
    const QFileInfo parentInfo(destinationInfo.absolutePath());
    if (!parentInfo.exists() || !parentInfo.isDir()) {
        return false;
    }
    const QString fileName = destinationInfo.fileName();
    if (fileName.endsWith(QLatin1Char('.')) || fileName.endsWith(QLatin1Char(' '))) {
        return false;
    }
    static const QRegularExpression invalidCharacters(
        QStringLiteral(R"([<>:"/\\|?*])"));
    if (fileName.contains(invalidCharacters)) {
        return false;
    }
    const QString stem = fileName.section(QLatin1Char('.'), 0, 0).toUpper();
    static const QRegularExpression reservedName(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$)"));
    return !reservedName.match(stem).hasMatch();
}

template <typename Args, typename Operation, typename Deferral>
HRESULT completeDownloadCancellation(Args* args, Operation* operation,
                                     Deferral* deferral) noexcept;

// 下载接管的四个安全动作彼此独立；保留首个错误，但不得用它短路后续动作。
template <typename Args, typename Operation, typename Deferral>
HRESULT prepareDownloadRequest(Args* const args, Operation** const operation,
                               Deferral** const deferral) noexcept {
    if (args == nullptr || operation == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    HRESULT result = args->put_Cancel(TRUE);
    result = firstFailure(result, args->put_Handled(TRUE));
    result = firstFailure(result, args->get_DownloadOperation(operation));
    return firstFailure(result, args->GetDeferral(deferral));
}

template <typename Args, typename Operation, typename Deferral>
HRESULT completeDownloadPathDecision(Args* const args, Operation* const operation,
                                     Deferral* const deferral,
                                     const QString& destination) {
    if (args == nullptr || operation == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    if (!isSafeDownloadDestination(destination)) {
        return firstFailure(
            E_INVALIDARG,
            completeDownloadCancellation(args, operation, deferral));
    }
    const std::wstring nativeDestination =
        QDir::toNativeSeparators(QDir::cleanPath(destination)).toStdWString();
    HRESULT result = args->put_ResultFilePath(nativeDestination.c_str());
    if (SUCCEEDED(result)) {
        result = args->put_Cancel(FALSE);
    }
    if (FAILED(result)) {
        return firstFailure(
            result, completeDownloadCancellation(args, operation, deferral));
    }
    const HRESULT completeResult = deferral->Complete();
    if (FAILED(completeResult)) {
        static_cast<void>(args->put_Cancel(TRUE));
        static_cast<void>(operation->Cancel());
    }
    return completeResult;
}

template <typename Args, typename Operation, typename Deferral>
HRESULT completeDownloadCancellation(Args* const args, Operation* const operation,
                                     Deferral* const deferral) noexcept {
    HRESULT result = args != nullptr ? args->put_Cancel(TRUE) : E_POINTER;
    result = firstFailure(
        result, operation != nullptr ? operation->Cancel() : E_POINTER);
    return firstFailure(
        result, deferral != nullptr ? deferral->Complete() : E_POINTER);
}

// 协调下载从待决定到活动状态；任一失败都会在返回前撤销订阅并完成 deferral。
template <typename Active, typename Register, typename Store, typename Complete,
          typename Reject, typename Remove>
HRESULT startDownloadTransaction(Active& active, Register&& registerActive,
                                 Store&& storeActive, Complete&& completeDecision,
                                 Reject&& rejectPending, Remove&& removeActive) {
    const HRESULT registrationResult = registerActive(active);
    if (FAILED(registrationResult)) {
        active.resetSubscriptions();
        return firstFailure(registrationResult, rejectPending());
    }
    if (!storeActive(active)) {
        active.resetSubscriptions();
        return firstFailure(E_UNEXPECTED, rejectPending());
    }
    const HRESULT decisionResult = completeDecision();
    if (FAILED(decisionResult)) {
        removeActive();
    }
    return decisionResult;
}

enum class PendingDownloadCancelAction {
    AwaitTerminal,
    ReportCancelled,
    ReportCancelFailed,
};

struct PendingDownloadCancelOutcome final {
    PendingDownloadCancelAction action{
        PendingDownloadCancelAction::ReportCancelFailed};
    HRESULT result{E_FAIL};
};

// 待选路径取消先尝试接入终态订阅；无法观察终态时只在取消失败后保留重试对象。
template <typename Active, typename Register, typename Store, typename Complete,
          typename Retain>
PendingDownloadCancelOutcome cancelPendingDownloadTransaction(
    Active& active, Register&& registerActive, Store&& storeActive,
    Complete&& completePending, Retain&& retainForRetry) {
    const HRESULT registrationResult = registerActive(active);
    HRESULT setupResult = registrationResult;
    Active* storedActive = nullptr;
    if (SUCCEEDED(registrationResult)) {
        storedActive = storeActive(std::move(active));
        if (storedActive == nullptr) {
            setupResult = E_UNEXPECTED;
            active.resetSubscriptions();
        }
    } else {
        active.resetSubscriptions();
    }

    Active& cancellationState =
        storedActive != nullptr ? *storedActive : active;
    cancellationState.isCancelRequested = true;
    const HRESULT cancellationResult = completePending();
    const HRESULT result = firstFailure(setupResult, cancellationResult);
    if (SUCCEEDED(cancellationResult)) {
        return {storedActive != nullptr
                    ? PendingDownloadCancelAction::AwaitTerminal
                    : PendingDownloadCancelAction::ReportCancelled,
                result};
    }

    cancellationState.isCancelRequested = false;
    if (storedActive == nullptr && !retainForRetry(std::move(active))) {
        return {PendingDownloadCancelAction::ReportCancelFailed,
                firstFailure(result, E_UNEXPECTED)};
    }
    return {PendingDownloadCancelAction::ReportCancelFailed, result};
}

// 活动下载的取消失败会恢复可重试状态，并同步通知稳定失败事件。
template <typename Active, typename Cancel, typename NotifyFailure>
HRESULT requestActiveDownloadCancellation(Active& active, Cancel&& cancel,
                                          NotifyFailure&& notifyFailure) {
    if (active.isCancelRequested) {
        return S_FALSE;
    }
    active.isCancelRequested = true;
    const HRESULT result = cancel();
    if (FAILED(result)) {
        active.isCancelRequested = false;
        notifyFailure();
    }
    return result;
}

// 关闭阶段不信任此前的取消请求结果，始终再次向 operation 提交 Cancel。
template <typename Active, typename Cancel>
HRESULT cancelActiveDownloadForShutdown(Active& active, Cancel&& cancel) {
    active.isCancelRequested = true;
    return cancel();
}

struct DownloadSnapshot final {
    gui::BrowserDownloadState state{gui::BrowserDownloadState::InProgress};
    std::int64_t receivedBytes{-1};
    std::int64_t totalBytes{-1};
    HRESULT stateResult{E_FAIL};
    bool hasState{false};
    bool isTerminal{false};
};

// 必须先读取终态；进度读取失败只降级数值，不能阻止终态释放活动任务。
template <typename Operation>
DownloadSnapshot readDownloadSnapshot(Operation* const operation,
                                      const std::int64_t previousTotalBytes,
                                      const bool isCancelRequested) {
    DownloadSnapshot snapshot;
    snapshot.totalBytes = previousTotalBytes;
    if (operation == nullptr) {
        snapshot.stateResult = E_POINTER;
        return snapshot;
    }

    COREWEBVIEW2_DOWNLOAD_STATE rawState =
        COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
    snapshot.stateResult = operation->get_State(&rawState);
    if (FAILED(snapshot.stateResult)) {
        return snapshot;
    }
    snapshot.hasState = true;

    INT64 receivedBytes = -1;
    if (SUCCEEDED(operation->get_BytesReceived(&receivedBytes))) {
        snapshot.receivedBytes = static_cast<std::int64_t>(receivedBytes);
    }
    INT64 totalBytes = previousTotalBytes;
    if (SUCCEEDED(operation->get_TotalBytesToReceive(&totalBytes))) {
        snapshot.totalBytes = static_cast<std::int64_t>(totalBytes);
    }

    if (rawState == COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED) {
        snapshot.state = gui::BrowserDownloadState::Completed;
        snapshot.isTerminal = true;
    } else if (rawState == COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED) {
        COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON reason =
            COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_NONE;
        static_cast<void>(operation->get_InterruptReason(&reason));
        snapshot.state =
            isCancelRequested ||
                    reason == COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_CANCELED ||
                    reason == COREWEBVIEW2_DOWNLOAD_INTERRUPT_REASON_USER_SHUTDOWN
                ? gui::BrowserDownloadState::Cancelled
                : gui::BrowserDownloadState::Failed;
        snapshot.isTerminal = true;
    }
    return snapshot;
}

// 排队释放只能删除同一生命周期的任务；shutdown 后的旧回调不得触碰新集合。
template <typename Store>
bool eraseTerminalDownloadIfCurrent(Store& store, const std::uint64_t requestId,
                                    const std::uint64_t expectedLifecycleSerial,
                                    const std::uint64_t currentLifecycleSerial,
                                    const bool isShuttingDown) {
    if (isShuttingDown || expectedLifecycleSerial != currentLifecycleSerial) {
        return false;
    }
    return store.erase(requestId) != 0;
}

}  // namespace mediahub::browser_webview2
