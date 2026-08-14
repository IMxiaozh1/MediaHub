#pragma once

#include <QWidget>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QVector>

#include <cstdint>
#include <optional>

#include "browser_event_listener.h"
#include "browser_data_store.h"
#include "browser_favicon_cache.h"
#include "browser_tab_group_model.h"

class QDialog;
class QFrame;
class QLabel;
class QKeyEvent;
class QLineEdit;
class QListWidget;
class QPoint;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QStackedLayout;
class QToolButton;
class QTabBar;
class QTimer;
class QWidget;

namespace mediahub::gui {

class BrowserBackend;
class BrowserDataStore;
class BrowserDownloadWidget;
class BrowserDownloadCenter;
class BrowserPermissionDialog;
class BrowserPermissionManagementDialog;
class BrowserPermissionStore;
class BrowserSessionStore;
class BrowserStartupSettingsDialog;
class BrowserStartupSettingsStore;
class BrowserTabGroupDialog;

// 网页页面维护独立导航状态，并把所有浏览器命令路由到可注入后端。
class BrowserPage final : public QWidget, public BrowserEventListener {
    Q_OBJECT

 public:
    // 调用线程：GUI 主线程。backend 生命周期必须覆盖本页面。
    BrowserPage(BrowserBackend& backend, QString userDataDirectory,
                QWidget* parent = nullptr,
                BrowserDataStore* dataStore = nullptr,
                BrowserSessionStore* sessionStore = nullptr,
                BrowserStartupSettingsStore* startupSettingsStore = nullptr,
                BrowserPermissionStore* permissionStore = nullptr);
    // 调用线程：GUI 主线程。析构会先断开监听再关闭后端。
    ~BrowserPage() override;

    BrowserPage(const BrowserPage&) = delete;
    BrowserPage& operator=(const BrowserPage&) = delete;

    // 调用线程：GUI 主线程。显示当前网页 Controller，不改变声音或挂起状态。
    void activate();
    // 调用线程：GUI 主线程。只隐藏网页 Controller，后台媒体继续运行。
    void deactivate();
    // 调用线程：GUI 主线程。可重复调用，不等待浏览器子进程。
    void shutdown() noexcept;
    [[nodiscard]] BrowserPageState state() const noexcept;
    [[nodiscard]] bool isWebFullScreen() const noexcept;
    // 调用线程：GUI 主线程。等待路径、下载中、取消中和可重试失败都属于活动任务。
    [[nodiscard]] int activeDownloadCount() const noexcept;
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
    void onDocumentStateChanged(std::uint64_t generation,
                                const QString& visibleUrl,
                                const QString& title,
                                bool canGoBack,
                                bool canGoForward) override;
    void onNavigationStopped(std::uint64_t generation,
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
    void onTabDownloadRequested(std::uint64_t tabId, std::uint64_t requestId,
                                const QString& origin,
                                const QString& suggestedFileName,
                                std::int64_t totalBytes) override;
    void onTabDownloadUpdated(std::uint64_t tabId, std::uint64_t requestId,
                              BrowserDownloadState state,
                              std::int64_t receivedBytes,
                              std::int64_t totalBytes) override;
    void onBrowsingDataCleared(std::uint64_t generation) override;
    void onPopupRejected() override;
    bool onNewTabRequested(std::uint64_t newWindowRequestId,
                           const QString& url) override;
    void onTabReady(std::uint64_t tabId, std::uint64_t generation) override;
    void onTabNavigationStarted(std::uint64_t tabId,
                                std::uint64_t generation) override;
    void onTabNavigationCompleted(std::uint64_t tabId,
                                  std::uint64_t generation,
                                  const QString& visibleUrl,
                                  const QString& title,
                                  bool canGoBack,
                                  bool canGoForward) override;
    void onTabDocumentStateChanged(std::uint64_t tabId,
                                   std::uint64_t generation,
                                   const QString& visibleUrl,
                                   const QString& title,
                                   bool canGoBack,
                                   bool canGoForward) override;
    void onTabNavigationStopped(std::uint64_t tabId,
                                std::uint64_t generation,
                                const QString& visibleUrl,
                                const QString& title,
                                bool canGoBack,
                                bool canGoForward) override;
    void onTabError(std::uint64_t tabId, std::uint64_t generation,
                    BrowserErrorKind kind, long errorCode) override;
    void onTabProcessFailed(std::uint64_t tabId, std::uint64_t generation,
                            BrowserProcessFailureKind kind) override;
    void onTabCloseRequested(std::uint64_t tabId) override;
    void onTabAudioStateChanged(std::uint64_t tabId,
                                 std::uint64_t generation,
                                 bool isPlayingAudio) override;
    void onTabFaviconChanged(std::uint64_t tabId, std::uint64_t generation,
                             const QByteArray& pngBytes) override;
    void onTabZoomFactorChanged(std::uint64_t tabId,
                                std::uint64_t generation,
                                double zoomFactor) override;
    void onFindResultChanged(std::uint64_t tabId, std::uint64_t generation,
                             int activeMatchIndex, int matchCount) override;
    void onFindFailed(std::uint64_t tabId, std::uint64_t generation,
                      long errorCode) override;

 signals:
    void fullScreenChanged(bool isFullScreen);
    // 网页模式隐藏时也会投递，供主窗口提示后台网页声音来源。
    void audibleTabCountChanged(int count);

 protected:
    // 调用线程：GUI 主线程。查找框优先消费 Esc，避免主窗口先退出网页全屏。
    bool eventFilter(QObject* watched, QEvent* event) override;
    // 调用线程：GUI 主线程。网页全屏时 Esc 只请求退出网页全屏。
    void keyPressEvent(QKeyEvent* event) override;
    // 调用线程：GUI 主线程。首次显示并取得原生句柄后才异步初始化后端。
    void showEvent(QShowEvent* event) override;
    // 调用线程：GUI 主线程。只提交最新有效的物理像素客户区。
    void resizeEvent(QResizeEvent* event) override;

 private slots:
    // 调用线程：GUI 主线程。标签栏关闭请求只关闭对应网页控制器。
    void closeTab(int index);
    // 调用线程：GUI 主线程。完整解析导入内容后才显示确认窗口。
    void prepareFavoriteImport(QByteArray html);
    // 调用线程：GUI 主线程。使用原子替换写入指定收藏 HTML 文件。
    void exportFavoritesToFile(QString filePath);
    // 调用线程：GUI 主线程。把当前可见收藏顺序一次性持久化。
    void persistFavoriteListOrder();
    // 调用线程：GUI 主线程。只更新标签分组元数据，不触碰网页 Controller。
    void moveTabToGroup(std::uint64_t tabId, const QString& groupId);
    [[nodiscard]] bool isTabCollapsedForTest(std::uint64_t tabId) const;

 private:
    void buildUi();
    [[nodiscard]] int maximumTabCount() const noexcept;
    void openNewTab();
    void closeCurrentTab();
    void reopenClosedTab();
    void cycleTab(int step);
    void submitAddress();
    void navigateTo(const QString& normalizedUrl);
    void showClearDataConfirmation();
    void confirmClearBrowsingData();
    void showHost();
    void showError(BrowserErrorKind kind);
    void showTabProcessFailure();
    void recoverFailedTab();
    void refreshRecoveryCooldown(std::uint64_t tabId);
    [[nodiscard]] qint64 recoveryCooldownMilliseconds() const noexcept;
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
    void updateRecordedNavigationTitle(const QString& visibleUrl,
                                       const QString& title);
    void applyTabDocumentState(int index, const QString& visibleUrl,
                               const QString& title, bool canGoBack,
                               bool canGoForward, bool didFinishNavigation,
                               bool shouldRecordHistory);
    void showHistory();
    void showFavorites();
    void refreshHistoryList();
    void refreshFavoritesList();
    void removeSelectedHistoryEntry();
    void showHistoryClearConfirmation();
    void confirmClearHistory();
    void openStoredUrl(const QString& url, bool isNewTab);
    void showFavoriteEditor(int favoriteIndex = -1);
    void saveFavoriteEditor();
    void removeSelectedFavorite();
    void chooseFavoriteImportFile();
    void confirmFavoriteImport();
    void chooseFavoriteExportFile();
    void setFavoriteTransferStatus(const QString& status);
    // 调用线程：GUI 主线程。切换标签前退出旧网页全屏并恢复宿主界面。
    void leaveWebFullScreenForTabChange();
    void activateTab(int index);
    void updateTabPresentation();
    void applyCachedFavicon(int index);
    void clearTabFavicons();
    void updateAudioPresentation();
    void updateAudibleTabCount();
    void toggleCurrentTabMuted();
    void toggleTabMuted(std::uint64_t tabId);
    void showAudioTabs();
    void refreshAudioTabs();
    void setCurrentTabZoom(double zoomFactor);
    void adjustCurrentTabZoom(double delta);
    void resetCurrentTabZoom();
    void showStartupSettings();
    void showPermissionSettings();
    void openConfiguredHome();
    void openInitialTabs();
    void openInitialTab(const QString& url, const QString& title = {},
                        bool isMuted = false, const QString& groupId = {},
                        bool isPinned = false, double zoomFactor = 1.0);
    [[nodiscard]] bool openRestoredTab(const QString& url,
                                       const QString& title,
                                       bool isMuted,
                                       const QString& groupId,
                                       bool isPinned,
                                       double zoomFactor);
    void saveSession();
    void showFindBar();
    void closeFindBar(bool clearSelection = true);
    void findNext(bool forward);
    void showTabSearch();
    void refreshTabSearch();
    void activateSelectedSearchTab();
    void showTabContextMenu(const QPoint& position);
    void showTabGroups();
    void removeGroupFromTabs(const QString& groupId);
    void updateTabGroupPresentation();
    void setTabPinned(std::uint64_t tabId, bool isPinned);
    void normalizePinnedTabOrder();
    void updateTabCloseButtons();
    void showPinnedCloseConfirmation(std::uint64_t tabId);
    void closeTabInternal(int index, bool isPinnedCloseConfirmed);
    void updateFindResult(int activeMatchIndex, int matchCount);
    [[nodiscard]] int audibleTabCount() const noexcept;
    [[nodiscard]] int findTabIndex(std::uint64_t tabId) const noexcept;
    [[nodiscard]] QString errorText(BrowserErrorKind kind) const;

    BrowserBackend& backend_;
    BrowserDataStore* dataStore_{nullptr};
    BrowserSessionStore* sessionStore_{nullptr};
    BrowserStartupSettingsStore* startupSettingsStore_{nullptr};
    BrowserPermissionStore* permissionStore_{nullptr};
    BrowserFaviconCache faviconCache_;
    QString userDataDirectory_;
    std::uint64_t generation_{1};
    BrowserPageState state_{BrowserPageState::Unavailable};
    bool isInitialized_{false};
    bool hasOpenedInitialHome_{false};
    bool isShuttingDown_{false};
    bool isWebFullScreen_{false};
    bool wasToolbarHidden_{false};
    bool wasInformationRowHidden_{false};
    bool wasDownloadWidgetHidden_{true};
    QFrame* toolbar_{nullptr};
    QTabBar* tabBar_{nullptr};
    QToolButton* newTabButton_{nullptr};
    QToolButton* tabSearchButton_{nullptr};
    QToolButton* tabGroupButton_{nullptr};
    QDialog* tabSearchDialog_{nullptr};
    QLineEdit* tabSearchEdit_{nullptr};
    QListWidget* tabSearchList_{nullptr};
    QPushButton* tabSearchSwitchButton_{nullptr};
    QDialog* pinnedCloseDialog_{nullptr};
    std::optional<std::uint64_t> pendingPinnedCloseTabId_;
    QWidget* findBar_{nullptr};
    QLineEdit* findEdit_{nullptr};
    QLabel* findResultLabel_{nullptr};
    QPushButton* findPreviousButton_{nullptr};
    QPushButton* findNextButton_{nullptr};
    QToolButton* findCloseButton_{nullptr};
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
    QToolButton* historyButton_{nullptr};
    QToolButton* favoritesButton_{nullptr};
    QToolButton* startupSettingsButton_{nullptr};
    QToolButton* permissionSettingsButton_{nullptr};
    QToolButton* currentTabMuteButton_{nullptr};
    QToolButton* audioTabsButton_{nullptr};
    QToolButton* zoomOutButton_{nullptr};
    QToolButton* zoomResetButton_{nullptr};
    QToolButton* zoomInButton_{nullptr};
    QLabel* titleLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* errorLabel_{nullptr};
    QWidget* processFailurePage_{nullptr};
    QLabel* processFailureTitleLabel_{nullptr};
    QLabel* processFailureDetailLabel_{nullptr};
    QPushButton* processRecoveryButton_{nullptr};
    QDialog* clearDataDialog_{nullptr};
    QDialog* historyDialog_{nullptr};
    QLineEdit* historySearchEdit_{nullptr};
    QListWidget* historyList_{nullptr};
    QDialog* historyClearDialog_{nullptr};
    QDialog* favoritesDialog_{nullptr};
    QLineEdit* favoritesSearchEdit_{nullptr};
    QListWidget* favoritesList_{nullptr};
    QLabel* favoriteTransferStatusLabel_{nullptr};
    QDialog* favoriteEditorDialog_{nullptr};
    QLineEdit* favoriteTitleEdit_{nullptr};
    QLineEdit* favoriteUrlEdit_{nullptr};
    QLineEdit* favoriteNoteEdit_{nullptr};
    int editingFavoriteIndex_{-1};
    QDialog* favoriteImportDialog_{nullptr};
    QLabel* favoriteImportSummaryLabel_{nullptr};
    QVector<BrowserFavoriteEntry> pendingImportedFavorites_;
    QDialog* audioTabsDialog_{nullptr};
    QListWidget* audioTabsList_{nullptr};
    QPushButton* audioTabSwitchButton_{nullptr};
    QPushButton* audioTabMuteButton_{nullptr};
    QPushButton* audioTabCloseButton_{nullptr};
    QPushButton* globalAudioMuteButton_{nullptr};
    BrowserStartupSettingsDialog* startupSettingsDialog_{nullptr};
    BrowserPermissionManagementDialog* permissionSettingsDialog_{nullptr};
    BrowserTabGroupDialog* tabGroupDialog_{nullptr};
    BrowserTabGroupModel tabGroupModel_;
    BrowserPermissionDialog* permissionDialog_{nullptr};
    std::optional<std::uint64_t> pendingPermissionId_;
    BrowserPermissionKind pendingPermissionKind_{BrowserPermissionKind::Other};
    QString pendingPermissionOrigin_;
    QDialog* externalProtocolDialog_{nullptr};
    QDialog* certificateDialog_{nullptr};
    BrowserDownloadWidget* downloadWidget_{nullptr};
    BrowserDownloadCenter* downloadCenter_{nullptr};
    QTimer* sessionCheckpointTimer_{nullptr};
    std::optional<std::uint64_t> pendingExternalProtocolId_;
    std::optional<std::uint64_t> pendingCertificateId_;
    std::optional<std::uint64_t> activeDownloadId_;
    bool isDownloadCancellationSent_{false};
    bool isGloballyMuted_{false};
    int lastAudibleTabCount_{0};
    QString responsiveSize_;
    bool isNormalizingPinnedTabs_{false};
    QHash<std::uint64_t, std::uint64_t> downloadTabIds_;

    struct BrowserTabRecord {
        std::uint64_t tabId{0};
        std::uint64_t generation{0};
        QString address;
        QString title;
        bool canGoBack{false};
        bool canGoForward{false};
        BrowserPageState state{BrowserPageState::Unavailable};
        std::optional<BrowserErrorKind> lastError;
        std::optional<BrowserProcessFailureKind> processFailure;
        bool isUserMuted{false};
        bool isPlayingAudio{false};
        QString groupId;
        bool isPinned{false};
        double zoomFactor{1.0};
        int recoveryAttempts{0};
        QDateTime lastRecoveryAt;
    };
    QVector<BrowserTabRecord> tabs_;
    struct ClosedTabRecord {
        QString address;
        QString title;
        QString groupId;
        bool isPinned{false};
        bool isUserMuted{false};
        double zoomFactor{1.0};
    };
    QVector<ClosedTabRecord> closedTabs_;
    int currentTabIndex_{0};
    std::uint64_t nextTabId_{2};
    std::uint64_t findTabId_{0};
    std::uint64_t findTabGeneration_{0};
    bool hasBrowserProcessExited_{false};
};

}  // namespace mediahub::gui
