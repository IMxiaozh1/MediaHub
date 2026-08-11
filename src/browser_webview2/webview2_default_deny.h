#pragma once

#include <WebView2.h>

namespace mediahub::browser_webview2 {

// 调用线程：WebView2 事件所在的 GUI STA；策略只写入拒绝决定，不读取请求内容。
template <typename Args>
HRESULT denyPermission(Args* const args) noexcept {
    return args != nullptr ? args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY)
                           : E_POINTER;
}

// 调用线程：WebView2 事件所在的 GUI STA；即使取消写入失败也尝试关闭默认下载界面。
template <typename Args>
HRESULT cancelDownload(Args* const args) noexcept {
    if (args == nullptr) {
        return E_POINTER;
    }
    const HRESULT cancelResult = args->put_Cancel(TRUE);
    const HRESULT handledResult = args->put_Handled(TRUE);
    return FAILED(cancelResult) ? cancelResult : handledResult;
}

// 调用线程：WebView2 事件所在的 GUI STA；策略不读取证书或请求地址。
template <typename Args>
HRESULT cancelCertificateError(Args* const args) noexcept {
    return args != nullptr
               ? args->put_Action(
                     COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL)
               : E_POINTER;
}

// 调用线程：WebView2 事件所在的 GUI STA；策略不读取或启动目标 URI。
template <typename Args>
HRESULT cancelExternalUri(Args* const args) noexcept {
    return args != nullptr ? args->put_Cancel(TRUE) : E_POINTER;
}

// 调用线程：WebView2 事件所在的 GUI STA；策略只阻止 Runtime 创建新窗口。
template <typename Args>
HRESULT rejectNewWindow(Args* const args) noexcept {
    return args != nullptr ? args->put_Handled(TRUE) : E_POINTER;
}

}  // namespace mediahub::browser_webview2
