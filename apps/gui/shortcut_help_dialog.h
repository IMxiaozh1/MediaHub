#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "theme_settings.h"

class QPushButton;
class QPaintEvent;
class QTableWidget;

namespace mediahub::gui {

struct ShortcutHelpEntry {
  QString shortcut;
  QString operation;
};

// 展示程序当前支持的键盘操作，内容与 MainWindow 的快捷键契约同步维护。
class ShortcutHelpDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit ShortcutHelpDialog(QWidget* parent = nullptr);

  // 更新窗口及快捷键表格使用的深浅模式和主题色。
  void setThemeSettings(const ThemeSettings& settings);
  [[nodiscard]] static const QVector<ShortcutHelpEntry>& entries();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  void applyTheme();

  ThemeSettings themeSettings_;
  QTableWidget* table_{nullptr};
  QPushButton* okButton_{nullptr};
};

}  // namespace mediahub::gui
