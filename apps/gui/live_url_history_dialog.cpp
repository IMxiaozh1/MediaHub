#include "live_url_history_dialog.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace mediahub::gui {

LiveUrlHistoryDialog::LiveUrlHistoryDialog(QStringList historyUrls,
                                           QWidget* const parent)
    : QDialog(parent), historyUrls_(std::move(historyUrls)) {
  setObjectName(QStringLiteral("liveUrlHistoryDialog"));
  setWindowTitle(QStringLiteral("历史直播源"));
  setModal(true);
  resize(620, 360);
  setMinimumSize(440, 280);

  auto* const layout = new QVBoxLayout(this);
  layout->setContentsMargins(18, 18, 18, 16);
  layout->setSpacing(12);

  auto* const introduction = new QLabel(
      QStringLiteral("选择一个地址回填到直播清单编辑框；回填后不会自动载入。"),
      this);
  introduction->setObjectName(QStringLiteral("liveUrlHistoryIntroduction"));
  introduction->setWordWrap(true);
  layout->addWidget(introduction);

  historyList_ = new QListWidget(this);
  historyList_->setObjectName(QStringLiteral("liveUrlHistoryList"));
  historyList_->setAccessibleName(QStringLiteral("历史直播源列表"));
  historyList_->setSelectionMode(QAbstractItemView::SingleSelection);
  for (const QString& url : std::as_const(historyUrls_)) {
    auto* const item = new QListWidgetItem(url, historyList_);
    item->setToolTip(url);
  }
  layout->addWidget(historyList_, 1);

  emptyLabel_ = new QLabel(QStringLiteral("暂无历史直播源"), this);
  emptyLabel_->setObjectName(QStringLiteral("liveUrlHistoryEmptyLabel"));
  emptyLabel_->setAlignment(Qt::AlignCenter);
  layout->addWidget(emptyLabel_);

  useButton_ = new QPushButton(QStringLiteral("使用所选"), this);
  useButton_->setObjectName(QStringLiteral("liveUrlHistoryUseButton"));
  useButton_->setDefault(true);
  deleteButton_ = new QPushButton(QStringLiteral("删除所选"), this);
  deleteButton_->setObjectName(QStringLiteral("liveUrlHistoryDeleteButton"));
  auto* const cancelButton = new QPushButton(QStringLiteral("取消"), this);
  cancelButton->setObjectName(QStringLiteral("liveUrlHistoryCancelButton"));

  auto* const buttonRow = new QHBoxLayout();
  buttonRow->addWidget(deleteButton_);
  buttonRow->addStretch(1);
  buttonRow->addWidget(cancelButton);
  buttonRow->addWidget(useButton_);
  layout->addLayout(buttonRow);

  connect(historyList_, &QListWidget::currentRowChanged, this,
          [this] { updateControls(); });
  connect(historyList_, &QListWidget::itemDoubleClicked, this,
          [this] { useCurrentUrl(); });
  connect(useButton_, &QPushButton::clicked, this,
          [this] { useCurrentUrl(); });
  connect(deleteButton_, &QPushButton::clicked, this,
          [this] { deleteCurrentUrl(); });
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

  if (historyList_->count() > 0) {
    historyList_->setCurrentRow(0);
  }
  updateControls();
}

const QStringList& LiveUrlHistoryDialog::historyUrls() const noexcept {
  return historyUrls_;
}

const QString& LiveUrlHistoryDialog::selectedUrl() const noexcept {
  return selectedUrl_;
}

void LiveUrlHistoryDialog::useCurrentUrl() {
  const int row = historyList_->currentRow();
  if (row < 0 || row >= historyUrls_.size()) {
    return;
  }
  selectedUrl_ = historyUrls_.at(row);
  accept();
}

void LiveUrlHistoryDialog::deleteCurrentUrl() {
  const int row = historyList_->currentRow();
  if (row < 0 || row >= historyUrls_.size()) {
    return;
  }
  historyUrls_.removeAt(row);
  delete historyList_->takeItem(row);
  if (historyList_->count() > 0) {
    historyList_->setCurrentRow(std::min(row, historyList_->count() - 1));
  }
  updateControls();
}

void LiveUrlHistoryDialog::updateControls() {
  const bool hasSelection = historyList_->currentRow() >= 0;
  useButton_->setEnabled(hasSelection);
  deleteButton_->setEnabled(hasSelection);
  emptyLabel_->setVisible(historyList_->count() == 0);
  historyList_->setVisible(historyList_->count() != 0);
}

}  // namespace mediahub::gui
