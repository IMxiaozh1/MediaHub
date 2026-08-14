#pragma once

#include <QDialog>
#include <QString>

#include "browser_tab_group_model.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace mediahub::gui {

// 管理标签分组的界面元数据，不直接操作标签或浏览器 Controller。
class BrowserTabGroupDialog final : public QDialog {
    Q_OBJECT

 public:
    explicit BrowserTabGroupDialog(BrowserTabGroupModel& model,
                                   QWidget* parent = nullptr);

    // 调用线程：GUI 主线程；外部恢复会话后可用它刷新当前模型快照。
    void reload();

 signals:
    void groupsChanged();
    void groupRemoved(const QString& groupId);

 private:
    [[nodiscard]] QString selectedGroupId() const;
    [[nodiscard]] QString selectedColor() const;
    void refreshList(const QString& selectedId = {});
    void updateEditor();
    void createGroup();
    void renameSelected();
    void recolorSelected();
    void toggleSelectedCollapsed();
    void removeSelected();
    void showStatus(const QString& text, bool isError = false);

    BrowserTabGroupModel& model_;
    QListWidget* list_{nullptr};
    QLineEdit* nameEdit_{nullptr};
    QComboBox* colorCombo_{nullptr};
    QPushButton* renameButton_{nullptr};
    QPushButton* recolorButton_{nullptr};
    QPushButton* toggleCollapsedButton_{nullptr};
    QPushButton* removeButton_{nullptr};
    QLabel* statusLabel_{nullptr};
};

}  // namespace mediahub::gui
