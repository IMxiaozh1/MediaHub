#pragma once

#include <QDialog>
#include <QVector>

#include "live_source_memo.h"
#include "theme_settings.h"

class QCloseEvent;
class QLabel;
class QPaintEvent;
class QPushButton;
class QTableWidget;

namespace mediahub::gui {

// 编辑直播源备忘。保存只更新本机配置，不会发起网络请求或播放媒体。
class LiveSourceMemoDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit LiveSourceMemoDialog(QVector<LiveSourceMemo> memos,
                                QWidget* parent = nullptr);

  // 更新窗口及其确认框使用的深浅模式和主题色。
  void setThemeSettings(const ThemeSettings& settings);

 signals:
  void memosSaved(const QVector<LiveSourceMemo>& memos);

 protected:
  // 调用线程：GUI 主线程。右上角关闭与“返回”共用未保存确认流程。
  void closeEvent(QCloseEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  void appendRow(const LiveSourceMemo& memo);
  void addEmptyRow();
  void deleteCurrentRow();
  void markDirty();
  void updateControls();
  void commitActiveEditor();
  void requestSave();
  void requestReturn();
  void applyTheme();
  [[nodiscard]] bool saveMemos(bool asksForConfirmation);
  [[nodiscard]] bool confirmClose();
  [[nodiscard]] bool collectMemos(QVector<LiveSourceMemo>* memos);

  QVector<LiveSourceMemo> savedMemos_;
  QTableWidget* table_{nullptr};
  QLabel* countBadge_{nullptr};
  QLabel* statusLabel_{nullptr};
  QPushButton* deleteButton_{nullptr};
  QPushButton* saveButton_{nullptr};
  ThemeSettings themeSettings_;
  bool isInitializing_{true};
  bool isDirty_{false};
  bool allowsClose_{false};
};

}  // namespace mediahub::gui
