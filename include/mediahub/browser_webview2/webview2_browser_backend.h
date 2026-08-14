#pragma once

#include <QRect>
#include <QString>

#include <cstdint>
#include <memory>

#include "browser_backend.h"

namespace mediahub::logging {
class Logger;
}

namespace mediahub::browser_webview2 {

// 使用专用持久 Profile 承载 WebView2，公开边界不暴露 COM 或 Windows 类型。
class WebView2BrowserBackend final : public gui::BrowserBackend {
 public:
    explicit WebView2BrowserBackend(logging::Logger* logger = nullptr);
    ~WebView2BrowserBackend() override;

    WebView2BrowserBackend(const WebView2BrowserBackend&) = delete;
    WebView2BrowserBackend& operator=(const WebView2BrowserBackend&) = delete;

    // 调用线程：GUI 主线程。
    void setEventListener(gui::BrowserEventListener* listener) override;
    // 调用线程：GUI 主线程。异步创建环境和控制器，不等待浏览器子进程。
    void initialize(void* parentWindowHandle, const QString& userDataDirectory,
                    std::uint64_t generation) override;
    // 调用线程：GUI 主线程。
    void navigate(const QString& normalizedUrl, std::uint64_t generation) override;
    [[nodiscard]] bool createTab(void* parentWindowHandle,
                                 std::uint64_t tabId,
                                 const QString& initialUrl,
                                 std::uint64_t generation,
                                 std::uint64_t newWindowRequestId = 0) override;
    void closeTab(std::uint64_t tabId) override;
    void activateTab(std::uint64_t tabId) override;
    // 调用线程：GUI 主线程。
    void goBack() override;
    // 调用线程：GUI 主线程。
    void goForward() override;
    // 调用线程：GUI 主线程。
    void reloadOrStop() override;
    // 调用线程：GUI 主线程。渲染进程失败后重新加载指定标签并报告同步提交结果。
    [[nodiscard]] bool recoverTab(std::uint64_t tabId,
                                  std::uint64_t generation) override;
    // 调用线程：GUI 主线程。使用 WebView2 原生 Find API 查找当前标签。
    void findInPage(const QString& text, bool forward) override;
    // 调用线程：GUI 主线程。
    void stopFinding(bool clearSelection) override;
    // 调用线程：GUI 主线程。
    void setBounds(const QRect& pixelBounds) override;
    // 调用线程：GUI 主线程。
    void setVisible(bool isVisible) override;
    // 调用线程：GUI 主线程。
    void setAudioMuted(bool isMuted) override;
    // 调用线程：GUI 主线程。只修改指定标签的独立静音状态。
    void setTabAudioMuted(std::uint64_t tabId, bool isMuted) override;
    // 调用线程：GUI 主线程。缩放属于指定标签，并限制在 25% 至 500%。
    void setTabZoomFactor(std::uint64_t tabId, double zoomFactor) override;
    // 调用线程：GUI 主线程。
    void setSuspended(bool isSuspended) override;
    // 调用线程：GUI 主线程。
    void clearBrowsingData(std::uint64_t generation) override;
    // 调用线程：GUI 主线程。只完成当前仍有效的宿主权限决定。
    void answerPermission(std::uint64_t requestId,
                          gui::BrowserPermissionDecision decision) override;
    // 调用线程：GUI 主线程。目标已由宿主完成安全校验。
    void chooseDownloadPath(std::uint64_t requestId,
                            const QString& destination) override;
    // 调用线程：GUI 主线程。
    void cancelDownload(std::uint64_t requestId) override;
    // 调用线程：GUI 主线程。仅恢复 WebView2 明确允许继续的中断下载。
    void retryDownload(std::uint64_t requestId) override;
    [[nodiscard]] bool supportsConcurrentDownloads() const noexcept override {
        return true;
    }
    // 调用线程：GUI 主线程。只允许用户明确确认的当前外部协议请求。
    void answerExternalProtocol(std::uint64_t requestId, bool isAllowed) override;
    // 调用线程：GUI 主线程。证书例外只应用于当前 WebView2 会话。
    void answerCertificateError(
        std::uint64_t requestId,
        gui::BrowserCertificateDecision decision) override;
    // 调用线程：GUI 主线程。
    void exitFullScreen() override;
    // 调用线程：GUI 主线程。不等待内核线程或浏览器子进程。
    void shutdown() noexcept override;

 private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mediahub::browser_webview2
