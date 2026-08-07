#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

namespace mediahub::gui {

struct ShortcutHelpEntry {
  QString shortcut;
  QString operation;
};

// 展示程序当前支持的键盘操作，内容与 MainWindow 的快捷键契约同步维护。
class ShortcutHelpDialog final : public QDialog {
 public:
  explicit ShortcutHelpDialog(QWidget* parent = nullptr);

  [[nodiscard]] static const QVector<ShortcutHelpEntry>& entries();
};

}  // namespace mediahub::gui
