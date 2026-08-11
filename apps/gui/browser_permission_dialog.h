#pragma once

#include <QDialog>

#include "browser_types.h"

class QLabel;
class QPushButton;

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

}  // namespace mediahub::gui
