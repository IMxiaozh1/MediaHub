#include "browser_page.h"

#include <QDialog>
#include <QDateTime>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
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
#include "browser_data_store.h"
#include "browser_download_widget.h"
#include "browser_navigation_policy.h"
#include "browser_permission_dialog.h"

namespace mediahub::gui {
namespace {

const QString kBrowserHomeUrl = QStringLiteral("https://www.microsoft.com/edge");

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

 protected:
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
};

// 历史和收藏项把左键、Ctrl+左键和中键统一成稳定打开动作。
class BrowserLinkListWidget final : public QListWidget {
 public:
    using OpenCallback = std::function<void(const QString&, bool)>;

    explicit BrowserLinkListWidget(QWidget* const parent)
        : QListWidget(parent) {}

    void setOpenCallback(OpenCallback callback) {
        openCallback_ = std::move(callback);
    }

 protected:
    void mouseReleaseEvent(QMouseEvent* const event) override {
        QListWidget::mouseReleaseEvent(event);
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

 private:
    OpenCallback openCallback_;
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
                         QWidget* parent, BrowserDataStore* const dataStore)
    : QWidget(parent),
      backend_(backend),
      dataStore_(dataStore),
      userDataDirectory_(std::move(userDataDirectory)) {
    setObjectName(QStringLiteral("browserPage"));
    buildUi();
    tabs_.append(BrowserTabRecord{1, generation_});
    tabBar_->addTab(QStringLiteral("新标签页"));
    backend_.setEventListener(this);
    updateControls();
}

BrowserPage::~BrowserPage() {
    shutdown();
}

void BrowserPage::activate() {
    if (isShuttingDown_) {
        return;
    }
    backend_.setVisible(true);
    backend_.setSuspended(false);
    backend_.setAudioMuted(false);
}

void BrowserPage::deactivate() {
    if (isShuttingDown_) {
        return;
    }
    backend_.setAudioMuted(true);
    backend_.setSuspended(true);
    backend_.setVisible(false);
}

void BrowserPage::shutdown() noexcept {
    if (isShuttingDown_) {
        return;
    }
    isShuttingDown_ = true;
    state_ = BrowserPageState::ShuttingDown;
    ++generation_;
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
    if (activeDownloadId_.has_value() && !downloadWidget_->isTerminal() &&
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
    if (isShuttingDown_ || index < 0 ||
        generation != tabs_.at(index).generation) {
        return;
    }
    tabs_[index].state = BrowserPageState::Ready;
    tabs_[index].lastError.reset();
    if (index != currentTabIndex_) {
        return;
    }
    state_ = BrowserPageState::Ready;
    statusLabel_->setText(QStringLiteral("网页组件已就绪"));
    showHost();
    updateControls();
    updateBackendBounds();
}

void BrowserPage::onBrowserError(std::uint64_t generation, BrowserErrorKind kind,
                                 long) {
    if (isShuttingDown_) {
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
    if (index != currentTabIndex_) {
        return;
    }
    state_ = BrowserPageState::Failed;
    showError(kind);
    updateControls();
}

void BrowserPage::onNavigationStarted(std::uint64_t generation) {
    const int index = findTabIndex(1);
    if (isShuttingDown_ || index < 0 ||
        generation != tabs_.at(index).generation) {
        return;
    }
    tabs_[index].state = BrowserPageState::Navigating;
    tabs_[index].lastError.reset();
    if (index != currentTabIndex_) {
        return;
    }
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
    if (isShuttingDown_ || index < 0 ||
        generation != tabs_.at(index).generation) {
        return;
    }
    applyTabDocumentState(index, visibleUrl, title, canGoBack, canGoForward,
                          true, true);
}

void BrowserPage::onDocumentStateChanged(
    const std::uint64_t generation, const QString& visibleUrl,
    const QString& title, const bool canGoBack, const bool canGoForward) {
    const int index = findTabIndex(1);
    if (isShuttingDown_ || index < 0 ||
        generation != tabs_.at(index).generation) {
        return;
    }
    applyTabDocumentState(index, visibleUrl, title, canGoBack, canGoForward,
                          false, false);
}

void BrowserPage::onNavigationStopped(
    const std::uint64_t generation, const QString& visibleUrl,
    const QString& title, const bool canGoBack, const bool canGoForward) {
    const int index = findTabIndex(1);
    if (isShuttingDown_ || index < 0 ||
        generation != tabs_.at(index).generation) {
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
    if (isFullScreen) {
        wasToolbarHidden_ = toolbar_->isHidden();
        wasInformationRowHidden_ = informationRow_->isHidden();
        wasDownloadWidgetHidden_ = downloadWidget_->isHidden();
        toolbar_->hide();
        informationRow_->hide();
        downloadWidget_->hide();
    } else {
        toolbar_->setVisible(!wasToolbarHidden_);
        informationRow_->setVisible(!wasInformationRowHidden_);
        downloadWidget_->setVisible(!wasDownloadWidgetHidden_);
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
        case BrowserAccelerator::ExitFullScreen:
            exitWebFullScreen();
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
    if (pendingPermissionId_.has_value()) {
        resolvePermission(*pendingPermissionId_, BrowserPermissionDecision::Deny);
    }

    pendingPermissionId_ = requestId;
    pendingPermissionKind_ = kind;
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
    if (activeDownloadId_.has_value() && !downloadWidget_->isTerminal()) {
        backend_.cancelDownload(requestId);
        return;
    }
    activeDownloadId_ = requestId;
    isDownloadCancellationSent_ = false;
    downloadWidget_->beginDownload(requestId, origin, suggestedFileName, totalBytes);
    if (isWebFullScreen_) {
        wasDownloadWidgetHidden_ = false;
        downloadWidget_->hide();
    }
}

void BrowserPage::onDownloadUpdated(const std::uint64_t requestId,
                                    const BrowserDownloadState state,
                                    const std::int64_t receivedBytes,
                                    const std::int64_t totalBytes) {
    if (isShuttingDown_ || !activeDownloadId_.has_value() ||
        *activeDownloadId_ != requestId) {
        return;
    }
    downloadWidget_->updateDownload(requestId, state, receivedBytes, totalBytes);
    if (state == BrowserDownloadState::CancelFailed) {
        isDownloadCancellationSent_ = false;
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
    tabBar_->setTabText(0, QStringLiteral("新标签页"));
    state_ = BrowserPageState::Ready;
    addressEdit_->clear();
    titleLabel_->setText(QStringLiteral("网页"));
    statusLabel_->setText(QStringLiteral("网页数据已清除"));
    showHost();
    updateControls();
}

void BrowserPage::onPopupRejected() {
    if (!isShuttingDown_) {
        statusLabel_->setText(QStringLiteral("无法打开新的网页标签"));
    }
}

bool BrowserPage::onNewTabRequested(const std::uint64_t newWindowRequestId,
                                    const QString& url) {
    if (isShuttingDown_ || url.trimmed().isEmpty()) {
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
    leaveWebFullScreenForTabChange();
    rejectUnansweredSensitiveRequests();
    generation_ = tabGeneration;
    ++nextTabId_;
    tabs_.append(BrowserTabRecord{tabId, tabGeneration, initialUrl, {}, false,
                                  false, BrowserPageState::Initializing});
    const int newIndex = tabs_.size() - 1;
    tabBar_->addTab(QStringLiteral("新标签页"));
    currentTabIndex_ = newIndex;
    tabBar_->setCurrentIndex(newIndex);
    backend_.activateTab(tabId);
    updateTabPresentation();
    return true;
}

void BrowserPage::onTabReady(const std::uint64_t tabId,
                             const std::uint64_t generation) {
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        isShuttingDown_) {
        return;
    }
    tabs_[index].state = BrowserPageState::Ready;
    tabs_[index].lastError.reset();
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
        isShuttingDown_) {
        return;
    }
    tabs_[index].state = BrowserPageState::Navigating;
    tabs_[index].lastError.reset();
    if (index == currentTabIndex_) {
        if (state_ == BrowserPageState::ClearingData) {
            return;
        }
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
        isShuttingDown_ || state_ == BrowserPageState::ClearingData) {
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
        isShuttingDown_ || state_ == BrowserPageState::ClearingData) {
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
        isShuttingDown_ || state_ == BrowserPageState::ClearingData) {
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
    tab.address = visibleUrl;
    tab.title = title;
    tab.canGoBack = canGoBack;
    tab.canGoForward = canGoForward;
    if (didFinishNavigation) {
        tab.state = BrowserPageState::Ready;
        tab.lastError.reset();
    }
    tabBar_->setTabText(index,
                        title.isEmpty() ? QStringLiteral("新标签页") : title);
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
}

void BrowserPage::onTabError(const std::uint64_t tabId,
                             const std::uint64_t generation,
                             const BrowserErrorKind kind,
                             const long errorCode) {
    Q_UNUSED(errorCode);
    const int index = findTabIndex(tabId);
    if (index < 0 || tabs_.at(index).generation != generation ||
        isShuttingDown_) {
        return;
    }
    tabs_[index].state = BrowserPageState::Failed;
    tabs_[index].lastError = kind;
    if (index == currentTabIndex_) {
        state_ = BrowserPageState::Failed;
        showError(kind);
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

void BrowserPage::keyPressEvent(QKeyEvent* const event) {
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
    clearDataButton_ = new QPushButton(QStringLiteral("清除网页数据"), toolbar_);
    clearDataButton_->setObjectName(QStringLiteral("browserClearDataButton"));
    historyButton_ = createToolButton(QStringLiteral("browserHistoryButton"),
                                      QStringLiteral("历史"), toolbar_);
    favoritesButton_ = createToolButton(
        QStringLiteral("browserFavoritesButton"), QStringLiteral("收藏夹"),
        toolbar_);

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
    informationLayout->addWidget(titleLabel_, 1);
    informationLayout->addWidget(statusLabel_);

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
    contentStack_->setCurrentWidget(errorLabel_);

    rootLayout->addWidget(tabBar_);
    rootLayout->addWidget(toolbar_);
    rootLayout->addWidget(informationRow_);
    downloadWidget_ = new BrowserDownloadWidget(this);
    rootLayout->addWidget(downloadWidget_);
    rootLayout->addWidget(content, 1);

    updateResponsiveStyle();

    connect(addressEdit_, &QLineEdit::returnPressed, this,
            &BrowserPage::submitAddress);
    connect(tabBar_, &QTabBar::currentChanged, this,
            &BrowserPage::activateTab);
    connect(tabBar_, &QTabBar::tabCloseRequested, this,
            &BrowserPage::closeTab);
    connect(tabBar_, &QTabBar::tabMoved, this,
            [this](const int from, const int to) {
                if (from < 0 || from >= tabs_.size() || to < 0 ||
                    to >= tabs_.size()) {
                    return;
                }
                tabs_.move(from, to);
                currentTabIndex_ = tabBar_->currentIndex();
            });
    connect(goButton_, &QPushButton::clicked, this, &BrowserPage::submitAddress);
    connect(backButton_, &QToolButton::clicked, this, [this] { backend_.goBack(); });
    connect(forwardButton_, &QToolButton::clicked, this,
            [this] { backend_.goForward(); });
    connect(reloadButton_, &QToolButton::clicked, this,
            [this] { backend_.reloadOrStop(); });
    connect(homeButton_, &QToolButton::clicked, this,
            [this] { navigateTo(kBrowserHomeUrl); });
    connect(clearDataButton_, &QPushButton::clicked, this,
            &BrowserPage::showClearDataConfirmation);
    connect(historyButton_, &QToolButton::clicked, this,
            &BrowserPage::showHistory);
    connect(favoritesButton_, &QToolButton::clicked, this,
            &BrowserPage::showFavorites);
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
    const QList<QWidget*> widgets{this, toolbar_, addressEdit_, goButton_,
                                  clearDataButton_, backButton_, forwardButton_,
                                  reloadButton_, homeButton_, historyButton_,
                                  favoritesButton_};
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
    if (isShuttingDown_ || state_ == BrowserPageState::Unavailable ||
        state_ == BrowserPageState::Initializing ||
        state_ == BrowserPageState::ClearingData) {
        return;
    }
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
    if (isShuttingDown_ || index < 0 || index >= tabs_.size() ||
        index == currentTabIndex_) {
        return;
    }
    leaveWebFullScreenForTabChange();
    rejectUnansweredSensitiveRequests();
    currentTabIndex_ = index;
    updateTabPresentation();
    const BrowserTabRecord& tab = tabs_.at(currentTabIndex_);
    backend_.activateTab(tab.tabId);
    state_ = tab.state;
    updateControls();
}

void BrowserPage::closeTab(const int index) {
    if (isShuttingDown_ || state_ == BrowserPageState::ClearingData ||
        index < 0 || index >= tabs_.size()) {
        return;
    }
    const bool wasCurrent = index == currentTabIndex_;
    if (wasCurrent) {
        leaveWebFullScreenForTabChange();
        rejectUnansweredSensitiveRequests();
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
        tabBar_->setTabText(0, QStringLiteral("新标签页"));
        updateTabPresentation();
        statusLabel_->setText(QStringLiteral("正在打开空白页..."));
        backend_.navigate(QStringLiteral("about:blank"), generation_);
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
    backend_.activateTab(tabs_.at(currentTabIndex_).tabId);
    updateTabPresentation();
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
    if (tab.state == BrowserPageState::Failed && tab.lastError.has_value()) {
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
        auto* list = new BrowserLinkListWidget(historyDialog_);
        historyList_ = list;
        historyList_->setObjectName(QStringLiteral("browserHistoryList"));
        historyList_->setAlternatingRowColors(true);
        list->setOpenCallback([this](const QString& url, const bool isNewTab) {
            openStoredUrl(url, isNewTab);
            historyDialog_->hide();
        });
        layout->addWidget(historyList_);
        auto* closeButton = new QPushButton(QStringLiteral("关闭"), historyDialog_);
        connect(closeButton, &QPushButton::clicked, historyDialog_, &QDialog::hide);
        layout->addWidget(closeButton, 0, Qt::AlignRight);
    }
    refreshHistoryList();
    historyDialog_->show();
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
        auto* list = new BrowserLinkListWidget(favoritesDialog_);
        favoritesList_ = list;
        favoritesList_->setObjectName(QStringLiteral("browserFavoritesList"));
        favoritesList_->setAlternatingRowColors(true);
        list->setOpenCallback([this](const QString& url, const bool isNewTab) {
            openStoredUrl(url, isNewTab);
            favoritesDialog_->hide();
        });
        layout->addWidget(favoritesList_);
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
        auto* closeButton = new QPushButton(QStringLiteral("关闭"),
                                            favoritesDialog_);
        buttons->addWidget(addButton);
        buttons->addWidget(editButton);
        buttons->addWidget(removeButton);
        buttons->addStretch();
        buttons->addWidget(closeButton);
        layout->addLayout(buttons);
        connect(addButton, &QPushButton::clicked, this,
                [this] { showFavoriteEditor(); });
        connect(editButton, &QPushButton::clicked, this, [this] {
            showFavoriteEditor(favoritesList_->currentRow());
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
        updateSelectionActions();
        connect(closeButton, &QPushButton::clicked, favoritesDialog_,
                &QDialog::hide);
    }
    refreshFavoritesList();
    favoritesDialog_->show();
    favoritesDialog_->raise();
    favoritesDialog_->activateWindow();
}

void BrowserPage::refreshHistoryList() {
    if (historyList_ == nullptr) {
        return;
    }
    historyList_->clear();
    if (dataStore_ == nullptr) {
        return;
    }
    const QVector<BrowserHistoryEntry> history = dataStore_->loadHistory();
    for (const BrowserHistoryEntry& entry : history) {
        const QString label = entry.title.isEmpty() ? entry.url
                                                    : entry.title + QStringLiteral("\n") +
                                                          entry.url;
        auto* item = new QListWidgetItem(label, historyList_);
        item->setData(Qt::UserRole, entry.url);
        item->setToolTip(entry.url);
    }
}

void BrowserPage::refreshFavoritesList() {
    if (favoritesList_ == nullptr) {
        return;
    }
    favoritesList_->clear();
    if (dataStore_ == nullptr) {
        return;
    }
    const QVector<BrowserFavoriteEntry> favorites = dataStore_->loadFavorites();
    for (const BrowserFavoriteEntry& entry : favorites) {
        QString label = entry.title.isEmpty() ? entry.url
                                              : entry.title + QStringLiteral("\n") +
                                                    entry.url;
        if (!entry.note.isEmpty()) {
            label += QStringLiteral("\n备注：") + entry.note;
        }
        auto* item = new QListWidgetItem(label, favoritesList_);
        item->setData(Qt::UserRole, entry.url);
        item->setToolTip(entry.url);
    }
    favoritesList_->setCurrentRow(-1);
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
    if (dataStore_ == nullptr || currentTabIndex_ < 0 ||
        currentTabIndex_ >= tabs_.size()) {
        return;
    }
    QVector<BrowserFavoriteEntry> favorites = dataStore_->loadFavorites();
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
    if (dataStore_ == nullptr) {
        return;
    }
    const BrowserAddress address =
        normalizeBrowserAddress(favoriteUrlEdit_->text());
    if (address.kind != BrowserAddressKind::Web) {
        favoriteUrlEdit_->setFocus();
        return;
    }
    QVector<BrowserFavoriteEntry> favorites = dataStore_->loadFavorites();
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
    dataStore_->saveFavorites(favorites);
    favoriteEditorDialog_->hide();
    refreshFavoritesList();
}

void BrowserPage::removeSelectedFavorite() {
    if (dataStore_ == nullptr || favoritesList_ == nullptr) {
        return;
    }
    const int index = favoritesList_->currentRow();
    QVector<BrowserFavoriteEntry> favorites = dataStore_->loadFavorites();
    if (index < 0 || index >= favorites.size()) {
        return;
    }
    favorites.removeAt(index);
    dataStore_->saveFavorites(favorites);
    refreshFavoritesList();
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
    leaveWebFullScreenForTabChange();
    rejectUnansweredSensitiveRequests();
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
    tabBar_->setCurrentIndex(0);
    tabBar_->blockSignals(false);
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
    pendingPermissionId_.reset();
    pendingPermissionKind_ = BrowserPermissionKind::Other;
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
    if (activeDownloadId_.has_value() && !downloadWidget_->isTerminal() &&
        !downloadWidget_->hasSubmittedDestination()) {
        downloadWidget_->completeDestinationSelection(QString{});
    }
}

void BrowserPage::updateControls() {
    const bool canNavigate = !isShuttingDown_ &&
                             state_ != BrowserPageState::Unavailable &&
                             state_ != BrowserPageState::Initializing &&
                             state_ != BrowserPageState::ClearingData;
    addressEdit_->setEnabled(canNavigate);
    goButton_->setEnabled(canNavigate);
    homeButton_->setEnabled(canNavigate);
    reloadButton_->setEnabled(canNavigate);
    clearDataButton_->setEnabled(canNavigate);
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
    if (dataStore_ == nullptr) {
        return;
    }
    const QString storedUrl = normalizeStoredBrowserUrl(visibleUrl);
    if (storedUrl.isEmpty()) {
        return;
    }
    QVector<BrowserHistoryEntry> history = dataStore_->loadHistory();
    for (auto iterator = history.begin(); iterator != history.end();) {
        if (iterator->url == storedUrl) {
            iterator = history.erase(iterator);
        } else {
            ++iterator;
        }
    }
    history.prepend(BrowserHistoryEntry{
        storedUrl, title.trimmed(), QDateTime::currentMSecsSinceEpoch()});
    dataStore_->saveHistory(history);
}

void BrowserPage::updateRecordedNavigationTitle(const QString& visibleUrl,
                                                const QString& title) {
    if (dataStore_ == nullptr) {
        return;
    }
    const QString storedUrl = normalizeStoredBrowserUrl(visibleUrl);
    if (storedUrl.isEmpty()) {
        return;
    }
    QVector<BrowserHistoryEntry> history = dataStore_->loadHistory();
    for (BrowserHistoryEntry& entry : history) {
        if (entry.url == storedUrl) {
            const QString trimmedTitle = title.trimmed();
            if (entry.title != trimmedTitle) {
                entry.title = trimmedTitle;
                dataStore_->saveHistory(history);
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
