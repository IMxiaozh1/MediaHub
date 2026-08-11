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
    // 调用线程：GUI 主线程。
    void goBack() override;
    // 调用线程：GUI 主线程。
    void goForward() override;
    // 调用线程：GUI 主线程。
    void reloadOrStop() override;
    // 调用线程：GUI 主线程。
    void setBounds(const QRect& pixelBounds) override;
    // 调用线程：GUI 主线程。
    void setVisible(bool isVisible) override;
    // 调用线程：GUI 主线程。
    void setAudioMuted(bool isMuted) override;
    // 调用线程：GUI 主线程。
    void setSuspended(bool isSuspended) override;
    // 调用线程：GUI 主线程。
    void clearBrowsingData(std::uint64_t generation) override;
    // 调用线程：GUI 主线程。安全决定在后续阶段接入，此阶段不自动允许。
    void answerPermission(std::uint64_t requestId,
                          gui::BrowserPermissionDecision decision) override;
    // 调用线程：GUI 主线程。下载决定在后续阶段接入，此阶段不开始下载。
    void chooseDownloadPath(std::uint64_t requestId,
                            const QString& destination) override;
    // 调用线程：GUI 主线程。
    void cancelDownload(std::uint64_t requestId) override;
    // 调用线程：GUI 主线程。外部协议在后续阶段接入，此阶段不启动应用。
    void answerExternalProtocol(std::uint64_t requestId, bool isAllowed) override;
    // 调用线程：GUI 主线程。证书例外在后续阶段接入，此阶段不放行。
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
