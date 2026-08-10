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
namespace {

const QString& dialogStyleSheet() {
  static const QString styleSheet = QStringLiteral(R"(
      QDialog#liveUrlHistoryDialog {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #0b0e10, stop:0.58 #12171a,
                                      stop:1 #21180f);
          color: #edf1f2;
          font-family: "Microsoft YaHei UI";
      }
      QLabel#liveUrlHistoryIntroduction {
          background: #201a14;
          border: 1px solid #624725;
          border-left: 4px solid #f0a94a;
          border-radius: 8px;
          color: #ead8bd;
          font-size: 13px;
          font-weight: 600;
          padding: 11px 13px;
      }
      QListWidget#liveUrlHistoryList {
          alternate-background-color: #151a1d;
          background: #0e1214;
          border: 1px solid #394247;
          border-radius: 10px;
          color: #dce3e6;
          font-family: "Cascadia Mono";
          font-size: 12px;
          outline: none;
          padding: 5px;
      }
      QListWidget#liveUrlHistoryList::item {
          border-bottom: 1px solid #252d31;
          border-radius: 5px;
          min-height: 30px;
          padding: 7px 10px;
      }
      QListWidget#liveUrlHistoryList::item:hover {
          background: #242b2f;
          color: #ffffff;
      }
      QListWidget#liveUrlHistoryList::item:selected {
          background: #8d6029;
          border: 1px solid #d99a49;
          color: #fff8ec;
          font-weight: 700;
      }
      QLabel#liveUrlHistoryEmptyLabel {
          background: #111619;
          border: 1px dashed #4d585d;
          border-radius: 10px;
          color: #8d999e;
          font-size: 14px;
          padding: 30px;
      }
      QPushButton {
          background: #252b2e;
          border: 2px solid #414b50;
          border-radius: 8px;
          color: #e4e9eb;
          font-size: 13px;
          font-weight: 700;
          min-height: 42px;
          min-width: 112px;
          padding: 0 16px;
      }
      QPushButton:hover:enabled {
          background: #343c40;
          border-color: #647177;
      }
      QPushButton#liveUrlHistoryUseButton {
          background: #f0a94a;
          border-color: #ffd18d;
          color: #211305;
          font-size: 14px;
          min-width: 142px;
      }
      QPushButton#liveUrlHistoryUseButton:hover:enabled {
          background: #ffc36f;
          border-color: #ffe1ad;
      }
      QPushButton#liveUrlHistoryDeleteButton {
          background: #3a2020;
          border-color: #824444;
          color: #ffb7b2;
      }
      QPushButton#liveUrlHistoryDeleteButton:hover:enabled {
          background: #5a2928;
          border-color: #d06a65;
          color: #ffe1de;
      }
      QPushButton:disabled {
          background: #1b2022;
          border-color: #2b3235;
          color: #657075;
      }
      QScrollBar:vertical {
          background: #0c1012;
          border: none;
          margin: 5px 3px;
          width: 10px;
      }
      QScrollBar::handle:vertical {
          background: #766047;
          border-radius: 4px;
          min-height: 32px;
      }
      QScrollBar::handle:vertical:hover {
          background: #d29448;
      }
      QScrollBar::add-line:vertical,
      QScrollBar::sub-line:vertical {
          height: 0;
      }
  )");
  return styleSheet;
}

}  // namespace

LiveUrlHistoryDialog::LiveUrlHistoryDialog(QStringList historyUrls,
                                           QWidget* const parent)
    : QDialog(parent), historyUrls_(std::move(historyUrls)) {
  setObjectName(QStringLiteral("liveUrlHistoryDialog"));
  setWindowTitle(QStringLiteral("历史直播源"));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setModal(true);
  resize(620, 360);
  setMinimumSize(440, 280);
  setStyleSheet(dialogStyleSheet());

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
  historyList_->setAlternatingRowColors(true);
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
  useButton_->setMinimumSize(142, 46);
  deleteButton_ = new QPushButton(QStringLiteral("删除所选"), this);
  deleteButton_->setObjectName(QStringLiteral("liveUrlHistoryDeleteButton"));
  auto* const cancelButton = new QPushButton(QStringLiteral("取消"), this);
  cancelButton->setObjectName(QStringLiteral("liveUrlHistoryCancelButton"));
  for (QPushButton* const button : {deleteButton_, cancelButton, useButton_}) {
    button->setCursor(Qt::PointingHandCursor);
  }

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
