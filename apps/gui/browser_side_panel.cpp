#include "browser_side_panel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "browser_icon_provider.h"

namespace mediahub::gui {

BrowserSidePanel::BrowserSidePanel(QWidget* const parent) : QFrame(parent) {
    setObjectName(QStringLiteral("browserSidePanel"));
    setFrameShape(QFrame::NoFrame);
    setMinimumWidth(320);
    setMaximumWidth(420);
    setFixedWidth(380);

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* const titleBar = new QFrame(this);
    titleBar->setObjectName(QStringLiteral("browserSidePanelTitleBar"));
    auto* const titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(16, 8, 8, 8);
    titleLabel_ = new QLabel(titleBar);
    titleLabel_->setObjectName(QStringLiteral("browserSidePanelTitle"));
    closeButton_ = new QToolButton(titleBar);
    closeButton_->setObjectName(QStringLiteral("browserSidePanelCloseButton"));
    closeButton_->setIcon(BrowserIconProvider::icon(
        BrowserIcon::Close, QColor(QStringLiteral("#344454"))));
    closeButton_->setToolTip(QStringLiteral("关闭侧边面板"));
    closeButton_->setAccessibleName(QStringLiteral("关闭侧边面板"));
    closeButton_->setFixedSize(32, 32);
    titleLayout->addWidget(titleLabel_, 1);
    titleLayout->addWidget(closeButton_);
    layout->addWidget(titleBar);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("browserSidePanelStack"));
    layout->addWidget(stack_, 1);

    connect(closeButton_, &QToolButton::clicked, this,
            &BrowserSidePanel::closePanel);
    hide();
}

void BrowserSidePanel::addPage(const Page page, QWidget* const widget,
                               const QString& title) {
    if (widget == nullptr || pages_.contains(static_cast<int>(page))) {
        return;
    }
    widget->setWindowFlags(Qt::Widget);
    stack_->addWidget(widget);
    pages_.insert(static_cast<int>(page), PageEntry{widget, title});
}

void BrowserSidePanel::showPage(const Page page) {
    const auto found = pages_.constFind(static_cast<int>(page));
    if (found == pages_.cend()) {
        return;
    }
    currentPage_ = page;
    titleLabel_->setText(found->title);
    stack_->setCurrentWidget(found->widget);
    found->widget->show();
    show();
    activateParentLayout();
    emit pageChanged(page);
}

void BrowserSidePanel::closePanel() {
    if (isHidden()) {
        return;
    }
    hide();
    activateParentLayout();
    emit panelClosed();
}

void BrowserSidePanel::setCompact(const bool isCompact) {
    setFixedWidth(isCompact ? 320 : 380);
}

bool BrowserSidePanel::containsPage(const Page page) const noexcept {
    return pages_.contains(static_cast<int>(page));
}

BrowserSidePanel::Page BrowserSidePanel::currentPage() const noexcept {
    return currentPage_;
}

QWidget* BrowserSidePanel::pageWidget(const Page page) const noexcept {
    const auto found = pages_.constFind(static_cast<int>(page));
    return found == pages_.cend() ? nullptr : found->widget;
}

void BrowserSidePanel::activateParentLayout() {
    updateGeometry();
    if (parentWidget() != nullptr && parentWidget()->layout() != nullptr) {
        parentWidget()->layout()->activate();
    }
}

}  // namespace mediahub::gui
