#pragma once

#include <QFrame>
#include <QHash>
#include <QSet>
#include <QTabBar>

class QAction;
class QLineEdit;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QToolButton;

namespace mediahub::gui {

// 提供折叠分组、标签声音状态和稳定关闭按钮区域的浏览器标签栏。
class BrowserTabBar final : public QTabBar {
    Q_OBJECT

 public:
    explicit BrowserTabBar(QWidget* parent = nullptr);

    void setCollapsedTabIds(const QSet<qulonglong>& tabIds);
    [[nodiscard]] bool isTabCollapsed(qulonglong tabId) const;
    void setTabAudioState(qulonglong tabId, bool isPlayingAudio, bool isMuted);
    void refreshCloseButtonVisibility();

 signals:
    void tabAudioToggleRequested(qulonglong tabId);

 protected:
    [[nodiscard]] QSize tabSizeHint(int index) const override;
    [[nodiscard]] QSize minimumTabSizeHint(int index) const override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void tabInserted(int index) override;
    void tabRemoved(int index) override;

 private:
    struct AudioState {
        bool isPlayingAudio{false};
        bool isMuted{false};
    };

    [[nodiscard]] QRect audioIconRect(int index) const;
    void updateHoveredTab(const QPoint& position);

    QSet<qulonglong> collapsedTabIds_;
    QHash<qulonglong, AudioState> audioStates_;
    int hoveredTab_{-1};
};

// 构建固定两层的标签栏和导航栏，只暴露稳定控件与语义菜单入口。
class BrowserChrome final : public QFrame {
    Q_OBJECT

 public:
    explicit BrowserChrome(QWidget* parent = nullptr);

    [[nodiscard]] BrowserTabBar* tabBar() const noexcept;
    [[nodiscard]] QFrame* toolbar() const noexcept;
    [[nodiscard]] QLineEdit* addressEdit() const noexcept;
    [[nodiscard]] QToolButton* newTabButton() const noexcept;
    [[nodiscard]] QToolButton* tabSearchButton() const noexcept;
    [[nodiscard]] QToolButton* tabGroupButton() const noexcept;
    [[nodiscard]] QToolButton* backButton() const noexcept;
    [[nodiscard]] QToolButton* forwardButton() const noexcept;
    [[nodiscard]] QToolButton* reloadButton() const noexcept;
    [[nodiscard]] QToolButton* homeButton() const noexcept;
    [[nodiscard]] QToolButton* siteControlButton() const noexcept;
    [[nodiscard]] QToolButton* currentPageFavoriteButton() const noexcept;
    [[nodiscard]] QToolButton* historyButton() const noexcept;
    [[nodiscard]] QToolButton* favoritesButton() const noexcept;
    [[nodiscard]] QToolButton* startupSettingsButton() const noexcept;
    [[nodiscard]] QToolButton* permissionSettingsButton() const noexcept;
    [[nodiscard]] QToolButton* currentTabMuteButton() const noexcept;
    [[nodiscard]] QToolButton* audioTabsButton() const noexcept;
    [[nodiscard]] QToolButton* downloadButton() const noexcept;
    [[nodiscard]] QToolButton* zoomOutButton() const noexcept;
    [[nodiscard]] QToolButton* zoomResetButton() const noexcept;
    [[nodiscard]] QToolButton* zoomInButton() const noexcept;
    [[nodiscard]] QPushButton* goButton() const noexcept;
    [[nodiscard]] QPushButton* clearDataButton() const noexcept;
    [[nodiscard]] QMenu* moreMenu() const noexcept;

    void setCompact(bool isCompact);
    void setReloading(bool isReloading);
    void setCurrentPageFavorite(bool isFavorite);
    void setCurrentTabMuted(bool isMuted);
    void setDownloadActive(bool isActive);
    void setZoomPercentage(int percentage);

 protected:
    void changeEvent(QEvent* event) override;

 private:
    void buildUi();
    void applyIcons();
    void buildMoreMenu();

    BrowserTabBar* tabBar_{nullptr};
    QFrame* tabStrip_{nullptr};
    QFrame* toolbar_{nullptr};
    QFrame* navigationBar_{nullptr};
    QLineEdit* addressEdit_{nullptr};
    QToolButton* newTabButton_{nullptr};
    QToolButton* tabSearchButton_{nullptr};
    QToolButton* tabGroupButton_{nullptr};
    QToolButton* backButton_{nullptr};
    QToolButton* forwardButton_{nullptr};
    QToolButton* reloadButton_{nullptr};
    QToolButton* homeButton_{nullptr};
    QToolButton* siteControlButton_{nullptr};
    QToolButton* currentPageFavoriteButton_{nullptr};
    QToolButton* historyButton_{nullptr};
    QToolButton* favoritesButton_{nullptr};
    QToolButton* startupSettingsButton_{nullptr};
    QToolButton* permissionSettingsButton_{nullptr};
    QToolButton* currentTabMuteButton_{nullptr};
    QToolButton* audioTabsButton_{nullptr};
    QToolButton* downloadButton_{nullptr};
    QToolButton* moreButton_{nullptr};
    QToolButton* zoomOutButton_{nullptr};
    QToolButton* zoomResetButton_{nullptr};
    QToolButton* zoomInButton_{nullptr};
    QPushButton* goButton_{nullptr};
    QPushButton* clearDataButton_{nullptr};
    QMenu* moreMenu_{nullptr};
    QAction* zoomResetAction_{nullptr};
    bool isReloading_{false};
    bool isCurrentPageFavorite_{false};
    bool isCurrentTabMuted_{false};
    bool isDownloadActive_{false};
};

}  // namespace mediahub::gui
