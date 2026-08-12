#pragma once

#include <QWidget>

#include <cstdint>
#include <optional>

#include "browser_event_listener.h"

class QDialog;
class QFrame;
class QLabel;
class QKeyEvent;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QStackedLayout;
class QToolButton;

namespace mediahub::gui {

class BrowserBackend;
class BrowserDataStore;
class BrowserDownloadWidget;
class BrowserPermissionDialog;

// 网页页面维护独立导航状态，并把所有浏览器命令路由到可注入后端。
class BrowserPage final : public QWidget, public BrowserEventListener {
    Q_OBJECT

 public:
    // 调用线程：GUI 主线程。backend 生命周期必须覆盖本页面。
    BrowserPage(BrowserBackend& backend, QString userDataDirectory,
                QWidget* parent = nullptr,
                BrowserDataStore* dataStore = nullptr);
    // 调用线程：GUI 主线程。析构会先断开监听再关闭后端。
    ~BrowserPage() override;

    BrowserPage(const BrowserPage&) = delete;
    BrowserPage& operator=(const BrowserPage&) = delete;

    // 调用线程：GUI 主线程。显示浏览器并恢复未自动播放的页面环境。
    void activate();
    // 调用线程：GUI 主线程。隐藏前同时静音和挂起网页。
    void deactivate();
    // 调用线程：GUI 主线程。可重复调用，不等待浏览器子进程。
    void shutdown() noexcept;
    [[nodiscard]] BrowserPageState state() const noexcept;
    [[nodiscard]] bool isWebFullScreen() const noexcept;
    // 调用线程：GUI 主线程。请求当前网页退出全屏。
    void exitWebFullScreen();

    void onBrowserReady(std::uint64_t generation) override;
    void onBrowserError(std::uint64_t generation, BrowserErrorKind kind,
                        long errorCode) override;
    void onNavigationStarted(std::uint64_t generation) override;
    void onNavigationCompleted(std::uint64_t generation,
                               const QString& visibleUrl,
                               const QString& title,
                               bool canGoBack,
                               bool canGoForward) override;
    void onFullScreenChanged(std::uint64_t generation,
                             bool isFullScreen) override;
    void onAcceleratorRequested(std::uint64_t generation,
                                BrowserAccelerator accelerator) override;
    void onPermissionRequested(std::uint64_t requestId,
                               const QString& origin,
                               BrowserPermissionKind kind) override;
    void onExternalProtocolRequested(std::uint64_t requestId,
                                     const QString& origin,
                                     const QString& target) override;
    void onCertificateErrorRequested(std::uint64_t requestId,
                                     const QString& origin,
                                     const QString& errorDescription) override;
    void onDownloadRequested(std::uint64_t requestId,
                             const QString& origin,
                             const QString& suggestedFileName,
                             std::int64_t totalBytes) override;
    void onDownloadUpdated(std::uint64_t requestId,
                           BrowserDownloadState state,
                           std::int64_t receivedBytes,
                           std::int64_t totalBytes) override;
    void onBrowsingDataCleared(std::uint64_t generation) override;
    void onPopupRejected() override;

 signals:
    void fullScreenChanged(bool isFullScreen);

 protected:
    // 调用线程：GUI 主线程。网页全屏时 Esc 只请求退出网页全屏。
    void keyPressEvent(QKeyEvent* event) override;
    // 调用线程：GUI 主线程。首次显示并取得原生句柄后才异步初始化后端。
    void showEvent(QShowEvent* event) override;
    // 调用线程：GUI 主线程。只提交最新有效的物理像素客户区。
    void resizeEvent(QResizeEvent* event) override;

 private:
    void buildUi();
    void submitAddress();
    void navigateTo(const QString& normalizedUrl);
    void showClearDataConfirmation();
    void confirmClearBrowsingData();
    void showHost();
    void showError(BrowserErrorKind kind);
    // 调用线程：GUI 主线程。只完成仍存活且 requestId 匹配的权限请求。
    void resolvePermission(std::uint64_t requestId,
                           BrowserPermissionDecision decision);
    // 调用线程：GUI 主线程。默认拒绝被关闭、超时或已被替换的外部协议请求。
    void resolveExternalProtocol(std::uint64_t requestId, bool isAllowed);
    // 调用线程：GUI 主线程。证书例外只完成当前来源对应的存活请求。
    void resolveCertificateError(std::uint64_t requestId,
                                 BrowserCertificateDecision decision);
    // 调用线程：GUI 主线程。导航前拒绝仍在等待用户回答的旧页面请求。
    void rejectUnansweredSensitiveRequests();
    void updateControls();
    // 调用线程：GUI 主线程。仅在响应式档位变化时刷新网页工具栏样式。
    void updateResponsiveStyle();
    void updateBackendBounds();
    void recordSuccessfulNavigation(const QString& visibleUrl,
                                    const QString& title);
    [[nodiscard]] QString errorText(BrowserErrorKind kind) const;

    BrowserBackend& backend_;
    BrowserDataStore* dataStore_{nullptr};
    QString userDataDirectory_;
    std::uint64_t generation_{1};
    BrowserPageState state_{BrowserPageState::Unavailable};
    bool isInitialized_{false};
    bool isShuttingDown_{false};
    bool isWebFullScreen_{false};
    bool wasToolbarHidden_{false};
    bool wasInformationRowHidden_{false};
    bool wasDownloadWidgetHidden_{true};
    QFrame* toolbar_{nullptr};
    QWidget* informationRow_{nullptr};
    QWidget* browserHost_{nullptr};
    QStackedLayout* contentStack_{nullptr};
    QToolButton* backButton_{nullptr};
    QToolButton* forwardButton_{nullptr};
    QToolButton* reloadButton_{nullptr};
    QToolButton* homeButton_{nullptr};
    QLineEdit* addressEdit_{nullptr};
    QPushButton* goButton_{nullptr};
    QPushButton* clearDataButton_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* errorLabel_{nullptr};
    QDialog* clearDataDialog_{nullptr};
    BrowserPermissionDialog* permissionDialog_{nullptr};
    std::optional<std::uint64_t> pendingPermissionId_;
    BrowserPermissionKind pendingPermissionKind_{BrowserPermissionKind::Other};
    QDialog* externalProtocolDialog_{nullptr};
    QDialog* certificateDialog_{nullptr};
    BrowserDownloadWidget* downloadWidget_{nullptr};
    std::optional<std::uint64_t> pendingExternalProtocolId_;
    std::optional<std::uint64_t> pendingCertificateId_;
    std::optional<std::uint64_t> activeDownloadId_;
    bool isDownloadCancellationSent_{false};
    QString responsiveSize_;
};

}  // namespace mediahub::gui
