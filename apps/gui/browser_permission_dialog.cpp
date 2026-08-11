#include "browser_permission_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace mediahub::gui {

BrowserPermissionDialog::BrowserPermissionDialog(const QString& origin,
                                                 const BrowserPermissionKind kind,
                                                 QWidget* const parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("browserPermissionDialog"));
    setWindowTitle(QStringLiteral("网页权限请求"));
    setModal(false);

    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        QStringLiteral("以下网站正在请求访问本机能力，请确认是否允许。"), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    originLabel_ = new QLabel(origin, this);
    originLabel_->setObjectName(QStringLiteral("browserPermissionOriginLabel"));
    originLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    permissionLabel_ = new QLabel(permissionName(kind), this);
    permissionLabel_->setObjectName(QStringLiteral("browserPermissionKindLabel"));
    layout->addWidget(new QLabel(QStringLiteral("来源："), this));
    layout->addWidget(originLabel_);
    layout->addWidget(new QLabel(QStringLiteral("权限："), this));
    layout->addWidget(permissionLabel_);

    auto* buttons = new QHBoxLayout();
    auto* denyButton = new QPushButton(QStringLiteral("拒绝"), this);
    denyButton->setObjectName(QStringLiteral("browserPermissionDenyButton"));
    allowOnceButton_ = new QPushButton(QStringLiteral("仅本次允许"), this);
    allowOnceButton_->setObjectName(
        QStringLiteral("browserPermissionAllowOnceButton"));
    rememberButton_ = new QPushButton(QStringLiteral("对此来源记住允许"), this);
    rememberButton_->setObjectName(QStringLiteral("browserPermissionRememberButton"));
    const bool canAllow = kind != BrowserPermissionKind::Other;
    allowOnceButton_->setEnabled(canAllow);
    rememberButton_->setEnabled(canAllow);
    buttons->addStretch();
    buttons->addWidget(denyButton);
    buttons->addWidget(allowOnceButton_);
    buttons->addWidget(rememberButton_);
    layout->addLayout(buttons);

    connect(denyButton, &QPushButton::clicked, this,
            [this] { finish(BrowserPermissionDecision::Deny); });
    connect(allowOnceButton_, &QPushButton::clicked, this,
            [this] { finish(BrowserPermissionDecision::AllowOnce); });
    connect(rememberButton_, &QPushButton::clicked, this, [this] {
        finish(BrowserPermissionDecision::RememberForOrigin);
    });
}

QString BrowserPermissionDialog::originText() const {
    return originLabel_->text();
}

QString BrowserPermissionDialog::permissionText() const {
    return permissionLabel_->text();
}

void BrowserPermissionDialog::reject() {
    finish(BrowserPermissionDecision::Deny);
}

void BrowserPermissionDialog::finish(const BrowserPermissionDecision decision) {
    if (isAnswered_) {
        return;
    }
    isAnswered_ = true;
    emit decisionMade(decision);
    QDialog::done(decision == BrowserPermissionDecision::Deny ? Rejected : Accepted);
}

QString BrowserPermissionDialog::permissionName(const BrowserPermissionKind kind) {
    switch (kind) {
    case BrowserPermissionKind::Camera:
        return QStringLiteral("摄像头");
    case BrowserPermissionKind::Microphone:
        return QStringLiteral("麦克风");
    case BrowserPermissionKind::Geolocation:
        return QStringLiteral("位置信息");
    case BrowserPermissionKind::Notifications:
        return QStringLiteral("通知");
    case BrowserPermissionKind::ScreenCapture:
        return QStringLiteral("屏幕捕获");
    case BrowserPermissionKind::ClipboardRead:
        return QStringLiteral("读取剪贴板");
    case BrowserPermissionKind::Other:
        return QStringLiteral("未知权限（不可允许）");
    }
    return QStringLiteral("未知权限（不可允许）");
}

}  // namespace mediahub::gui
