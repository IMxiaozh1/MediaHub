#pragma once

#include <QDialog>

#include "browser_permission_store.h"
#include "browser_types.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace mediahub::gui {

// 显示来源感知的网页权限请求，关闭窗口始终按拒绝处理。
class BrowserPermissionDialog final : public QDialog {
    Q_OBJECT

 public:
    // 调用线程：GUI 主线程。
    explicit BrowserPermissionDialog(const QString& origin,
                                     BrowserPermissionKind kind,
                                     QWidget* parent = nullptr);

    [[nodiscard]] QString originText() const;
    [[nodiscard]] QString permissionText() const;

 signals:
    void decisionMade(BrowserPermissionDecision decision);

 public slots:
    // 调用线程：GUI 主线程。窗口关闭与拒绝按钮共用拒绝语义。
    void reject() override;

 private:
    void finish(BrowserPermissionDecision decision);
    [[nodiscard]] static QString permissionName(BrowserPermissionKind kind);

    QLabel* originLabel_{nullptr};
    QLabel* permissionLabel_{nullptr};
    QPushButton* allowOnceButton_{nullptr};
    QPushButton* rememberButton_{nullptr};
    bool isAnswered_{false};
};

// 管理宿主保存的网站权限，不直接访问 WebView2 Profile。
class BrowserPermissionManagementDialog final : public QDialog {
    Q_OBJECT

 public:
    // 调用线程：GUI 主线程。
    explicit BrowserPermissionManagementDialog(BrowserPermissionStore& store,
                                                QWidget* parent = nullptr);

    [[nodiscard]] int visibleEntryCount() const;
    [[nodiscard]] QString statusText() const;
    // 调用线程：GUI 主线程。每次显示前重新读取持久化权限。
    void reloadEntries();
    // 调用线程：GUI 主线程。空来源显示全部，否则只显示规范化来源。
    void setOriginFilter(const QString& origin);

 signals:
    void permissionsChanged();

 private:
    void applyFilter(const QString& text);
    void updateActions();
    void saveSelected();
    void removeSelected();

    BrowserPermissionStore& store_;
    QVector<BrowserPermissionEntry> entries_;
    QLineEdit* searchEdit_{nullptr};
    QTableWidget* table_{nullptr};
    QComboBox* stateCombo_{nullptr};
    QPushButton* saveButton_{nullptr};
    QPushButton* removeButton_{nullptr};
    QLabel* statusLabel_{nullptr};
    QString exactOriginFilter_;
};

}  // namespace mediahub::gui
