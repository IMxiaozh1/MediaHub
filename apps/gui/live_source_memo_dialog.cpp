#include "live_source_memo_dialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <utility>

namespace mediahub::gui {
namespace {

const QString& dialogStyleSheet() {
  static const QString styleSheet = QStringLiteral(R"(
      QDialog#liveSourceMemoDialog {
          background: #121417;
          color: #e7e9ec;
          font-family: "Microsoft YaHei UI";
      }
      QFrame#liveSourceMemoHeader {
          background: transparent;
          border: none;
          border-bottom: 1px solid #2a2e34;
      }
      QLabel#liveSourceMemoEyebrow {
          color: #7f8790;
          font-size: 10px;
      }
      QLabel#liveSourceMemoTitle {
          color: #f1f3f5;
          font-family: "Segoe UI Semibold", "Microsoft YaHei UI";
          font-size: 22px;
          font-weight: 600;
      }
      QLabel#liveSourceMemoIntroduction {
          color: #89919a;
          font-size: 12px;
      }
      QLabel#liveSourceMemoNotice {
          background: transparent;
          border: none;
          color: #858d96;
          font-size: 11px;
          padding: 0;
      }
      QLabel#liveSourceMemoCountBadge {
          background: transparent;
          border: none;
          color: #89919a;
          font-size: 11px;
          padding: 0;
      }
      QTableWidget#liveSourceMemoTable {
          alternate-background-color: #15181c;
          background: #101216;
          border: 1px solid #2a2e34;
          border-radius: 5px;
          color: #d7dbe0;
          gridline-color: #22262c;
          outline: none;
          selection-background-color: #253b4e;
          selection-color: #ffffff;
      }
      QTableWidget#liveSourceMemoTable::item {
          border-bottom: 1px solid #22262c;
          padding: 8px 11px;
      }
      QTableWidget#liveSourceMemoTable::item:hover {
          background: #1d2228;
      }
      QHeaderView::section {
          background: #181b20;
          border: none;
          border-bottom: 1px solid #30353c;
          color: #9299a2;
          font-size: 12px;
          font-weight: 600;
          padding: 9px 11px;
      }
      QScrollBar:vertical {
          background: #101216;
          border: none;
          margin: 0;
          width: 8px;
      }
      QScrollBar::handle:vertical {
          background: #3b4047;
          border-radius: 3px;
          min-height: 28px;
      }
      QScrollBar::handle:vertical:hover {
          background: #59616a;
      }
      QScrollBar::add-line:vertical,
      QScrollBar::sub-line:vertical {
          height: 0;
      }
      QScrollBar::add-page:vertical,
      QScrollBar::sub-page:vertical {
          background: transparent;
      }
      QLabel#liveSourceMemoStatus {
          color: #858d96;
          font-size: 11px;
      }
      QPushButton {
          background: #20242a;
          border: 1px solid #3a4048;
          border-radius: 4px;
          color: #d7dbe0;
          font-size: 13px;
          font-weight: 600;
          min-height: 34px;
          max-height: 36px;
          min-width: 82px;
          padding: 0 14px;
      }
      QPushButton#liveSourceMemoAddButton {
          background: #202a34;
          border-color: #385c78;
          color: #9bc9ef;
      }
      QPushButton#liveSourceMemoAddButton:hover {
          background: #273746;
          border-color: #4f789b;
          color: #d9efff;
      }
      QPushButton#liveSourceMemoDeleteButton {
          background: #2b1d20;
          border-color: #5d3439;
          color: #e9979e;
      }
      QPushButton#liveSourceMemoDeleteButton:hover:enabled {
          background: #3a2428;
          border-color: #7a4249;
          color: #ffc1c6;
      }
      QPushButton#liveSourceMemoDeleteButton:disabled {
          background: #1a1d21;
          border-color: #2b3036;
          color: #606770;
      }
      QPushButton#liveSourceMemoReturnButton {
          background: #20242a;
          border-color: #3a4048;
          color: #cfd3d7;
      }
      QPushButton#liveSourceMemoReturnButton:hover {
          background: #292e35;
          border-color: #505862;
      }
      QPushButton#liveSourceMemoSaveButton {
          background: #2f78b7;
          border-color: #438dca;
          color: #ffffff;
          min-width: 92px;
      }
      QPushButton#liveSourceMemoSaveButton:hover {
          background: #3d89c8;
          border-color: #65a8dd;
      }
      QPushButton:pressed {
          padding-top: 2px;
      }
  )");
  return styleSheet;
}

const QString& confirmationStyleSheet() {
  static const QString styleSheet = QStringLiteral(R"(
      QDialog {
          background: #15171b;
          color: #e7e9ec;
          font-family: "Microsoft YaHei UI";
      }
      QLabel#memoConfirmationEyebrow {
          color: #7f8790;
          font-size: 10px;
      }
      QLabel#memoConfirmationTitle {
          color: #f1f3f5;
          font-family: "Segoe UI Semibold", "Microsoft YaHei UI";
          font-size: 18px;
          font-weight: 600;
      }
      QLabel#memoConfirmationMessage {
          background: #111317;
          border: 1px solid #2b3036;
          border-radius: 4px;
          color: #c5cad0;
          font-size: 13px;
          padding: 12px 13px;
      }
      QPushButton {
          background: #24282e;
          border: 1px solid #3d434b;
          border-radius: 4px;
          color: #d7dbe0;
          font-size: 13px;
          font-weight: 600;
          min-height: 34px;
          max-height: 36px;
          min-width: 96px;
          padding: 0 14px;
      }
      QPushButton:hover {
          background: #2e333a;
          border-color: #565f69;
      }
      QPushButton#memoConfirmationAcceptButton {
          background: #2f78b7;
          border-color: #438dca;
          color: #ffffff;
      }
      QPushButton#memoConfirmationAcceptButton:hover {
          background: #3d89c8;
          border-color: #65a8dd;
      }
  )");
  return styleSheet;
}

bool showConfirmation(QWidget* const parent, const QString& objectName,
                      const QString& title, const QString& message,
                      const QString& detail, const QString& acceptText,
                      const QString& rejectText) {
  QDialog dialog(parent);
  dialog.setObjectName(objectName);
  dialog.setWindowTitle(title);
  dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  dialog.setWindowFlag(Qt::WindowCloseButtonHint, false);
  dialog.setModal(true);
  dialog.setFixedWidth(450);
  dialog.setStyleSheet(confirmationStyleSheet());

  auto* const layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(22, 20, 22, 18);
  layout->setSpacing(10);
  auto* const eyebrow = new QLabel(&dialog);
  eyebrow->setObjectName(QStringLiteral("memoConfirmationEyebrow"));
  eyebrow->hide();
  auto* const titleLabel = new QLabel(title, &dialog);
  titleLabel->setObjectName(QStringLiteral("memoConfirmationTitle"));
  auto* const messageLabel =
      new QLabel(message + QStringLiteral("\n") + detail, &dialog);
  messageLabel->setObjectName(QStringLiteral("memoConfirmationMessage"));
  messageLabel->setWordWrap(true);
  layout->addWidget(eyebrow);
  layout->addWidget(titleLabel);
  layout->addWidget(messageLabel);

  auto* const buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(10);
  buttonLayout->addStretch(1);
  if (!rejectText.isEmpty()) {
    auto* const rejectButton = new QPushButton(rejectText, &dialog);
    rejectButton->setObjectName(
        QStringLiteral("memoConfirmationRejectButton"));
    rejectButton->setCursor(Qt::PointingHandCursor);
    QObject::connect(rejectButton, &QPushButton::clicked, &dialog,
                     &QDialog::reject);
    buttonLayout->addWidget(rejectButton);
  }
  auto* const acceptButton = new QPushButton(acceptText, &dialog);
  acceptButton->setObjectName(
      QStringLiteral("memoConfirmationAcceptButton"));
  acceptButton->setCursor(Qt::PointingHandCursor);
  acceptButton->setDefault(true);
  QObject::connect(acceptButton, &QPushButton::clicked, &dialog,
                   &QDialog::accept);
  buttonLayout->addWidget(acceptButton);
  layout->addLayout(buttonLayout);
  return dialog.exec() == QDialog::Accepted;
}

bool confirmsExplicitSave(QWidget* const parent) {
  return showConfirmation(
      parent, QStringLiteral("liveSourceMemoSaveConfirmation"),
      QStringLiteral("确认保存"),
      QStringLiteral("是否保存当前编辑的直播源？"),
      QStringLiteral("选择“确定”后，地址和备注会写入本机配置。"),
      QStringLiteral("确定"), QStringLiteral("取消"));
}

bool confirmsSaveBeforeClose(QWidget* const parent) {
  return showConfirmation(
      parent, QStringLiteral("liveSourceMemoCloseConfirmation"),
      QStringLiteral("还有未保存的修改"),
      QStringLiteral("你还没有保存，是否保存后关闭窗口？"),
      QStringLiteral("选择“否”会放弃本次未保存的修改。"),
      QStringLiteral("是，保存并关闭"), QStringLiteral("否，直接关闭"));
}

void showMissingAddressWarning(QWidget* const parent, const int row) {
  static_cast<void>(showConfirmation(
      parent, QStringLiteral("liveSourceMemoValidationWarning"),
      QStringLiteral("直播源地址不能为空"),
      QStringLiteral("第 %1 行填写了备注，但还没有直播源地址。")
          .arg(row + 1),
      QStringLiteral("请补全地址后再保存。"), QStringLiteral("知道了"),
      QString{}));
}

}  // namespace

LiveSourceMemoDialog::LiveSourceMemoDialog(QVector<LiveSourceMemo> memos,
                                           QWidget* const parent)
    : QDialog(parent), savedMemos_(std::move(memos)) {
  setObjectName(QStringLiteral("liveSourceMemoDialog"));
  setWindowTitle(QStringLiteral("直播源备忘"));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setModal(true);
  resize(860, 600);
  setMinimumSize(650, 460);
  setStyleSheet(dialogStyleSheet());

  auto* const layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 20, 22, 18);
  layout->setSpacing(12);

  auto* const header = new QFrame(this);
  header->setObjectName(QStringLiteral("liveSourceMemoHeader"));
  auto* const headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 14);
  headerLayout->setSpacing(16);
  auto* const titleLayout = new QVBoxLayout();
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(3);
  auto* const eyebrow = new QLabel(header);
  eyebrow->setObjectName(QStringLiteral("liveSourceMemoEyebrow"));
  eyebrow->hide();
  auto* const title = new QLabel(QStringLiteral("直播源备忘"), header);
  title->setObjectName(QStringLiteral("liveSourceMemoTitle"));
  auto* const introduction = new QLabel(
      QStringLiteral("记录常用直播地址和备注"), header);
  introduction->setObjectName(QStringLiteral("liveSourceMemoIntroduction"));
  titleLayout->addWidget(eyebrow);
  titleLayout->addWidget(title);
  titleLayout->addWidget(introduction);
  headerLayout->addLayout(titleLayout, 1);
  countBadge_ = new QLabel(header);
  countBadge_->setObjectName(QStringLiteral("liveSourceMemoCountBadge"));
  countBadge_->setAlignment(Qt::AlignCenter);
  countBadge_->setMaximumHeight(24);
  headerLayout->addWidget(countBadge_, 0, Qt::AlignVCenter);
  layout->addWidget(header);

  auto* const notice = new QLabel(
      QStringLiteral("仅保存在本机，不会自动载入或播放"), this);
  notice->setObjectName(QStringLiteral("liveSourceMemoNotice"));
  notice->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  layout->addWidget(notice);

  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("liveSourceMemoTable"));
  table_->setAccessibleName(QStringLiteral("直播源备忘表格"));
  table_->setColumnCount(2);
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("直播源地址"), QStringLiteral("备注")});
  table_->verticalHeader()->setVisible(false);
  table_->verticalHeader()->setMinimumSectionSize(52);
  table_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setFixedHeight(42);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
  table_->setColumnWidth(1, 280);
  table_->setAlternatingRowColors(true);
  table_->setWordWrap(true);
  table_->setTextElideMode(Qt::ElideNone);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::DoubleClicked |
                          QAbstractItemView::EditKeyPressed |
                          QAbstractItemView::SelectedClicked);
  table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  table_->setCornerButtonEnabled(false);
  for (const LiveSourceMemo& memo : std::as_const(savedMemos_)) {
    appendRow(memo);
  }
  layout->addWidget(table_, 1);

  auto* const actionLayout = new QHBoxLayout();
  actionLayout->setSpacing(10);
  auto* const addButton = new QPushButton(QStringLiteral("新增"), this);
  addButton->setObjectName(QStringLiteral("liveSourceMemoAddButton"));
  deleteButton_ = new QPushButton(QStringLiteral("删除"), this);
  deleteButton_->setObjectName(QStringLiteral("liveSourceMemoDeleteButton"));
  statusLabel_ = new QLabel(this);
  statusLabel_->setObjectName(QStringLiteral("liveSourceMemoStatus"));
  statusLabel_->setAlignment(Qt::AlignCenter);
  auto* const returnButton = new QPushButton(QStringLiteral("关闭"), this);
  returnButton->setObjectName(QStringLiteral("liveSourceMemoReturnButton"));
  saveButton_ = new QPushButton(QStringLiteral("保存"), this);
  saveButton_->setObjectName(QStringLiteral("liveSourceMemoSaveButton"));
  saveButton_->setToolTip(QStringLiteral("保存（Ctrl+S）"));
  saveButton_->setDefault(true);
  for (QPushButton* const button :
       {addButton, deleteButton_, returnButton, saveButton_}) {
    button->setCursor(Qt::PointingHandCursor);
    button->setMaximumHeight(36);
  }
  actionLayout->addWidget(addButton);
  actionLayout->addWidget(deleteButton_);
  actionLayout->addWidget(statusLabel_, 1);
  actionLayout->addWidget(returnButton);
  actionLayout->addWidget(saveButton_);
  layout->addLayout(actionLayout);

  auto* const saveShortcut =
      new QShortcut(QKeySequence::Save, this);
  saveShortcut->setObjectName(QStringLiteral("liveSourceMemoSaveShortcut"));
  saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  saveShortcut->setAutoRepeat(false);

  connect(table_, &QTableWidget::cellChanged, this,
          [this](const int row, const int) {
            table_->resizeRowToContents(row);
            markDirty();
          });
  connect(table_->horizontalHeader(), &QHeaderView::sectionResized, this,
          [this](const int logicalIndex, const int, const int) {
            if (logicalIndex == 1) {
              table_->resizeRowsToContents();
            }
          });
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &LiveSourceMemoDialog::updateControls);
  connect(addButton, &QPushButton::clicked, this,
          &LiveSourceMemoDialog::addEmptyRow);
  connect(deleteButton_, &QPushButton::clicked, this,
          &LiveSourceMemoDialog::deleteCurrentRow);
  connect(returnButton, &QPushButton::clicked, this,
          &LiveSourceMemoDialog::requestReturn);
  connect(saveButton_, &QPushButton::clicked, this,
          &LiveSourceMemoDialog::requestSave);
  connect(saveShortcut, &QShortcut::activated, this,
          &LiveSourceMemoDialog::requestSave);

  isInitializing_ = false;
  updateControls();
}

void LiveSourceMemoDialog::closeEvent(QCloseEvent* const event) {
  if (allowsClose_ || confirmClose()) {
    allowsClose_ = true;
    event->accept();
    return;
  }
  event->ignore();
}

void LiveSourceMemoDialog::appendRow(const LiveSourceMemo& memo) {
  const int row = table_->rowCount();
  table_->insertRow(row);
  auto* const sourceItem = new QTableWidgetItem(memo.sourceUrl);
  sourceItem->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
  table_->setItem(row, 0, sourceItem);
  auto* const noteItem = new QTableWidgetItem(memo.note);
  noteItem->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
  table_->setItem(row, 1, noteItem);
  table_->resizeRowToContents(row);
}

void LiveSourceMemoDialog::addEmptyRow() {
  appendRow(LiveSourceMemo{});
  const int row = table_->rowCount() - 1;
  table_->setCurrentCell(row, 0);
  table_->scrollToItem(table_->item(row, 0));
  table_->editItem(table_->item(row, 0));
  markDirty();
}

void LiveSourceMemoDialog::deleteCurrentRow() {
  const int row = table_->currentRow();
  if (row < 0) {
    return;
  }
  table_->removeRow(row);
  if (table_->rowCount() > 0) {
    table_->setCurrentCell(qMin(row, table_->rowCount() - 1), 0);
  }
  markDirty();
}

void LiveSourceMemoDialog::markDirty() {
  if (isInitializing_) {
    return;
  }
  isDirty_ = true;
  updateControls();
}

void LiveSourceMemoDialog::updateControls() {
  countBadge_->setText(QStringLiteral("%1 条记录").arg(table_->rowCount()));
  deleteButton_->setEnabled(table_->currentRow() >= 0);
  if (isDirty_) {
    statusLabel_->setText(QStringLiteral("有未保存的修改"));
    statusLabel_->setStyleSheet(QStringLiteral("color: #d9a441;"));
  } else {
    statusLabel_->setText(
        QStringLiteral("已保存 %1 条  |  双击单元格编辑")
            .arg(savedMemos_.size()));
    statusLabel_->setStyleSheet(QStringLiteral("color: #7f8790;"));
  }
}

void LiveSourceMemoDialog::commitActiveEditor() {
  QWidget* const focusedWidget = QApplication::focusWidget();
  if (focusedWidget != nullptr && focusedWidget != table_ &&
      table_->isAncestorOf(focusedWidget)) {
    table_->setFocus(Qt::OtherFocusReason);
  }
}

void LiveSourceMemoDialog::requestSave() {
  static_cast<void>(saveMemos(true));
}

void LiveSourceMemoDialog::requestReturn() {
  if (!confirmClose()) {
    return;
  }
  allowsClose_ = true;
  reject();
}

bool LiveSourceMemoDialog::saveMemos(const bool asksForConfirmation) {
  commitActiveEditor();
  QVector<LiveSourceMemo> memos;
  if (!collectMemos(&memos)) {
    return false;
  }
  if (asksForConfirmation && !confirmsExplicitSave(this)) {
    return false;
  }
  savedMemos_ = std::move(memos);
  isDirty_ = false;
  emit memosSaved(savedMemos_);
  updateControls();
  return true;
}

bool LiveSourceMemoDialog::confirmClose() {
  commitActiveEditor();
  if (!isDirty_) {
    return true;
  }
  if (!confirmsSaveBeforeClose(this)) {
    return true;
  }
  return saveMemos(false);
}

bool LiveSourceMemoDialog::collectMemos(
    QVector<LiveSourceMemo>* const memos) {
  Q_ASSERT(memos != nullptr);
  memos->clear();
  memos->reserve(table_->rowCount());
  for (int row = 0; row < table_->rowCount(); ++row) {
    const QString sourceUrl = table_->item(row, 0)->text().trimmed();
    const QString note = table_->item(row, 1)->text().trimmed();
    if (sourceUrl.isEmpty() && note.isEmpty()) {
      continue;
    }
    if (sourceUrl.isEmpty()) {
      table_->setCurrentCell(row, 0);
      showMissingAddressWarning(this, row);
      return false;
    }
    memos->append(LiveSourceMemo{sourceUrl, note});
  }
  return true;
}

}  // namespace mediahub::gui
