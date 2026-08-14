#include "browser_chrome.h"

#include <QAbstractButton>
#include <QAction>
#include <QEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "browser_icon_provider.h"

namespace mediahub::gui {
namespace {

constexpr int kToolButtonSize = 32;
constexpr int kIconSize = 18;
constexpr int kAudioIconSlotWidth = 18;

class BrowserAddressEdit final : public QLineEdit {
 public:
    explicit BrowserAddressEdit(QWidget* const parent) : QLineEdit(parent) {}

 protected:
    void focusInEvent(QFocusEvent* const event) override {
        QLineEdit::focusInEvent(event);
        selectAll();
        selectsOnMouseRelease_ = event->reason() == Qt::MouseFocusReason;
    }

    void mouseReleaseEvent(QMouseEvent* const event) override {
        QLineEdit::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && selectsOnMouseRelease_) {
            selectAll();
            selectsOnMouseRelease_ = false;
        }
    }

 private:
    bool selectsOnMouseRelease_{false};
};

QToolButton* createToolButton(const QString& objectName,
                              const QString& accessibleName,
                              QWidget* const parent) {
    auto* const button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setToolTip(accessibleName);
    button->setAccessibleName(accessibleName);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFixedSize(kToolButtonSize, kToolButtonSize);
    button->setIconSize(QSize(kIconSize, kIconSize));
    return button;
}

QAction* addMenuAction(QMenu* const menu, const QString& objectName,
                       const QString& text, const BrowserIcon icon,
                       const QColor& color) {
    QAction* const action = menu->addAction(
        BrowserIconProvider::icon(icon, color, kIconSize), text);
    action->setObjectName(objectName);
    return action;
}

}  // namespace

BrowserTabBar::BrowserTabBar(QWidget* const parent) : QTabBar(parent) {
    setMouseTracking(true);
    connect(this, &QTabBar::currentChanged, this,
            [this] { refreshCloseButtonVisibility(); });
}

void BrowserTabBar::setCollapsedTabIds(const QSet<qulonglong>& tabIds) {
    if (collapsedTabIds_ == tabIds) {
        return;
    }
    collapsedTabIds_ = tabIds;
    const bool wasExpanding = expanding();
    setExpanding(!wasExpanding);
    setExpanding(wasExpanding);
    updateGeometry();
    update();
}

bool BrowserTabBar::isTabCollapsed(const qulonglong tabId) const {
    return collapsedTabIds_.contains(tabId);
}

void BrowserTabBar::setTabAudioState(const qulonglong tabId,
                                     const bool isPlayingAudio,
                                     const bool isMuted) {
    const AudioState next{isPlayingAudio, isMuted};
    const auto found = audioStates_.constFind(tabId);
    if (found != audioStates_.cend() &&
        found->isPlayingAudio == next.isPlayingAudio &&
        found->isMuted == next.isMuted) {
        return;
    }
    if (!isPlayingAudio && !isMuted) {
        audioStates_.remove(tabId);
    } else {
        audioStates_.insert(tabId, next);
    }
    update();
}

void BrowserTabBar::refreshCloseButtonVisibility() {
    const QColor color(QStringLiteral("#344454"));
    const QIcon closeIcon =
        BrowserIconProvider::icon(BrowserIcon::Close, color, 14);
    for (int index = 0; index < count(); ++index) {
        const bool isVisible = index == currentIndex() || index == hoveredTab_;
        for (const ButtonPosition position : {LeftSide, RightSide}) {
            QWidget* const widget = tabButton(index, position);
            if (widget == nullptr) {
                continue;
            }
            widget->setVisible(isVisible);
            if (auto* const button = qobject_cast<QAbstractButton*>(widget)) {
                button->setIcon(closeIcon);
                button->setToolTip(QStringLiteral("关闭标签"));
                button->setAccessibleName(QStringLiteral("关闭标签"));
            }
        }
    }
}

QSize BrowserTabBar::tabSizeHint(const int index) const {
    QSize result = QTabBar::tabSizeHint(index);
    if (collapsedTabIds_.contains(tabData(index).toULongLong())) {
        return QSize(0, result.height());
    }
    result.rwidth() += kAudioIconSlotWidth;
    return result;
}

QSize BrowserTabBar::minimumTabSizeHint(const int index) const {
    QSize result = QTabBar::minimumTabSizeHint(index);
    if (collapsedTabIds_.contains(tabData(index).toULongLong())) {
        return QSize(0, result.height());
    }
    result.rwidth() += kAudioIconSlotWidth;
    return result;
}

void BrowserTabBar::mouseMoveEvent(QMouseEvent* const event) {
    updateHoveredTab(event->pos());
    QTabBar::mouseMoveEvent(event);
}

void BrowserTabBar::mousePressEvent(QMouseEvent* const event) {
    if (event->button() == Qt::MiddleButton) {
        const int index = tabAt(event->pos());
        if (index >= 0) {
            emit tabCloseRequested(index);
            event->accept();
            return;
        }
    }
    QTabBar::mousePressEvent(event);
}

void BrowserTabBar::mouseReleaseEvent(QMouseEvent* const event) {
    const int index = tabAt(event->pos());
    if (event->button() == Qt::LeftButton && index >= 0 &&
        audioIconRect(index).contains(event->pos())) {
        const qulonglong tabId = tabData(index).toULongLong();
        if (audioStates_.contains(tabId)) {
            emit tabAudioToggleRequested(tabId);
            event->accept();
            return;
        }
    }
    QTabBar::mouseReleaseEvent(event);
}

void BrowserTabBar::leaveEvent(QEvent* const event) {
    hoveredTab_ = -1;
    refreshCloseButtonVisibility();
    QTabBar::leaveEvent(event);
}

void BrowserTabBar::paintEvent(QPaintEvent* const event) {
    QTabBar::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color(QStringLiteral("#344454"));
    for (int index = 0; index < count(); ++index) {
        const auto found = audioStates_.constFind(tabData(index).toULongLong());
        if (found == audioStates_.cend()) {
            continue;
        }
        const BrowserIcon icon = found->isMuted ? BrowserIcon::AudioMuted
                                                : BrowserIcon::Audio;
        BrowserIconProvider::icon(icon, color, 14)
            .paint(&painter, audioIconRect(index));
    }
}

void BrowserTabBar::tabInserted(const int index) {
    QTabBar::tabInserted(index);
    refreshCloseButtonVisibility();
}

void BrowserTabBar::tabRemoved(const int index) {
    QTabBar::tabRemoved(index);
    refreshCloseButtonVisibility();
}

QRect BrowserTabBar::audioIconRect(const int index) const {
    const QRect rect = tabRect(index);
    return QRect(rect.right() - 42, rect.center().y() - 7, 14, 14);
}

void BrowserTabBar::updateHoveredTab(const QPoint& position) {
    const int nextHoveredTab = tabAt(position);
    if (hoveredTab_ == nextHoveredTab) {
        return;
    }
    hoveredTab_ = nextHoveredTab;
    refreshCloseButtonVisibility();
}

BrowserChrome::BrowserChrome(QWidget* const parent) : QFrame(parent) {
    setObjectName(QStringLiteral("browserChrome"));
    setFrameShape(QFrame::NoFrame);
    buildUi();
    buildMoreMenu();
    applyIcons();
}

void BrowserChrome::buildUi() {
    auto* const rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    tabStrip_ = new QFrame(this);
    tabStrip_->setObjectName(QStringLiteral("browserTabStrip"));
    tabStrip_->setFixedHeight(38);
    auto* const tabLayout = new QHBoxLayout(tabStrip_);
    tabLayout->setContentsMargins(8, 3, 8, 0);
    tabLayout->setSpacing(4);
    tabBar_ = new BrowserTabBar(tabStrip_);
    tabBar_->setObjectName(QStringLiteral("browserTabBar"));
    tabBar_->setTabsClosable(true);
    tabBar_->setMovable(true);
    tabBar_->setContextMenuPolicy(Qt::CustomContextMenu);
    newTabButton_ = createToolButton(QStringLiteral("browserNewTabButton"),
                                     QStringLiteral("新建网页标签（Ctrl+T）"),
                                     tabStrip_);
    newTabButton_->setText(QStringLiteral("+"));
    tabSearchButton_ = createToolButton(
        QStringLiteral("browserTabSearchButton"),
        QStringLiteral("搜索已打开的标签（Ctrl+Shift+A）"), tabStrip_);
    tabLayout->addWidget(tabBar_, 1);
    tabLayout->addWidget(newTabButton_);
    tabLayout->addWidget(tabSearchButton_);
    rootLayout->addWidget(tabStrip_);

    toolbar_ = new QFrame(this);
    toolbar_->setObjectName(QStringLiteral("browserToolbar"));
    toolbar_->setFixedHeight(44);
    auto* const toolbarLayout = new QHBoxLayout(toolbar_);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    toolbarLayout->setSpacing(0);
    navigationBar_ = new QFrame(toolbar_);
    navigationBar_->setObjectName(QStringLiteral("browserNavigationBar"));
    auto* const navigationLayout = new QHBoxLayout(navigationBar_);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(4);

    backButton_ = createToolButton(QStringLiteral("browserBackButton"),
                                   QStringLiteral("后退（Alt+左箭头）"),
                                   navigationBar_);
    forwardButton_ = createToolButton(QStringLiteral("browserForwardButton"),
                                      QStringLiteral("前进（Alt+右箭头）"),
                                      navigationBar_);
    reloadButton_ = createToolButton(QStringLiteral("browserReloadButton"),
                                     QStringLiteral("刷新（Ctrl+R）"),
                                     navigationBar_);
    homeButton_ = createToolButton(QStringLiteral("browserHomeButton"),
                                   QStringLiteral("打开主页"), navigationBar_);
    historyButton_ = createToolButton(QStringLiteral("browserHistoryButton"),
                                      QStringLiteral("打开浏览历史（Ctrl+H）"),
                                      navigationBar_);
    favoritesButton_ = createToolButton(
        QStringLiteral("browserFavoritesButton"),
        QStringLiteral("打开收藏夹（Ctrl+Shift+O）"), navigationBar_);

    auto* const addressFrame = new QFrame(navigationBar_);
    addressFrame->setObjectName(QStringLiteral("browserAddressContainer"));
    auto* const addressLayout = new QHBoxLayout(addressFrame);
    addressLayout->setContentsMargins(4, 0, 4, 0);
    addressLayout->setSpacing(0);
    siteControlButton_ = createToolButton(
        QStringLiteral("browserSiteControlButton"),
        QStringLiteral("查看当前网站权限"), addressFrame);
    siteControlButton_->setFixedSize(30, 30);
    addressEdit_ = new BrowserAddressEdit(addressFrame);
    addressEdit_->setObjectName(QStringLiteral("browserAddressEdit"));
    addressEdit_->setPlaceholderText(QStringLiteral("输入网站地址"));
    addressEdit_->setMinimumWidth(180);
    addressEdit_->setFrame(false);
    currentPageFavoriteButton_ = createToolButton(
        QStringLiteral("browserFavoriteCurrentButton"),
        QStringLiteral("收藏当前网页"), addressFrame);
    currentPageFavoriteButton_->setFixedSize(30, 30);
    addressLayout->addWidget(siteControlButton_);
    addressLayout->addWidget(addressEdit_, 1);
    addressLayout->addWidget(currentPageFavoriteButton_);

    goButton_ = new QPushButton(navigationBar_);
    goButton_->setObjectName(QStringLiteral("browserGoButton"));
    goButton_->setToolTip(QStringLiteral("访问输入的网站"));
    goButton_->setAccessibleName(QStringLiteral("访问输入的网站"));
    goButton_->setFixedSize(kToolButtonSize, kToolButtonSize);
    audioTabsButton_ = createToolButton(
        QStringLiteral("browserAudioTabsButton"),
        QStringLiteral("管理网页声音"), navigationBar_);
    downloadButton_ = createToolButton(
        QStringLiteral("browserDownloadButton"),
        QStringLiteral("打开下载（Ctrl+J）"), navigationBar_);
    moreButton_ = createToolButton(QStringLiteral("browserMoreButton"),
                                   QStringLiteral("更多浏览器设置"),
                                   navigationBar_);
    moreButton_->setPopupMode(QToolButton::InstantPopup);

    navigationLayout->addWidget(backButton_);
    navigationLayout->addWidget(forwardButton_);
    navigationLayout->addWidget(reloadButton_);
    navigationLayout->addWidget(homeButton_);
    navigationLayout->addWidget(historyButton_);
    navigationLayout->addWidget(favoritesButton_);
    navigationLayout->addWidget(addressFrame, 1);
    navigationLayout->addWidget(goButton_);
    navigationLayout->addWidget(audioTabsButton_);
    navigationLayout->addWidget(downloadButton_);
    navigationLayout->addWidget(moreButton_);
    toolbarLayout->addWidget(navigationBar_);
    rootLayout->addWidget(toolbar_);

    tabGroupButton_ = createToolButton(
        QStringLiteral("browserTabGroupButton"),
        QStringLiteral("管理标签分组"), this);
    startupSettingsButton_ = createToolButton(
        QStringLiteral("browserStartupSettingsButton"),
        QStringLiteral("主页、启动页和会话恢复设置"), this);
    permissionSettingsButton_ = createToolButton(
        QStringLiteral("browserPermissionSettingsButton"),
        QStringLiteral("管理网站权限"), this);
    currentTabMuteButton_ = createToolButton(
        QStringLiteral("browserCurrentTabMuteButton"),
        QStringLiteral("静音当前网页标签"), this);
    zoomOutButton_ = createToolButton(QStringLiteral("browserZoomOutButton"),
                                      QStringLiteral("缩小当前网页（Ctrl+-）"),
                                      this);
    zoomResetButton_ = createToolButton(
        QStringLiteral("browserZoomResetButton"),
        QStringLiteral("重置当前网页缩放（Ctrl+0）"), this);
    zoomInButton_ = createToolButton(QStringLiteral("browserZoomInButton"),
                                     QStringLiteral("放大当前网页（Ctrl++）"),
                                     this);
    clearDataButton_ = new QPushButton(this);
    clearDataButton_->setObjectName(QStringLiteral("browserClearDataButton"));
    clearDataButton_->setToolTip(
        QStringLiteral("清除网页 Cookie、缓存和宿主保存状态"));
    clearDataButton_->setAccessibleName(QStringLiteral("清除网页数据"));
    const QList<QWidget*> lowFrequencyCommands{
        tabGroupButton_, startupSettingsButton_, permissionSettingsButton_,
        currentTabMuteButton_, zoomOutButton_, zoomResetButton_, zoomInButton_,
        clearDataButton_};
    for (QWidget* const command : lowFrequencyCommands) {
        command->hide();
    }
}

void BrowserChrome::buildMoreMenu() {
    moreMenu_ = new QMenu(moreButton_);
    moreMenu_->setObjectName(QStringLiteral("browserMoreMenu"));
    const QColor color(QStringLiteral("#344454"));
    QAction* const history = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreHistoryAction"),
        QStringLiteral("历史记录"), BrowserIcon::History, color);
    history->setShortcut(QKeySequence(QStringLiteral("Ctrl+H")));
    QAction* const favorites = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreFavoritesAction"),
        QStringLiteral("收藏夹"), BrowserIcon::Favorite, color);
    favorites->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    QAction* const downloads = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreDownloadsAction"),
        QStringLiteral("下载"), BrowserIcon::Download, color);
    downloads->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    QAction* const audio = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreAudioAction"),
        QStringLiteral("网页声音"), BrowserIcon::Audio, color);
    QAction* const tabs = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreTabSearchAction"),
        QStringLiteral("搜索标签"), BrowserIcon::TabSearch, color);
    QAction* const groups = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreGroupsAction"),
        QStringLiteral("标签分组"), BrowserIcon::Group, color);
    moreMenu_->addSeparator();
    QAction* const zoomOut = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreZoomOutAction"),
        QStringLiteral("缩小"), BrowserIcon::ZoomOut, color);
    zoomResetAction_ = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreZoomResetAction"),
        QStringLiteral("缩放 100%"), BrowserIcon::Reload, color);
    QAction* const zoomIn = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreZoomInAction"),
        QStringLiteral("放大"), BrowserIcon::ZoomIn, color);
    moreMenu_->addSeparator();
    QAction* const startup = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreStartupAction"),
        QStringLiteral("启动设置"), BrowserIcon::Settings, color);
    QAction* const permissions = addMenuAction(
        moreMenu_, QStringLiteral("browserMorePermissionsAction"),
        QStringLiteral("网站权限"), BrowserIcon::Permissions, color);
    QAction* const clearData = addMenuAction(
        moreMenu_, QStringLiteral("browserMoreClearDataAction"),
        QStringLiteral("清除网页数据"), BrowserIcon::ClearData, color);
    moreButton_->setMenu(moreMenu_);

    connect(history, &QAction::triggered, historyButton_, &QToolButton::click);
    connect(favorites, &QAction::triggered, favoritesButton_, &QToolButton::click);
    connect(downloads, &QAction::triggered, downloadButton_, &QToolButton::click);
    connect(audio, &QAction::triggered, audioTabsButton_, &QToolButton::click);
    connect(tabs, &QAction::triggered, tabSearchButton_, &QToolButton::click);
    connect(groups, &QAction::triggered, tabGroupButton_, &QToolButton::click);
    connect(zoomOut, &QAction::triggered, zoomOutButton_, &QToolButton::click);
    connect(zoomResetAction_, &QAction::triggered, zoomResetButton_,
            &QToolButton::click);
    connect(zoomIn, &QAction::triggered, zoomInButton_, &QToolButton::click);
    connect(startup, &QAction::triggered, startupSettingsButton_,
            &QToolButton::click);
    connect(permissions, &QAction::triggered, permissionSettingsButton_,
            &QToolButton::click);
    connect(clearData, &QAction::triggered, clearDataButton_,
            &QPushButton::click);
}

void BrowserChrome::applyIcons() {
    const QColor color(QStringLiteral("#344454"));
    const QList<QToolButton*> toolButtons{
        newTabButton_, tabSearchButton_, tabGroupButton_, backButton_,
        forwardButton_, reloadButton_, homeButton_, siteControlButton_,
        currentPageFavoriteButton_, historyButton_, favoritesButton_,
        startupSettingsButton_,
        permissionSettingsButton_, currentTabMuteButton_, audioTabsButton_,
        downloadButton_, moreButton_, zoomOutButton_, zoomResetButton_,
        zoomInButton_};
    for (QToolButton* const button : toolButtons) {
        button->setFixedSize(kToolButtonSize, kToolButtonSize);
    }
    backButton_->setIcon(BrowserIconProvider::icon(BrowserIcon::Back, color));
    forwardButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::Forward, color));
    reloadButton_->setIcon(BrowserIconProvider::icon(
        isReloading_ ? BrowserIcon::Stop : BrowserIcon::Reload, color));
    homeButton_->setIcon(BrowserIconProvider::icon(BrowserIcon::Home, color));
    siteControlButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::SiteControl, color));
    historyButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::History, color));
    favoritesButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::Favorite, color));
    currentPageFavoriteButton_->setIcon(BrowserIconProvider::icon(
        isCurrentPageFavorite_ ? BrowserIcon::FavoriteFilled
                               : BrowserIcon::Favorite,
        color));
    newTabButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::NewTab, color));
    tabSearchButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::TabSearch, color));
    tabGroupButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::Group, color));
    startupSettingsButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::Settings, color));
    permissionSettingsButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::Permissions, color));
    const BrowserIcon audioIcon = isCurrentTabMuted_ ? BrowserIcon::AudioMuted
                                                     : BrowserIcon::Audio;
    currentTabMuteButton_->setIcon(
        BrowserIconProvider::icon(audioIcon, color));
    audioTabsButton_->setIcon(BrowserIconProvider::icon(audioIcon, color));
    downloadButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::Download, color));
    downloadButton_->setProperty("hasActivity", isDownloadActive_);
    moreButton_->setIcon(BrowserIconProvider::icon(BrowserIcon::More, color));
    zoomOutButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::ZoomOut, color));
    zoomInButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::ZoomIn, color));
    goButton_->setIcon(BrowserIconProvider::icon(BrowserIcon::Forward, color));
    goButton_->setIconSize(QSize(kIconSize, kIconSize));
    clearDataButton_->setIcon(
        BrowserIconProvider::icon(BrowserIcon::ClearData, color));
    tabBar_->refreshCloseButtonVisibility();
}

BrowserTabBar* BrowserChrome::tabBar() const noexcept { return tabBar_; }
QFrame* BrowserChrome::toolbar() const noexcept { return toolbar_; }
QLineEdit* BrowserChrome::addressEdit() const noexcept { return addressEdit_; }
QToolButton* BrowserChrome::newTabButton() const noexcept { return newTabButton_; }
QToolButton* BrowserChrome::tabSearchButton() const noexcept { return tabSearchButton_; }
QToolButton* BrowserChrome::tabGroupButton() const noexcept { return tabGroupButton_; }
QToolButton* BrowserChrome::backButton() const noexcept { return backButton_; }
QToolButton* BrowserChrome::forwardButton() const noexcept { return forwardButton_; }
QToolButton* BrowserChrome::reloadButton() const noexcept { return reloadButton_; }
QToolButton* BrowserChrome::homeButton() const noexcept { return homeButton_; }
QToolButton* BrowserChrome::siteControlButton() const noexcept { return siteControlButton_; }
QToolButton* BrowserChrome::currentPageFavoriteButton() const noexcept {
    return currentPageFavoriteButton_;
}
QToolButton* BrowserChrome::historyButton() const noexcept { return historyButton_; }
QToolButton* BrowserChrome::favoritesButton() const noexcept { return favoritesButton_; }
QToolButton* BrowserChrome::startupSettingsButton() const noexcept { return startupSettingsButton_; }
QToolButton* BrowserChrome::permissionSettingsButton() const noexcept { return permissionSettingsButton_; }
QToolButton* BrowserChrome::currentTabMuteButton() const noexcept { return currentTabMuteButton_; }
QToolButton* BrowserChrome::audioTabsButton() const noexcept { return audioTabsButton_; }
QToolButton* BrowserChrome::downloadButton() const noexcept { return downloadButton_; }
QToolButton* BrowserChrome::zoomOutButton() const noexcept { return zoomOutButton_; }
QToolButton* BrowserChrome::zoomResetButton() const noexcept { return zoomResetButton_; }
QToolButton* BrowserChrome::zoomInButton() const noexcept { return zoomInButton_; }
QPushButton* BrowserChrome::goButton() const noexcept { return goButton_; }
QPushButton* BrowserChrome::clearDataButton() const noexcept { return clearDataButton_; }
QMenu* BrowserChrome::moreMenu() const noexcept { return moreMenu_; }

void BrowserChrome::setCompact(const bool isCompact) {
    homeButton_->setVisible(!isCompact);
    historyButton_->setVisible(!isCompact);
    favoritesButton_->setVisible(!isCompact);
    goButton_->setVisible(!isCompact);
    addressEdit_->setMinimumWidth(isCompact ? 120 : 180);
}

void BrowserChrome::setReloading(const bool isReloading) {
    if (isReloading_ == isReloading) {
        return;
    }
    isReloading_ = isReloading;
    reloadButton_->setToolTip(isReloading ? QStringLiteral("停止加载")
                                         : QStringLiteral("刷新（Ctrl+R）"));
    reloadButton_->setAccessibleName(isReloading ? QStringLiteral("停止加载")
                                                 : QStringLiteral("刷新"));
    applyIcons();
}

void BrowserChrome::setCurrentPageFavorite(const bool isFavorite) {
    if (isCurrentPageFavorite_ == isFavorite) {
        return;
    }
    isCurrentPageFavorite_ = isFavorite;
    currentPageFavoriteButton_->setToolTip(
        isFavorite ? QStringLiteral("编辑当前网页收藏")
                   : QStringLiteral("收藏当前网页"));
    currentPageFavoriteButton_->setAccessibleName(
        currentPageFavoriteButton_->toolTip());
    applyIcons();
}

void BrowserChrome::setCurrentTabMuted(const bool isMuted) {
    if (isCurrentTabMuted_ == isMuted) {
        return;
    }
    isCurrentTabMuted_ = isMuted;
    applyIcons();
}

void BrowserChrome::setDownloadActive(const bool isActive) {
    if (isDownloadActive_ == isActive) {
        return;
    }
    isDownloadActive_ = isActive;
    downloadButton_->setProperty("hasActivity", isActive);
    downloadButton_->style()->unpolish(downloadButton_);
    downloadButton_->style()->polish(downloadButton_);
}

void BrowserChrome::setZoomPercentage(const int percentage) {
    zoomResetButton_->setText(QStringLiteral("%1%").arg(percentage));
    if (zoomResetAction_ != nullptr) {
        zoomResetAction_->setText(
            QStringLiteral("缩放 %1%").arg(percentage));
    }
}

void BrowserChrome::changeEvent(QEvent* const event) {
    QFrame::changeEvent(event);
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::StyleChange) {
        applyIcons();
    }
}

}  // namespace mediahub::gui
