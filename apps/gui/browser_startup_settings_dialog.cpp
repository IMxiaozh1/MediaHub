#include "browser_startup_settings_dialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QStyle>

#include <algorithm>

#include "browser_data_store.h"

namespace mediahub::gui {

BrowserStartupSettingsDialog::BrowserStartupSettingsDialog(
    BrowserStartupSettingsStore& store, QWidget* const parent)
    : QDialog(parent), store_(store) {
    setObjectName(QStringLiteral("browserStartupSettingsDialog"));
    setWindowTitle(QStringLiteral("网页主页与启动设置"));
    resize(620, 500);

    auto* const layout = new QVBoxLayout(this);
    auto* const explanation = new QLabel(
        QStringLiteral("主页按钮与应用启动行为相互独立。启动页地址不会保存查询参数或"
                       "网页片段；恢复会话地址保存在 Windows 加密文件中。"),
        this);
    explanation->setObjectName(
        QStringLiteral("browserStartupSettingsExplanation"));
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    layout->addWidget(new QLabel(QStringLiteral("主页地址"), this));
    homeUrlEdit_ = new QLineEdit(this);
    homeUrlEdit_->setObjectName(QStringLiteral("browserHomeUrlEdit"));
    homeUrlEdit_->setPlaceholderText(QStringLiteral("https://www.bing.com/"));
    layout->addWidget(homeUrlEdit_);

    layout->addWidget(new QLabel(QStringLiteral("启动行为"), this));
    startupModeCombo_ = new QComboBox(this);
    startupModeCombo_->setObjectName(QStringLiteral("browserStartupModeCombo"));
    startupModeCombo_->addItem(QStringLiteral("打开 Bing"), 0);
    startupModeCombo_->addItem(QStringLiteral("恢复上次网页会话"), 1);
    startupModeCombo_->addItem(QStringLiteral("打开下列启动页"), 2);
    layout->addWidget(startupModeCombo_);

    auto* const tabLimitLabel =
        new QLabel(QStringLiteral("最大网页标签数（5-100）"), this);
    tabLimitLabel->setObjectName(QStringLiteral("browserMaximumTabCountLabel"));
    layout->addWidget(tabLimitLabel);
    maximumTabCountSpin_ = new QSpinBox(this);
    maximumTabCountSpin_->setObjectName(
        QStringLiteral("browserMaximumTabCountSpin"));
    maximumTabCountSpin_->setRange(5, 100);
    maximumTabCountSpin_->setSingleStep(5);
    layout->addWidget(maximumTabCountSpin_);

    startupUrlsList_ = new QListWidget(this);
    startupUrlsList_->setObjectName(QStringLiteral("browserStartupUrlsList"));
    layout->addWidget(startupUrlsList_, 1);

    auto* const inputRow = new QHBoxLayout();
    startupUrlEdit_ = new QLineEdit(this);
    startupUrlEdit_->setObjectName(QStringLiteral("browserStartupUrlEdit"));
    startupUrlEdit_->setPlaceholderText(QStringLiteral("输入一个 HTTP(S) 地址"));
    auto* const addButton = new QPushButton(QStringLiteral("添加地址"), this);
    addButton->setObjectName(QStringLiteral("browserStartupAddButton"));
    inputRow->addWidget(startupUrlEdit_, 1);
    inputRow->addWidget(addButton);
    layout->addLayout(inputRow);

    auto* const actions = new QHBoxLayout();
    auto* const addCurrentButton =
        new QPushButton(QStringLiteral("添加当前标签"), this);
    addCurrentButton->setObjectName(
        QStringLiteral("browserStartupAddCurrentButton"));
    auto* const addAllButton =
        new QPushButton(QStringLiteral("添加全部标签"), this);
    addAllButton->setObjectName(QStringLiteral("browserStartupAddAllButton"));
    removeButton_ = new QPushButton(QStringLiteral("删除"), this);
    removeButton_->setObjectName(QStringLiteral("browserStartupRemoveButton"));
    moveUpButton_ = new QPushButton(QStringLiteral("上移"), this);
    moveUpButton_->setObjectName(QStringLiteral("browserStartupMoveUpButton"));
    moveDownButton_ = new QPushButton(QStringLiteral("下移"), this);
    moveDownButton_->setObjectName(QStringLiteral("browserStartupMoveDownButton"));
    actions->addWidget(addCurrentButton);
    actions->addWidget(addAllButton);
    actions->addStretch(1);
    actions->addWidget(moveUpButton_);
    actions->addWidget(moveDownButton_);
    actions->addWidget(removeButton_);
    layout->addLayout(actions);

    auto* const footer = new QHBoxLayout();
    auto* const cancelButton = new QPushButton(QStringLiteral("取消"), this);
    auto* const saveButton = new QPushButton(QStringLiteral("保存"), this);
    saveButton->setObjectName(QStringLiteral("browserStartupSaveButton"));
    footer->addStretch(1);
    footer->addWidget(cancelButton);
    footer->addWidget(saveButton);
    layout->addLayout(footer);

    connect(addButton, &QPushButton::clicked, this,
            [this] { addUrl(startupUrlEdit_->text()); });
    connect(startupUrlEdit_, &QLineEdit::returnPressed, this,
            [this] { addUrl(startupUrlEdit_->text()); });
    connect(addCurrentButton, &QPushButton::clicked, this,
            &BrowserStartupSettingsDialog::addCurrentTab);
    connect(addAllButton, &QPushButton::clicked, this,
            &BrowserStartupSettingsDialog::addAllTabs);
    connect(removeButton_, &QPushButton::clicked, this,
            &BrowserStartupSettingsDialog::removeSelected);
    connect(moveUpButton_, &QPushButton::clicked, this,
            [this] { moveSelected(-1); });
    connect(moveDownButton_, &QPushButton::clicked, this,
            [this] { moveSelected(1); });
    connect(startupUrlsList_, &QListWidget::currentRowChanged, this,
            [this] { updateActions(); });
    connect(startupModeCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { updateActions(); });
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::hide);
    connect(saveButton, &QPushButton::clicked, this,
            &BrowserStartupSettingsDialog::saveSettings);
    reload();
}

void BrowserStartupSettingsDialog::setCurrentTabUrls(const QStringList& urls,
                                                     const int currentIndex) {
    currentTabUrls_ = urls;
    currentTabIndex_ = currentTabUrls_.isEmpty()
                           ? 0
                           : std::clamp(
                                 currentIndex, 0,
                                 static_cast<int>(currentTabUrls_.size()) - 1);
    updateActions();
}

void BrowserStartupSettingsDialog::reload() {
    const BrowserStartupSettings settings = store_.load();
    homeUrlEdit_->setText(settings.homeUrl);
    startupModeCombo_->setCurrentIndex(static_cast<int>(settings.mode));
    maximumTabCountSpin_->setValue(settings.maximumTabCount);
    startupUrlsList_->clear();
    for (const QString& url : settings.startupUrls) {
        startupUrlsList_->addItem(url);
    }
    updateActions();
}

void BrowserStartupSettingsDialog::updateActions() {
    const int row = startupUrlsList_->currentRow();
    const bool hasSelection = row >= 0;
    removeButton_->setEnabled(hasSelection);
    moveUpButton_->setEnabled(row > 0);
    moveDownButton_->setEnabled(hasSelection &&
                                row + 1 < startupUrlsList_->count());
    startupUrlsList_->setEnabled(startupModeCombo_->currentIndex() == 2);
    const bool canEditPages = startupModeCombo_->currentIndex() == 2;
    startupUrlEdit_->setEnabled(canEditPages);
}

void BrowserStartupSettingsDialog::addUrl(const QString& value) {
    const QString url = normalizeStoredBrowserUrl(value);
    if (url.isEmpty()) {
        startupUrlEdit_->setFocus();
        return;
    }
    for (int index = 0; index < startupUrlsList_->count(); ++index) {
        if (startupUrlsList_->item(index)->text() == url) {
            startupUrlsList_->setCurrentRow(index);
            startupUrlEdit_->clear();
            return;
        }
    }
    if (startupUrlsList_->count() < 20) {
        startupUrlsList_->addItem(url);
        startupUrlsList_->setCurrentRow(startupUrlsList_->count() - 1);
    }
    startupUrlEdit_->clear();
}

void BrowserStartupSettingsDialog::addCurrentTab() {
    if (!currentTabUrls_.isEmpty()) {
        addUrl(currentTabUrls_.at(currentTabIndex_));
    }
}

void BrowserStartupSettingsDialog::addAllTabs() {
    for (const QString& url : currentTabUrls_) {
        addUrl(url);
    }
}

void BrowserStartupSettingsDialog::removeSelected() {
    delete startupUrlsList_->takeItem(startupUrlsList_->currentRow());
    updateActions();
}

void BrowserStartupSettingsDialog::moveSelected(const int offset) {
    const int row = startupUrlsList_->currentRow();
    const int target = row + offset;
    if (row < 0 || target < 0 || target >= startupUrlsList_->count()) {
        return;
    }
    QListWidgetItem* const item = startupUrlsList_->takeItem(row);
    startupUrlsList_->insertItem(target, item);
    startupUrlsList_->setCurrentRow(target);
}

void BrowserStartupSettingsDialog::saveSettings() {
    const QString homeUrl = normalizeStoredBrowserUrl(homeUrlEdit_->text());
    if (homeUrl.isEmpty()) {
        homeUrlEdit_->setFocus();
        return;
    }
    BrowserStartupSettings settings;
    settings.homeUrl = homeUrl;
    settings.mode = static_cast<BrowserStartupMode>(
        startupModeCombo_->currentData().toInt());
    settings.maximumTabCount = maximumTabCountSpin_->value();
    for (int index = 0; index < startupUrlsList_->count(); ++index) {
        settings.startupUrls.append(startupUrlsList_->item(index)->text());
    }
    store_.save(settings);
    emit settingsSaved();
    hide();
}

}  // namespace mediahub::gui
