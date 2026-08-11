#pragma once

#include <QString>

#include <cstdint>

#include "browser_types.h"

namespace mediahub::gui {

// 浏览器事件仅携带界面所需的稳定值，不暴露网页内容、凭据或内核对象。
class BrowserEventListener {
 public:
    virtual ~BrowserEventListener() = default;

    // 调用线程：GUI 主线程。通知当前代次的浏览器环境和控制器已可用。
    virtual void onBrowserReady(std::uint64_t generation) = 0;
    // 调用线程：GUI 主线程。只上报稳定类别与数值错误码。
    virtual void onBrowserError(std::uint64_t generation,
                                BrowserErrorKind kind,
                                long errorCode) = 0;
    // 调用线程：GUI 主线程。通知主框架开始导航。
    virtual void onNavigationStarted(std::uint64_t generation) = 0;
    // 调用线程：GUI 主线程。当前地址和标题只用于可见界面，不进入日志。
    virtual void onNavigationCompleted(std::uint64_t generation,
                                       const QString& visibleUrl,
                                       const QString& title,
                                       bool canGoBack,
                                       bool canGoForward) = 0;
    // 调用线程：GUI 主线程。通知网页全屏状态变化。
    virtual void onFullScreenChanged(std::uint64_t generation,
                                     bool isFullScreen) = 0;
    // 调用线程：GUI 主线程。来源必须是无查询参数的规范化站点来源。
    virtual void onPermissionRequested(std::uint64_t requestId,
                                       const QString& origin,
                                       BrowserPermissionKind kind) = 0;
    // 调用线程：GUI 主线程。来源和目标只在确认界面中展示，不进入日志。
    virtual void onExternalProtocolRequested(std::uint64_t requestId,
                                             const QString& origin,
                                             const QString& target) = 0;
    // 调用线程：GUI 主线程。证书例外只允许当前会话和对应来源。
    virtual void onCertificateErrorRequested(std::uint64_t requestId,
                                             const QString& origin,
                                             const QString& errorDescription) = 0;
    // 调用线程：GUI 主线程。建议文件名不得包含目录。
    virtual void onDownloadRequested(std::uint64_t requestId,
                                     const QString& origin,
                                     const QString& suggestedFileName,
                                     std::int64_t totalBytes) = 0;
    // 调用线程：GUI 主线程。下载状态只对应一个已由用户确认的目标。
    virtual void onDownloadUpdated(std::uint64_t requestId,
                                   BrowserDownloadState state,
                                   std::int64_t receivedBytes,
                                   std::int64_t totalBytes) = 0;
    // 调用线程：GUI 主线程。通知当前代次的网页资料已经清除。
    virtual void onBrowsingDataCleared(std::uint64_t generation) = 0;
    // 调用线程：GUI 主线程。通知弹窗因数量或关闭状态被拒绝。
    virtual void onPopupRejected() = 0;
};

}  // namespace mediahub::gui
