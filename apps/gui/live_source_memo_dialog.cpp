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
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #0b1215, stop:0.52 #101c1d,
                                      stop:1 #172625);
          color: #eef7f4;
          font-family: "Microsoft YaHei UI";
      }
      QFrame#liveSourceMemoHeader {
          background: rgba(22, 40, 39, 232);
          border: 1px solid #315451;
          border-radius: 15px;
      }
      QLabel#liveSourceMemoEyebrow {
          color: #55e2b3;
          font-family: "Bahnschrift SemiCondensed";
          font-size: 11px;
          font-weight: 700;
      }
      QLabel#liveSourceMemoTitle {
          color: #f5fbf9;
          font-size: 26px;
          font-weight: 700;
      }
      QLabel#liveSourceMemoIntroduction {
          color: #9cb0aa;
          font-size: 12px;
      }
      QLabel#liveSourceMemoNotice {
          background: #3b1719;
          border: 1px solid #7e3035;
          border-radius: 8px;
          color: #ff8f95;
          font-size: 12px;
          font-weight: 700;
          padding: 9px 12px;
      }
      QLabel#liveSourceMemoCountBadge {
          background: #1d3a35;
          border: 1px solid #347564;
          border-radius: 14px;
          color: #7af0c5;
          font-size: 11px;
          font-weight: 700;
          padding: 7px 12px;
      }
      QTableWidget#liveSourceMemoTable {
          alternate-background-color: #142120;
          background: #0f1919;
          border: 1px solid #2b4845;
          border-radius: 11px;
          color: #e2eeea;
          gridline-color: #223b38;
          outline: none;
          selection-background-color: #245b4e;
          selection-color: #ffffff;
      }
      QTableWidget#liveSourceMemoTable::item {
          border-bottom: 1px solid #213936;
          padding: 9px 12px;
      }
      QTableWidget#liveSourceMemoTable::item:hover {
          background: #1b302d;
      }
      QHeaderView::section {
          background: #19302e;
          border: none;
          border-bottom: 1px solid #3b625d;
          color: #a7bbb5;
          font-size: 12px;
          font-weight: 700;
          padding: 10px 12px;
      }
      QScrollBar:vertical {
          background: #0d1717;
          border: none;
          margin: 5px 3px;
          width: 10px;
      }
      QScrollBar::handle:vertical {
          background: #426c64;
          border-radius: 4px;
          min-height: 34px;
      }
      QScrollBar::handle:vertical:hover {
          background: #57c49f;
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
          color: #83a198;
          font-size: 11px;
      }
      QPushButton {
          border-radius: 9px;
          font-size: 13px;
          font-weight: 700;
          min-height: 42px;
          min-width: 104px;
          padding: 0 17px;
      }
      QPushButton#liveSourceMemoAddButton {
          background: #48dcb0;
          border: 2px solid #77ecc8;
          color: #071713;
      }
      QPushButton#liveSourceMemoAddButton:hover {
          background: #72edc8;
          border-color: #a0f6dc;
      }
      QPushButton#liveSourceMemoDeleteButton {
          background: #ff6f6f;
          border: 2px solid #ff9999;
          color: #260909;
      }
      QPushButton#liveSourceMemoDeleteButton:hover:enabled {
          background: #ff9292;
          border-color: #ffc0c0;
      }
      QPushButton#liveSourceMemoDeleteButton:disabled {
          background: #293634;
          border: 2px solid #3b4a47;
          color: #71827d;
      }
      QPushButton#liveSourceMemoReturnButton {
          background: #e9f2ef;
          border: 2px solid #ffffff;
          color: #10201c;
      }
      QPushButton#liveSourceMemoReturnButton:hover {
          background: #ffffff;
          border-color: #77ecc8;
      }
      QPushButton#liveSourceMemoSaveButton {
          background: #ffd166;
          border: 2px solid #ffe29c;
          color: #241900;
          font-size: 14px;
          min-width: 132px;
      }
      QPushButton#liveSourceMemoSaveButton:hover {
          background: #ffe08f;
          border-color: #fff0c2;
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
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #101a19, stop:1 #1a2b28);
          color: #eef7f4;
          font-family: "Microsoft YaHei UI";
      }
      QLabel#memoConfirmationEyebrow {
          color: #ffd166;
          font-family: "Bahnschrift SemiCondensed";
          font-size: 11px;
          font-weight: 700;
      }
      QLabel#memoConfirmationTitle {
          color: #f5fbf9;
          font-size: 20px;
          font-weight: 700;
      }
      QLabel#memoConfirmationMessage {
          background: #13211f;
          border: 1px solid #304b46;
          border-radius: 9px;
          color: #c7d8d2;
          font-size: 13px;
          padding: 11px 13px;
      }
      QPushButton {
          background: #edf5f2;
          border: 2px solid #ffffff;
          border-radius: 8px;
          color: #10201c;
          font-size: 13px;
          font-weight: 700;
          min-height: 38px;
          min-width: 96px;
          padding: 0 14px;
      }
      QPushButton:hover {
          background: #ffffff;
          border-color: #74e9c4;
      }
      QPushButton#memoConfirmationAcceptButton {
          background: #ffd166;
          border-color: #ffe6aa;
          color: #241900;
      }
      QPushButton#memoConfirmationAcceptButton:hover {
          background: #ffe193;
          border-color: #fff2c9;
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
  dialog.setFixedWidth(470);
  dialog.setStyleSheet(confirmationStyleSheet());

  auto* const layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(22, 20, 22, 18);
  layout->setSpacing(10);
  auto* const eyebrow =
      new QLabel(QStringLiteral("MEDIAHUB  /  CONFIRM"), &dialog);
  eyebrow->setObjectName(QStringLiteral("memoConfirmationEyebrow"));
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
  setWindowTitle(QStringLiteral("直播源"));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setModal(true);
  resize(860, 600);
  setMinimumSize(650, 460);
  setStyleSheet(dialogStyleSheet());

  auto* const layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 24, 24, 20);
  layout->setSpacing(15);

  auto* const header = new QFrame(this);
  header->setObjectName(QStringLiteral("liveSourceMemoHeader"));
  auto* const headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(21, 17, 19, 17);
  headerLayout->setSpacing(18);
  auto* const titleLayout = new QVBoxLayout();
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(3);
  auto* const eyebrow =
      new QLabel(QStringLiteral("MEDIAHUB  /  LIVE NOTES"), header);
  eyebrow->setObjectName(QStringLiteral("liveSourceMemoEyebrow"));
  auto* const title = new QLabel(QStringLiteral("直播源"), header);
  title->setObjectName(QStringLiteral("liveSourceMemoTitle"));
  auto* const introduction = new QLabel(
      QStringLiteral("只保存地址与备注，不会自动解析、载入或播放。"), header);
  introduction->setObjectName(QStringLiteral("liveSourceMemoIntroduction"));
  titleLayout->addWidget(eyebrow);
  titleLayout->addWidget(title);
  titleLayout->addWidget(introduction);
  headerLayout->addLayout(titleLayout, 1);
  countBadge_ = new QLabel(header);
  countBadge_->setObjectName(QStringLiteral("liveSourceMemoCountBadge"));
  countBadge_->setAlignment(Qt::AlignCenter);
  headerLayout->addWidget(countBadge_, 0, Qt::AlignVCenter);
  layout->addWidget(header);

  auto* const notice = new QLabel(
      QStringLiteral("本窗口为方便用户保存直播源"), this);
  notice->setObjectName(QStringLiteral("liveSourceMemoNotice"));
  notice->setAlignment(Qt::AlignCenter);
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
  auto* const addButton = new QPushButton(QStringLiteral("+ 新增一行"), this);
  addButton->setObjectName(QStringLiteral("liveSourceMemoAddButton"));
  deleteButton_ = new QPushButton(QStringLiteral("删除此行"), this);
  deleteButton_->setObjectName(QStringLiteral("liveSourceMemoDeleteButton"));
  statusLabel_ = new QLabel(this);
  statusLabel_->setObjectName(QStringLiteral("liveSourceMemoStatus"));
  statusLabel_->setAlignment(Qt::AlignCenter);
  auto* const returnButton = new QPushButton(QStringLiteral("返回"), this);
  returnButton->setObjectName(QStringLiteral("liveSourceMemoReturnButton"));
  saveButton_ = new QPushButton(QStringLiteral("保存  Ctrl+S"), this);
  saveButton_->setObjectName(QStringLiteral("liveSourceMemoSaveButton"));
  saveButton_->setDefault(true);
  for (QPushButton* const button :
       {addButton, deleteButton_, returnButton, saveButton_}) {
    button->setCursor(Qt::PointingHandCursor);
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
    statusLabel_->setStyleSheet(QStringLiteral("color: #ffd166;"));
  } else {
    statusLabel_->setText(
        QStringLiteral("已保存 %1 条  |  双击单元格编辑")
            .arg(savedMemos_.size()));
    statusLabel_->setStyleSheet(QStringLiteral("color: #7de8bd;"));
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
