#pragma once

#include <QDialog>
#include <QStringList>

#include "browser_startup_settings.h"

class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace mediahub::gui {

// 网页启动设置窗口只编辑宿主配置，不读取 WebView2 Profile 或网页内容。
class BrowserStartupSettingsDialog final : public QDialog {
    Q_OBJECT

 public:
    explicit BrowserStartupSettingsDialog(BrowserStartupSettingsStore& store,
                                          QWidget* parent = nullptr);

    // 调用线程：GUI 主线程。地址来自当前标签模型，保存时仍会再次规范化。
    void setCurrentTabUrls(const QStringList& urls, int currentIndex);
    // 调用线程：GUI 主线程。每次显示前重新读取持久化设置。
    void reload();

 signals:
    void settingsSaved();

 private:
    void updateActions();
    void addUrl(const QString& url);
    void addCurrentTab();
    void addAllTabs();
    void removeSelected();
    void moveSelected(int offset);
    void saveSettings();

    BrowserStartupSettingsStore& store_;
    QStringList currentTabUrls_;
    int currentTabIndex_{0};
    QLineEdit* homeUrlEdit_{nullptr};
    QComboBox* startupModeCombo_{nullptr};
    QSpinBox* maximumTabCountSpin_{nullptr};
    QListWidget* startupUrlsList_{nullptr};
    QLineEdit* startupUrlEdit_{nullptr};
    QPushButton* removeButton_{nullptr};
    QPushButton* moveUpButton_{nullptr};
    QPushButton* moveDownButton_{nullptr};
};

}  // namespace mediahub::gui
