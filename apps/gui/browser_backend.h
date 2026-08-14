#pragma once

#include <QRect>
#include <QString>

#include <cstdint>

#include "browser_types.h"

namespace mediahub::gui {

class BrowserEventListener;

// 网页后端抽象隔离 Qt 界面与具体 WebView2/COM 实现。
class BrowserBackend {
 public:
    virtual ~BrowserBackend() = default;

    // 调用线程：GUI 主线程。listener 的生命周期由调用方管理，可传空停止事件投递。
    virtual void setEventListener(BrowserEventListener* listener) = 0;
    // 调用线程：GUI 主线程。异步创建浏览器，不得等待 WebView2 子进程。
    virtual void initialize(void* parentWindowHandle,
                            const QString& userDataDirectory,
                            std::uint64_t generation) = 0;
    // 调用线程：GUI 主线程。url 已通过顶层地址策略规范化。
    virtual void navigate(const QString& normalizedUrl,
                          std::uint64_t generation) = 0;
    // 调用线程：GUI 主线程。创建与现有 Profile 共享的新网页选项卡。
    [[nodiscard]] virtual bool createTab(void* parentWindowHandle,
                                         std::uint64_t tabId,
                                         const QString& initialUrl,
                                         std::uint64_t generation,
                                         std::uint64_t newWindowRequestId = 0) {
        Q_UNUSED(parentWindowHandle);
        Q_UNUSED(tabId);
        Q_UNUSED(initialUrl);
        Q_UNUSED(generation);
        Q_UNUSED(newWindowRequestId);
        return false;
    }
    // 调用线程：GUI 主线程。关闭一个标签的 WebView Controller。
    virtual void closeTab(std::uint64_t tabId) { Q_UNUSED(tabId); }
    // 调用线程：GUI 主线程。切换当前可见 Controller。
    virtual void activateTab(std::uint64_t tabId) { Q_UNUSED(tabId); }
    // 调用线程：GUI 主线程。
    virtual void goBack() = 0;
    // 调用线程：GUI 主线程。
    virtual void goForward() = 0;
    // 调用线程：GUI 主线程。载入中停止，否则刷新。
    virtual void reloadOrStop() = 0;
    // 调用线程：GUI 主线程。返回恢复请求是否已成功提交；失败时界面保留恢复入口。
    [[nodiscard]] virtual bool recoverTab(std::uint64_t tabId,
                                          std::uint64_t generation) {
        Q_UNUSED(tabId);
        Q_UNUSED(generation);
        reloadOrStop();
        return true;
    }
    // 调用线程：GUI 主线程。只在当前标签中查找，不记录查询文本。
    virtual void findInPage(const QString& text, bool forward) {
        Q_UNUSED(text);
        Q_UNUSED(forward);
    }
    // 调用线程：GUI 主线程。clearSelection 表示关闭查找条时清除网页中的匹配选择。
    virtual void stopFinding(bool clearSelection) { Q_UNUSED(clearSelection); }
    // 调用线程：GUI 主线程。坐标为宿主控件的物理像素客户区。
    virtual void setBounds(const QRect& pixelBounds) = 0;
    // 调用线程：GUI 主线程。
    virtual void setVisible(bool isVisible) = 0;
    // 调用线程：GUI 主线程。
    virtual void setAudioMuted(bool isMuted) = 0;
    // 调用线程：GUI 主线程。标签静音与全局静音叠加，切换标签不得改变此状态。
    virtual void setTabAudioMuted(std::uint64_t tabId, bool isMuted) = 0;
    // 调用线程：GUI 主线程。缩放属于指定标签，后端会限制在 25% 至 500%。
    virtual void setTabZoomFactor(std::uint64_t tabId, double zoomFactor) = 0;
    // 调用线程：GUI 主线程。挂起失败时仍必须维持静音。
    virtual void setSuspended(bool isSuspended) = 0;
    // 调用线程：GUI 主线程。异步清除专用 Profile 中的网站数据。
    virtual void clearBrowsingData(std::uint64_t generation) = 0;
    // 调用线程：GUI 主线程。每个 requestId 只能回答一次。
    virtual void answerPermission(std::uint64_t requestId,
                                  BrowserPermissionDecision decision) = 0;
    // 调用线程：GUI 主线程。目标必须由用户通过系统保存窗口选择。
    virtual void chooseDownloadPath(std::uint64_t requestId,
                                    const QString& destination) = 0;
    // 调用线程：GUI 主线程。
    virtual void cancelDownload(std::uint64_t requestId) = 0;
    // 调用线程：GUI 主线程。仅恢复后端明确标记为可恢复中断的下载。
    virtual void retryDownload(std::uint64_t requestId) {
        Q_UNUSED(requestId);
    }
    // 调用线程：GUI 主线程。真实后端支持多个独立下载任务时返回 true。
    [[nodiscard]] virtual bool supportsConcurrentDownloads() const noexcept {
        return false;
    }
    // 调用线程：GUI 主线程。外部协议只有用户确认后才允许。
    virtual void answerExternalProtocol(std::uint64_t requestId, bool isAllowed) = 0;
    // 调用线程：GUI 主线程。证书继续决定只作用于当前会话。
    virtual void answerCertificateError(std::uint64_t requestId,
                                        BrowserCertificateDecision decision) = 0;
    // 调用线程：GUI 主线程。
    virtual void exitFullScreen() = 0;
    // 调用线程：GUI 主线程。必须丢弃迟到回调并确定性释放所有控制器。
    virtual void shutdown() noexcept = 0;
};

}  // namespace mediahub::gui
