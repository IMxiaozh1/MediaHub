#pragma once

#include <QDialog>
#include <QVector>

#include "live_source_memo.h"

class QCloseEvent;
class QLabel;
class QPushButton;
class QTableWidget;

namespace mediahub::gui {

// 编辑直播源备忘。保存只更新本机配置，不会发起网络请求或播放媒体。
class LiveSourceMemoDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit LiveSourceMemoDialog(QVector<LiveSourceMemo> memos,
                                QWidget* parent = nullptr);

 signals:
  void memosSaved(const QVector<LiveSourceMemo>& memos);

 protected:
  // 调用线程：GUI 主线程。右上角关闭与“返回”共用未保存确认流程。
  void closeEvent(QCloseEvent* event) override;

 private:
  void appendRow(const LiveSourceMemo& memo);
  void addEmptyRow();
  void deleteCurrentRow();
  void markDirty();
  void updateControls();
  void commitActiveEditor();
  void requestSave();
  void requestReturn();
  [[nodiscard]] bool saveMemos(bool asksForConfirmation);
  [[nodiscard]] bool confirmClose();
  [[nodiscard]] bool collectMemos(QVector<LiveSourceMemo>* memos);

  QVector<LiveSourceMemo> savedMemos_;
  QTableWidget* table_{nullptr};
  QLabel* countBadge_{nullptr};
  QLabel* statusLabel_{nullptr};
  QPushButton* deleteButton_{nullptr};
  QPushButton* saveButton_{nullptr};
  bool isInitializing_{true};
  bool isDirty_{false};
  bool allowsClose_{false};
};

}  // namespace mediahub::gui
