#include "browser_page.h"

#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QStackedLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <utility>

#include "browser_backend.h"
#include "browser_navigation_policy.h"

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
    backend_.setEventListener(nullptr);
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
    isWebFullScreen_ = isFullScreen;
    emit fullScreenChanged(isFullScreen);
}

void BrowserPage::onPermissionRequested(std::uint64_t requestId, const QString&,
                                        BrowserPermissionKind) {
    backend_.answerPermission(requestId, BrowserPermissionDecision::Deny);
}

void BrowserPage::onExternalProtocolRequested(std::uint64_t requestId,
                                              const QString&) {
    backend_.answerExternalProtocol(requestId, false);
}

void BrowserPage::onCertificateErrorRequested(std::uint64_t requestId,
                                              const QString&, const QString&) {
    backend_.answerCertificateError(requestId,
                                    BrowserCertificateDecision::ReturnToSafety);
}

void BrowserPage::onDownloadRequested(std::uint64_t requestId, const QString&,
                                      const QString&, std::int64_t) {
    backend_.cancelDownload(requestId);
}

void BrowserPage::onDownloadUpdated(std::uint64_t, BrowserDownloadState,
                                    std::int64_t, std::int64_t) {}

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
    updateBackendBounds();
}

void BrowserPage::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);

    auto* toolbar = new QFrame(this);
    toolbar->setObjectName(QStringLiteral("browserToolbar"));
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(10, 8, 10, 8);
    toolbarLayout->setSpacing(6);

    backButton_ = createToolButton(QStringLiteral("browserBackButton"),
                                   QStringLiteral("后退"), toolbar);
    forwardButton_ = createToolButton(QStringLiteral("browserForwardButton"),
                                      QStringLiteral("前进"), toolbar);
    reloadButton_ = createToolButton(QStringLiteral("browserReloadButton"),
                                     QStringLiteral("刷新"), toolbar);
    homeButton_ = createToolButton(QStringLiteral("browserHomeButton"),
                                   QStringLiteral("主页"), toolbar);
    addressEdit_ = new QLineEdit(toolbar);
    addressEdit_->setObjectName(QStringLiteral("browserAddressEdit"));
    addressEdit_->setPlaceholderText(QStringLiteral("输入网站地址"));
    addressEdit_->setMinimumWidth(180);
    goButton_ = new QPushButton(QStringLiteral("访问"), toolbar);
    goButton_->setObjectName(QStringLiteral("browserGoButton"));
    clearDataButton_ = new QPushButton(QStringLiteral("清除网页数据"), toolbar);
    clearDataButton_->setObjectName(QStringLiteral("browserClearDataButton"));

    toolbarLayout->addWidget(backButton_);
    toolbarLayout->addWidget(forwardButton_);
    toolbarLayout->addWidget(reloadButton_);
    toolbarLayout->addWidget(homeButton_);
    toolbarLayout->addWidget(addressEdit_, 1);
    toolbarLayout->addWidget(goButton_);
    toolbarLayout->addWidget(clearDataButton_);

    auto* informationRow = new QWidget(this);
    auto* informationLayout = new QHBoxLayout(informationRow);
    informationLayout->setContentsMargins(10, 0, 10, 0);
    titleLabel_ = new QLabel(QStringLiteral("网页"), informationRow);
    titleLabel_->setObjectName(QStringLiteral("browserTitleLabel"));
    statusLabel_ = new QLabel(QStringLiteral("网页组件尚未初始化"), informationRow);
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

    rootLayout->addWidget(toolbar);
    rootLayout->addWidget(informationRow);
    rootLayout->addWidget(content, 1);

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
