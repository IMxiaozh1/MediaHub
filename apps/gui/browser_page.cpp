#include "browser_page.h"

#include <QDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QStyle>
#include <QStackedLayout>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtMath>

#include <utility>

#include "browser_backend.h"
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
                         QWidget* parent)
    : QWidget(parent),
      backend_(backend),
      userDataDirectory_(std::move(userDataDirectory)) {
    setObjectName(QStringLiteral("browserPage"));
    buildUi();
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
    backend_.closePopups();
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
    if (generation != generation_ || isShuttingDown_) {
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
    if (generation != generation_ || isShuttingDown_) {
        return;
    }
    state_ = BrowserPageState::Failed;
    showError(kind);
    updateControls();
}

void BrowserPage::onNavigationStarted(std::uint64_t generation) {
    if (generation != generation_ || isShuttingDown_) {
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
    if (generation != generation_ || isShuttingDown_) {
        return;
    }
    state_ = BrowserPageState::Ready;
    addressEdit_->setText(visibleUrl);
    titleLabel_->setText(title.isEmpty() ? QStringLiteral("网页") : title);
    statusLabel_->setText(QStringLiteral("载入完成"));
    backButton_->setEnabled(canGoBack);
    forwardButton_->setEnabled(canGoForward);
    showHost();
    updateControls();
}

void BrowserPage::onFullScreenChanged(std::uint64_t generation,
                                      bool isFullScreen) {
    if (generation != generation_ || isShuttingDown_) {
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
    if (generation != generation_ || isShuttingDown_) {
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
    state_ = BrowserPageState::Ready;
    addressEdit_->clear();
    titleLabel_->setText(QStringLiteral("网页"));
    statusLabel_->setText(QStringLiteral("网页数据已清除"));
    showHost();
    updateControls();
}

void BrowserPage::onPopupRejected() {
    if (!isShuttingDown_) {
        statusLabel_->setText(QStringLiteral("登录弹窗数量已达上限"));
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

    backButton_ = createToolButton(QStringLiteral("browserBackButton"),
                                   QStringLiteral("后退"), toolbar_);
    forwardButton_ = createToolButton(QStringLiteral("browserForwardButton"),
                                      QStringLiteral("前进"), toolbar_);
    reloadButton_ = createToolButton(QStringLiteral("browserReloadButton"),
                                     QStringLiteral("刷新"), toolbar_);
    homeButton_ = createToolButton(QStringLiteral("browserHomeButton"),
                                   QStringLiteral("主页"), toolbar_);
    addressEdit_ = new QLineEdit(toolbar_);
    addressEdit_->setObjectName(QStringLiteral("browserAddressEdit"));
    addressEdit_->setPlaceholderText(QStringLiteral("输入网站地址"));
    addressEdit_->setMinimumWidth(180);
    goButton_ = new QPushButton(QStringLiteral("访问"), toolbar_);
    goButton_->setObjectName(QStringLiteral("browserGoButton"));
    clearDataButton_ = new QPushButton(QStringLiteral("清除网页数据"), toolbar_);
    clearDataButton_->setObjectName(QStringLiteral("browserClearDataButton"));

    toolbarLayout->addWidget(backButton_);
    toolbarLayout->addWidget(forwardButton_);
    toolbarLayout->addWidget(reloadButton_);
    toolbarLayout->addWidget(homeButton_);
    toolbarLayout->addWidget(addressEdit_, 1);
    toolbarLayout->addWidget(goButton_);
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

    rootLayout->addWidget(toolbar_);
    rootLayout->addWidget(informationRow_);
    downloadWidget_ = new BrowserDownloadWidget(this);
    rootLayout->addWidget(downloadWidget_);
    rootLayout->addWidget(content, 1);

    updateResponsiveStyle();

    connect(addressEdit_, &QLineEdit::returnPressed, this,
            &BrowserPage::submitAddress);
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
                                  reloadButton_, homeButton_};
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
    ++generation_;
    state_ = BrowserPageState::Navigating;
    statusLabel_->setText(QStringLiteral("正在载入..."));
    updateControls();
    backend_.navigate(normalizedUrl, generation_);
}

void BrowserPage::showClearDataConfirmation() {
    if (clearDataDialog_ == nullptr) {
        clearDataDialog_ = new QDialog(this);
        clearDataDialog_->setObjectName(QStringLiteral("browserClearDataDialog"));
        clearDataDialog_->setWindowTitle(QStringLiteral("清除网页数据"));
        auto* layout = new QVBoxLayout(clearDataDialog_);
        auto* explanation = new QLabel(
            QStringLiteral("将清除 MediaHub 内置网页的 Cookie、缓存和网站存储，"
                           "并退出网页账号；系统 Edge 不受影响。"),
            clearDataDialog_);
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
    rejectUnansweredSensitiveRequests();
    ++generation_;
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
