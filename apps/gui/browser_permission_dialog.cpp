#include "browser_permission_dialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTableWidgetItem>
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
    if (kind == BrowserPermissionKind::ScreenCapture) {
        auto* limitation = new QLabel(
            QStringLiteral("当前 WebView2 对屏幕捕获仅本次允许，不能为来源记住。"),
            this);
        limitation->setObjectName(
            QStringLiteral("browserPermissionLimitationLabel"));
        limitation->setWordWrap(true);
        layout->addWidget(limitation);
    }

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
    rememberButton_->setEnabled(
        canAllow && kind != BrowserPermissionKind::ScreenCapture);
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

BrowserPermissionManagementDialog::BrowserPermissionManagementDialog(
    BrowserPermissionStore& store, QWidget* parent)
    : QDialog(parent), store_(store) {
    setObjectName(QStringLiteral("browserPermissionManagementDialog"));
    setWindowTitle(QStringLiteral("网站权限管理"));
    resize(680, 470);

    auto* const layout = new QVBoxLayout(this);
    auto* const explanation = new QLabel(
        QStringLiteral("按网站来源和权限类型管理 MediaHub 保存的选择。"), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("browserPermissionSearchEdit"));
    searchEdit_->setPlaceholderText(QStringLiteral("搜索来源或权限类型"));
    layout->addWidget(searchEdit_);

    table_ = new QTableWidget(0, 3, this);
    table_->setObjectName(QStringLiteral("browserPermissionTable"));
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("来源"), QStringLiteral("权限"), QStringLiteral("状态")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1,
                                                     QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2,
                                                     QHeaderView::ResizeToContents);
    layout->addWidget(table_, 1);

    auto* const actions = new QHBoxLayout();
    stateCombo_ = new QComboBox(this);
    stateCombo_->setObjectName(QStringLiteral("browserPermissionStateCombo"));
    stateCombo_->addItem(
        BrowserPermissionStore::stateName(BrowserPermissionState::Ask),
        static_cast<int>(BrowserPermissionState::Ask));
    stateCombo_->addItem(
        BrowserPermissionStore::stateName(BrowserPermissionState::Allow),
        static_cast<int>(BrowserPermissionState::Allow));
    stateCombo_->addItem(
        BrowserPermissionStore::stateName(BrowserPermissionState::Block),
        static_cast<int>(BrowserPermissionState::Block));
    saveButton_ = new QPushButton(QStringLiteral("保存修改"), this);
    saveButton_->setObjectName(QStringLiteral("browserPermissionSaveButton"));
    removeButton_ = new QPushButton(QStringLiteral("删除记录"), this);
    removeButton_->setObjectName(QStringLiteral("browserPermissionRemoveButton"));
    auto* const closeButton = new QPushButton(QStringLiteral("关闭"), this);
    closeButton->setObjectName(QStringLiteral("browserPermissionCloseButton"));
    actions->addWidget(stateCombo_);
    actions->addWidget(saveButton_);
    actions->addWidget(removeButton_);
    actions->addStretch(1);
    actions->addWidget(closeButton);
    layout->addLayout(actions);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("browserPermissionStatusLabel"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    connect(searchEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) {
                exactOriginFilter_.clear();
                applyFilter(text);
            });
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &BrowserPermissionManagementDialog::updateActions);
    connect(saveButton_, &QPushButton::clicked, this,
            &BrowserPermissionManagementDialog::saveSelected);
    connect(removeButton_, &QPushButton::clicked, this,
            &BrowserPermissionManagementDialog::removeSelected);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    reloadEntries();
}

int BrowserPermissionManagementDialog::visibleEntryCount() const {
    int count = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (!table_->isRowHidden(row)) {
            ++count;
        }
    }
    return count;
}

QString BrowserPermissionManagementDialog::statusText() const {
    return statusLabel_->text();
}

void BrowserPermissionManagementDialog::reloadEntries() {
    entries_ = store_.entries();
    table_->setRowCount(entries_.size());
    for (int row = 0; row < entries_.size(); ++row) {
        const BrowserPermissionEntry& entry = entries_.at(row);
        auto* const originItem = new QTableWidgetItem(entry.origin);
        originItem->setData(Qt::UserRole, row);
        table_->setItem(row, 0, originItem);
        table_->setItem(row, 1, new QTableWidgetItem(
                                    BrowserPermissionStore::permissionName(
                                        entry.kind)));
        table_->setItem(row, 2,
                        new QTableWidgetItem(
                            BrowserPermissionStore::stateName(entry.state)));
    }
    applyFilter(searchEdit_->text());
    updateActions();
}

void BrowserPermissionManagementDialog::setOriginFilter(
    const QString& origin) {
    exactOriginFilter_ = BrowserPermissionStore::normalizeOrigin(origin);
    const QSignalBlocker blocker(searchEdit_);
    searchEdit_->setText(exactOriginFilter_);
    applyFilter(exactOriginFilter_);
}

void BrowserPermissionManagementDialog::applyFilter(const QString& text) {
    const QString query = text.trimmed();
    for (int row = 0; row < table_->rowCount(); ++row) {
        bool matches = query.isEmpty();
        if (!exactOriginFilter_.isEmpty()) {
            const QTableWidgetItem* const originItem = table_->item(row, 0);
            matches = originItem != nullptr &&
                      originItem->text() == exactOriginFilter_;
        } else {
            for (int column = 0;
                 !matches && column < table_->columnCount(); ++column) {
                const QTableWidgetItem* const item = table_->item(row, column);
                matches = item != nullptr && item->text().contains(
                                               query, Qt::CaseInsensitive);
            }
        }
        table_->setRowHidden(row, !matches);
    }
    updateActions();
}

void BrowserPermissionManagementDialog::updateActions() {
    const int row = table_->currentRow();
    const bool hasSelection = row >= 0 && !table_->isRowHidden(row) &&
                              table_->item(row, 0) != nullptr;
    saveButton_->setEnabled(hasSelection);
    removeButton_->setEnabled(hasSelection);
    stateCombo_->setEnabled(hasSelection);
    if (!hasSelection) {
        return;
    }
    const int sourceIndex = table_->item(row, 0)->data(Qt::UserRole).toInt();
    if (sourceIndex < 0 || sourceIndex >= entries_.size()) {
        return;
    }
    const BrowserPermissionEntry& entry = entries_.at(sourceIndex);
    stateCombo_->setCurrentIndex(
        stateCombo_->findData(static_cast<int>(entry.state)));
    auto* const model = qobject_cast<QStandardItemModel*>(stateCombo_->model());
    if (model != nullptr) {
        QStandardItem* const allowItem = model->item(
            stateCombo_->findData(static_cast<int>(BrowserPermissionState::Allow)));
        if (allowItem != nullptr) {
            allowItem->setEnabled(entry.kind !=
                                  BrowserPermissionKind::ScreenCapture);
        }
    }
}

void BrowserPermissionManagementDialog::saveSelected() {
    const int row = table_->currentRow();
    if (row < 0 || table_->item(row, 0) == nullptr) {
        return;
    }
    const int sourceIndex = table_->item(row, 0)->data(Qt::UserRole).toInt();
    if (sourceIndex < 0 || sourceIndex >= entries_.size()) {
        return;
    }
    const BrowserPermissionEntry entry = entries_.at(sourceIndex);
    const auto state = static_cast<BrowserPermissionState>(
        stateCombo_->currentData().toInt());
    if (!store_.set(entry.origin, entry.kind, state)) {
        statusLabel_->setText(QStringLiteral("保存失败，原有权限设置未改变。"));
        return;
    }
    statusLabel_->setText(QStringLiteral("权限设置已更新。"));
    emit permissionsChanged();
    reloadEntries();
}

void BrowserPermissionManagementDialog::removeSelected() {
    const int row = table_->currentRow();
    if (row < 0 || table_->item(row, 0) == nullptr) {
        return;
    }
    const int sourceIndex = table_->item(row, 0)->data(Qt::UserRole).toInt();
    if (sourceIndex < 0 || sourceIndex >= entries_.size()) {
        return;
    }
    const BrowserPermissionEntry entry = entries_.at(sourceIndex);
    if (!store_.remove(entry.origin, entry.kind)) {
        statusLabel_->setText(QStringLiteral("删除失败，原有权限设置未改变。"));
        return;
    }
    statusLabel_->setText(QStringLiteral("权限记录已删除。"));
    emit permissionsChanged();
    reloadEntries();
}

}  // namespace mediahub::gui
