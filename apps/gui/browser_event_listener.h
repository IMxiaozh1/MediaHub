#pragma once

#include <QByteArray>
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
    // 调用线程：GUI 主线程。页面标题等展示信息变化不代表发生了一次新访问。
    virtual void onDocumentStateChanged(std::uint64_t generation,
                                        const QString& visibleUrl,
                                        const QString& title,
                                        bool canGoBack,
                                        bool canGoForward) {
        onNavigationCompleted(generation, visibleUrl, title, canGoBack,
                              canGoForward);
    }
    // 调用线程：GUI 主线程。用户停止导航后恢复现有页面，但不产生成功访问记录。
    virtual void onNavigationStopped(std::uint64_t generation,
                                     const QString& visibleUrl,
                                     const QString& title,
                                     bool canGoBack,
                                     bool canGoForward) {
        onNavigationCompleted(generation, visibleUrl, title, canGoBack,
                              canGoForward);
    }
    // 调用线程：GUI 主线程。通知网页全屏状态变化。
    virtual void onFullScreenChanged(std::uint64_t generation,
                                     bool isFullScreen) = 0;
    // 调用线程：GUI 主线程。原生网页子窗口按键只携带稳定动作和当前代次。
    virtual void onAcceleratorRequested(std::uint64_t generation,
                                        BrowserAccelerator accelerator) = 0;
    // 调用线程：GUI 主线程。索引从 0 开始，无活动匹配时为 -1。
    virtual void onFindResultChanged(std::uint64_t tabId,
                                     std::uint64_t generation,
                                     int activeMatchIndex,
                                     int matchCount) {
        Q_UNUSED(tabId);
        Q_UNUSED(generation);
        Q_UNUSED(activeMatchIndex);
        Q_UNUSED(matchCount);
    }
    // 调用线程：GUI 主线程。原生查找不可用或执行失败时通知当前标签。
    virtual void onFindFailed(std::uint64_t tabId,
                              std::uint64_t generation,
                              long errorCode) {
        Q_UNUSED(tabId);
        Q_UNUSED(generation);
        Q_UNUSED(errorCode);
    }
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
    // 调用线程：GUI 主线程。并发后端必须携带稳定标签 ID，供危险批量操作保护。
    virtual void onTabDownloadRequested(std::uint64_t tabId,
                                        std::uint64_t requestId,
                                        const QString& origin,
                                        const QString& suggestedFileName,
                                        std::int64_t totalBytes) {
        Q_UNUSED(tabId);
        onDownloadRequested(requestId, origin, suggestedFileName, totalBytes);
    }
    // 调用线程：GUI 主线程。下载状态只对应一个已由用户确认的目标。
    virtual void onDownloadUpdated(std::uint64_t requestId,
                                   BrowserDownloadState state,
                                   std::int64_t receivedBytes,
                                   std::int64_t totalBytes) = 0;
    virtual void onTabDownloadUpdated(std::uint64_t tabId,
                                      std::uint64_t requestId,
                                      BrowserDownloadState state,
                                      std::int64_t receivedBytes,
                                      std::int64_t totalBytes) {
        Q_UNUSED(tabId);
        onDownloadUpdated(requestId, state, receivedBytes, totalBytes);
    }
    // 调用线程：GUI 主线程。通知当前代次的网页资料已经清除。
    virtual void onBrowsingDataCleared(std::uint64_t generation) = 0;
    // 调用线程：GUI 主线程。通知弹窗因数量或关闭状态被拒绝。
    virtual void onPopupRejected() = 0;

    // 调用线程：GUI 主线程。返回 true 表示宿主已把新窗口请求转换为网页选项卡；
    // 返回 false 时后端拒绝该请求，不创建原生窗口。
    virtual bool onNewTabRequested(std::uint64_t newWindowRequestId,
                                   const QString& url) {
        Q_UNUSED(newWindowRequestId);
        Q_UNUSED(url);
        return false;
    }

    // 调用线程：GUI 主线程。标签事件的默认实现兼容首个网页标签。
    virtual void onTabReady(std::uint64_t tabId, std::uint64_t generation) {
        Q_UNUSED(tabId);
        onBrowserReady(generation);
    }
    virtual void onTabNavigationStarted(std::uint64_t tabId,
                                        std::uint64_t generation) {
        Q_UNUSED(tabId);
        onNavigationStarted(generation);
    }
    virtual void onTabNavigationCompleted(std::uint64_t tabId,
                                          std::uint64_t generation,
                                          const QString& visibleUrl,
                                          const QString& title,
                                          bool canGoBack,
                                          bool canGoForward) {
        Q_UNUSED(tabId);
        onNavigationCompleted(generation, visibleUrl, title, canGoBack,
                              canGoForward);
    }
    virtual void onTabDocumentStateChanged(std::uint64_t tabId,
                                           std::uint64_t generation,
                                           const QString& visibleUrl,
                                           const QString& title,
                                           bool canGoBack,
                                           bool canGoForward) {
        onTabNavigationCompleted(tabId, generation, visibleUrl, title,
                                 canGoBack, canGoForward);
    }
    virtual void onTabNavigationStopped(std::uint64_t tabId,
                                        std::uint64_t generation,
                                        const QString& visibleUrl,
                                        const QString& title,
                                        bool canGoBack,
                                        bool canGoForward) {
        Q_UNUSED(tabId);
        onNavigationStopped(generation, visibleUrl, title, canGoBack,
                            canGoForward);
    }
    virtual void onTabError(std::uint64_t tabId, std::uint64_t generation,
                            BrowserErrorKind kind, long errorCode) {
        Q_UNUSED(tabId);
        onBrowserError(generation, kind, errorCode);
    }
    // 调用线程：GUI 主线程。事件不携带 COM 参数、网页地址或进程敏感信息。
    virtual void onTabProcessFailed(std::uint64_t tabId,
                                    std::uint64_t generation,
                                    BrowserProcessFailureKind kind) {
        Q_UNUSED(tabId);
        Q_UNUSED(generation);
        Q_UNUSED(kind);
    }
    // 调用线程：GUI 主线程。状态表示网页文档是否正在播放音频，与静音设置无关。
    virtual void onTabAudioStateChanged(std::uint64_t tabId,
                                        std::uint64_t generation,
                                        bool isPlayingAudio) {
        Q_UNUSED(tabId);
        Q_UNUSED(generation);
        Q_UNUSED(isPlayingAudio);
    }
    // 调用线程：GUI 主线程。pngBytes 为空表示清除旧图标并显示通用图标。
    virtual void onTabFaviconChanged(std::uint64_t tabId,
                                     std::uint64_t generation,
                                     const QByteArray& pngBytes) {
        Q_UNUSED(tabId);
        Q_UNUSED(generation);
        Q_UNUSED(pngBytes);
    }
    // 调用线程：GUI 主线程。比例已经限制在 0.25 至 5.0。
    virtual void onTabZoomFactorChanged(std::uint64_t tabId,
                                        std::uint64_t generation,
                                        double zoomFactor) {
        Q_UNUSED(tabId);
        Q_UNUSED(generation);
        Q_UNUSED(zoomFactor);
    }
    virtual void onTabCloseRequested(std::uint64_t tabId) { Q_UNUSED(tabId); }
};

}  // namespace mediahub::gui
