#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLabel;
class QListWidget;
class QPushButton;

namespace mediahub::gui {

// 选择或删除直播清单历史；对话框本身不发起任何网络请求。
class LiveUrlHistoryDialog final : public QDialog {
 public:
  explicit LiveUrlHistoryDialog(QStringList historyUrls,
                                QWidget* parent = nullptr);

  [[nodiscard]] const QStringList& historyUrls() const noexcept;
  [[nodiscard]] const QString& selectedUrl() const noexcept;

 private:
  void useCurrentUrl();
  void deleteCurrentUrl();
  void updateControls();

  QStringList historyUrls_;
  QString selectedUrl_;
  QListWidget* historyList_{nullptr};
  QLabel* emptyLabel_{nullptr};
  QPushButton* useButton_{nullptr};
  QPushButton* deleteButton_{nullptr};
};

}  // namespace mediahub::gui
