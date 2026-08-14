#include "browser_page.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QClipboard>
#include <QColor>
#include <QDialog>
#include <QDateTime>
#include <QDir>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QPixmap>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSet>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QStackedLayout>
#include <QTabBar>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtMath>

#include <utility>
#include <algorithm>
#include <functional>

#include "browser_backend.h"
#include "browser_bookmark_html.h"
#include "browser_data_store.h"
#include "browser_download_widget.h"
#include "browser_download_center.h"
#include "browser_navigation_policy.h"
#include "browser_permission_dialog.h"
#include "browser_permission_store.h"
#include "browser_session_store.h"
#include "browser_startup_settings.h"
#include "browser_startup_settings_dialog.h"
#include "browser_tab_group_dialog.h"

namespace mediahub::gui {
namespace {

const QString kBrowserHomeUrl = QStringLiteral("https://www.bing.com/");
constexpr int kDefaultMaximumTabCount = 20;
constexpr int kMaximumClosedTabCount = 20;
constexpr qint64 kRecoveryCooldownMilliseconds = 30000;
constexpr int kSessionCheckpointMilliseconds = 30000;
constexpr int kHistoryPersistenceDelayMilliseconds = 1000;
constexpr int kListSearchDebounceMilliseconds = 150;
constexpr int kMaximumRecoveryAttemptsPerWindow = 3;

bool haveSameSessionTab(const BrowserSessionTab& left,
                        const BrowserSessionTab& right) {
    return left.url == right.url && left.title == right.title &&
           left.groupId == right.groupId && left.isPinned == right.isPinned &&
           left.isMuted == right.isMuted && left.zoomFactor == right.zoomFactor;
}

bool haveSameSessionGroup(const BrowserSessionGroup& left,
                          const BrowserSessionGroup& right) {
    return left.id == right.id && left.name == right.name &&
           left.color == right.color &&
           left.isCollapsed == right.isCollapsed;
}

template <typename Value, typename Equal>
bool haveSameValues(const QVector<Value>& left, const QVector<Value>& right,
                    Equal equal) {
    if (left.size() != right.size()) {
        return false;
    }
    for (int index = 0; index < left.size(); ++index) {
        if (!equal(left.at(index), right.at(index))) {
            return false;
        }
    }
    return true;
}

bool haveSameSession(const BrowserSessionState& left,
                     const BrowserSessionState& right) {
    return left.currentIndex == right.currentIndex &&
           haveSameValues(left.tabs, right.tabs, haveSameSessionTab) &&
           haveSameValues(left.closedTabs, right.closedTabs,
                          haveSameSessionTab) &&
           haveSameValues(left.groups, right.groups, haveSameSessionGroup);
}

QToolButton* createToolButton(const QString& objectName, const QString& text,
                              QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(text);
    button->setAutoRaise(false);
    return button;
}

// 地址栏的单击语义是准备替换旧地址，键盘仍可正常移动光标和选择文本。
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

class BrowserTabBar final : public QTabBar {
 public:
    explicit BrowserTabBar(QWidget* const parent) : QTabBar(parent) {}

    void setCollapsedTabIds(const QSet<qulonglong>& tabIds) {
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

    [[nodiscard]] bool isTabCollapsed(const qulonglong tabId) const {
        return collapsedTabIds_.contains(tabId);
    }

 protected:
    QSize tabSizeHint(const int index) const override {
        const QSize normalSize = QTabBar::tabSizeHint(index);
        if (collapsedTabIds_.contains(tabData(index).toULongLong())) {
            return QSize(0, normalSize.height());
        }
        return normalSize;
    }

    QSize minimumTabSizeHint(const int index) const override {
        const QSize normalSize = QTabBar::minimumTabSizeHint(index);
        if (collapsedTabIds_.contains(tabData(index).toULongLong())) {
            return QSize(0, normalSize.height());
        }
        return normalSize;
    }

    void mousePressEvent(QMouseEvent* const event) override {
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

 private:
    QSet<qulonglong> collapsedTabIds_;
};

// 历史和收藏项把左键、Ctrl+左键和中键统一成稳定打开动作。
class BrowserLinkListWidget final : public QListWidget {
 public:
    using OpenCallback = std::function<void(const QString&, bool)>;
    using OrderChangedCallback = std::function<void()>;

    explicit BrowserLinkListWidget(QWidget* const parent)
        : QListWidget(parent) {}

    void setOpenCallback(OpenCallback callback) {
        openCallback_ = std::move(callback);
    }

    void setOrderChangedCallback(OrderChangedCallback callback) {
        orderChangedCallback_ = std::move(callback);
    }

 protected:
    void mousePressEvent(QMouseEvent* const event) override {
        pressPosition_ = event->pos();
        didDrag_ = false;
        QListWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* const event) override {
        if ((event->pos() - pressPosition_).manhattanLength() >=
            QApplication::startDragDistance()) {
            didDrag_ = true;
        }
        QListWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* const event) override {
        QListWidget::mouseReleaseEvent(event);
        if (didDrag_) {
            return;
        }
        if (event->button() != Qt::LeftButton &&
            event->button() != Qt::MiddleButton) {
            return;
        }
        QListWidgetItem* const item = itemAt(event->pos());
        if (item == nullptr || !openCallback_) {
            return;
        }
        const bool isNewTab = event->button() == Qt::MiddleButton ||
                              event->modifiers().testFlag(Qt::ControlModifier);
        openCallback_(item->data(Qt::UserRole).toString(), isNewTab);
    }

    void dropEvent(QDropEvent* const event) override {
        QListWidget::dropEvent(event);
        if (event->isAccepted() && orderChangedCallback_) {
            orderChangedCallback_();
        }
    }

 private:
    OpenCallback openCallback_;
    OrderChangedCallback orderChangedCallback_;
    QPoint pressPosition_;
    bool didDrag_{false};
};

QString normalizedWebOrigin(const QString& origin) {
    const QUrl parsed(origin, QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || (scheme != QStringLiteral("https") &&
                              scheme != QStringLiteral("http")) ||
        parsed.host().isEmpty() || !parsed.userName().isEmpty() ||
        !parsed.password().isEmpty() || !parsed.path().isEmpty() ||
        parsed.hasQuery() || parsed.hasFragment()) {
        return {};
    }
    QUrl normalized;
    normalized.setScheme(scheme);
    normalized.setHost(parsed.host().toLower());
    normalized.setPort(parsed.port(-1));
    return normalized.toString(QUrl::FullyEncoded | QUrl::RemovePath |
                               QUrl::RemoveQuery | QUrl::RemoveFragment |
                               QUrl::RemoveUserInfo);
}

bool isAcceptableExternalTarget(const QString& target) {
    const QUrl parsed(target, QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || scheme.isEmpty() || target.contains(QLatin1Char('\n')) ||
        target.contains(QLatin1Char('\r'))) {
        return false;
    }
    return scheme != QStringLiteral("http") && scheme != QStringLiteral("https") &&
           scheme != QStringLiteral("file") && scheme != QStringLiteral("data") &&
           scheme != QStringLiteral("javascript") && scheme != QStringLiteral("about") &&
           scheme != QStringLiteral("blob");
}

}  // namespace

BrowserPage::BrowserPage(BrowserBackend& backend, QString userDataDirectory,
                         QWidget* parent, BrowserDataStore* const dataStore,
                         BrowserSessionStore* const sessionStore,
                         BrowserStartupSettingsStore* const startupSettingsStore,
                         BrowserPermissionStore* const permissionStore)
    : QWidget(parent),
      backend_(backend),
      dataModel_(dataStore),
      sessionStore_(sessionStore),
      startupSettingsStore_(startupSettingsStore),
      permissionStore_(permissionStore),
      userDataDirectory_(std::move(userDataDirectory)) {
    setObjectName(QStringLiteral("browserPage"));
    buildUi();
    tabs_.append(BrowserTabRecord{1, generation_});
    tabBar_->addTab(QStringLiteral("新标签页"));
    tabBar_->setTabData(0, QVariant::fromValue<qulonglong>(1));
    updateTabCloseButtons();
    backend_.setEventListener(this);
    if (dataModel_.isAvailable()) {
        historyPersistenceTimer_ = new QTimer(this);
        historyPersistenceTimer_->setObjectName(
            QStringLiteral("browserHistoryPersistenceTimer"));
        historyPersistenceTimer_->setSingleShot(true);
        historyPersistenceTimer_->setInterval(
            kHistoryPersistenceDelayMilliseconds);
        connect(historyPersistenceTimer_, &QTimer::timeout, this,
                &BrowserPage::flushPendingHistory);
    }
    if (sessionStore_ != nullptr) {
        sessionCheckpointTimer_ = new QTimer(this);
        sessionCheckpointTimer_->setObjectName(
            QStringLiteral("browserSessionCheckpointTimer"));
        sessionCheckpointTimer_->setInterval(kSessionCheckpointMilliseconds);
        connect(sessionCheckpointTimer_, &QTimer::timeout, this,
                &BrowserPage::saveSession);
        sessionCheckpointTimer_->start();
    }
    updateControls();
}

BrowserPage::~BrowserPage() {
    shutdown();
}

int BrowserPage::activeDownloadCount() const noexcept {
    if (backend_.supportsConcurrentDownloads() && downloadCenter_ != nullptr) {
        return downloadCenter_->activeItemCount();
    }
    return activeDownloadId_.has_value() && downloadWidget_ != nullptr &&
                   !downloadWidget_->isTerminal()
               ? 1
               : 0;
}

int BrowserPage::maximumTabCount() const noexcept {
    if (startupSettingsStore_ == nullptr) {
        return kDefaultMaximumTabCount;
    }
    try {
        return std::clamp(startupSettingsStore_->load().maximumTabCount, 5, 100);
    } catch (...) {
        return kDefaultMaximumTabCount;
    }
}

void BrowserPage::activate() {
    if (isShuttingDown_) {
        return;
    }
    backend_.setVisible(true);
}

void BrowserPage::deactivate() {
    if (isShuttingDown_) {
        return;
    }
    backend_.setVisible(false);
}

void BrowserPage::shutdown() noexcept {
    if (isShuttingDown_) {
        return;
    }
    isShuttingDown_ = true;
    state_ = BrowserPageState::ShuttingDown;
    ++generation_;
    if (sessionCheckpointTimer_ != nullptr) {
        sessionCheckpointTimer_->stop();
    }
    if (historyPersistenceTimer_ != nullptr) {
        historyPersistenceTimer_->stop();
    }
    flushPendingHistory();
    saveSession();
    closeFindBar(true);
    if (pendingPermissionId_.has_value()) {
        resolvePermission(*pendingPermissionId_, BrowserPermissionDecision::Deny);
    }
    if (pendingExternalProtocolId_.has_value()) {
        resolveExternalProtocol(*pendingExternalProtocolId_, false);
    }
    if (pendingCertificateId_.has_value()) {
        resolveCertificateError(*pendingCertificateId_,
                                BrowserCertificateDecision::ReturnToSafety);
    }
    if (backend_.supportsConcurrentDownloads() && downloadCenter_ != nullptr) {
        const QVector<std::uint64_t> activeRequests =
            downloadCenter_->activeRequestIds();
        for (const std::uint64_t requestId : activeRequests) {
            if (downloadCenter_->requestCancel(requestId)) {
                // shutdown 已经屏蔽常规信号槽，必须在 GUI 线程直接通知后端取消。
                backend_.cancelDownload(requestId);
            }
        }
    } else if (activeDownloadId_.has_value() && !downloadWidget_->isTerminal() &&
               !isDownloadCancellationSent_) {
        isDownloadCancellationSent_ = true;
        backend_.cancelDownload(*activeDownloadId_);
    }
    backend_.setEventListener(nullptr);
    if (isWebFullScreen_) {
        isWebFullScreen_ = false;
        backend_.exitFullScreen();
    }
    backend_.shutdown();
    state_ = BrowserPageState::Unavailable;
}

BrowserPageState BrowserPage::state() const noexcept {
    return state_;
}

bool BrowserPage::isWebFullScreen() const noexcept {
    return isWebFullScreen_;
}

void BrowserPage::exitWebFullScreen() {
    if (isWebFullScreen_ && !isShuttingDown_) {
        backend_.exitFullScreen();
    }
}

void BrowserPage::onBrowserReady(std::uint64_t generation) {
    const int index = findTabIndex(1);
    if (isShuttingDown_ || hasBrowserProcessExited_ || index < 0 ||
        generation != tabs_.at(index).generation) {
        return;
    }
    tabs_[index].state = BrowserPageState::Ready;
    tabs_[index].lastError.reset();
    tabs_[index].processFailure.reset();
    if (index != currentTabIndex_) {
        return;
    }
    state_ = BrowserPageState::Ready;
    statusLabel_->setText(QStringLiteral("网页组件已就绪"));
    showHost();
    updateControls();
    updateBackendBounds();
    if (!hasOpenedInitialHome_ && tabs_[index].address.isEmpty()) {
        hasOpenedInitialHome_ = true;
        openInitialTabs();
    }
}

void BrowserPage::onBrowserError(std::uint64_t generation, BrowserErrorKind kind,
                                 long) {
    if (isShuttingDown_ || hasBrowserProcessExited_) {
        return;
    }
    const int index = kind == BrowserErrorKind::ClearDataFailed
                          ? currentTabIndex_
                          : findTabIndex(1);
    if (index < 0 || index >= tabs_.size() ||
        tabs_.at(index).generation != generation ||
        (kind == BrowserErrorKind::ClearDataFailed && generation != generation_)) {
        return;
    }
    tabs_[index].state = BrowserPageState::Failed;
    tabs_[index].lastError = kind;
    tabs_[index].processFailure.reset();
    if (index != currentTabIndex_) {
        return;
    }
    state_ = BrowserPageState::Failed;
    showError(kind);
    updateControls();
}

void BrowserPage::onNavigationStarted(std::uint64_t generation) {
    const int index = findTabIndex(1);
    if (isShuttingDown_ || hasBrowserProcessExited_ || index < 0 ||
        generation != tabs_.at(index).generation ||
        tabs_.at(index).processFailure.has_value()) {
        return;
    }
    tabs_[index].state = BrowserPageState::Navigating;
    tabs_[index].lastError.reset();
    tabs_[index].processFailure.reset();
    if (index != currentTabIndex_) {
        return;
    }
    closeFindBar(true);
    rejectUnansweredSensitiveRequests();
    state_ = BrowserPageState::Navigating;
    statusLabel_->setText(QStringLiteral("正在载入..."));
    updateControls();
}

void BrowserPage::onNavigationCompleted(std::uint64_t generation,
                                        const QString& visibleUrl,
                                        const QString& title,
                                        bool canGoBack,
                                        bool canGoForward) {
    const int index = findTabIndex(1);
    if (isShuttingDown_ || hasBrowserProcessExited_ || index < 0 ||
        generation != tabs_.at(index).generation ||
        tabs_.at(index).processFailure.has_value()) {
        return;
    }
    applyTabDocumentState(index, visibleUrl, title, canGoBack, canGoForward,
                          true, true);
}

void BrowserPage::onDocumentStateChanged(
    const std::uint64_t generation, const QString& visibleUrl,
    const QString& title, const bool canGoBack, const bool canGoForward) {
    const int index = findTabIndex(1);
    if (isShuttingDown_ || hasBrowserProcessExited_ || index < 0 ||
        generation != tabs_.at(index).generation ||
        tabs_.at(index).processFailure.has_value()) {
        return;
    }
    applyTabDocumentState(index, visibleUrl, title, canGoBack, canGoForward,
                          false, false);
}

void BrowserPage::onNavigationStopped(
    const std::uint64_t generation, const QString& visibleUrl,
    const QString& title, const bool canGoBack, const bool canGoForward) {
    const int index = findTabIndex(1);
    if (isShuttingDown_ || hasBrowserProcessExited_ || index < 0 ||
        generation != tabs_.at(index).generation ||
        tabs_.at(index).processFailure.has_value()) {
        return;
    }
    applyTabDocumentState(index, visibleUrl, title, canGoBack, canGoForward,
                          true, false);
}

void BrowserPage::onFullScreenChanged(std::uint64_t generation,
                                      bool isFullScreen) {
    if (isShuttingDown_ || currentTabIndex_ < 0 ||
        currentTabIndex_ >= tabs_.size() ||
        generation != tabs_.at(currentTabIndex_).generation) {
        return;
    }
    if (isFullScreen == isWebFullScreen_) {
        return;
    }
    QWidget* const downloadPresentation =
        backend_.supportsConcurrentDownloads()
            ? static_cast<QWidget*>(downloadCenter_)
                       : static_cast<QWidget*>(downloadWidget_);
    if (isFullScreen) {
        wasToolbarHidden_ = toolbar_->isHidden();
        wasInformationRowHidden_ = informationRow_->isHidden();
        wasDownloadWidgetHidden_ = downloadPresentation->isHidden();
        toolbar_->hide();
        informationRow_->hide();
        downloadPresentation->hide();
    } else {
        toolbar_->setVisible(!wasToolbarHidden_);
        informationRow_->setVisible(!wasInformationRowHidden_);
        downloadPresentation->setVisible(!wasDownloadWidgetHidden_);
    }
    isWebFullScreen_ = isFullScreen;
    emit fullScreenChanged(isFullScreen);
}

void BrowserPage::onAcceleratorRequested(
    const std::uint64_t generation, const BrowserAccelerator accelerator) {
    if (isShuttingDown_ || currentTabIndex_ < 0 ||
        currentTabIndex_ >= tabs_.size() ||
        generation != tabs_.at(currentTabIndex_).generation) {
        return;
    }
    switch (accelerator) {
        case BrowserAccelerator::FocusAddress:
            addressEdit_->setFocus();
            addressEdit_->selectAll();
            break;
        case BrowserAccelerator::Back:
            if (backButton_->isEnabled()) {
                backend_.goBack();
            }
            break;
        case BrowserAccelerator::Forward:
            if (forwardButton_->isEnabled()) {
                backend_.goForward();
            }
            break;
        case BrowserAccelerator::Reload:
            if (reloadButton_->isEnabled()) {
                backend_.reloadOrStop();
            }
            break;
        case BrowserAccelerator::ZoomIn:
            adjustCurrentTabZoom(0.1);
            break;
        case BrowserAccelerator::ZoomOut:
            adjustCurrentTabZoom(-0.1);
            break;
        case BrowserAccelerator::ResetZoom:
            resetCurrentTabZoom();
            break;
        case BrowserAccelerator::NewTab:
            openNewTab();
            break;
        case BrowserAccelerator::CloseTab:
            closeCurrentTab();
            break;
        case BrowserAccelerator::NextTab:
            cycleTab(1);
            break;
        case BrowserAccelerator::PreviousTab:
            cycleTab(-1);
            break;
        case BrowserAccelerator::FindInPage:
            showFindBar();
            break;
        case BrowserAccelerator::ReopenClosedTab:
            reopenClosedTab();
            break;
        case BrowserAccelerator::ExitFullScreen:
            if (findBar_ != nullptr && findBar_->isVisible()) {
                closeFindBar(true);
            } else {
                exitWebFullScreen();
            }
            break;
    }
}

void BrowserPage::onPermissionRequested(const std::uint64_t requestId,
                                        const QString& origin,
                                        const BrowserPermissionKind kind) {
    if (isShuttingDown_ || normalizedWebOrigin(origin) != origin) {
        backend_.answerPermission(requestId, BrowserPermissionDecision::Deny);
        return;
    }
    if (permissionStore_ != nullptr) {
        const BrowserPermissionState stored =
            permissionStore_->stateFor(origin, kind);
        if (stored == BrowserPermissionState::Allow &&
            kind != BrowserPermissionKind::ScreenCapture) {
            backend_.answerPermission(requestId,
                                      BrowserPermissionDecision::AllowOnce);
            return;
        }
        if (stored == BrowserPermissionState::Block) {
            backend_.answerPermission(requestId,
                                      BrowserPermissionDecision::Deny);
            return;
        }
    }
    if (pendingPermissionId_.has_value()) {
        resolvePermission(*pendingPermissionId_, BrowserPermissionDecision::Deny);
    }

    pendingPermissionId_ = requestId;
    pendingPermissionKind_ = kind;
    pendingPermissionOrigin_ = origin;
    permissionDialog_ = new BrowserPermissionDialog(origin, kind, this);
    connect(permissionDialog_, &BrowserPermissionDialog::decisionMade, this,
            [this, requestId](const BrowserPermissionDecision decision) {
                resolvePermission(requestId, decision);
            });
    auto* timeout = new QTimer(permissionDialog_);
    timeout->setObjectName(QStringLiteral("browserPermissionTimeout"));
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, this, [this, requestId] {
        resolvePermission(requestId, BrowserPermissionDecision::Deny);
    });
    timeout->start(30000);
    permissionDialog_->show();
    permissionDialog_->raise();
    permissionDialog_->activateWindow();
}

void BrowserPage::onExternalProtocolRequested(std::uint64_t requestId,
                                              const QString& origin,
                                              const QString& target) {
    if (isShuttingDown_ || normalizedWebOrigin(origin) != origin ||
        !isAcceptableExternalTarget(target)) {
        backend_.answerExternalProtocol(requestId, false);
        return;
    }
    if (pendingExternalProtocolId_.has_value()) {
        resolveExternalProtocol(*pendingExternalProtocolId_, false);
    }

    pendingExternalProtocolId_ = requestId;
    externalProtocolDialog_ = new QDialog(this);
    externalProtocolDialog_->setObjectName(
        QStringLiteral("browserExternalProtocolDialog"));
    externalProtocolDialog_->setWindowTitle(QStringLiteral("打开外部应用"));
    externalProtocolDialog_->setModal(false);
    auto* layout = new QVBoxLayout(externalProtocolDialog_);
    auto* explanation = new QLabel(
        QStringLiteral("确认后将由 Windows 默认应用处理。MediaHub 不识别或选择具体应用。"),
        externalProtocolDialog_);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    layout->addWidget(new QLabel(QStringLiteral("请求来源"),
                                 externalProtocolDialog_));
    auto* originLabel = new QLabel(origin, externalProtocolDialog_);
    originLabel->setObjectName(
        QStringLiteral("browserExternalProtocolOriginLabel"));
    originLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    originLabel->setWordWrap(true);
    layout->addWidget(originLabel);
    layout->addWidget(new QLabel(QStringLiteral("目标协议/目标"),
                                 externalProtocolDialog_));
    auto* targetLabel = new QLabel(target, externalProtocolDialog_);
    targetLabel->setObjectName(
        QStringLiteral("browserExternalProtocolTargetLabel"));
    targetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    targetLabel->setWordWrap(true);
    layout->addWidget(targetLabel);
    auto* buttons = new QHBoxLayout();
    auto* cancelButton = new QPushButton(QStringLiteral("取消"),
                                         externalProtocolDialog_);
    cancelButton->setObjectName(
        QStringLiteral("browserExternalProtocolCancelButton"));
    auto* confirmButton = new QPushButton(QStringLiteral("打开外部应用"),
                                          externalProtocolDialog_);
    confirmButton->setObjectName(
        QStringLiteral("browserExternalProtocolConfirmButton"));
    buttons->addStretch();
    buttons->addWidget(cancelButton);
    buttons->addWidget(confirmButton);
    layout->addLayout(buttons);
    connect(cancelButton, &QPushButton::clicked, this,
            [this, requestId] { resolveExternalProtocol(requestId, false); });
    connect(confirmButton, &QPushButton::clicked, this,
            [this, requestId] { resolveExternalProtocol(requestId, true); });
    connect(externalProtocolDialog_, &QDialog::rejected, this,
            [this, requestId] { resolveExternalProtocol(requestId, false); });
    auto* timeout = new QTimer(externalProtocolDialog_);
    timeout->setSingleShot(true);
    timeout->setObjectName(QStringLiteral("browserExternalProtocolTimeout"));
    connect(timeout, &QTimer::timeout, this,
            [this, requestId] { resolveExternalProtocol(requestId, false); });
    timeout->start(30000);
    externalProtocolDialog_->show();
    externalProtocolDialog_->raise();
    externalProtocolDialog_->activateWindow();
}

void BrowserPage::onCertificateErrorRequested(std::uint64_t requestId,
                                              const QString& origin,
                                              const QString& errorDescription) {
    if (isShuttingDown_ || normalizedWebOrigin(origin) != origin ||
        errorDescription.trimmed().isEmpty()) {
        backend_.answerCertificateError(
            requestId, BrowserCertificateDecision::ReturnToSafety);
        return;
    }
    if (pendingCertificateId_.has_value()) {
        resolveCertificateError(*pendingCertificateId_,
                                BrowserCertificateDecision::ReturnToSafety);
    }

    pendingCertificateId_ = requestId;
    certificateDialog_ = new QDialog(this);
    certificateDialog_->setObjectName(QStringLiteral("browserCertificateDialog"));
    certificateDialog_->setWindowTitle(QStringLiteral("证书安全警告"));
    certificateDialog_->setModal(false);
    auto* layout = new QVBoxLayout(certificateDialog_);
    auto* warning = new QLabel(
        QStringLiteral("此网站的服务器证书存在问题。继续仅对当前网页会话生效。"),
        certificateDialog_);
    warning->setWordWrap(true);
    layout->addWidget(warning);
    auto* originLabel = new QLabel(origin, certificateDialog_);
    originLabel->setObjectName(QStringLiteral("browserCertificateOriginLabel"));
    originLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(originLabel);
    auto* errorLabel = new QLabel(errorDescription, certificateDialog_);
    errorLabel->setObjectName(QStringLiteral("browserCertificateErrorLabel"));
    errorLabel->setWordWrap(true);
    layout->addWidget(errorLabel);
    auto* buttons = new QHBoxLayout();
    auto* safetyButton = new QPushButton(QStringLiteral("返回安全页面"),
                                         certificateDialog_);
    safetyButton->setObjectName(QStringLiteral("browserCertificateSafetyButton"));
    auto* continueButton = new QPushButton(QStringLiteral("仅本次会话继续"),
                                           certificateDialog_);
    continueButton->setObjectName(
        QStringLiteral("browserCertificateContinueButton"));
    buttons->addStretch();
    buttons->addWidget(safetyButton);
    buttons->addWidget(continueButton);
    layout->addLayout(buttons);
    connect(safetyButton, &QPushButton::clicked, this, [this, requestId] {
        resolveCertificateError(requestId,
                                BrowserCertificateDecision::ReturnToSafety);
    });
    connect(continueButton, &QPushButton::clicked, this, [this, requestId] {
        resolveCertificateError(requestId,
                                BrowserCertificateDecision::ContinueForSession);
    });
    connect(certificateDialog_, &QDialog::rejected, this, [this, requestId] {
        resolveCertificateError(requestId,
                                BrowserCertificateDecision::ReturnToSafety);
    });
    auto* timeout = new QTimer(certificateDialog_);
    timeout->setSingleShot(true);
    timeout->setObjectName(QStringLiteral("browserCertificateTimeout"));
    connect(timeout, &QTimer::timeout, this, [this, requestId] {
        resolveCertificateError(requestId,
                                BrowserCertificateDecision::ReturnToSafety);
    });
    timeout->start(30000);
    certificateDialog_->show();
    certificateDialog_->raise();
    certificateDialog_->activateWindow();
}

void BrowserPage::onDownloadRequested(const std::uint64_t requestId,
                                      const QString& origin,
                                      const QString& suggestedFileName,
                                      const std::int64_t totalBytes) {
    const QFileInfo suggestedInfo(suggestedFileName);
    const bool isSafeFileName = !suggestedFileName.trimmed().isEmpty() &&
                                suggestedInfo.fileName() == suggestedFileName &&
                                !suggestedFileName.contains(QLatin1Char('/')) &&
                                !suggestedFileName.contains(QLatin1Char('\\'));
    if (isShuttingDown_ || normalizedWebOrigin(origin) != origin ||
        !isSafeFileName || totalBytes < -1) {
        backend_.cancelDownload(requestId);
        return;
    }
    // 未显示页面的假后端测试仍保留旧单任务控件语义；真实 Runtime 使用下载中心。
    if (!backend_.supportsConcurrentDownloads()) {
        if (activeDownloadId_.has_value() && !downloadWidget_->isTerminal()) {
            backend_.cancelDownload(requestId);
            return;
        }
        activeDownloadId_ = requestId;
        isDownloadCancellationSent_ = false;
        downloadWidget_->beginDownload(requestId, origin, suggestedFileName,
                                       totalBytes);
        if (isWebFullScreen_) {
            // 全屏期间新出现的下载在退出全屏后必须恢复可见。
            wasDownloadWidgetHidden_ = false;
            downloadWidget_->hide();
        }
        return;
    }
    if (downloadCenter_ == nullptr ||
        !downloadCenter_->beginDownload(requestId, origin, suggestedFileName,
                                        totalBytes)) {
        backend_.cancelDownload(requestId);
        return;
    }
    downloadCenter_->show();
    if (isWebFullScreen_) {
        wasDownloadWidgetHidden_ = false;
        downloadCenter_->hide();
    }
}

void BrowserPage::onTabDownloadRequested(
    const std::uint64_t tabId, const std::uint64_t requestId,
    const QString& origin, const QString& suggestedFileName,
    const std::int64_t totalBytes) {
    if (findTabIndex(tabId) < 0) {
        backend_.cancelDownload(requestId);
        return;
    }
    downloadTabIds_.insert(requestId, tabId);
    onDownloadRequested(requestId, origin, suggestedFileName, totalBytes);
}

void BrowserPage::onDownloadUpdated(const std::uint64_t requestId,
                                    const BrowserDownloadState state,
                                    const std::int64_t receivedBytes,
                                    const std::int64_t totalBytes) {
    if (isShuttingDown_) {
        return;
    }
    if (!backend_.supportsConcurrentDownloads()) {
        if (!activeDownloadId_.has_value() || *activeDownloadId_ != requestId) {
            return;
        }
        downloadWidget_->updateDownload(requestId, state, receivedBytes,
                                       totalBytes);
        if (state == BrowserDownloadState::CancelFailed) {
            isDownloadCancellationSent_ = false;
        }
        return;
    }
    if (downloadCenter_ != nullptr) {
        downloadCenter_->updateDownload(requestId, state, receivedBytes,
                                        totalBytes);
    }
}

void BrowserPage::onTabDownloadUpdated(
    const std::uint64_t tabId, const std::uint64_t requestId,
    const BrowserDownloadState state, const std::int64_t receivedBytes,
    const std::int64_t totalBytes) {
    const auto found = downloadTabIds_.constFind(requestId);
    if (found == downloadTabIds_.cend() || found.value() != tabId) {
        return;
    }
    onDownloadUpdated(requestId, state, receivedBytes, totalBytes);
    if (state == BrowserDownloadState::Completed ||
        state == BrowserDownloadState::Failed ||
        state == BrowserDownloadState::Cancelled) {
        downloadTabIds_.remove(requestId);
    }
}

void BrowserPage::onBrowsingDataCleared(std::uint64_t generation) {
    if (generation != generation_ || isShuttingDown_) {
        return;
    }
    BrowserTabRecord& tab = tabs_[0];
    tab.address.clear();
    tab.title.clear();
    tab.canGoBack = false;
    tab.canGoForward = false;
    tab.state = BrowserPageState::Ready;
    tab.lastError.reset();
    tab.isUserMuted = false;
    tab.isPlayingAudio = false;
    closedTabs_.clear();
    if (sessionStore_ != nullptr) {
        try {
            static_cast<void>(sessionStore_->clear());
        } catch (...) {
        }
    }
    if (permissionStore_ != nullptr) {
        static_cast<void>(permissionStore_->clear());
    }
    if (downloadCenter_ != nullptr) {
        // 清除 Profile 同时移除下载中心记录，但不重新触发已结束任务的后端命令。
        static_cast<void>(downloadCenter_->clearForBrowsingData());
        downloadTabIds_.clear();
    }
    if (downloadWidget_ != nullptr) {
        downloadWidget_->hide();
        activeDownloadId_.reset();
        isDownloadCancellationSent_ = false;
    }
    backend_.setTabAudioMuted(tab.tabId, false);
    tabBar_->setTabText(0, QStringLiteral("新标签页"));
    state_ = BrowserPageState::Ready;
    addressEdit_->clear();
    titleLabel_->setText(QStringLiteral("网页"));
    statusLabel_->setText(QStringLiteral("网页数据已清除"));
    showHost();
    updateControls();
    updateAudioPresentation();
    updateAudibleTabCount();
}

void BrowserPage::onPopupRejected() {
    if (!isShuttingDown_) {
        statusLabel_->setText(QStringLiteral("无法打开新的网页标签"));
    }
}

bool BrowserPage::onNewTabRequested(const std::uint64_t newWindowRequestId,
                                    const QString& url) {
    if (isShuttingDown_ || hasBrowserProcessExited_ ||
        tabs_.size() >= maximumTabCount() ||
        url.trimmed().isEmpty()) {
        return false;
    }
    const QString trimmedUrl = url.trimmed();
    const bool isInternalBlank =
        trimmedUrl.compare(QStringLiteral("about:blank"),
                           Qt::CaseInsensitive) == 0;
    const BrowserAddress address = normalizeBrowserAddress(trimmedUrl);
    if (!isInternalBlank && address.kind != BrowserAddressKind::Web) {
        return false;
    }
    const QString initialUrl =
        isInternalBlank ? QStringLiteral("about:blank") : address.url;
    const std::uint64_t tabId = nextTabId_;
    const std::uint64_t tabGeneration = generation_ + 1;
    const auto nativeHandle = reinterpret_cast<void*>(
        static_cast<quintptr>(browserHost_->winId()));
    if (!backend_.createTab(nativeHandle, tabId, initialUrl, tabGeneration,
                            newWindowRequestId)) {
        return false;
    }
    closeFindBar(true);
    leaveWebFullScreenForTabChange();
    rejectUnansweredSensitiveRequests();
    generation_ = tabGeneration;
    ++nextTabId_;
    tabs_.append(BrowserTabRecord{tabId, tabGeneration, initialUrl, {}, false,
                                  false, BrowserPageState::Initializing});
    const int newIndex = tabs_.size() - 1;
    tabBar_->addTab(QStringLiteral("新标签页"));
    tabBar_->setTabData(newIndex,
                        QVariant::fromValue<qulonglong>(tabId));
    applyCachedFavicon(newIndex);
    currentTabIndex_ = newIndex;
    tabBar_->setCurrentIndex(newIndex);
    updateTabCloseButtons();
    backend_.activateTab(tabId);
    updateTabPresentation();
    updateTabSearchPresentation();
    return true;
}

void BrowserPage::onTabReady(const std::uint64_t tabId,
                             const std::uint64_t generation) {
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        tabs_.at(index).processFailure.has_value() || isShuttingDown_ ||
        hasBrowserProcessExited_) {
        return;
    }
    tabs_[index].state = BrowserPageState::Ready;
    tabs_[index].lastError.reset();
    tabs_[index].processFailure.reset();
    if (index == currentTabIndex_) {
        state_ = BrowserPageState::Ready;
        statusLabel_->setText(QStringLiteral("网页组件已就绪"));
        showHost();
        updateControls();
        updateBackendBounds();
    }
}

void BrowserPage::onTabNavigationStarted(const std::uint64_t tabId,
                                         const std::uint64_t generation) {
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        tabs_.at(index).processFailure.has_value() || isShuttingDown_ ||
        hasBrowserProcessExited_) {
        return;
    }
    tabs_[index].state = BrowserPageState::Navigating;
    tabs_[index].lastError.reset();
    tabs_[index].processFailure.reset();
    if (index == currentTabIndex_) {
        if (state_ == BrowserPageState::ClearingData) {
            return;
        }
        closeFindBar(true);
        rejectUnansweredSensitiveRequests();
        state_ = BrowserPageState::Navigating;
        statusLabel_->setText(QStringLiteral("正在载入..."));
        updateControls();
    }
}

void BrowserPage::onTabNavigationCompleted(
    const std::uint64_t tabId, const std::uint64_t generation,
    const QString& visibleUrl, const QString& title, const bool canGoBack,
    const bool canGoForward) {
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        tabs_.at(index).processFailure.has_value() || isShuttingDown_ ||
        hasBrowserProcessExited_ || state_ == BrowserPageState::ClearingData) {
        return;
    }
    applyTabDocumentState(index, visibleUrl, title, canGoBack, canGoForward,
                          true, true);
}

void BrowserPage::onTabDocumentStateChanged(
    const std::uint64_t tabId, const std::uint64_t generation,
    const QString& visibleUrl, const QString& title, const bool canGoBack,
    const bool canGoForward) {
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        tabs_.at(index).processFailure.has_value() || isShuttingDown_ ||
        hasBrowserProcessExited_ || state_ == BrowserPageState::ClearingData) {
        return;
    }
    applyTabDocumentState(index, visibleUrl, title, canGoBack, canGoForward,
                          false, false);
}

void BrowserPage::onTabNavigationStopped(
    const std::uint64_t tabId, const std::uint64_t generation,
    const QString& visibleUrl, const QString& title, const bool canGoBack,
    const bool canGoForward) {
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        tabs_.at(index).processFailure.has_value() || isShuttingDown_ ||
        hasBrowserProcessExited_ || state_ == BrowserPageState::ClearingData) {
        return;
    }
    applyTabDocumentState(index, visibleUrl, title, canGoBack, canGoForward,
                          true, false);
}

void BrowserPage::applyTabDocumentState(
    const int index, const QString& visibleUrl, const QString& title,
    const bool canGoBack, const bool canGoForward,
    const bool didFinishNavigation, const bool shouldRecordHistory) {
    BrowserTabRecord& tab = tabs_[index];
    const bool didAddressChange = tab.address != visibleUrl;
    tab.address = visibleUrl;
    tab.title = title;
    tab.canGoBack = canGoBack;
    tab.canGoForward = canGoForward;
    if (didFinishNavigation) {
        tab.state = BrowserPageState::Ready;
        tab.lastError.reset();
        tab.recoveryAttempts = 0;
        tab.lastRecoveryAt = {};
    }
    tabBar_->setTabText(index,
                        title.isEmpty() ? QStringLiteral("新标签页") : title);
    if (didAddressChange) {
        applyCachedFavicon(index);
    }
    if (index == currentTabIndex_) {
        addressEdit_->setText(visibleUrl);
        titleLabel_->setText(title.isEmpty() ? QStringLiteral("网页") : title);
        backButton_->setEnabled(canGoBack);
        forwardButton_->setEnabled(canGoForward);
        if (didFinishNavigation) {
            state_ = BrowserPageState::Ready;
            statusLabel_->setText(QStringLiteral("载入完成"));
            showHost();
        }
        updateControls();
    }
    if (shouldRecordHistory) {
        recordSuccessfulNavigation(visibleUrl, title);
    } else if (!didFinishNavigation) {
        updateRecordedNavigationTitle(visibleUrl, title);
    }
    updateTabGroupPresentation();
}

void BrowserPage::onTabError(const std::uint64_t tabId,
                             const std::uint64_t generation,
                             const BrowserErrorKind kind,
                             const long errorCode) {
    Q_UNUSED(errorCode);
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        isShuttingDown_ || hasBrowserProcessExited_) {
        return;
    }
    tabs_[index].state = BrowserPageState::Failed;
    tabs_[index].lastError = kind;
    tabs_[index].processFailure.reset();
    if (index == currentTabIndex_) {
        state_ = BrowserPageState::Failed;
        showError(kind);
        updateControls();
    }
}

void BrowserPage::onTabProcessFailed(
    const std::uint64_t tabId, const std::uint64_t generation,
    const BrowserProcessFailureKind kind) {
    const int index = findTabIndex(tabId);
    if (isShuttingDown_ || index < 0 ||
        tabs_.at(index).generation != generation) {
        return;
    }
    if (kind == BrowserProcessFailureKind::OtherProcessExited) {
        if (index == currentTabIndex_) {
            statusLabel_->setText(
                QStringLiteral("网页辅助进程已退出，WebView2 正在自行恢复"));
        }
        return;
    }
    if (kind == BrowserProcessFailureKind::BrowserProcessExited) {
        hasBrowserProcessExited_ = true;
        for (BrowserTabRecord& tab : tabs_) {
            tab.state = BrowserPageState::Failed;
            tab.lastError.reset();
            tab.processFailure = kind;
        }
    } else {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 cooldownMilliseconds = recoveryCooldownMilliseconds();
        if (!tabs_[index].lastRecoveryAt.isValid() ||
            tabs_[index].lastRecoveryAt.msecsTo(now) >
                cooldownMilliseconds) {
            tabs_[index].recoveryAttempts = 0;
        }
        tabs_[index].lastRecoveryAt = now;
        ++tabs_[index].recoveryAttempts;
        tabs_[index].state = BrowserPageState::Failed;
        tabs_[index].lastError.reset();
        tabs_[index].processFailure = kind;
        if (tabs_[index].recoveryAttempts >=
            kMaximumRecoveryAttemptsPerWindow) {
            const std::uint64_t failedTabId = tabs_[index].tabId;
            QTimer::singleShot(static_cast<int>(cooldownMilliseconds), this,
                               [this, failedTabId] {
                                   refreshRecoveryCooldown(failedTabId);
                               });
        }
    }
    if (index == currentTabIndex_ || hasBrowserProcessExited_) {
        state_ = BrowserPageState::Failed;
        showTabProcessFailure();
        updateControls();
    }
}

void BrowserPage::onTabCloseRequested(const std::uint64_t tabId) {
    for (int index = 0; index < tabs_.size(); ++index) {
        if (tabs_.at(index).tabId == tabId) {
            closeTab(index);
            return;
        }
    }
}

void BrowserPage::onTabAudioStateChanged(const std::uint64_t tabId,
                                         const std::uint64_t generation,
                                         const bool isPlayingAudio) {
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        isShuttingDown_) {
        return;
    }
    const bool wasListed =
        tabs_.at(index).isPlayingAudio || tabs_.at(index).isUserMuted;
    tabs_[index].isPlayingAudio = isPlayingAudio;
    updateAudioTabPresentation(tabId, wasListed);
    updateAudibleTabCount();
}

void BrowserPage::onTabFaviconChanged(const std::uint64_t tabId,
                                      const std::uint64_t generation,
                                      const QByteArray& pngBytes) {
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        isShuttingDown_) {
        return;
    }
    if (pngBytes.isEmpty()) {
        tabBar_->setTabIcon(index, QIcon{});
        return;
    }
    if (!faviconCache_.put(tabs_.at(index).address, pngBytes)) {
        return;
    }
    applyCachedFavicon(index);
}

void BrowserPage::onTabZoomFactorChanged(const std::uint64_t tabId,
                                         const std::uint64_t generation,
                                         const double zoomFactor) {
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        isShuttingDown_) {
        return;
    }
    tabs_[index].zoomFactor = std::clamp(zoomFactor, 0.25, 5.0);
    if (index == currentTabIndex_) {
        zoomResetButton_->setText(QStringLiteral("%1%").arg(
            qRound(tabs_.at(index).zoomFactor * 100.0)));
    }
}

void BrowserPage::onFindResultChanged(const std::uint64_t tabId,
                                      const std::uint64_t generation,
                                      const int activeMatchIndex,
                                      const int matchCount) {
    if (isShuttingDown_ || findBar_ == nullptr || !findBar_->isVisible() ||
        findTabId_ == 0) {
        return;
    }
    const int index = findTabIndex(findTabId_);
    if (index < 0 || tabId != findTabId_ ||
        tabs_.at(index).generation != generation ||
        generation != findTabGeneration_) {
        return;
    }
    updateFindResult(activeMatchIndex, matchCount);
}

void BrowserPage::onFindFailed(const std::uint64_t tabId,
                               const std::uint64_t generation,
                               const long errorCode) {
    Q_UNUSED(errorCode);
    if (isShuttingDown_ || findBar_ == nullptr || !findBar_->isVisible() ||
        findTabId_ == 0) {
        return;
    }
    const int index = findTabIndex(findTabId_);
    if (index < 0 || tabId != findTabId_ ||
        tabs_.at(index).generation != generation ||
        generation != findTabGeneration_) {
        return;
    }
    findResultLabel_->setText(QStringLiteral("不支持页内查找"));
}

bool BrowserPage::eventFilter(QObject* const watched, QEvent* const event) {
    if (watched == findEdit_ && event->type() == QEvent::KeyPress) {
        auto* const keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && findBar_->isVisible()) {
            closeFindBar(true);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BrowserPage::keyPressEvent(QKeyEvent* const event) {
    if (event->key() == Qt::Key_Escape && findBar_ != nullptr &&
        findBar_->isVisible()) {
        closeFindBar(true);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && isWebFullScreen_ && !isShuttingDown_) {
        backend_.exitFullScreen();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(Qt::AltModifier) &&
        !event->modifiers().testFlag(Qt::ControlModifier) &&
        !event->modifiers().testFlag(Qt::ShiftModifier) &&
        !event->modifiers().testFlag(Qt::MetaModifier)) {
        if (event->key() == Qt::Key_Left) {
            backend_.goBack();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            backend_.goForward();
            event->accept();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

void BrowserPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (isInitialized_ || isShuttingDown_) {
        return;
    }
    isInitialized_ = true;
    state_ = BrowserPageState::Initializing;
    statusLabel_->setText(QStringLiteral("正在初始化网页组件..."));
    updateControls();
    const auto nativeHandle = reinterpret_cast<void*>(
        static_cast<quintptr>(browserHost_->winId()));
    backend_.initialize(nativeHandle, userDataDirectory_, generation_);
}

void BrowserPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResponsiveStyle();
    updateBackendBounds();
}

void BrowserPage::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);

    toolbar_ = new QFrame(this);
    toolbar_->setObjectName(QStringLiteral("browserToolbar"));
    auto* toolbarLayout = new QHBoxLayout(toolbar_);
    toolbarLayout->setContentsMargins(10, 8, 10, 8);
    toolbarLayout->setSpacing(6);

    tabBar_ = new BrowserTabBar(this);
    tabBar_->setObjectName(QStringLiteral("browserTabBar"));
    tabBar_->setTabsClosable(true);
    tabBar_->setMovable(true);
    tabBar_->setContextMenuPolicy(Qt::CustomContextMenu);
    newTabButton_ = createToolButton(QStringLiteral("browserNewTabButton"),
                                     QStringLiteral("+"), this);
    newTabButton_->setToolTip(QStringLiteral("新建网页标签（Ctrl+T）"));
    tabSearchButton_ = createToolButton(
        QStringLiteral("browserTabSearchButton"), QStringLiteral("搜索"), this);
    tabSearchButton_->setToolTip(QStringLiteral("搜索已打开的标签（Ctrl+Shift+A）"));

    backButton_ = createToolButton(QStringLiteral("browserBackButton"),
                                   QStringLiteral("后退"), toolbar_);
    forwardButton_ = createToolButton(QStringLiteral("browserForwardButton"),
                                      QStringLiteral("前进"), toolbar_);
    reloadButton_ = createToolButton(QStringLiteral("browserReloadButton"),
                                     QStringLiteral("刷新"), toolbar_);
    homeButton_ = createToolButton(QStringLiteral("browserHomeButton"),
                                   QStringLiteral("主页"), toolbar_);
    addressEdit_ = new BrowserAddressEdit(toolbar_);
    addressEdit_->setObjectName(QStringLiteral("browserAddressEdit"));
    addressEdit_->setPlaceholderText(QStringLiteral("输入网站地址"));
    addressEdit_->setMinimumWidth(180);
    goButton_ = new QPushButton(QStringLiteral("访问"), toolbar_);
    goButton_->setObjectName(QStringLiteral("browserGoButton"));
    clearDataButton_ = new QPushButton(QStringLiteral("清除"), toolbar_);
    clearDataButton_->setObjectName(QStringLiteral("browserClearDataButton"));
    clearDataButton_->setToolTip(QStringLiteral("清除网页 Cookie、缓存和宿主保存状态"));
    historyButton_ = createToolButton(QStringLiteral("browserHistoryButton"),
                                       QStringLiteral("历史"), toolbar_);
    favoritesButton_ = createToolButton(
        QStringLiteral("browserFavoritesButton"), QStringLiteral("收藏夹"),
        toolbar_);
    startupSettingsButton_ = createToolButton(
        QStringLiteral("browserStartupSettingsButton"), QStringLiteral("启动"),
        toolbar_);
    startupSettingsButton_->setToolTip(
        QStringLiteral("主页、启动页和会话恢复设置"));
    permissionSettingsButton_ = createToolButton(
        QStringLiteral("browserPermissionSettingsButton"),
        QStringLiteral("权限"), toolbar_);
    permissionSettingsButton_->setToolTip(
        QStringLiteral("管理网站摄像头、麦克风和通知权限"));
    currentTabMuteButton_ = createToolButton(
        QStringLiteral("browserCurrentTabMuteButton"), QStringLiteral("静音"),
        toolbar_);
    audioTabsButton_ = createToolButton(
        QStringLiteral("browserAudioTabsButton"), QStringLiteral("声音"),
        toolbar_);
    zoomOutButton_ = createToolButton(
        QStringLiteral("browserZoomOutButton"), QStringLiteral("-"), toolbar_);
    zoomResetButton_ = createToolButton(
        QStringLiteral("browserZoomResetButton"), QStringLiteral("100%"),
        toolbar_);
    zoomInButton_ = createToolButton(
        QStringLiteral("browserZoomInButton"), QStringLiteral("+"), toolbar_);
    zoomOutButton_->setToolTip(QStringLiteral("缩小当前网页（Ctrl+-）"));
    zoomResetButton_->setToolTip(QStringLiteral("重置当前网页缩放（Ctrl+0）"));
    zoomInButton_->setToolTip(QStringLiteral("放大当前网页（Ctrl++）"));

    toolbarLayout->addWidget(backButton_);
    toolbarLayout->addWidget(forwardButton_);
    toolbarLayout->addWidget(reloadButton_);
    toolbarLayout->addWidget(homeButton_);
    toolbarLayout->addWidget(addressEdit_, 1);
    toolbarLayout->addWidget(goButton_);
    toolbarLayout->addWidget(historyButton_);
    toolbarLayout->addWidget(favoritesButton_);
    toolbarLayout->addWidget(clearDataButton_);

    informationRow_ = new QWidget(this);
    informationRow_->setObjectName(QStringLiteral("browserInformationRow"));
    auto* informationLayout = new QHBoxLayout(informationRow_);
    informationLayout->setContentsMargins(10, 0, 10, 0);
    titleLabel_ = new QLabel(QStringLiteral("网页"), informationRow_);
    titleLabel_->setObjectName(QStringLiteral("browserTitleLabel"));
    statusLabel_ = new QLabel(QStringLiteral("网页组件尚未初始化"), informationRow_);
    statusLabel_->setObjectName(QStringLiteral("browserStatusLabel"));
    statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    informationLayout->addWidget(titleLabel_, 1);
    informationLayout->addWidget(statusLabel_);
    informationLayout->addWidget(startupSettingsButton_);
    informationLayout->addWidget(permissionSettingsButton_);
    informationLayout->addWidget(zoomOutButton_);
    informationLayout->addWidget(zoomResetButton_);
    informationLayout->addWidget(zoomInButton_);
    informationLayout->addWidget(currentTabMuteButton_);
    informationLayout->addWidget(audioTabsButton_);

    findBar_ = new QFrame(this);
    findBar_->setObjectName(QStringLiteral("browserFindBar"));
    auto* const findLayout = new QHBoxLayout(findBar_);
    findLayout->setContentsMargins(10, 4, 10, 4);
    findLayout->setSpacing(6);
    findEdit_ = new QLineEdit(findBar_);
    findEdit_->setObjectName(QStringLiteral("browserFindEdit"));
    findEdit_->setPlaceholderText(QStringLiteral("在当前网页中查找"));
    findEdit_->installEventFilter(this);
    findResultLabel_ = new QLabel(QStringLiteral("0/0"), findBar_);
    findResultLabel_->setObjectName(QStringLiteral("browserFindResultLabel"));
    findPreviousButton_ = new QPushButton(QStringLiteral("上一个"), findBar_);
    findPreviousButton_->setObjectName(
        QStringLiteral("browserFindPreviousButton"));
    findNextButton_ = new QPushButton(QStringLiteral("下一个"), findBar_);
    findNextButton_->setObjectName(QStringLiteral("browserFindNextButton"));
    findCloseButton_ = createToolButton(QStringLiteral("browserFindCloseButton"),
                                        QStringLiteral("关闭"), findBar_);
    findCloseButton_->setToolTip(QStringLiteral("关闭页内查找（Esc）"));
    findLayout->addWidget(findEdit_, 1);
    findLayout->addWidget(findResultLabel_);
    findLayout->addWidget(findPreviousButton_);
    findLayout->addWidget(findNextButton_);
    findLayout->addWidget(findCloseButton_);
    findBar_->hide();

    auto* content = new QWidget(this);
    contentStack_ = new QStackedLayout(content);
    contentStack_->setContentsMargins(0, 0, 0, 0);
    browserHost_ = new QWidget(content);
    browserHost_->setObjectName(QStringLiteral("browserNativeHost"));
    browserHost_->setAttribute(Qt::WA_NativeWindow);
    browserHost_->setMinimumSize(320, 240);
    errorLabel_ = new QLabel(QStringLiteral("网页组件尚未初始化"), content);
    errorLabel_->setObjectName(QStringLiteral("browserErrorLabel"));
    errorLabel_->setAlignment(Qt::AlignCenter);
    errorLabel_->setWordWrap(true);
    contentStack_->addWidget(browserHost_);
    contentStack_->addWidget(errorLabel_);

    processFailurePage_ = new QWidget(content);
    processFailurePage_->setObjectName(
        QStringLiteral("browserProcessFailurePage"));
    auto* const processFailureLayout = new QVBoxLayout(processFailurePage_);
    processFailureLayout->setContentsMargins(36, 36, 36, 36);
    processFailureLayout->setSpacing(12);
    processFailureLayout->addStretch();
    processFailureTitleLabel_ = new QLabel(processFailurePage_);
    processFailureTitleLabel_->setObjectName(
        QStringLiteral("browserProcessFailureTitle"));
    processFailureTitleLabel_->setAlignment(Qt::AlignCenter);
    processFailureDetailLabel_ = new QLabel(processFailurePage_);
    processFailureDetailLabel_->setObjectName(
        QStringLiteral("browserProcessFailureDetail"));
    processFailureDetailLabel_->setAlignment(Qt::AlignCenter);
    processFailureDetailLabel_->setWordWrap(true);
    processRecoveryButton_ = new QPushButton(
        QStringLiteral("重新加载标签"), processFailurePage_);
    processRecoveryButton_->setObjectName(
        QStringLiteral("browserProcessRecoveryButton"));
    processRecoveryButton_->setMaximumWidth(180);
    processFailureLayout->addWidget(processFailureTitleLabel_);
    processFailureLayout->addWidget(processFailureDetailLabel_);
    processFailureLayout->addWidget(processRecoveryButton_, 0,
                                    Qt::AlignHCenter);
    processFailureLayout->addStretch();
    contentStack_->addWidget(processFailurePage_);
    contentStack_->setCurrentWidget(errorLabel_);

    auto* const tabRow = new QHBoxLayout();
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(6);
    tabRow->addWidget(tabBar_, 1);
    tabRow->addWidget(tabSearchButton_);
    tabGroupButton_ = createToolButton(
        QStringLiteral("browserTabGroupButton"), QStringLiteral("分组"), this);
    tabGroupButton_->setToolTip(QStringLiteral("管理标签分组"));
    tabRow->addWidget(tabGroupButton_);
    tabRow->addWidget(newTabButton_);
    rootLayout->addLayout(tabRow);
    rootLayout->addWidget(toolbar_);
    rootLayout->addWidget(informationRow_);
    rootLayout->addWidget(findBar_);
    downloadWidget_ = new BrowserDownloadWidget(this);
    downloadWidget_->hide();
    downloadCenter_ = new BrowserDownloadCenter(this);
    downloadCenter_->setObjectName(QStringLiteral("browserDownloadCenter"));
    downloadCenter_->hide();
    rootLayout->addWidget(downloadCenter_);
    rootLayout->addWidget(content, 1);

    updateResponsiveStyle();

    connect(addressEdit_, &QLineEdit::returnPressed, this,
            &BrowserPage::submitAddress);
    connect(tabBar_, &QTabBar::currentChanged, this,
            &BrowserPage::activateTab);
    connect(tabBar_, &QTabBar::tabCloseRequested, this,
            &BrowserPage::closeTab);
    connect(tabBar_, &QTabBar::customContextMenuRequested, this,
            &BrowserPage::showTabContextMenu);
    connect(newTabButton_, &QToolButton::clicked, this,
            &BrowserPage::openNewTab);
    connect(tabSearchButton_, &QToolButton::clicked, this,
            &BrowserPage::showTabSearch);
    connect(tabGroupButton_, &QToolButton::clicked, this,
            &BrowserPage::showTabGroups);
    connect(tabBar_, &QTabBar::tabMoved, this,
            [this](const int from, const int to) {
                if (from < 0 || from >= tabs_.size() || to < 0 ||
                    to >= tabs_.size()) {
                    return;
                }
                tabs_.move(from, to);
                const QVariant currentData =
                    tabBar_->tabData(tabBar_->currentIndex());
                currentTabIndex_ = currentData.isValid()
                                       ? findTabIndex(currentData.toULongLong())
                                       : tabBar_->currentIndex();
                if (!isNormalizingPinnedTabs_) {
                    normalizePinnedTabOrder();
                }
                updateTabCloseButtons();
                updateTabGroupPresentation();
            });
    connect(goButton_, &QPushButton::clicked, this, &BrowserPage::submitAddress);
    connect(backButton_, &QToolButton::clicked, this, [this] { backend_.goBack(); });
    connect(forwardButton_, &QToolButton::clicked, this,
            [this] { backend_.goForward(); });
    connect(reloadButton_, &QToolButton::clicked, this,
            [this] { backend_.reloadOrStop(); });
    connect(processRecoveryButton_, &QPushButton::clicked, this,
            &BrowserPage::recoverFailedTab);
    connect(homeButton_, &QToolButton::clicked, this,
            &BrowserPage::openConfiguredHome);
    connect(clearDataButton_, &QPushButton::clicked, this,
            &BrowserPage::showClearDataConfirmation);
    connect(historyButton_, &QToolButton::clicked, this,
            &BrowserPage::showHistory);
    connect(favoritesButton_, &QToolButton::clicked, this,
            &BrowserPage::showFavorites);
    connect(startupSettingsButton_, &QToolButton::clicked, this,
            &BrowserPage::showStartupSettings);
    connect(permissionSettingsButton_, &QToolButton::clicked, this,
            &BrowserPage::showPermissionSettings);
    connect(currentTabMuteButton_, &QToolButton::clicked, this,
            &BrowserPage::toggleCurrentTabMuted);
    connect(audioTabsButton_, &QToolButton::clicked, this,
            &BrowserPage::showAudioTabs);
    connect(zoomOutButton_, &QToolButton::clicked, this,
            [this] { adjustCurrentTabZoom(-0.1); });
    connect(zoomResetButton_, &QToolButton::clicked, this,
            &BrowserPage::resetCurrentTabZoom);
    connect(zoomInButton_, &QToolButton::clicked, this,
            [this] { adjustCurrentTabZoom(0.1); });
    connect(findEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                if (findBar_->isVisible()) {
                    updateFindResult(-1, 0);
                    if (text.isEmpty()) {
                        backend_.stopFinding(true);
                    } else {
                        backend_.findInPage(text, true);
                    }
                }
            });
    connect(findEdit_, &QLineEdit::returnPressed, this,
            [this] {
                findNext(!QApplication::keyboardModifiers().testFlag(
                    Qt::ShiftModifier));
            });
    connect(findPreviousButton_, &QPushButton::clicked, this,
            [this] { findNext(false); });
    connect(findNextButton_, &QPushButton::clicked, this,
            [this] { findNext(true); });
    connect(findCloseButton_, &QToolButton::clicked, this,
            [this] { closeFindBar(true); });
    connect(downloadWidget_, &BrowserDownloadWidget::destinationChosen, this,
            [this](const std::uint64_t requestId, const QString& destination) {
                if (!isShuttingDown_ && activeDownloadId_.has_value() &&
                    *activeDownloadId_ == requestId) {
                    backend_.chooseDownloadPath(requestId, destination);
                }
            });
    connect(downloadWidget_, &BrowserDownloadWidget::cancelRequested, this,
            [this](const std::uint64_t requestId) {
                if (!isShuttingDown_ && activeDownloadId_.has_value() &&
                    *activeDownloadId_ == requestId &&
                    !isDownloadCancellationSent_) {
                    isDownloadCancellationSent_ = true;
                    backend_.cancelDownload(requestId);
                }
            });
    connect(downloadCenter_, &BrowserDownloadCenter::destinationChosen, this,
            [this](const std::uint64_t requestId, const QString& destination) {
                if (!isShuttingDown_) {
                    backend_.chooseDownloadPath(requestId, destination);
                }
            });
    connect(downloadCenter_, &BrowserDownloadCenter::cancelRequested, this,
            [this](const std::uint64_t requestId) {
                if (!isShuttingDown_) {
                    backend_.cancelDownload(requestId);
                }
            });
    connect(downloadCenter_, &BrowserDownloadCenter::retryRequested, this,
            [this](const std::uint64_t requestId) {
                if (!isShuttingDown_) {
                    backend_.retryDownload(requestId);
                }
            });

    auto* focusAddress = new QShortcut(QKeySequence(QStringLiteral("Ctrl+L")), this);
    connect(focusAddress, &QShortcut::activated, this, [this] {
        addressEdit_->setFocus();
        addressEdit_->selectAll();
    });
    auto* reload = new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this);
    connect(reload, &QShortcut::activated, this, [this] { backend_.reloadOrStop(); });
    auto* reloadF5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(reloadF5, &QShortcut::activated, this,
            [this] { backend_.reloadOrStop(); });
    auto* const newTab =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+T")), this);
    connect(newTab, &QShortcut::activated, this, &BrowserPage::openNewTab);
    auto* const closeCurrent =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+W")), this);
    connect(closeCurrent, &QShortcut::activated, this,
            &BrowserPage::closeCurrentTab);
    auto* const nextTab =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), this);
    connect(nextTab, &QShortcut::activated, this,
            [this] { cycleTab(1); });
    auto* const previousTab = new QShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this);
    connect(previousTab, &QShortcut::activated, this,
            [this] { cycleTab(-1); });
    auto* const reopenClosed = new QShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+T")), this);
    connect(reopenClosed, &QShortcut::activated, this,
            &BrowserPage::reopenClosedTab);
    auto* const findInPage =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this);
    connect(findInPage, &QShortcut::activated, this, &BrowserPage::showFindBar);
    auto* const searchTabs = new QShortcut(
        QKeySequence(QStringLiteral("Ctrl+Shift+A")), this);
    connect(searchTabs, &QShortcut::activated, this,
            &BrowserPage::showTabSearch);
    auto* const zoomIn = new QShortcut(QKeySequence::ZoomIn, this);
    connect(zoomIn, &QShortcut::activated, this,
            [this] { adjustCurrentTabZoom(0.1); });
    auto* const zoomOut = new QShortcut(QKeySequence::ZoomOut, this);
    connect(zoomOut, &QShortcut::activated, this,
            [this] { adjustCurrentTabZoom(-0.1); });
    auto* const resetZoom =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this);
    connect(resetZoom, &QShortcut::activated, this,
            &BrowserPage::resetCurrentTabZoom);
}

void BrowserPage::updateResponsiveStyle() {
    QString sizeKey;
    if (width() >= 1500) {
        sizeKey = QStringLiteral("extraLarge");
    } else if (width() >= 1200) {
        sizeKey = QStringLiteral("large");
    } else if (width() >= 900) {
        sizeKey = QStringLiteral("normal");
    } else {
        sizeKey = QStringLiteral("compact");
    }
    if (responsiveSize_ == sizeKey) {
        return;
    }
    responsiveSize_ = sizeKey;
    setProperty("responsiveSize", sizeKey);
    const bool isCompact = sizeKey == QStringLiteral("compact");
    backButton_->setText(isCompact ? QStringLiteral("<") : QStringLiteral("后退"));
    forwardButton_->setText(isCompact ? QStringLiteral(">") : QStringLiteral("前进"));
    reloadButton_->setText(isCompact ? QStringLiteral("R") : QStringLiteral("刷新"));
    homeButton_->setText(isCompact ? QStringLiteral("H") : QStringLiteral("主页"));
    historyButton_->setText(isCompact ? QStringLiteral("历") : QStringLiteral("历史"));
    favoritesButton_->setText(isCompact ? QStringLiteral("藏") : QStringLiteral("收藏夹"));
    clearDataButton_->setText(isCompact ? QStringLiteral("清") : QStringLiteral("清除"));
    startupSettingsButton_->setText(isCompact ? QStringLiteral("启") : QStringLiteral("启动"));
    permissionSettingsButton_->setText(isCompact ? QStringLiteral("权") : QStringLiteral("权限"));
    currentTabMuteButton_->setText(isCompact ? QStringLiteral("静") : QStringLiteral("静音"));
    audioTabsButton_->setText(isCompact ? QStringLiteral("声") : QStringLiteral("声音"));
    if (auto* const informationLayout =
            qobject_cast<QHBoxLayout*>(informationRow_->layout())) {
        informationLayout->setContentsMargins(isCompact ? 4 : 10, 0,
                                               isCompact ? 4 : 10, 0);
        informationLayout->setSpacing(isCompact ? 2 : 6);
    }
    const QList<QWidget*> toolbarControls{
        backButton_, forwardButton_, reloadButton_, homeButton_, goButton_,
        historyButton_, favoritesButton_, clearDataButton_};
    for (QWidget* const control : toolbarControls) {
        if (control != nullptr) {
            control->setMinimumWidth(isCompact ? 0 : control->minimumSizeHint().width());
        }
    }
    if (auto* const toolbarLayout =
            qobject_cast<QHBoxLayout*>(toolbar_->layout())) {
        toolbarLayout->setContentsMargins(isCompact ? 5 : 10,
                                          isCompact ? 5 : 8,
                                          isCompact ? 5 : 10,
                                          isCompact ? 5 : 8);
        toolbarLayout->setSpacing(isCompact ? 2 : 6);
    }
    const QList<QWidget*> widgets{this, toolbar_, addressEdit_, goButton_,
                                  clearDataButton_, backButton_, forwardButton_,
                                  reloadButton_, homeButton_, historyButton_,
                                  favoritesButton_, startupSettingsButton_,
                                  permissionSettingsButton_,
                                  currentTabMuteButton_,
                                  audioTabsButton_, zoomOutButton_,
                                  zoomResetButton_, zoomInButton_, newTabButton_,
                                  tabSearchButton_, findBar_,
                                  findEdit_, findResultLabel_,
                                  findPreviousButton_, findNextButton_,
                                  findCloseButton_};
    for (QWidget* const widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        widget->setProperty("responsiveSize", sizeKey);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->updateGeometry();
    }
}

void BrowserPage::openNewTab() {
    if (tabs_.size() >= maximumTabCount()) {
        statusLabel_->setText(QStringLiteral("已达到网页标签数量上限"));
        return;
    }
    static_cast<void>(onNewTabRequested(0, kBrowserHomeUrl));
}

void BrowserPage::reopenClosedTab() {
    if (isShuttingDown_ || closedTabs_.isEmpty()) {
        statusLabel_->setText(QStringLiteral("没有可恢复的已关闭标签"));
        return;
    }
    if (tabs_.size() >= maximumTabCount()) {
        statusLabel_->setText(QStringLiteral("已达到网页标签数量上限"));
        return;
    }
    const ClosedTabRecord tab = closedTabs_.constLast();
    if (!openRestoredTab(tab.address, tab.title, tab.isUserMuted, tab.groupId,
                         tab.isPinned, tab.zoomFactor)) {
        statusLabel_->setText(QStringLiteral("无法恢复已关闭标签"));
        return;
    }
    closedTabs_.removeLast();
}

void BrowserPage::showStartupSettings() {
    if (startupSettingsStore_ == nullptr) {
        statusLabel_->setText(QStringLiteral("启动设置暂不可用"));
        return;
    }
    if (startupSettingsDialog_ == nullptr) {
        startupSettingsDialog_ =
            new BrowserStartupSettingsDialog(*startupSettingsStore_, this);
        startupSettingsDialog_->setObjectName(
            QStringLiteral("browserStartupSettingsDialog"));
    }
    QStringList urls;
    urls.reserve(tabs_.size());
    for (const BrowserTabRecord& tab : tabs_) {
        urls.append(tab.address);
    }
    startupSettingsDialog_->setCurrentTabUrls(urls, currentTabIndex_);
    startupSettingsDialog_->reload();
    startupSettingsDialog_->show();
    startupSettingsDialog_->raise();
    startupSettingsDialog_->activateWindow();
}

void BrowserPage::showPermissionSettings() {
    if (permissionStore_ == nullptr) {
        statusLabel_->setText(QStringLiteral("网站权限设置暂不可用"));
        return;
    }
    if (permissionSettingsDialog_ == nullptr) {
        permissionSettingsDialog_ =
            new BrowserPermissionManagementDialog(*permissionStore_, this);
        permissionSettingsDialog_->setObjectName(
            QStringLiteral("browserPermissionManagementDialog"));
    }
    permissionSettingsDialog_->reloadEntries();
    permissionSettingsDialog_->show();
    permissionSettingsDialog_->raise();
    permissionSettingsDialog_->activateWindow();
}

void BrowserPage::openConfiguredHome() {
    QString homeUrl = kBrowserHomeUrl;
    if (startupSettingsStore_ != nullptr) {
        try {
            homeUrl = startupSettingsStore_->load().homeUrl;
        } catch (...) {
            homeUrl = kBrowserHomeUrl;
        }
    }
    const BrowserAddress address = normalizeBrowserAddress(homeUrl);
    navigateTo(address.kind == BrowserAddressKind::Web ? address.url
                                                       : kBrowserHomeUrl);
}

void BrowserPage::openInitialTabs() {
    BrowserStartupSettings settings;
    if (startupSettingsStore_ != nullptr) {
        try {
            settings = startupSettingsStore_->load();
        } catch (...) {
            settings = BrowserStartupSettings{};
        }
    }

    if (settings.mode == BrowserStartupMode::RestoreSession &&
        sessionStore_ != nullptr) {
        std::optional<BrowserSessionState> restored;
        try {
            restored = sessionStore_->load();
        } catch (...) {
            restored.reset();
        }
        if (!restored.has_value()) {
            try {
                static_cast<void>(sessionStore_->clear());
            } catch (...) {
            }
        } else if (!restored->tabs.isEmpty()) {
            tabGroupModel_.replace(restored->groups);
            updateTabGroupDialogPresentation();
            const int savedCurrentIndex = std::clamp(
                restored->currentIndex, 0, restored->tabs.size() - 1);
            const QString savedCurrentUrl =
                restored->tabs.at(savedCurrentIndex).url;
            std::uint64_t savedCurrentTabId = 0;
            closedTabs_.clear();
            for (const BrowserSessionTab& tab : restored->closedTabs) {
                closedTabs_.append(ClosedTabRecord{
                    tab.url, tab.title, tab.groupId, tab.isPinned,
                    tab.isMuted, tab.zoomFactor});
            }
            openInitialTab(restored->tabs.constFirst().url,
                           restored->tabs.constFirst().title,
                           restored->tabs.constFirst().isMuted,
                           restored->tabs.constFirst().groupId,
                           restored->tabs.constFirst().isPinned,
                           restored->tabs.constFirst().zoomFactor);
            if (savedCurrentIndex == 0) {
                savedCurrentTabId = tabs_.at(currentTabIndex_).tabId;
            }
            for (int index = 1; index < restored->tabs.size(); ++index) {
                const BrowserSessionTab& tab = restored->tabs.at(index);
                const bool didOpen = openRestoredTab(
                    tab.url, tab.title, tab.isMuted, tab.groupId,
                    tab.isPinned, tab.zoomFactor);
                if (didOpen && index == savedCurrentIndex) {
                    savedCurrentTabId =
                        tabs_.at(currentTabIndex_).tabId;
                }
            }
            currentTabIndex_ = findTabIndex(savedCurrentTabId);
            if (currentTabIndex_ < 0) {
                for (int index = 0; index < tabs_.size(); ++index) {
                    if (tabs_.at(index).address == savedCurrentUrl) {
                        currentTabIndex_ = index;
                        break;
                    }
                }
            }
            currentTabIndex_ = std::clamp(currentTabIndex_, 0,
                                          tabs_.size() - 1);
            tabBar_->setCurrentIndex(currentTabIndex_);
            backend_.activateTab(tabs_.at(currentTabIndex_).tabId);
            updateTabPresentation();
            updateTabGroupPresentation();
            return;
        }
    }

    if (settings.mode == BrowserStartupMode::OpenStartupPages &&
        !settings.startupUrls.isEmpty()) {
        openInitialTab(settings.startupUrls.constFirst());
        for (int index = 1; index < settings.startupUrls.size(); ++index) {
            static_cast<void>(openRestoredTab(settings.startupUrls.at(index),
                                              {}, false, {}, false, 1.0));
        }
        tabBar_->setCurrentIndex(0);
        return;
    }
    openInitialTab(kBrowserHomeUrl);
}

void BrowserPage::openInitialTab(const QString& url, const QString& title,
                                 const bool isMuted, const QString& groupId,
                                 const bool isPinned,
                                 const double zoomFactor) {
    const QString safeUrl = normalizeBrowserSessionUrl(url);
    const std::uint64_t tabId = tabs_.at(0).tabId;
    BrowserTabRecord& tab = tabs_[0];
    tab.address = safeUrl.isEmpty() ? kBrowserHomeUrl : safeUrl;
    tab.title = title;
    tab.state = BrowserPageState::Navigating;
    tab.lastError.reset();
    tab.isUserMuted = isMuted;
    tab.groupId = groupId;
    tab.isPinned = isPinned;
    tab.zoomFactor = zoomFactor;
    tabBar_->setTabText(0, title.isEmpty() ? QStringLiteral("新标签页") : title);
    applyCachedFavicon(0);
    normalizePinnedTabOrder();
    const int normalizedIndex = findTabIndex(tabId);
    if (normalizedIndex < 0) {
        return;
    }
    currentTabIndex_ = normalizedIndex;
    BrowserTabRecord& normalizedTab = tabs_[normalizedIndex];
    updateTabCloseButtons();
    state_ = BrowserPageState::Navigating;
    addressEdit_->setText(normalizedTab.address);
    statusLabel_->setText(QStringLiteral("正在载入..."));
    backend_.setTabAudioMuted(tabId, isMuted);
    backend_.setTabZoomFactor(tabId, zoomFactor);
    updateControls();
    backend_.navigate(normalizedTab.address, normalizedTab.generation);
}

bool BrowserPage::openRestoredTab(const QString& url, const QString& title,
                                  const bool isMuted, const QString& groupId,
                                  const bool isPinned,
                                  const double zoomFactor) {
    const QString safeUrl = normalizeBrowserSessionUrl(url);
    if (safeUrl.isEmpty() || tabs_.size() >= maximumTabCount() ||
        !onNewTabRequested(0, safeUrl)) {
        return false;
    }
    const std::uint64_t tabId = tabs_.at(currentTabIndex_).tabId;
    BrowserTabRecord& tab = tabs_[currentTabIndex_];
    tab.title = title;
    tab.isUserMuted = isMuted;
    tab.groupId = groupId;
    tab.isPinned = isPinned;
    tab.zoomFactor = zoomFactor;
    tabBar_->setTabText(currentTabIndex_,
                        title.isEmpty() ? QStringLiteral("新标签页") : title);
    normalizePinnedTabOrder();
    const int normalizedIndex = findTabIndex(tabId);
    if (normalizedIndex < 0) {
        return false;
    }
    currentTabIndex_ = normalizedIndex;
    updateTabCloseButtons();
    backend_.setTabAudioMuted(tabId, isMuted);
    backend_.setTabZoomFactor(tabId, zoomFactor);
    updateAudioPresentation();
    return true;
}

void BrowserPage::saveSession() {
    if (sessionStore_ == nullptr || !hasOpenedInitialHome_) {
        return;
    }
    BrowserSessionState session;
    session.currentIndex = currentTabIndex_;
    for (const BrowserTabRecord& tab : tabs_) {
        session.tabs.append(BrowserSessionTab{
            tab.address, tab.title, tab.groupId, tab.isPinned,
            tab.isUserMuted, tab.zoomFactor});
    }
    for (const ClosedTabRecord& tab : closedTabs_) {
        session.closedTabs.append(BrowserSessionTab{
            tab.address, tab.title, tab.groupId, tab.isPinned,
            tab.isUserMuted, tab.zoomFactor});
    }
    session.groups = tabGroupModel_.groups();
    if (lastSavedSession_.has_value() &&
        haveSameSession(*lastSavedSession_, session)) {
        return;
    }
    try {
        if (sessionStore_->save(session)) {
            lastSavedSession_ = std::move(session);
        }
    } catch (...) {
    }
}

void BrowserPage::showFindBar() {
    if (isShuttingDown_ || currentTabIndex_ < 0 ||
        currentTabIndex_ >= tabs_.size()) {
        return;
    }
    findTabId_ = tabs_.at(currentTabIndex_).tabId;
    findTabGeneration_ = tabs_.at(currentTabIndex_).generation;
    findBar_->show();
    findEdit_->setFocus();
    findEdit_->selectAll();
}

void BrowserPage::closeFindBar(const bool clearSelection) {
    if (findBar_ == nullptr ||
        (!findBar_->isVisible() && findTabId_ == 0)) {
        return;
    }
    backend_.stopFinding(clearSelection);
    findBar_->hide();
    const QSignalBlocker blocker(findEdit_);
    findEdit_->clear();
    updateFindResult(-1, 0);
    findTabId_ = 0;
    findTabGeneration_ = 0;
}

void BrowserPage::findNext(const bool forward) {
    if (isShuttingDown_ || findBar_ == nullptr || !findBar_->isVisible() ||
        findEdit_->text().isEmpty()) {
        return;
    }
    backend_.findInPage(findEdit_->text(), forward);
}

void BrowserPage::updateFindResult(const int activeMatchIndex,
                                   const int matchCount) {
    if (findResultLabel_ == nullptr) {
        return;
    }
    const int displayIndex = matchCount > 0 && activeMatchIndex >= 0
                                 ? activeMatchIndex + 1
                                 : 0;
    findResultLabel_->setText(
        QStringLiteral("%1/%2").arg(displayIndex).arg(std::max(matchCount, 0)));
}

void BrowserPage::showTabSearch() {
    if (isShuttingDown_) {
        return;
    }
    if (tabSearchDialog_ == nullptr) {
        tabSearchDialog_ = new QDialog(this);
        tabSearchDialog_->setObjectName(QStringLiteral("browserTabSearchDialog"));
        tabSearchDialog_->setWindowTitle(QStringLiteral("搜索标签"));
        tabSearchDialog_->resize(520, 360);
        auto* const layout = new QVBoxLayout(tabSearchDialog_);
        tabSearchEdit_ = new QLineEdit(tabSearchDialog_);
        tabSearchEdit_->setObjectName(QStringLiteral("browserTabSearchEdit"));
        tabSearchEdit_->setPlaceholderText(QStringLiteral("按标题或网站域名搜索"));
        tabSearchEdit_->setClearButtonEnabled(true);
        layout->addWidget(tabSearchEdit_);
        tabSearchList_ = new QListWidget(tabSearchDialog_);
        tabSearchList_->setObjectName(QStringLiteral("browserTabSearchList"));
        tabSearchList_->setAlternatingRowColors(true);
        layout->addWidget(tabSearchList_, 1);
        auto* const buttons = new QHBoxLayout();
        tabSearchSwitchButton_ = new QPushButton(
            QStringLiteral("切换到标签"), tabSearchDialog_);
        tabSearchSwitchButton_->setObjectName(
            QStringLiteral("browserTabSearchSwitchButton"));
        auto* const closeButton = new QPushButton(
            QStringLiteral("关闭"), tabSearchDialog_);
        closeButton->setObjectName(
            QStringLiteral("browserTabSearchCloseButton"));
        buttons->addWidget(tabSearchSwitchButton_);
        buttons->addStretch();
        buttons->addWidget(closeButton);
        layout->addLayout(buttons);
        connect(tabSearchEdit_, &QLineEdit::textChanged, this,
                [this] { updateTabSearchPresentation(); });
        connect(tabSearchList_, &QListWidget::currentRowChanged,
                tabSearchDialog_, [this](int) {
                    tabSearchSwitchButton_->setEnabled(
                        tabSearchList_->currentItem() != nullptr);
                });
        connect(tabSearchList_, &QListWidget::itemDoubleClicked,
                tabSearchDialog_, [this](QListWidgetItem*) {
                    activateSelectedSearchTab();
                });
        connect(tabSearchEdit_, &QLineEdit::returnPressed, this,
                &BrowserPage::activateSelectedSearchTab);
        connect(tabSearchSwitchButton_, &QPushButton::clicked, this,
                &BrowserPage::activateSelectedSearchTab);
        connect(closeButton, &QPushButton::clicked, tabSearchDialog_,
                &QDialog::hide);
    }
    if (isTabSearchDirty_) {
        refreshTabSearch();
    }
    tabSearchDialog_->show();
    tabSearchDialog_->raise();
    tabSearchDialog_->activateWindow();
    tabSearchEdit_->setFocus();
    tabSearchEdit_->selectAll();
}

void BrowserPage::refreshTabSearch() {
    if (tabSearchList_ == nullptr) {
        return;
    }
    const QString query = tabSearchEdit_ == nullptr
                              ? QString{}
                              : tabSearchEdit_->text().trimmed();
    tabSearchList_->clear();
    for (const BrowserTabRecord& tab : tabs_) {
        const QUrl url(tab.address, QUrl::StrictMode);
        const QString domain = url.host().toLower();
        if (!query.isEmpty() &&
            !tab.title.contains(query, Qt::CaseInsensitive) &&
            !domain.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        QString title = tab.title.trimmed();
        if (title.isEmpty()) {
            title = QStringLiteral("新标签页");
        }
        const QString label = domain.isEmpty()
                                  ? title
                                  : QStringLiteral("%1\n%2").arg(title, domain);
        auto* const item = new QListWidgetItem(label, tabSearchList_);
        item->setData(Qt::UserRole,
                      QVariant::fromValue<qulonglong>(tab.tabId));
        item->setToolTip(domain);
        if (tab.isPinned) {
            item->setText(QStringLiteral("固定 · ") + item->text());
        }
    }
    tabSearchList_->setCurrentRow(tabSearchList_->count() > 0 ? 0 : -1);
    if (tabSearchSwitchButton_ != nullptr) {
        tabSearchSwitchButton_->setEnabled(tabSearchList_->count() > 0);
    }
    isTabSearchDirty_ = false;
}

void BrowserPage::updateTabSearchPresentation() {
    if (tabSearchDialog_ != nullptr && tabSearchDialog_->isVisible()) {
        refreshTabSearch();
    } else {
        isTabSearchDirty_ = true;
    }
}

void BrowserPage::activateSelectedSearchTab() {
    if (tabSearchList_ == nullptr || tabSearchList_->currentItem() == nullptr) {
        return;
    }
    const std::uint64_t tabId =
        tabSearchList_->currentItem()->data(Qt::UserRole).toULongLong();
    const int index = findTabIndex(tabId);
    if (index < 0) {
        refreshTabSearch();
        return;
    }
    tabBar_->setCurrentIndex(index);
    tabSearchDialog_->hide();
}

void BrowserPage::showTabContextMenu(const QPoint& position) {
    const int targetIndex = tabBar_->tabAt(position);
    if (targetIndex < 0 || targetIndex >= tabs_.size()) {
        return;
    }
    const std::uint64_t targetTabId = tabs_.at(targetIndex).tabId;
    const QString targetAddress = tabs_.at(targetIndex).address;
    const QString targetTitle = tabs_.at(targetIndex).title;
    const bool isTargetMuted = tabs_.at(targetIndex).isUserMuted;
    const bool isTargetPinned = tabs_.at(targetIndex).isPinned;
    const QString targetGroupId = tabs_.at(targetIndex).groupId;
    QVector<std::uint64_t> otherTabIds;
    QVector<std::uint64_t> rightTabIds;
    const auto hasActiveDownload = [this](const std::uint64_t tabId) {
        return std::any_of(
            downloadTabIds_.cbegin(), downloadTabIds_.cend(),
            [tabId](const std::uint64_t ownerTabId) {
                return ownerTabId == tabId;
            });
    };
    for (int index = 0; index < tabs_.size(); ++index) {
        if (tabs_.at(index).tabId != targetTabId &&
            !tabs_.at(index).isPinned && !tabs_.at(index).isPlayingAudio &&
            !hasActiveDownload(tabs_.at(index).tabId)) {
            otherTabIds.append(tabs_.at(index).tabId);
        }
        if (index > targetIndex && !tabs_.at(index).isPinned &&
            !tabs_.at(index).isPlayingAudio &&
            !hasActiveDownload(tabs_.at(index).tabId)) {
            rightTabIds.append(tabs_.at(index).tabId);
        }
    }

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("browserTabContextMenu"));
    QAction* const newTabAction = menu.addAction(QStringLiteral("新建标签"));
    newTabAction->setObjectName(QStringLiteral("browserTabMenuNewAction"));
    QAction* const duplicateAction = menu.addAction(QStringLiteral("复制标签"));
    duplicateAction->setObjectName(
        QStringLiteral("browserTabMenuDuplicateAction"));
    QAction* const reloadAction = menu.addAction(QStringLiteral("重新加载"));
    reloadAction->setObjectName(QStringLiteral("browserTabMenuReloadAction"));
    menu.addSeparator();
    QAction* const copyAddressAction = menu.addAction(QStringLiteral("复制地址"));
    copyAddressAction->setObjectName(
        QStringLiteral("browserTabMenuCopyAddressAction"));
    QAction* const copyTitleAction = menu.addAction(QStringLiteral("复制标题"));
    copyTitleAction->setObjectName(
        QStringLiteral("browserTabMenuCopyTitleAction"));
    copyTitleAction->setEnabled(!targetTitle.isEmpty());
    QAction* const muteAction = menu.addAction(
        isTargetMuted ? QStringLiteral("取消静音") : QStringLiteral("静音"));
    muteAction->setObjectName(QStringLiteral("browserTabMenuMuteAction"));
    QAction* const pinAction = menu.addAction(
        isTargetPinned ? QStringLiteral("取消固定") : QStringLiteral("固定标签"));
    pinAction->setObjectName(QStringLiteral("browserTabMenuPinAction"));
    QAction* const sleepAction = menu.addAction(
        QStringLiteral("标签睡眠不可用（无法安全判断静音视频活动）"));
    sleepAction->setObjectName(QStringLiteral("browserTabMenuSleepAction"));
    sleepAction->setToolTip(
        QStringLiteral("当前 WebView2 Runtime 无法可靠判断静音视频是否仍在播放，"
                       "因此不会挂起此标签"));
    sleepAction->setEnabled(false);
    QMenu* const groupMenu = menu.addMenu(QStringLiteral("移动到分组"));
    groupMenu->setObjectName(QStringLiteral("browserTabMenuGroupMenu"));
    QAction* const manageGroupsAction =
        groupMenu->addAction(QStringLiteral("管理分组..."));
    manageGroupsAction->setObjectName(
        QStringLiteral("browserTabMenuManageGroupsAction"));
    groupMenu->addSeparator();
    QAction* const removeFromGroupAction =
        groupMenu->addAction(QStringLiteral("移出分组"));
    removeFromGroupAction->setObjectName(
        QStringLiteral("browserTabMenuRemoveFromGroupAction"));
    removeFromGroupAction->setEnabled(!targetGroupId.isEmpty());
    QHash<QAction*, QString> groupActions;
    for (const BrowserSessionGroup& group : tabGroupModel_.groups()) {
        QAction* const action = groupMenu->addAction(group.name);
        action->setObjectName(QStringLiteral("browserTabMenuGroupAction"));
        action->setCheckable(true);
        action->setChecked(group.id == targetGroupId);
        action->setData(group.id);
        groupActions.insert(action, group.id);
    }
    menu.addSeparator();
    QAction* const closeAction = menu.addAction(QStringLiteral("关闭标签"));
    closeAction->setObjectName(QStringLiteral("browserTabMenuCloseAction"));
    QAction* const closeOthersAction =
        menu.addAction(QStringLiteral("关闭其他标签"));
    closeOthersAction->setObjectName(
        QStringLiteral("browserTabMenuCloseOthersAction"));
    closeOthersAction->setEnabled(!otherTabIds.isEmpty());
    QAction* const closeRightAction =
        menu.addAction(QStringLiteral("关闭右侧标签"));
    closeRightAction->setObjectName(
        QStringLiteral("browserTabMenuCloseRightAction"));
    closeRightAction->setEnabled(!rightTabIds.isEmpty());

    QAction* const selected = menu.exec(tabBar_->mapToGlobal(position));
    if (selected == nullptr || isShuttingDown_) {
        return;
    }
    const auto currentIndexForId = [this](const std::uint64_t tabId) {
        return findTabIndex(tabId);
    };
    if (selected == newTabAction) {
        openNewTab();
    } else if (selected == duplicateAction) {
        const BrowserAddress address = normalizeBrowserAddress(targetAddress);
        static_cast<void>(onNewTabRequested(
            0, address.kind == BrowserAddressKind::Web ? address.url
                                                       : kBrowserHomeUrl));
    } else if (selected == reloadAction) {
        const int index = currentIndexForId(targetTabId);
        if (index >= 0) {
            tabBar_->setCurrentIndex(index);
            backend_.reloadOrStop();
        }
    } else if (selected == copyAddressAction) {
        QApplication::clipboard()->setText(targetAddress);
    } else if (selected == copyTitleAction) {
        QApplication::clipboard()->setText(targetTitle);
    } else if (selected == muteAction) {
        toggleTabMuted(targetTabId);
    } else if (selected == pinAction) {
        setTabPinned(targetTabId, !isTargetPinned);
    } else if (selected == manageGroupsAction) {
        showTabGroups();
    } else if (selected == removeFromGroupAction) {
        moveTabToGroup(targetTabId, {});
    } else if (groupActions.contains(selected)) {
        moveTabToGroup(targetTabId, groupActions.value(selected));
    } else if (selected == closeAction) {
        closeTab(currentIndexForId(targetTabId));
    } else if (selected == closeOthersAction) {
        for (const std::uint64_t tabId : otherTabIds) {
            closeTab(currentIndexForId(tabId));
        }
    } else if (selected == closeRightAction) {
        for (const std::uint64_t tabId : rightTabIds) {
            closeTab(currentIndexForId(tabId));
        }
    }
}

void BrowserPage::showTabGroups() {
    if (tabGroupDialog_ == nullptr) {
        tabGroupDialog_ = new BrowserTabGroupDialog(tabGroupModel_, this);
        connect(tabGroupDialog_, &BrowserTabGroupDialog::groupsChanged, this,
                [this] {
                    isTabGroupDialogDirty_ = false;
                    updateTabGroupPresentation();
                });
        connect(tabGroupDialog_, &BrowserTabGroupDialog::groupRemoved, this,
                &BrowserPage::removeGroupFromTabs);
        isTabGroupDialogDirty_ = false;
    } else if (isTabGroupDialogDirty_) {
        tabGroupDialog_->reload();
        isTabGroupDialogDirty_ = false;
    }
    tabGroupDialog_->show();
    tabGroupDialog_->raise();
    tabGroupDialog_->activateWindow();
}

void BrowserPage::updateTabGroupDialogPresentation() {
    if (tabGroupDialog_ != nullptr && tabGroupDialog_->isVisible()) {
        tabGroupDialog_->reload();
        isTabGroupDialogDirty_ = false;
    } else {
        isTabGroupDialogDirty_ = true;
    }
}

void BrowserPage::moveTabToGroup(const std::uint64_t tabId,
                                 const QString& groupId) {
    const int index = findTabIndex(tabId);
    if (index < 0 || (!groupId.isEmpty() &&
                      tabGroupModel_.find(groupId) == nullptr)) {
        return;
    }
    tabs_[index].groupId = groupId;
    updateTabGroupPresentation();
}

bool BrowserPage::isTabCollapsedForTest(const std::uint64_t tabId) const {
    return static_cast<const BrowserTabBar*>(tabBar_)->isTabCollapsed(tabId);
}

void BrowserPage::removeGroupFromTabs(const QString& groupId) {
    for (BrowserTabRecord& tab : tabs_) {
        if (tab.groupId == groupId) {
            tab.groupId.clear();
        }
    }
    for (ClosedTabRecord& tab : closedTabs_) {
        if (tab.groupId == groupId) {
            tab.groupId.clear();
        }
    }
    updateTabGroupPresentation();
}

void BrowserPage::updateTabGroupPresentation() {
    QHash<QString, int> representativeByGroup;
    for (int index = 0; index < tabs_.size(); ++index) {
        const BrowserSessionGroup* const group =
            tabGroupModel_.find(tabs_.at(index).groupId);
        if (group == nullptr || !group->isCollapsed) {
            continue;
        }
        if (!representativeByGroup.contains(group->id) ||
            index == currentTabIndex_) {
            representativeByGroup.insert(group->id, index);
        }
    }
    QSet<qulonglong> collapsedTabIds;
    for (int index = 0; index < tabs_.size(); ++index) {
        const BrowserTabRecord& tab = tabs_.at(index);
        const BrowserSessionGroup* const group =
            tabGroupModel_.find(tab.groupId);
        QString toolTip = tab.address;
        if (group != nullptr) {
            toolTip = QStringLiteral("分组：%1\n%2")
                          .arg(group->name, tab.address);
            tabBar_->setTabTextColor(index, QColor(group->color));
            if (group->isCollapsed &&
                representativeByGroup.value(group->id, index) != index) {
                collapsedTabIds.insert(tab.tabId);
            }
        } else {
            if (!tab.groupId.isEmpty()) {
                tabs_[index].groupId.clear();
            }
            tabBar_->setTabTextColor(index, {});
        }
        tabBar_->setTabToolTip(index, toolTip);
    }
    static_cast<BrowserTabBar*>(tabBar_)->setCollapsedTabIds(collapsedTabIds);
    updateTabSearchPresentation();
}

void BrowserPage::closeCurrentTab() {
    if (isShuttingDown_ || currentTabIndex_ < 0 ||
        currentTabIndex_ >= tabs_.size()) {
        return;
    }
    const BrowserTabRecord& tab = tabs_.at(currentTabIndex_);
    if (tab.isPinned) {
        showPinnedCloseConfirmation(tab.tabId);
        return;
    }
    closeTab(currentTabIndex_);
}

void BrowserPage::setTabPinned(const std::uint64_t tabId,
                               const bool isPinned) {
    const int index = findTabIndex(tabId);
    if (isShuttingDown_ || index < 0 || tabs_.at(index).isPinned == isPinned) {
        return;
    }
    tabs_[index].isPinned = isPinned;
    normalizePinnedTabOrder();
    updateTabCloseButtons();
    updateTabSearchPresentation();
}

void BrowserPage::normalizePinnedTabOrder() {
    if (isNormalizingPinnedTabs_ || tabs_.size() < 2) {
        return;
    }
    const std::uint64_t currentTabId =
        tabs_.at(std::clamp(currentTabIndex_, 0, tabs_.size() - 1)).tabId;
    isNormalizingPinnedTabs_ = true;
    int pinnedDestination = 0;
    for (int index = 0; index < tabs_.size(); ++index) {
        if (!tabs_.at(index).isPinned) {
            continue;
        }
        if (index != pinnedDestination) {
            tabBar_->moveTab(index, pinnedDestination);
        }
        ++pinnedDestination;
    }
    isNormalizingPinnedTabs_ = false;
    currentTabIndex_ = findTabIndex(currentTabId);
    tabBar_->setCurrentIndex(currentTabIndex_);
}

void BrowserPage::updateTabCloseButtons() {
    if (tabBar_ == nullptr) {
        return;
    }
    tabBar_->setTabsClosable(false);
    tabBar_->setTabsClosable(true);
    for (int index = 0; index < tabs_.size(); ++index) {
        if (!tabs_.at(index).isPinned) {
            continue;
        }
        tabBar_->setTabButton(index, QTabBar::LeftSide, nullptr);
        tabBar_->setTabButton(index, QTabBar::RightSide, nullptr);
    }
}

void BrowserPage::showPinnedCloseConfirmation(const std::uint64_t tabId) {
    if (findTabIndex(tabId) < 0) {
        return;
    }
    pendingPinnedCloseTabId_ = tabId;
    if (pinnedCloseDialog_ == nullptr) {
        pinnedCloseDialog_ = new QDialog(this);
        pinnedCloseDialog_->setObjectName(
            QStringLiteral("browserPinnedCloseDialog"));
        pinnedCloseDialog_->setWindowTitle(QStringLiteral("关闭固定标签"));
        auto* const layout = new QVBoxLayout(pinnedCloseDialog_);
        auto* const explanation = new QLabel(
            QStringLiteral("当前标签已固定。是否仍要关闭它？"),
            pinnedCloseDialog_);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto* const buttons = new QHBoxLayout();
        auto* const cancelButton = new QPushButton(
            QStringLiteral("取消"), pinnedCloseDialog_);
        cancelButton->setObjectName(
            QStringLiteral("browserPinnedCloseCancelButton"));
        auto* const confirmButton = new QPushButton(
            QStringLiteral("关闭固定标签"), pinnedCloseDialog_);
        confirmButton->setObjectName(
            QStringLiteral("browserPinnedCloseConfirmButton"));
        buttons->addStretch();
        buttons->addWidget(cancelButton);
        buttons->addWidget(confirmButton);
        layout->addLayout(buttons);
        connect(cancelButton, &QPushButton::clicked, this, [this] {
            pendingPinnedCloseTabId_.reset();
            pinnedCloseDialog_->hide();
        });
        connect(pinnedCloseDialog_, &QDialog::rejected, this,
                [this] { pendingPinnedCloseTabId_.reset(); });
        connect(confirmButton, &QPushButton::clicked, this, [this] {
            const std::optional<std::uint64_t> tabId =
                pendingPinnedCloseTabId_;
            pendingPinnedCloseTabId_.reset();
            pinnedCloseDialog_->hide();
            if (tabId.has_value()) {
                const int index = findTabIndex(*tabId);
                if (index >= 0) {
                    closeTabInternal(index, true);
                }
            }
        });
    }
    pinnedCloseDialog_->show();
    pinnedCloseDialog_->raise();
    pinnedCloseDialog_->activateWindow();
}

void BrowserPage::cycleTab(const int step) {
    if (isShuttingDown_ || tabs_.size() < 2 || step == 0) {
        return;
    }
    const int count = tabs_.size();
    const int target = (currentTabIndex_ + step % count + count) % count;
    tabBar_->setCurrentIndex(target);
}

void BrowserPage::submitAddress() {
    const BrowserAddress address = normalizeBrowserAddress(addressEdit_->text());
    if (address.kind == BrowserAddressKind::Web) {
        navigateTo(address.url);
        return;
    }
    showError(address.kind == BrowserAddressKind::Blocked
                  ? BrowserErrorKind::BlockedScheme
                  : BrowserErrorKind::InvalidAddress);
}

void BrowserPage::navigateTo(const QString& normalizedUrl) {
    if (isShuttingDown_ || hasBrowserProcessExited_ ||
        state_ == BrowserPageState::Unavailable ||
        state_ == BrowserPageState::Initializing ||
        state_ == BrowserPageState::ClearingData) {
        return;
    }
    closeFindBar(true);
    rejectUnansweredSensitiveRequests();
    tabs_[currentTabIndex_].address = normalizedUrl;
    ++generation_;
    tabs_[currentTabIndex_].generation = generation_;
    tabs_[currentTabIndex_].state = BrowserPageState::Navigating;
    tabs_[currentTabIndex_].lastError.reset();
    state_ = BrowserPageState::Navigating;
    statusLabel_->setText(QStringLiteral("正在载入..."));
    updateControls();
    backend_.navigate(normalizedUrl, generation_);
}

void BrowserPage::activateTab(const int index) {
    if (isShuttingDown_ || index < 0 || index >= tabBar_->count()) {
        return;
    }
    int modelIndex = index;
    const QVariant tabData = tabBar_->tabData(index);
    if (tabData.isValid()) {
        modelIndex = findTabIndex(tabData.toULongLong());
    }
    if (modelIndex < 0 || modelIndex >= tabs_.size() ||
        modelIndex == currentTabIndex_) {
        return;
    }
    closeFindBar(true);
    leaveWebFullScreenForTabChange();
    rejectUnansweredSensitiveRequests();
    currentTabIndex_ = modelIndex;
    updateTabPresentation();
    updateTabGroupPresentation();
    const BrowserTabRecord& tab = tabs_.at(currentTabIndex_);
    backend_.activateTab(tab.tabId);
    state_ = tab.state;
    updateControls();
}

void BrowserPage::closeTab(const int index) {
    closeTabInternal(index, false);
}

void BrowserPage::closeTabInternal(const int index,
                                   const bool isPinnedCloseConfirmed) {
    if (isShuttingDown_ || state_ == BrowserPageState::ClearingData ||
        index < 0 || index >= tabs_.size()) {
        return;
    }
    if (tabs_.at(index).isPinned && !isPinnedCloseConfirmed) {
        showPinnedCloseConfirmation(tabs_.at(index).tabId);
        return;
    }
    const bool wasCurrent = index == currentTabIndex_;
    if (wasCurrent) {
        closeFindBar(true);
        leaveWebFullScreenForTabChange();
        rejectUnansweredSensitiveRequests();
    }
    const BrowserTabRecord closingTab = tabs_.at(index);
    const QString restorableAddress =
        normalizeBrowserSessionUrl(closingTab.address);
    if (!restorableAddress.isEmpty()) {
        closedTabs_.append(ClosedTabRecord{
            restorableAddress, closingTab.title, closingTab.groupId,
            closingTab.isPinned, closingTab.isUserMuted,
            closingTab.zoomFactor});
        if (closedTabs_.size() > kMaximumClosedTabCount) {
            closedTabs_.removeFirst();
        }
    }
    if (tabs_.size() == 1) {
        BrowserTabRecord& tab = tabs_[0];
        ++generation_;
        tab.generation = generation_;
        tab.address.clear();
        tab.title.clear();
        tab.canGoBack = false;
        tab.canGoForward = false;
        tab.state = BrowserPageState::Navigating;
        tab.lastError.reset();
        tab.processFailure.reset();
        tab.recoveryAttempts = 0;
        tab.lastRecoveryAt = {};
        if (tab.isUserMuted) {
            tab.isUserMuted = false;
            backend_.setTabAudioMuted(tab.tabId, false);
        }
        tab.isPlayingAudio = false;
        tab.isPinned = false;
        tab.groupId.clear();
        tab.zoomFactor = 1.0;
        backend_.setTabZoomFactor(tab.tabId, 1.0);
        tabBar_->setTabText(0, QStringLiteral("新标签页"));
        updateTabCloseButtons();
        updateTabPresentation();
        statusLabel_->setText(QStringLiteral("正在打开主页..."));
        tabs_[0].address = kBrowserHomeUrl;
        addressEdit_->setText(kBrowserHomeUrl);
        backend_.navigate(kBrowserHomeUrl, generation_);
        updateAudioPresentation();
        updateAudibleTabCount();
        updateTabGroupPresentation();
        return;
    }
    const std::uint64_t closedTabId = tabs_.at(index).tabId;
    tabs_.removeAt(index);
    tabBar_->blockSignals(true);
    tabBar_->removeTab(index);
    backend_.closeTab(closedTabId);
    if (wasCurrent) {
        currentTabIndex_ = std::min(index, tabs_.size() - 1);
    } else if (index < currentTabIndex_) {
        --currentTabIndex_;
    }
    currentTabIndex_ = std::clamp(currentTabIndex_, 0, tabs_.size() - 1);
    tabBar_->setCurrentIndex(currentTabIndex_);
    tabBar_->blockSignals(false);
    updateTabCloseButtons();
    backend_.activateTab(tabs_.at(currentTabIndex_).tabId);
    updateTabPresentation();
    updateAudibleTabCount();
    updateTabGroupPresentation();
}

void BrowserPage::updateTabPresentation() {
    if (currentTabIndex_ < 0 || currentTabIndex_ >= tabs_.size()) {
        return;
    }
    const BrowserTabRecord& tab = tabs_.at(currentTabIndex_);
    addressEdit_->setText(tab.address);
    titleLabel_->setText(tab.title.isEmpty() ? QStringLiteral("网页") : tab.title);
    backButton_->setEnabled(tab.canGoBack);
    forwardButton_->setEnabled(tab.canGoForward);
    state_ = tab.state;
    zoomResetButton_->setText(
        QStringLiteral("%1%").arg(qRound(tab.zoomFactor * 100.0)));
    if (tab.processFailure.has_value()) {
        showTabProcessFailure();
    } else if (tab.state == BrowserPageState::Failed &&
               tab.lastError.has_value()) {
        showError(*tab.lastError);
    } else {
        showHost();
        switch (tab.state) {
            case BrowserPageState::Initializing:
                statusLabel_->setText(QStringLiteral("正在初始化网页组件..."));
                break;
            case BrowserPageState::Navigating:
                statusLabel_->setText(QStringLiteral("正在载入..."));
                break;
            case BrowserPageState::Ready:
                statusLabel_->setText(QStringLiteral("载入完成"));
                break;
            case BrowserPageState::ClearingData:
                statusLabel_->setText(QStringLiteral("正在清除网页数据..."));
                break;
            case BrowserPageState::Unavailable:
                statusLabel_->setText(QStringLiteral("网页组件尚未初始化"));
                break;
            case BrowserPageState::Failed:
                break;
        }
    }
    updateControls();
    updateAudioPresentation();
}

void BrowserPage::applyCachedFavicon(const int index) {
    if (index < 0 || index >= tabs_.size() || tabBar_ == nullptr) {
        return;
    }
    const QByteArray pngBytes = faviconCache_.lookup(tabs_.at(index).address);
    QPixmap pixmap;
    if (pngBytes.isEmpty() || !pixmap.loadFromData(pngBytes, "PNG")) {
        tabBar_->setTabIcon(index, QIcon{});
        return;
    }
    tabBar_->setTabIcon(index, QIcon(pixmap));
}

void BrowserPage::clearTabFavicons() {
    faviconCache_.clear();
    if (tabBar_ == nullptr) {
        return;
    }
    for (int index = 0; index < tabBar_->count(); ++index) {
        tabBar_->setTabIcon(index, QIcon{});
    }
}

void BrowserPage::updateAudioPresentation() {
    updateAudioControls();
    if (audioTabsDialog_ != nullptr && audioTabsDialog_->isVisible()) {
        refreshAudioTabs();
    } else {
        isAudioTabsDirty_ = true;
    }
}

void BrowserPage::updateAudioTabPresentation(const std::uint64_t tabId,
                                             const bool wasListed) {
    updateAudioControls();
    updateAudioTabRow(tabId, wasListed);
}

void BrowserPage::updateAudioControls() {
    if (currentTabMuteButton_ == nullptr || audioTabsButton_ == nullptr ||
        currentTabIndex_ < 0 || currentTabIndex_ >= tabs_.size()) {
        return;
    }
    const BrowserTabRecord& currentTab = tabs_.at(currentTabIndex_);
    currentTabMuteButton_->setText(currentTab.isUserMuted
                                       ? QStringLiteral("取消静音")
                                       : QStringLiteral("静音"));
    currentTabMuteButton_->setToolTip(
        currentTab.isUserMuted
            ? QStringLiteral("恢复当前网页标签的声音")
            : QStringLiteral("仅静音当前网页标签"));

    const int count = audibleTabCount();
    audioTabsButton_->setText(count > 0
                                  ? QStringLiteral("声音 · %1").arg(count)
                                  : QStringLiteral("声音"));
    audioTabsButton_->setToolTip(
        count > 0 ? QStringLiteral("%1 个网页标签正在出声").arg(count)
                  : QStringLiteral("查看网页声音标签"));
    if (globalAudioMuteButton_ != nullptr) {
        globalAudioMuteButton_->setText(
            isGloballyMuted_ ? QStringLiteral("恢复网页声音")
                             : QStringLiteral("全部网页静音"));
    }
}

int BrowserPage::audibleTabCount() const noexcept {
    if (isGloballyMuted_) {
        return 0;
    }
    int count = 0;
    for (const BrowserTabRecord& tab : tabs_) {
        if (tab.isPlayingAudio && !tab.isUserMuted) {
            ++count;
        }
    }
    return count;
}

void BrowserPage::updateAudibleTabCount() {
    const int count = audibleTabCount();
    if (count != lastAudibleTabCount_) {
        lastAudibleTabCount_ = count;
        emit audibleTabCountChanged(count);
    }
}

void BrowserPage::toggleCurrentTabMuted() {
    if (currentTabIndex_ < 0 || currentTabIndex_ >= tabs_.size()) {
        return;
    }
    toggleTabMuted(tabs_.at(currentTabIndex_).tabId);
}

void BrowserPage::toggleTabMuted(const std::uint64_t tabId) {
    const int index = findTabIndex(tabId);
    if (index < 0 || isShuttingDown_) {
        return;
    }
    BrowserTabRecord& tab = tabs_[index];
    const bool wasListed = tab.isPlayingAudio || tab.isUserMuted;
    tab.isUserMuted = !tab.isUserMuted;
    backend_.setTabAudioMuted(tab.tabId, tab.isUserMuted);
    updateAudioTabPresentation(tab.tabId, wasListed);
    updateAudibleTabCount();
}

void BrowserPage::showAudioTabs() {
    if (audioTabsDialog_ == nullptr) {
        audioTabsDialog_ = new QDialog(this);
        audioTabsDialog_->setObjectName(QStringLiteral("browserAudioTabsDialog"));
        audioTabsDialog_->setWindowTitle(QStringLiteral("网页声音"));
        audioTabsDialog_->resize(540, 360);
        auto* const layout = new QVBoxLayout(audioTabsDialog_);
        auto* const explanation = new QLabel(
            QStringLiteral("管理正在播放声音或已单独静音的网页标签。"),
            audioTabsDialog_);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        audioTabsList_ = new QListWidget(audioTabsDialog_);
        audioTabsList_->setObjectName(QStringLiteral("browserAudioTabsList"));
        layout->addWidget(audioTabsList_, 1);

        auto* const buttons = new QHBoxLayout();
        audioTabSwitchButton_ =
            new QPushButton(QStringLiteral("切换到标签"), audioTabsDialog_);
        audioTabSwitchButton_->setObjectName(
            QStringLiteral("browserAudioTabSwitchButton"));
        audioTabMuteButton_ =
            new QPushButton(QStringLiteral("切换静音"), audioTabsDialog_);
        audioTabMuteButton_->setObjectName(
            QStringLiteral("browserAudioTabMuteButton"));
        audioTabCloseButton_ =
            new QPushButton(QStringLiteral("关闭标签"), audioTabsDialog_);
        audioTabCloseButton_->setObjectName(
            QStringLiteral("browserAudioTabCloseButton"));
        globalAudioMuteButton_ =
            new QPushButton(QStringLiteral("全部网页静音"), audioTabsDialog_);
        globalAudioMuteButton_->setObjectName(
            QStringLiteral("browserGlobalAudioMuteButton"));
        buttons->addWidget(audioTabSwitchButton_);
        buttons->addWidget(audioTabMuteButton_);
        buttons->addWidget(audioTabCloseButton_);
        buttons->addStretch(1);
        buttons->addWidget(globalAudioMuteButton_);
        layout->addLayout(buttons);

        connect(audioTabSwitchButton_, &QPushButton::clicked, this, [this] {
            const QListWidgetItem* const item = audioTabsList_->currentItem();
            if (item == nullptr) {
                return;
            }
            const int index = findTabIndex(
                item->data(Qt::UserRole).toULongLong());
            if (index >= 0) {
                tabBar_->setCurrentIndex(index);
                audioTabsDialog_->hide();
            }
        });
        connect(audioTabMuteButton_, &QPushButton::clicked, this, [this] {
            const QListWidgetItem* const item = audioTabsList_->currentItem();
            if (item != nullptr) {
                toggleTabMuted(item->data(Qt::UserRole).toULongLong());
            }
        });
        connect(audioTabCloseButton_, &QPushButton::clicked, this, [this] {
            const QListWidgetItem* const item = audioTabsList_->currentItem();
            if (item != nullptr) {
                closeTab(findTabIndex(item->data(Qt::UserRole).toULongLong()));
            }
        });
        connect(globalAudioMuteButton_, &QPushButton::clicked, this, [this] {
            isGloballyMuted_ = !isGloballyMuted_;
            backend_.setAudioMuted(isGloballyMuted_);
            updateAudioPresentation();
            updateAudibleTabCount();
        });
    }
    if (isAudioTabsDirty_) {
        refreshAudioTabs();
    }
    audioTabsDialog_->show();
    audioTabsDialog_->raise();
    audioTabsDialog_->activateWindow();
}

void BrowserPage::refreshAudioTabs() {
    if (audioTabsList_ == nullptr) {
        return;
    }
    const std::uint64_t selectedTabId =
        audioTabsList_->currentItem() != nullptr
            ? audioTabsList_->currentItem()->data(Qt::UserRole).toULongLong()
            : 0;
    audioTabsList_->clear();
    int selectedRow = -1;
    for (const BrowserTabRecord& tab : tabs_) {
        if (!tab.isPlayingAudio && !tab.isUserMuted) {
            continue;
        }
        auto* const item = new QListWidgetItem(audioTabText(tab), audioTabsList_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(tab.tabId));
        if (tab.tabId == selectedTabId) {
            selectedRow = audioTabsList_->count() - 1;
        }
    }
    if (selectedRow >= 0) {
        audioTabsList_->setCurrentRow(selectedRow);
    } else if (audioTabsList_->count() > 0) {
        audioTabsList_->setCurrentRow(0);
    }
    isAudioTabsDirty_ = false;
}

void BrowserPage::updateAudioTabRow(const std::uint64_t tabId,
                                    const bool wasListed) {
    if (audioTabsDialog_ == nullptr || audioTabsList_ == nullptr ||
        !audioTabsDialog_->isVisible()) {
        isAudioTabsDirty_ = true;
        return;
    }

    const int tabIndex = findTabIndex(tabId);
    const bool isListed = tabIndex >= 0 &&
                          (tabs_.at(tabIndex).isPlayingAudio ||
                           tabs_.at(tabIndex).isUserMuted);
    if (isAudioTabsDirty_ || wasListed != isListed) {
        refreshAudioTabs();
        return;
    }
    if (!isListed) {
        return;
    }

    for (int row = 0; row < audioTabsList_->count(); ++row) {
        QListWidgetItem* const item = audioTabsList_->item(row);
        if (item->data(Qt::UserRole).toULongLong() == tabId) {
            item->setText(audioTabText(tabs_.at(tabIndex)));
            return;
        }
    }
    refreshAudioTabs();
}

QString BrowserPage::audioTabText(const BrowserTabRecord& tab) const {
    const QString state = tab.isUserMuted || isGloballyMuted_
                              ? QStringLiteral("已静音")
                              : QStringLiteral("正在出声");
    return QStringLiteral("[%1] %2").arg(
        state, tab.title.isEmpty() ? QStringLiteral("新标签页") : tab.title);
}

void BrowserPage::setCurrentTabZoom(const double zoomFactor) {
    if (isShuttingDown_ || currentTabIndex_ < 0 ||
        currentTabIndex_ >= tabs_.size()) {
        return;
    }
    BrowserTabRecord& tab = tabs_[currentTabIndex_];
    tab.zoomFactor = std::clamp(zoomFactor, 0.25, 5.0);
    backend_.setTabZoomFactor(tab.tabId, tab.zoomFactor);
    zoomResetButton_->setText(
        QStringLiteral("%1%").arg(qRound(tab.zoomFactor * 100.0)));
}

void BrowserPage::adjustCurrentTabZoom(const double delta) {
    if (currentTabIndex_ < 0 || currentTabIndex_ >= tabs_.size()) {
        return;
    }
    setCurrentTabZoom(tabs_.at(currentTabIndex_).zoomFactor + delta);
}

void BrowserPage::resetCurrentTabZoom() {
    setCurrentTabZoom(1.0);
}

void BrowserPage::leaveWebFullScreenForTabChange() {
    if (!isWebFullScreen_) {
        return;
    }
    backend_.exitFullScreen();
    onFullScreenChanged(tabs_.at(currentTabIndex_).generation, false);
}

int BrowserPage::findTabIndex(const std::uint64_t tabId) const noexcept {
    for (int index = 0; index < tabs_.size(); ++index) {
        if (tabs_.at(index).tabId == tabId) {
            return index;
        }
    }
    return -1;
}

void BrowserPage::showHistory() {
    if (historyDialog_ == nullptr) {
        historyDialog_ = new QDialog(this);
        historyDialog_->setObjectName(QStringLiteral("browserHistoryDialog"));
        historyDialog_->setWindowTitle(QStringLiteral("浏览历史"));
        historyDialog_->resize(680, 460);
        auto* layout = new QVBoxLayout(historyDialog_);
        historySearchEdit_ = new QLineEdit(historyDialog_);
        historySearchEdit_->setObjectName(
            QStringLiteral("browserHistorySearchEdit"));
        historySearchEdit_->setPlaceholderText(
            QStringLiteral("搜索标题或网址"));
        historySearchEdit_->setClearButtonEnabled(true);
        layout->addWidget(historySearchEdit_);
        historySearchTimer_ = new QTimer(historyDialog_);
        historySearchTimer_->setObjectName(
            QStringLiteral("browserHistorySearchTimer"));
        historySearchTimer_->setSingleShot(true);
        historySearchTimer_->setInterval(kListSearchDebounceMilliseconds);
        auto* list = new BrowserLinkListWidget(historyDialog_);
        historyList_ = list;
        historyList_->setObjectName(QStringLiteral("browserHistoryList"));
        historyList_->setAlternatingRowColors(true);
        list->setOpenCallback([this](const QString& url, const bool isNewTab) {
            openStoredUrl(url, isNewTab);
            historyDialog_->hide();
        });
        layout->addWidget(historyList_);
        auto* buttons = new QHBoxLayout();
        auto* removeButton = new QPushButton(QStringLiteral("删除选中"),
                                             historyDialog_);
        removeButton->setObjectName(
            QStringLiteral("browserHistoryRemoveButton"));
        auto* clearButton = new QPushButton(QStringLiteral("清空历史"),
                                            historyDialog_);
        clearButton->setObjectName(QStringLiteral("browserHistoryClearButton"));
        auto* closeButton = new QPushButton(QStringLiteral("关闭"), historyDialog_);
        buttons->addWidget(removeButton);
        buttons->addWidget(clearButton);
        buttons->addStretch();
        buttons->addWidget(closeButton);
        layout->addLayout(buttons);
        connect(historySearchEdit_, &QLineEdit::textChanged, this, [this] {
            isHistoryListDirty_ = true;
            historyList_->setCurrentRow(-1);
            historySearchTimer_->start();
        });
        connect(historySearchTimer_, &QTimer::timeout, this,
                &BrowserPage::refreshHistoryList);
        connect(removeButton, &QPushButton::clicked, this,
                &BrowserPage::removeSelectedHistoryEntry);
        connect(clearButton, &QPushButton::clicked, this,
                &BrowserPage::showHistoryClearConfirmation);
        const auto updateSelectionAction = [this, removeButton] {
            removeButton->setEnabled(historyList_->currentRow() >= 0);
        };
        connect(historyList_, &QListWidget::currentRowChanged, historyDialog_,
                [updateSelectionAction](int) { updateSelectionAction(); });
        updateSelectionAction();
        connect(closeButton, &QPushButton::clicked, historyDialog_, &QDialog::hide);
    }
    historyDialog_->show();
    refreshHistoryList();
    historyDialog_->raise();
    historyDialog_->activateWindow();
}

void BrowserPage::showFavorites() {
    if (favoritesDialog_ == nullptr) {
        favoritesDialog_ = new QDialog(this);
        favoritesDialog_->setObjectName(QStringLiteral("browserFavoritesDialog"));
        favoritesDialog_->setWindowTitle(QStringLiteral("收藏夹"));
        favoritesDialog_->resize(680, 460);
        auto* layout = new QVBoxLayout(favoritesDialog_);
        favoritesSearchEdit_ = new QLineEdit(favoritesDialog_);
        favoritesSearchEdit_->setObjectName(
            QStringLiteral("browserFavoritesSearchEdit"));
        favoritesSearchEdit_->setPlaceholderText(
            QStringLiteral("搜索标题、网址或备注"));
        favoritesSearchEdit_->setClearButtonEnabled(true);
        layout->addWidget(favoritesSearchEdit_);
        favoritesSearchTimer_ = new QTimer(favoritesDialog_);
        favoritesSearchTimer_->setObjectName(
            QStringLiteral("browserFavoritesSearchTimer"));
        favoritesSearchTimer_->setSingleShot(true);
        favoritesSearchTimer_->setInterval(kListSearchDebounceMilliseconds);
        auto* list = new BrowserLinkListWidget(favoritesDialog_);
        favoritesList_ = list;
        favoritesList_->setObjectName(QStringLiteral("browserFavoritesList"));
        favoritesList_->setAlternatingRowColors(true);
        favoritesList_->setDragDropMode(QAbstractItemView::InternalMove);
        favoritesList_->setDefaultDropAction(Qt::MoveAction);
        list->setOpenCallback([this](const QString& url, const bool isNewTab) {
            openStoredUrl(url, isNewTab);
            favoritesDialog_->hide();
        });
        list->setOrderChangedCallback(
            [this] { persistFavoriteListOrder(); });
        layout->addWidget(favoritesList_);
        favoriteTransferStatusLabel_ = new QLabel(favoritesDialog_);
        favoriteTransferStatusLabel_->setObjectName(
            QStringLiteral("browserFavoriteTransferStatusLabel"));
        favoriteTransferStatusLabel_->setWordWrap(true);
        favoriteTransferStatusLabel_->hide();
        layout->addWidget(favoriteTransferStatusLabel_);
        auto* buttons = new QHBoxLayout();
        auto* addButton = new QPushButton(QStringLiteral("收藏当前网页"),
                                          favoritesDialog_);
        addButton->setObjectName(QStringLiteral("browserFavoriteAddButton"));
        auto* editButton = new QPushButton(QStringLiteral("编辑"),
                                            favoritesDialog_);
        editButton->setObjectName(QStringLiteral("browserFavoriteEditButton"));
        auto* removeButton = new QPushButton(QStringLiteral("删除"),
                                              favoritesDialog_);
        removeButton->setObjectName(QStringLiteral("browserFavoriteRemoveButton"));
        auto* importButton = new QPushButton(QStringLiteral("导入 HTML"),
                                              favoritesDialog_);
        importButton->setObjectName(QStringLiteral("browserFavoriteImportButton"));
        auto* exportButton = new QPushButton(QStringLiteral("导出 HTML"),
                                              favoritesDialog_);
        exportButton->setObjectName(QStringLiteral("browserFavoriteExportButton"));
        auto* closeButton = new QPushButton(QStringLiteral("关闭"),
                                            favoritesDialog_);
        buttons->addWidget(addButton);
        buttons->addWidget(editButton);
        buttons->addWidget(removeButton);
        buttons->addWidget(importButton);
        buttons->addWidget(exportButton);
        buttons->addStretch();
        buttons->addWidget(closeButton);
        layout->addLayout(buttons);
        connect(addButton, &QPushButton::clicked, this,
                [this] { showFavoriteEditor(); });
        connect(editButton, &QPushButton::clicked, this, [this] {
            QListWidgetItem* const item = favoritesList_->currentItem();
            if (item != nullptr) {
                showFavoriteEditor(item->data(Qt::UserRole + 1).toInt());
            }
        });
        connect(removeButton, &QPushButton::clicked, this,
                &BrowserPage::removeSelectedFavorite);
        const auto updateSelectionActions = [this, editButton, removeButton] {
            const bool hasSelection = favoritesList_->currentRow() >= 0;
            editButton->setEnabled(hasSelection);
            removeButton->setEnabled(hasSelection);
        };
        connect(favoritesList_, &QListWidget::currentRowChanged,
                favoritesDialog_, [updateSelectionActions](int) {
                    updateSelectionActions();
                });
        connect(favoritesSearchEdit_, &QLineEdit::textChanged, this, [this] {
            isFavoritesListDirty_ = true;
            favoritesList_->setCurrentRow(-1);
            favoritesSearchTimer_->start();
        });
        connect(favoritesSearchTimer_, &QTimer::timeout, this,
                &BrowserPage::refreshFavoritesList);
        connect(importButton, &QPushButton::clicked, this,
                &BrowserPage::chooseFavoriteImportFile);
        connect(exportButton, &QPushButton::clicked, this,
                &BrowserPage::chooseFavoriteExportFile);
        updateSelectionActions();
        connect(closeButton, &QPushButton::clicked, favoritesDialog_,
                &QDialog::hide);
    }
    favoritesDialog_->show();
    refreshFavoritesList();
    favoritesDialog_->raise();
    favoritesDialog_->activateWindow();
}

void BrowserPage::refreshHistoryList() {
    if (!isHistoryListDirty_ || historyDialog_ == nullptr ||
        !historyDialog_->isVisible() || historyList_ == nullptr) {
        return;
    }
    historyList_->setUpdatesEnabled(false);
    historyList_->clear();
    const QString query = historySearchEdit_ == nullptr
                              ? QString{}
                              : historySearchEdit_->text().trimmed();
    const QVector<BrowserHistoryEntry>& history = dataModel_.history();
    for (int index = 0; index < history.size(); ++index) {
        const BrowserHistoryEntry& entry = history.at(index);
        if (!query.isEmpty() &&
            !entry.title.contains(query, Qt::CaseInsensitive) &&
            !entry.url.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        const QString label = entry.title.isEmpty() ? entry.url
                                                    : entry.title + QStringLiteral("\n") +
                                                          entry.url;
        auto* item = new QListWidgetItem(label, historyList_);
        item->setData(Qt::UserRole, entry.url);
        item->setData(Qt::UserRole + 1, index);
        item->setToolTip(entry.url);
    }
    historyList_->setCurrentRow(-1);
    historyList_->setUpdatesEnabled(true);
    isHistoryListDirty_ = false;
}

void BrowserPage::refreshFavoritesList() {
    if (!isFavoritesListDirty_ || favoritesDialog_ == nullptr ||
        !favoritesDialog_->isVisible() || favoritesList_ == nullptr) {
        return;
    }
    favoritesList_->setUpdatesEnabled(false);
    favoritesList_->clear();
    const QString query = favoritesSearchEdit_ == nullptr
                              ? QString{}
                              : favoritesSearchEdit_->text().trimmed();
    const QVector<BrowserFavoriteEntry>& favorites = dataModel_.favorites();
    for (int index = 0; index < favorites.size(); ++index) {
        const BrowserFavoriteEntry& entry = favorites.at(index);
        if (!query.isEmpty() &&
            !entry.title.contains(query, Qt::CaseInsensitive) &&
            !entry.url.contains(query, Qt::CaseInsensitive) &&
            !entry.note.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        QString label = entry.title.isEmpty() ? entry.url
                                              : entry.title + QStringLiteral("\n") +
                                                    entry.url;
        if (!entry.note.isEmpty()) {
            label += QStringLiteral("\n备注：") + entry.note;
        }
        auto* item = new QListWidgetItem(label, favoritesList_);
        item->setData(Qt::UserRole, entry.url);
        item->setData(Qt::UserRole + 1, index);
        item->setToolTip(entry.url);
    }
    favoritesList_->setDragEnabled(query.isEmpty());
    favoritesList_->setAcceptDrops(query.isEmpty());
    favoritesList_->setCurrentRow(-1);
    favoritesList_->setUpdatesEnabled(true);
    isFavoritesListDirty_ = false;
}

void BrowserPage::replaceHistoryData(QVector<BrowserHistoryEntry> history) {
    if (historyPersistenceTimer_ != nullptr) {
        historyPersistenceTimer_->stop();
    }
    dataModel_.replaceHistory(std::move(history));
    isHistoryListDirty_ = true;
    refreshHistoryList();
}

void BrowserPage::replaceHistoryDataDeferred(
    QVector<BrowserHistoryEntry> history) {
    dataModel_.replaceHistoryDeferred(std::move(history));
    isHistoryListDirty_ = true;
    refreshHistoryList();
    if (historyPersistenceTimer_ != nullptr) {
        historyPersistenceTimer_->start();
    }
}

void BrowserPage::flushPendingHistory() {
    dataModel_.flushPendingHistory();
}

void BrowserPage::replaceFavoritesData(
    QVector<BrowserFavoriteEntry> favorites) {
    dataModel_.replaceFavorites(std::move(favorites));
    isFavoritesListDirty_ = true;
    refreshFavoritesList();
}

void BrowserPage::removeSelectedHistoryEntry() {
    if (!dataModel_.isAvailable() || historyList_ == nullptr ||
        historyList_->currentItem() == nullptr) {
        return;
    }
    const int index =
        historyList_->currentItem()->data(Qt::UserRole + 1).toInt();
    QVector<BrowserHistoryEntry> history = dataModel_.history();
    if (index < 0 || index >= history.size()) {
        return;
    }
    history.removeAt(index);
    replaceHistoryData(std::move(history));
}

void BrowserPage::showHistoryClearConfirmation() {
    if (historyClearDialog_ == nullptr) {
        historyClearDialog_ = new QDialog(this);
        historyClearDialog_->setObjectName(
            QStringLiteral("browserHistoryClearDialog"));
        historyClearDialog_->setWindowTitle(QStringLiteral("清空浏览历史"));
        auto* layout = new QVBoxLayout(historyClearDialog_);
        auto* explanation = new QLabel(
            QStringLiteral("将清空 MediaHub 保存的全部浏览历史。"
                           "收藏夹和网页 Profile 数据不受影响。"),
            historyClearDialog_);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto* buttons = new QHBoxLayout();
        auto* cancelButton = new QPushButton(QStringLiteral("取消"),
                                             historyClearDialog_);
        auto* confirmButton = new QPushButton(QStringLiteral("确认清空"),
                                              historyClearDialog_);
        confirmButton->setObjectName(
            QStringLiteral("browserHistoryClearConfirmButton"));
        buttons->addStretch();
        buttons->addWidget(cancelButton);
        buttons->addWidget(confirmButton);
        layout->addLayout(buttons);
        connect(cancelButton, &QPushButton::clicked, historyClearDialog_,
                &QDialog::hide);
        connect(confirmButton, &QPushButton::clicked, this,
                &BrowserPage::confirmClearHistory);
    }
    historyClearDialog_->show();
    historyClearDialog_->raise();
    historyClearDialog_->activateWindow();
}

void BrowserPage::confirmClearHistory() {
    if (!dataModel_.isAvailable()) {
        return;
    }
    replaceHistoryData({});
    historyClearDialog_->hide();
}

void BrowserPage::openStoredUrl(const QString& url, const bool isNewTab) {
    const BrowserAddress address = normalizeBrowserAddress(url);
    if (address.kind != BrowserAddressKind::Web || isShuttingDown_) {
        return;
    }
    if (isNewTab) {
        static_cast<void>(onNewTabRequested(0, address.url));
        return;
    }
    navigateTo(address.url);
}

void BrowserPage::showFavoriteEditor(const int favoriteIndex) {
    if (!dataModel_.isAvailable() || currentTabIndex_ < 0 ||
        currentTabIndex_ >= tabs_.size()) {
        return;
    }
    const QVector<BrowserFavoriteEntry>& favorites = dataModel_.favorites();
    BrowserFavoriteEntry entry;
    editingFavoriteIndex_ = favoriteIndex;
    if (favoriteIndex >= 0 && favoriteIndex < favorites.size()) {
        entry = favorites.at(favoriteIndex);
    } else {
        editingFavoriteIndex_ = -1;
        const BrowserTabRecord& tab = tabs_.at(currentTabIndex_);
        const BrowserAddress address = normalizeBrowserAddress(tab.address);
        if (address.kind != BrowserAddressKind::Web) {
            statusLabel_->setText(QStringLiteral("当前标签没有可收藏的网址"));
            return;
        }
        entry = BrowserFavoriteEntry{address.url, tab.title, {}};
    }

    if (favoriteEditorDialog_ == nullptr) {
        favoriteEditorDialog_ = new QDialog(this);
        favoriteEditorDialog_->setObjectName(
            QStringLiteral("browserFavoriteEditorDialog"));
        favoriteEditorDialog_->setWindowTitle(QStringLiteral("编辑收藏"));
        auto* layout = new QVBoxLayout(favoriteEditorDialog_);
        layout->addWidget(new QLabel(QStringLiteral("标题"),
                                     favoriteEditorDialog_));
        favoriteTitleEdit_ = new QLineEdit(favoriteEditorDialog_);
        favoriteTitleEdit_->setObjectName(
            QStringLiteral("browserFavoriteTitleEdit"));
        layout->addWidget(favoriteTitleEdit_);
        layout->addWidget(new QLabel(QStringLiteral("网址"),
                                     favoriteEditorDialog_));
        favoriteUrlEdit_ = new QLineEdit(favoriteEditorDialog_);
        favoriteUrlEdit_->setObjectName(
            QStringLiteral("browserFavoriteUrlEdit"));
        layout->addWidget(favoriteUrlEdit_);
        layout->addWidget(new QLabel(QStringLiteral("备注"),
                                     favoriteEditorDialog_));
        favoriteNoteEdit_ = new QLineEdit(favoriteEditorDialog_);
        favoriteNoteEdit_->setObjectName(
            QStringLiteral("browserFavoriteNoteEdit"));
        layout->addWidget(favoriteNoteEdit_);
        auto* buttons = new QHBoxLayout();
        auto* cancelButton = new QPushButton(QStringLiteral("取消"),
                                             favoriteEditorDialog_);
        auto* saveButton = new QPushButton(QStringLiteral("保存"),
                                           favoriteEditorDialog_);
        saveButton->setObjectName(QStringLiteral("browserFavoriteSaveButton"));
        buttons->addStretch();
        buttons->addWidget(cancelButton);
        buttons->addWidget(saveButton);
        layout->addLayout(buttons);
        connect(cancelButton, &QPushButton::clicked, favoriteEditorDialog_,
                &QDialog::hide);
        connect(saveButton, &QPushButton::clicked, this,
                &BrowserPage::saveFavoriteEditor);
    }
    favoriteTitleEdit_->setText(entry.title);
    favoriteUrlEdit_->setText(entry.url);
    favoriteNoteEdit_->setText(entry.note);
    favoriteEditorDialog_->show();
    favoriteEditorDialog_->raise();
    favoriteEditorDialog_->activateWindow();
}

void BrowserPage::saveFavoriteEditor() {
    if (!dataModel_.isAvailable()) {
        return;
    }
    const BrowserAddress address =
        normalizeBrowserAddress(favoriteUrlEdit_->text());
    if (address.kind != BrowserAddressKind::Web) {
        favoriteUrlEdit_->setFocus();
        return;
    }
    QVector<BrowserFavoriteEntry> favorites = dataModel_.favorites();
    BrowserFavoriteEntry entry{address.url, favoriteTitleEdit_->text(),
                               favoriteNoteEdit_->text()};
    const bool isEditing = editingFavoriteIndex_ >= 0 &&
                           editingFavoriteIndex_ < favorites.size();
    int insertionIndex = isEditing ? editingFavoriteIndex_ : 0;
    if (isEditing) {
        favorites.removeAt(editingFavoriteIndex_);
    }
    for (int index = favorites.size() - 1; index >= 0; --index) {
        if (favorites.at(index).url.compare(entry.url, Qt::CaseInsensitive) == 0) {
            favorites.removeAt(index);
            if (index < insertionIndex) {
                --insertionIndex;
            }
        }
    }
    favorites.insert(std::clamp(insertionIndex, 0, favorites.size()), entry);
    replaceFavoritesData(std::move(favorites));
    favoriteEditorDialog_->hide();
}

void BrowserPage::removeSelectedFavorite() {
    if (!dataModel_.isAvailable() || favoritesList_ == nullptr) {
        return;
    }
    QListWidgetItem* const item = favoritesList_->currentItem();
    if (item == nullptr) {
        return;
    }
    const int index = item->data(Qt::UserRole + 1).toInt();
    QVector<BrowserFavoriteEntry> favorites = dataModel_.favorites();
    if (index < 0 || index >= favorites.size()) {
        return;
    }
    favorites.removeAt(index);
    replaceFavoritesData(std::move(favorites));
}

void BrowserPage::persistFavoriteListOrder() {
    if (!dataModel_.isAvailable() || favoritesList_ == nullptr ||
        (favoritesSearchEdit_ != nullptr &&
         !favoritesSearchEdit_->text().trimmed().isEmpty())) {
        return;
    }
    const QVector<BrowserFavoriteEntry>& favorites = dataModel_.favorites();
    if (favoritesList_->count() != favorites.size()) {
        refreshFavoritesList();
        return;
    }
    QVector<BrowserFavoriteEntry> reordered;
    reordered.reserve(favorites.size());
    QSet<int> knownIndices;
    for (int row = 0; row < favoritesList_->count(); ++row) {
        const int index =
            favoritesList_->item(row)->data(Qt::UserRole + 1).toInt();
        if (index < 0 || index >= favorites.size() || knownIndices.contains(index)) {
            refreshFavoritesList();
            return;
        }
        knownIndices.insert(index);
        reordered.append(favorites.at(index));
    }
    replaceFavoritesData(std::move(reordered));
}

void BrowserPage::chooseFavoriteImportFile() {
    const QString filePath = QFileDialog::getOpenFileName(
        favoritesDialog_, QStringLiteral("导入收藏夹"), {},
        QStringLiteral("收藏夹 HTML (*.html *.htm);;所有文件 (*)"));
    if (filePath.isEmpty()) {
        return;
    }
    QFile file(filePath);
    constexpr qint64 kMaximumBookmarkHtmlBytes = 8 * 1024 * 1024;
    if (file.size() > kMaximumBookmarkHtmlBytes ||
        !file.open(QIODevice::ReadOnly)) {
        setFavoriteTransferStatus(QStringLiteral("导入失败，原收藏夹已保留"));
        return;
    }
    const QByteArray html = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        setFavoriteTransferStatus(QStringLiteral("导入失败，原收藏夹已保留"));
        return;
    }
    prepareFavoriteImport(html);
}

void BrowserPage::prepareFavoriteImport(QByteArray html) {
    if (!dataModel_.isAvailable()) {
        return;
    }
    const BrowserBookmarkImportResult result = importBrowserBookmarksHtml(html);
    if (result.isInputTooLarge || result.favorites.isEmpty()) {
        pendingImportedFavorites_.clear();
        setFavoriteTransferStatus(
            result.isInputTooLarge
                ? QStringLiteral("导入失败：文件超过 8 MiB，原收藏夹已保留")
                : QStringLiteral("未找到可导入的 HTTP(S) 收藏，原收藏夹已保留"));
        return;
    }

    const QVector<BrowserFavoriteEntry>& existing = dataModel_.favorites();
    pendingImportedFavorites_ = existing;
    constexpr int kMaximumFavoriteEntries = 5000;
    QSet<QString> knownUrls;
    for (const BrowserFavoriteEntry& favorite : existing) {
        knownUrls.insert(favorite.url.toLower());
    }
    int addedCount = 0;
    int skippedCount = result.rejectedEntries;
    for (const BrowserFavoriteEntry& favorite : result.favorites) {
        const QString key = favorite.url.toLower();
        if (knownUrls.contains(key) ||
            pendingImportedFavorites_.size() == kMaximumFavoriteEntries) {
            ++skippedCount;
            continue;
        }
        knownUrls.insert(key);
        pendingImportedFavorites_.append(favorite);
        ++addedCount;
    }
    if (addedCount == 0) {
        pendingImportedFavorites_.clear();
        setFavoriteTransferStatus(
            QStringLiteral("没有新收藏需要导入，已跳过 %1 项").arg(skippedCount));
        return;
    }

    if (favoriteImportDialog_ == nullptr) {
        favoriteImportDialog_ = new QDialog(this);
        favoriteImportDialog_->setObjectName(
            QStringLiteral("browserFavoriteImportDialog"));
        favoriteImportDialog_->setWindowTitle(QStringLiteral("确认导入收藏"));
        auto* layout = new QVBoxLayout(favoriteImportDialog_);
        favoriteImportSummaryLabel_ = new QLabel(favoriteImportDialog_);
        favoriteImportSummaryLabel_->setObjectName(
            QStringLiteral("browserFavoriteImportSummaryLabel"));
        favoriteImportSummaryLabel_->setWordWrap(true);
        layout->addWidget(favoriteImportSummaryLabel_);
        auto* buttons = new QHBoxLayout();
        auto* cancelButton = new QPushButton(QStringLiteral("取消"),
                                             favoriteImportDialog_);
        auto* confirmButton = new QPushButton(QStringLiteral("确认导入"),
                                              favoriteImportDialog_);
        confirmButton->setObjectName(
            QStringLiteral("browserFavoriteImportConfirmButton"));
        buttons->addStretch();
        buttons->addWidget(cancelButton);
        buttons->addWidget(confirmButton);
        layout->addLayout(buttons);
        connect(cancelButton, &QPushButton::clicked, favoriteImportDialog_,
                [this] {
                    pendingImportedFavorites_.clear();
                    favoriteImportDialog_->hide();
                });
        connect(confirmButton, &QPushButton::clicked, this,
                &BrowserPage::confirmFavoriteImport);
    }
    favoriteImportSummaryLabel_->setText(
        QStringLiteral("将新增 %1 项收藏，并跳过 %2 项无效或重复内容。")
            .arg(addedCount)
            .arg(skippedCount));
    favoriteImportDialog_->show();
    favoriteImportDialog_->raise();
    favoriteImportDialog_->activateWindow();
}

void BrowserPage::confirmFavoriteImport() {
    if (!dataModel_.isAvailable() || pendingImportedFavorites_.isEmpty()) {
        return;
    }
    replaceFavoritesData(pendingImportedFavorites_);
    pendingImportedFavorites_.clear();
    favoriteImportDialog_->hide();
    setFavoriteTransferStatus(QStringLiteral("收藏导入完成"));
}

void BrowserPage::chooseFavoriteExportFile() {
    QString filePath = QFileDialog::getSaveFileName(
        favoritesDialog_, QStringLiteral("导出收藏夹"),
        QDir::home().filePath(QStringLiteral("MediaHub-bookmarks.html")),
        QStringLiteral("收藏夹 HTML (*.html *.htm)"));
    if (filePath.isEmpty()) {
        return;
    }
    if (QFileInfo(filePath).suffix().isEmpty()) {
        filePath += QStringLiteral(".html");
    }
    exportFavoritesToFile(filePath);
}

void BrowserPage::exportFavoritesToFile(QString filePath) {
    if (!dataModel_.isAvailable() || filePath.isEmpty()) {
        return;
    }
    QSaveFile file(filePath);
    const QByteArray html = exportBrowserBookmarksHtml(dataModel_.favorites());
    if (!file.open(QIODevice::WriteOnly) || file.write(html) != html.size() ||
        !file.commit()) {
        file.cancelWriting();
        setFavoriteTransferStatus(QStringLiteral("导出失败，未写入目标文件"));
        return;
    }
    setFavoriteTransferStatus(QStringLiteral("收藏已安全导出"));
}

void BrowserPage::setFavoriteTransferStatus(const QString& status) {
    if (favoriteTransferStatusLabel_ == nullptr) {
        statusLabel_->setText(status);
        return;
    }
    favoriteTransferStatusLabel_->setText(status);
    favoriteTransferStatusLabel_->setVisible(!status.isEmpty());
}

void BrowserPage::showClearDataConfirmation() {
    if (clearDataDialog_ == nullptr) {
        clearDataDialog_ = new QDialog(this);
        clearDataDialog_->setObjectName(QStringLiteral("browserClearDataDialog"));
        clearDataDialog_->setWindowTitle(QStringLiteral("清除网页数据"));
        auto* layout = new QVBoxLayout(clearDataDialog_);
        auto* explanation = new QLabel(
            QStringLiteral("将清除 MediaHub 内置浏览器专用 Profile 的 Cookie、"
                           "LocalStorage、IndexedDB、缓存、已保存密码和自动填充数据，"
                           "并关闭其他网页标签。MediaHub 浏览历史和收藏夹会保留，"
                           "系统 Edge 的资料不受影响。"),
            clearDataDialog_);
        explanation->setObjectName(
            QStringLiteral("browserClearDataExplanation"));
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto* buttons = new QHBoxLayout();
        auto* cancelButton = new QPushButton(QStringLiteral("取消"), clearDataDialog_);
        cancelButton->setObjectName(QStringLiteral("browserClearDataCancelButton"));
        auto* confirmButton =
            new QPushButton(QStringLiteral("确认清除"), clearDataDialog_);
        confirmButton->setObjectName(QStringLiteral("browserClearDataConfirmButton"));
        buttons->addStretch();
        buttons->addWidget(cancelButton);
        buttons->addWidget(confirmButton);
        layout->addLayout(buttons);
        connect(cancelButton, &QPushButton::clicked, clearDataDialog_, &QDialog::hide);
        connect(confirmButton, &QPushButton::clicked, this,
                &BrowserPage::confirmClearBrowsingData);
    }
    clearDataDialog_->show();
    clearDataDialog_->raise();
    clearDataDialog_->activateWindow();
}

void BrowserPage::confirmClearBrowsingData() {
    clearDataDialog_->hide();
    if (isShuttingDown_ || state_ == BrowserPageState::Initializing ||
        state_ == BrowserPageState::ClearingData) {
        return;
    }
    closeFindBar(true);
    leaveWebFullScreenForTabChange();
    rejectUnansweredSensitiveRequests();
    clearTabFavicons();
    if (backend_.supportsConcurrentDownloads() && downloadCenter_ != nullptr) {
        const QVector<std::uint64_t> activeRequests =
            downloadCenter_->activeRequestIds();
        for (const std::uint64_t requestId : activeRequests) {
            if (downloadCenter_->requestCancel(requestId)) {
                backend_.cancelDownload(requestId);
            }
        }
    } else if (activeDownloadId_.has_value() && downloadWidget_ != nullptr &&
               !downloadWidget_->isTerminal()) {
        downloadWidget_->completeDestinationSelection(QString{});
    }
    tabGroupModel_.replace({});
    updateTabGroupDialogPresentation();
    ++generation_;
    const BrowserTabRecord survivingTab = tabs_.at(currentTabIndex_);
    for (const BrowserTabRecord& tab : tabs_) {
        if (tab.tabId != survivingTab.tabId) {
            backend_.closeTab(tab.tabId);
        }
    }
    tabs_ = {survivingTab};
    tabs_[0].generation = generation_;
    tabs_[0].state = BrowserPageState::ClearingData;
    currentTabIndex_ = 0;
    tabBar_->blockSignals(true);
    while (tabBar_->count() > 0) {
        tabBar_->removeTab(0);
    }
    tabBar_->addTab(survivingTab.title.isEmpty()
                        ? QStringLiteral("新标签页")
                        : survivingTab.title);
    tabBar_->setTabData(0,
                        QVariant::fromValue<qulonglong>(survivingTab.tabId));
    tabBar_->setCurrentIndex(0);
    tabBar_->blockSignals(false);
    updateTabSearchPresentation();
    state_ = BrowserPageState::ClearingData;
    statusLabel_->setText(QStringLiteral("正在清除网页数据..."));
    updateControls();
    backend_.clearBrowsingData(generation_);
}

void BrowserPage::showHost() {
    contentStack_->setCurrentWidget(browserHost_);
}

void BrowserPage::showError(BrowserErrorKind kind) {
    errorLabel_->setText(errorText(kind));
    contentStack_->setCurrentWidget(errorLabel_);
    statusLabel_->setText(QStringLiteral("网页功能需要处理"));
}

void BrowserPage::showTabProcessFailure() {
    if (currentTabIndex_ < 0 || currentTabIndex_ >= tabs_.size() ||
        !tabs_.at(currentTabIndex_).processFailure.has_value()) {
        return;
    }
    const BrowserProcessFailureKind kind =
        *tabs_.at(currentTabIndex_).processFailure;
    const bool canRecover =
        kind == BrowserProcessFailureKind::RenderProcessExited ||
        kind == BrowserProcessFailureKind::RenderProcessUnresponsive;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 cooldownMilliseconds = recoveryCooldownMilliseconds();
    const bool isRecoveryBlocked =
        tabs_.at(currentTabIndex_).recoveryAttempts >=
            kMaximumRecoveryAttemptsPerWindow &&
        tabs_.at(currentTabIndex_).lastRecoveryAt.isValid() &&
        tabs_.at(currentTabIndex_).lastRecoveryAt.msecsTo(now) <
            cooldownMilliseconds;
    processFailureTitleLabel_->setText(
        canRecover ? QStringLiteral("这个网页标签停止响应")
                   : QStringLiteral("浏览器进程已退出"));
    processFailureDetailLabel_->setText(
        canRecover
            ? (isRecoveryBlocked
                   ? QStringLiteral("该标签在短时间内连续失败，已暂停恢复请求。"
                                    "请关闭标签或稍后再试。")
                   : QStringLiteral("标签信息、固定状态、分组、静音和缩放仍然保留。"
                                    "请手动重新加载，MediaHub 不会无限自动重启网页。"))
            : QStringLiteral("共享 WebView2 环境已经失效，无法只重新加载单个标签。"
                             "请重启 MediaHub 后恢复网页会话。"));
    processRecoveryButton_->setVisible(canRecover);
    processRecoveryButton_->setEnabled(canRecover && !isRecoveryBlocked);
    contentStack_->setCurrentWidget(processFailurePage_);
    statusLabel_->setText(canRecover ? QStringLiteral("网页标签需要恢复")
                                     : QStringLiteral("网页组件需要重启"));
}

void BrowserPage::recoverFailedTab() {
    if (isShuttingDown_ || hasBrowserProcessExited_ || currentTabIndex_ < 0 ||
        currentTabIndex_ >= tabs_.size()) {
        return;
    }
    const std::uint64_t tabId = tabs_.at(currentTabIndex_).tabId;
    const int index = findTabIndex(tabId);
    if (index < 0 || !tabs_.at(index).processFailure.has_value()) {
        return;
    }
    const BrowserProcessFailureKind kind = *tabs_.at(index).processFailure;
    if (kind != BrowserProcessFailureKind::RenderProcessExited &&
        kind != BrowserProcessFailureKind::RenderProcessUnresponsive) {
        return;
    }
    if (tabs_.at(index).recoveryAttempts >=
        kMaximumRecoveryAttemptsPerWindow) {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 cooldownMilliseconds = recoveryCooldownMilliseconds();
        if (tabs_.at(index).lastRecoveryAt.isValid() &&
            tabs_.at(index).lastRecoveryAt.msecsTo(now) <
                cooldownMilliseconds) {
            return;
        }
        tabs_[index].recoveryAttempts = 0;
        tabs_[index].lastRecoveryAt = {};
    }
    const std::uint64_t recoveryGeneration = generation_ + 1;
    if (!backend_.recoverTab(tabId, recoveryGeneration)) {
        processFailureDetailLabel_->setText(
            QStringLiteral("重新加载请求未能提交，标签仍可再次恢复。"));
        statusLabel_->setText(QStringLiteral("网页标签恢复失败"));
        return;
    }
    generation_ = recoveryGeneration;
    tabs_[index].generation = recoveryGeneration;
    tabs_[index].processFailure.reset();
    tabs_[index].state = BrowserPageState::Navigating;
    state_ = BrowserPageState::Navigating;
    statusLabel_->setText(QStringLiteral("正在恢复网页标签..."));
    showHost();
    updateControls();
}

void BrowserPage::refreshRecoveryCooldown(const std::uint64_t tabId) {
    if (isShuttingDown_ || hasBrowserProcessExited_) {
        return;
    }
    const int index = findTabIndex(tabId);
    if (index < 0 || !tabs_.at(index).processFailure.has_value() ||
        tabs_.at(index).recoveryAttempts <
            kMaximumRecoveryAttemptsPerWindow ||
        !tabs_.at(index).lastRecoveryAt.isValid()) {
        return;
    }
    const qint64 elapsed = tabs_.at(index).lastRecoveryAt.msecsTo(
        QDateTime::currentDateTimeUtc());
    const qint64 cooldownMilliseconds = recoveryCooldownMilliseconds();
    if (elapsed < cooldownMilliseconds) {
        QTimer::singleShot(
            static_cast<int>(cooldownMilliseconds - elapsed), this,
            [this, tabId] { refreshRecoveryCooldown(tabId); });
        return;
    }
    tabs_[index].recoveryAttempts = 0;
    tabs_[index].lastRecoveryAt = {};
    if (index == currentTabIndex_) {
        showTabProcessFailure();
        updateControls();
    }
}

qint64 BrowserPage::recoveryCooldownMilliseconds() const noexcept {
    bool isValid = false;
    const qint64 configured =
        property("browserRecoveryCooldownMilliseconds").toLongLong(&isValid);
    if (!isValid) {
        return kRecoveryCooldownMilliseconds;
    }
    return std::clamp<qint64>(configured, 1, 24 * 60 * 60 * 1000);
}

void BrowserPage::resolvePermission(
    const std::uint64_t requestId, BrowserPermissionDecision decision) {
    if (!pendingPermissionId_.has_value() || *pendingPermissionId_ != requestId) {
        return;
    }
    if (pendingPermissionKind_ == BrowserPermissionKind::Other ||
        (pendingPermissionKind_ == BrowserPermissionKind::ScreenCapture &&
         decision == BrowserPermissionDecision::RememberForOrigin)) {
        decision = BrowserPermissionDecision::Deny;
    }
    if (decision == BrowserPermissionDecision::RememberForOrigin &&
        permissionStore_ != nullptr) {
        const bool didSave = permissionStore_->set(
            pendingPermissionOrigin_, pendingPermissionKind_,
            BrowserPermissionState::Allow);
        decision = didSave ? BrowserPermissionDecision::AllowOnce
                           : BrowserPermissionDecision::Deny;
    }
    pendingPermissionId_.reset();
    pendingPermissionKind_ = BrowserPermissionKind::Other;
    pendingPermissionOrigin_.clear();
    BrowserPermissionDialog* const dialog = permissionDialog_;
    permissionDialog_ = nullptr;
    if (dialog != nullptr) {
        dialog->setObjectName(QStringLiteral("browserPermissionDialogFinished"));
        dialog->hide();
        dialog->deleteLater();
    }
    backend_.answerPermission(requestId, decision);
}

void BrowserPage::resolveExternalProtocol(const std::uint64_t requestId,
                                          const bool isAllowed) {
    if (!pendingExternalProtocolId_.has_value() ||
        *pendingExternalProtocolId_ != requestId) {
        return;
    }
    pendingExternalProtocolId_.reset();
    QDialog* const dialog = externalProtocolDialog_;
    externalProtocolDialog_ = nullptr;
    if (dialog != nullptr) {
        dialog->setObjectName(QStringLiteral("browserExternalProtocolDialogFinished"));
        dialog->hide();
        dialog->deleteLater();
    }
    backend_.answerExternalProtocol(requestId, isAllowed);
}

void BrowserPage::resolveCertificateError(
    const std::uint64_t requestId,
    const BrowserCertificateDecision decision) {
    if (!pendingCertificateId_.has_value() ||
        *pendingCertificateId_ != requestId) {
        return;
    }
    pendingCertificateId_.reset();
    QDialog* const dialog = certificateDialog_;
    certificateDialog_ = nullptr;
    if (dialog != nullptr) {
        dialog->setObjectName(QStringLiteral("browserCertificateDialogFinished"));
        dialog->hide();
        dialog->deleteLater();
    }
    backend_.answerCertificateError(requestId, decision);
}

void BrowserPage::rejectUnansweredSensitiveRequests() {
    if (pendingPermissionId_.has_value()) {
        resolvePermission(*pendingPermissionId_, BrowserPermissionDecision::Deny);
    }
    if (pendingExternalProtocolId_.has_value()) {
        resolveExternalProtocol(*pendingExternalProtocolId_, false);
    }
    if (pendingCertificateId_.has_value()) {
        resolveCertificateError(*pendingCertificateId_,
                                BrowserCertificateDecision::ReturnToSafety);
    }
    if (!backend_.supportsConcurrentDownloads() && activeDownloadId_.has_value() &&
        !downloadWidget_->isTerminal() &&
        !downloadWidget_->hasSubmittedDestination()) {
        downloadWidget_->completeDestinationSelection(QString{});
    } else if (downloadCenter_ != nullptr) {
        for (const std::uint64_t requestId : downloadCenter_->activeRequestIds()) {
            const auto snapshot = downloadCenter_->itemSnapshot(requestId);
            if (snapshot.has_value() && !snapshot->hasSubmittedDestination) {
                downloadCenter_->completeDestinationSelection(requestId, QString{});
            }
        }
    }
}

void BrowserPage::updateControls() {
    const bool hasRecoverableFailure =
        currentTabIndex_ >= 0 && currentTabIndex_ < tabs_.size() &&
        tabs_.at(currentTabIndex_).processFailure.has_value();
    const bool canNavigate = !isShuttingDown_ && !hasBrowserProcessExited_ &&
                             !hasRecoverableFailure &&
                             state_ != BrowserPageState::Unavailable &&
                             state_ != BrowserPageState::Initializing &&
                             state_ != BrowserPageState::ClearingData;
    addressEdit_->setEnabled(canNavigate);
    goButton_->setEnabled(canNavigate);
    homeButton_->setEnabled(canNavigate);
    reloadButton_->setEnabled(canNavigate);
    clearDataButton_->setEnabled(canNavigate);
    newTabButton_->setEnabled(!isShuttingDown_ && !hasBrowserProcessExited_ &&
                              tabs_.size() < maximumTabCount());
    if (!canNavigate) {
        backButton_->setEnabled(false);
        forwardButton_->setEnabled(false);
    }
    reloadButton_->setText(state_ == BrowserPageState::Navigating
                               ? QStringLiteral("停止")
                               : QStringLiteral("刷新"));
}

void BrowserPage::updateBackendBounds() {
    if (!isInitialized_ || isShuttingDown_ || browserHost_->width() <= 0 ||
        browserHost_->height() <= 0) {
        return;
    }
    const qreal scale = browserHost_->devicePixelRatioF();
    backend_.setBounds(QRect(0, 0, qRound(browserHost_->width() * scale),
                             qRound(browserHost_->height() * scale)));
}

void BrowserPage::recordSuccessfulNavigation(const QString& visibleUrl,
                                             const QString& title) {
    if (!dataModel_.isAvailable()) {
        return;
    }
    const QString storedUrl = normalizeStoredBrowserUrl(visibleUrl);
    if (storedUrl.isEmpty()) {
        return;
    }
    QVector<BrowserHistoryEntry> history = dataModel_.history();
    for (auto iterator = history.begin(); iterator != history.end();) {
        if (iterator->url == storedUrl) {
            iterator = history.erase(iterator);
        } else {
            ++iterator;
        }
    }
    history.prepend(BrowserHistoryEntry{
        storedUrl, title.trimmed(), QDateTime::currentMSecsSinceEpoch()});
    replaceHistoryDataDeferred(std::move(history));
}

void BrowserPage::updateRecordedNavigationTitle(const QString& visibleUrl,
                                                const QString& title) {
    if (!dataModel_.isAvailable()) {
        return;
    }
    const QString storedUrl = normalizeStoredBrowserUrl(visibleUrl);
    if (storedUrl.isEmpty()) {
        return;
    }
    QVector<BrowserHistoryEntry> history = dataModel_.history();
    for (BrowserHistoryEntry& entry : history) {
        if (entry.url == storedUrl) {
            const QString trimmedTitle = title.trimmed();
            if (entry.title != trimmedTitle) {
                entry.title = trimmedTitle;
                replaceHistoryDataDeferred(std::move(history));
            }
            return;
        }
    }
}

QString BrowserPage::errorText(BrowserErrorKind kind) const {
    switch (kind) {
        case BrowserErrorKind::RuntimeUnavailable:
            return QStringLiteral(
                "未检测到 Microsoft Edge WebView2 Runtime，请安装或修复后重试。");
        case BrowserErrorKind::InitializationFailed:
            return QStringLiteral("网页组件初始化失败，播放器其他功能仍可使用。");
        case BrowserErrorKind::ProfileUnavailable:
            return QStringLiteral("无法建立 MediaHub 专用网页资料目录。");
        case BrowserErrorKind::InvalidAddress:
            return QStringLiteral("请输入完整、合法的网站地址。");
        case BrowserErrorKind::NavigationFailed:
            return QStringLiteral("页面打开失败，请检查网络或网站状态。");
        case BrowserErrorKind::BlockedScheme:
            return QStringLiteral("该地址栏命令不安全或不受支持，已阻止执行。");
        case BrowserErrorKind::ExternalProtocolFailed:
            return QStringLiteral("无法打开已确认的外部应用。");
        case BrowserErrorKind::CertificateRejected:
            return QStringLiteral("已返回安全页面，未继续访问证书异常站点。");
        case BrowserErrorKind::PermissionDenied:
            return QStringLiteral("网站权限请求已拒绝。");
        case BrowserErrorKind::DownloadFailed:
            return QStringLiteral("下载失败或目标不可写。");
        case BrowserErrorKind::ClearDataFailed:
            return QStringLiteral("网页数据清除失败，请关闭页面后重试。");
    }
    return QStringLiteral("网页组件发生未知错误。");
}

}  // namespace mediahub::gui
