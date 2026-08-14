#include "browser_tab_group_dialog.h"

#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace mediahub::gui {
namespace {

constexpr int kColorSwatchSize = 14;

struct ColorPreset {
    const char* name;
    const char* value;
};

constexpr ColorPreset kColorPresets[] = {
    {"松石绿", "#256f62"}, {"琥珀橙", "#d97745"},
    {"海湾蓝", "#3f7cac"}, {"石榴红", "#b94b55"},
    {"苔藓绿", "#6b8e4e"}, {"石墨灰", "#66727d"},
};

QIcon colorIcon(const QString& color) {
    QPixmap swatch(kColorSwatchSize, kColorSwatchSize);
    swatch.fill(QColor(color));
    return QIcon(swatch);
}

}  // namespace

BrowserTabGroupDialog::BrowserTabGroupDialog(BrowserTabGroupModel& model,
                                             QWidget* const parent)
    : QDialog(parent), model_(model) {
    setObjectName(QStringLiteral("browserTabGroupDialog"));
    setWindowTitle(QStringLiteral("管理标签分组"));
    resize(560, 430);

    auto* const layout = new QVBoxLayout(this);
    auto* const explanation = new QLabel(
        QStringLiteral("分组只整理标签栏显示，不会暂停或静音组内网页。"), this);
    explanation->setObjectName(QStringLiteral("browserTabGroupExplanation"));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("browserTabGroupList"));
    list_->setAlternatingRowColors(true);
    layout->addWidget(list_, 1);

    auto* const nameRow = new QHBoxLayout();
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setObjectName(QStringLiteral("browserTabGroupNameEdit"));
    nameEdit_->setPlaceholderText(QStringLiteral("分组名称"));
    nameEdit_->setMaxLength(64);
    auto* const createButton = new QPushButton(QStringLiteral("新建"), this);
    createButton->setObjectName(QStringLiteral("browserTabGroupCreateButton"));
    renameButton_ = new QPushButton(QStringLiteral("重命名"), this);
    renameButton_->setObjectName(QStringLiteral("browserTabGroupRenameButton"));
    nameRow->addWidget(nameEdit_, 1);
    nameRow->addWidget(createButton);
    nameRow->addWidget(renameButton_);
    layout->addLayout(nameRow);

    auto* const actionRow = new QHBoxLayout();
    colorCombo_ = new QComboBox(this);
    colorCombo_->setObjectName(QStringLiteral("browserTabGroupColorCombo"));
    for (const ColorPreset& preset : kColorPresets) {
        const QString color = QString::fromLatin1(preset.value);
        colorCombo_->addItem(colorIcon(color), QString::fromUtf8(preset.name),
                             color);
    }
    recolorButton_ = new QPushButton(QStringLiteral("更改颜色"), this);
    recolorButton_->setObjectName(
        QStringLiteral("browserTabGroupRecolorButton"));
    toggleCollapsedButton_ = new QPushButton(QStringLiteral("折叠分组"), this);
    toggleCollapsedButton_->setObjectName(
        QStringLiteral("browserTabGroupToggleCollapsedButton"));
    removeButton_ = new QPushButton(QStringLiteral("删除分组"), this);
    removeButton_->setObjectName(QStringLiteral("browserTabGroupRemoveButton"));
    actionRow->addWidget(colorCombo_, 1);
    actionRow->addWidget(recolorButton_);
    actionRow->addWidget(toggleCollapsedButton_);
    actionRow->addWidget(removeButton_);
    layout->addLayout(actionRow);

    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("browserTabGroupStatusLabel"));
    statusLabel_->setWordWrap(true);
    statusLabel_->hide();
    layout->addWidget(statusLabel_);

    auto* const footer = new QHBoxLayout();
    auto* const closeButton = new QPushButton(QStringLiteral("关闭"), this);
    closeButton->setObjectName(QStringLiteral("browserTabGroupCloseButton"));
    footer->addStretch(1);
    footer->addWidget(closeButton);
    layout->addLayout(footer);

    connect(list_, &QListWidget::currentRowChanged, this,
            [this] { updateEditor(); });
    connect(createButton, &QPushButton::clicked, this,
            &BrowserTabGroupDialog::createGroup);
    connect(nameEdit_, &QLineEdit::returnPressed, this,
            &BrowserTabGroupDialog::createGroup);
    connect(renameButton_, &QPushButton::clicked, this,
            &BrowserTabGroupDialog::renameSelected);
    connect(recolorButton_, &QPushButton::clicked, this,
            &BrowserTabGroupDialog::recolorSelected);
    connect(toggleCollapsedButton_, &QPushButton::clicked, this,
            &BrowserTabGroupDialog::toggleSelectedCollapsed);
    connect(removeButton_, &QPushButton::clicked, this,
            &BrowserTabGroupDialog::removeSelected);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);

    reload();
}

void BrowserTabGroupDialog::reload() {
    refreshList(selectedGroupId());
}

QString BrowserTabGroupDialog::selectedGroupId() const {
    const QListWidgetItem* const item = list_->currentItem();
    return item == nullptr ? QString{} : item->data(Qt::UserRole).toString();
}

QString BrowserTabGroupDialog::selectedColor() const {
    return colorCombo_->currentData().toString();
}

void BrowserTabGroupDialog::refreshList(const QString& selectedId) {
    list_->clear();
    int selectedRow = -1;
    for (const BrowserSessionGroup& group : model_.groups()) {
        const QString state = group.isCollapsed ? QStringLiteral("已折叠")
                                                 : QStringLiteral("已展开");
        auto* const item = new QListWidgetItem(
            colorIcon(group.color),
            QStringLiteral("%1  ·  %2").arg(group.name, state), list_);
        item->setData(Qt::UserRole, group.id);
        item->setToolTip(QStringLiteral("颜色 %1").arg(group.color));
        if (group.id == selectedId) {
            selectedRow = list_->count() - 1;
        }
    }
    if (selectedRow < 0 && list_->count() > 0) {
        selectedRow = 0;
    }
    list_->setCurrentRow(selectedRow);
    updateEditor();
}

void BrowserTabGroupDialog::updateEditor() {
    const BrowserSessionGroup* const group = model_.find(selectedGroupId());
    const bool hasSelection = group != nullptr;
    renameButton_->setEnabled(hasSelection);
    recolorButton_->setEnabled(hasSelection);
    toggleCollapsedButton_->setEnabled(hasSelection);
    removeButton_->setEnabled(hasSelection);
    if (!hasSelection) {
        toggleCollapsedButton_->setText(QStringLiteral("折叠分组"));
        return;
    }

    nameEdit_->setText(group->name);
    const int colorIndex = colorCombo_->findData(group->color);
    if (colorIndex >= 0) {
        colorCombo_->setCurrentIndex(colorIndex);
    }
    toggleCollapsedButton_->setText(
        group->isCollapsed ? QStringLiteral("展开分组")
                           : QStringLiteral("折叠分组"));
}

void BrowserTabGroupDialog::createGroup() {
    const std::optional<QString> id =
        model_.create(nameEdit_->text(), selectedColor());
    if (!id.has_value()) {
        showStatus(QStringLiteral("无法新建分组，请检查名称或分组数量。"), true);
        nameEdit_->setFocus();
        return;
    }
    refreshList(*id);
    showStatus(QStringLiteral("分组已新建。"));
    emit groupsChanged();
}

void BrowserTabGroupDialog::renameSelected() {
    const QString id = selectedGroupId();
    if (id.isEmpty() || !model_.rename(id, nameEdit_->text())) {
        showStatus(QStringLiteral("无法重命名分组，请输入有效名称。"), true);
        return;
    }
    refreshList(id);
    showStatus(QStringLiteral("分组名称已更新。"));
    emit groupsChanged();
}

void BrowserTabGroupDialog::recolorSelected() {
    const QString id = selectedGroupId();
    if (id.isEmpty() || !model_.setColor(id, selectedColor())) {
        showStatus(QStringLiteral("无法更新分组颜色。"), true);
        return;
    }
    refreshList(id);
    showStatus(QStringLiteral("分组颜色已更新。"));
    emit groupsChanged();
}

void BrowserTabGroupDialog::toggleSelectedCollapsed() {
    const QString id = selectedGroupId();
    const BrowserSessionGroup* const group = model_.find(id);
    if (group == nullptr || !model_.setCollapsed(id, !group->isCollapsed)) {
        showStatus(QStringLiteral("无法更新分组折叠状态。"), true);
        return;
    }
    refreshList(id);
    showStatus(QStringLiteral("分组显示状态已更新。"));
    emit groupsChanged();
}

void BrowserTabGroupDialog::removeSelected() {
    const QString id = selectedGroupId();
    if (id.isEmpty() || !model_.remove(id)) {
        showStatus(QStringLiteral("无法删除分组。"), true);
        return;
    }
    refreshList();
    showStatus(QStringLiteral("分组已删除，组内标签将回到未分组状态。"));
    emit groupRemoved(id);
    emit groupsChanged();
}

void BrowserTabGroupDialog::showStatus(const QString& text,
                                       const bool isError) {
    statusLabel_->setText(text);
    statusLabel_->setProperty("status", isError ? QStringLiteral("error")
                                                 : QStringLiteral("success"));
    statusLabel_->style()->unpolish(statusLabel_);
    statusLabel_->style()->polish(statusLabel_);
    statusLabel_->show();
}

}  // namespace mediahub::gui
