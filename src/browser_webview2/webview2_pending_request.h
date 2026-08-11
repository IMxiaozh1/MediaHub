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
    const QString stem = QFileInfo(fileName).completeBaseName().toUpper();
    static const QRegularExpression reservedName(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$)"));
    return !reservedName.match(stem).hasMatch();
}

template <typename Args, typename Deferral>
HRESULT completeDownloadPathDecision(Args* const args, Deferral* const deferral,
                                     const QString& destination) {
    if (args == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    if (!isSafeDownloadDestination(destination)) {
        return E_INVALIDARG;
    }
    const std::wstring nativeDestination =
        QDir::toNativeSeparators(QDir::cleanPath(destination)).toStdWString();
    HRESULT result = args->put_ResultFilePath(nativeDestination.c_str());
    if (SUCCEEDED(result)) {
        result = args->put_Cancel(FALSE);
    }
    return firstFailure(result, deferral->Complete());
}

template <typename Args, typename Operation, typename Deferral>
HRESULT completeDownloadCancellation(Args* const args, Operation* const operation,
                                     Deferral* const deferral) noexcept {
    if (args == nullptr || operation == nullptr || deferral == nullptr) {
        return E_POINTER;
    }
    HRESULT result = args->put_Cancel(TRUE);
    result = firstFailure(result, operation->Cancel());
    return firstFailure(result, deferral->Complete());
}

}  // namespace mediahub::browser_webview2
