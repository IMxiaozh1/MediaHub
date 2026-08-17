#include "live_source_memo_dialog.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPalette>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <utility>

#include "ui_theme.h"

namespace mediahub::gui {
namespace {

class ThemeConfirmationDialog final : public QDialog {
 public:
  ThemeConfirmationDialog(const ThemeSettings& settings,
                          QWidget* const parent)
      : QDialog(parent), backgroundColor_(resolvedThemePalette(settings).panel) {}

 protected:
  void paintEvent(QPaintEvent* const event) override {
    QDialog::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(rect(), backgroundColor_);
  }

 private:
  QColor backgroundColor_;
};

QString dialogStyleSheet(const ThemeSettings& settings) {
  const UiThemePalette palette = resolvedThemePalette(settings);
  QString style = QStringLiteral(R"(
      #liveSourceMemoDialog {
          background: @WINDOW@;
          color: @TEXT@;
          font-family: "Microsoft YaHei UI";
      }
      QFrame#liveSourceMemoHeader {
          background: transparent;
          border: none;
          border-bottom: 1px solid @BORDER@;
      }
      QLabel#liveSourceMemoEyebrow {
          color: @MUTED_TEXT@;
          font-size: 10px;
      }
      QLabel#liveSourceMemoTitle {
          color: @TEXT@;
          font-family: "Segoe UI Semibold", "Microsoft YaHei UI";
          font-size: 22px;
          font-weight: 600;
      }
      QLabel#liveSourceMemoIntroduction {
          color: @MUTED_TEXT@;
          font-size: 12px;
      }
      QLabel#liveSourceMemoNotice {
          background: transparent;
          border: none;
          color: @MUTED_TEXT@;
          font-size: 11px;
          padding: 0;
      }
      QLabel#liveSourceMemoCountBadge {
          background: transparent;
          border: none;
          color: @MUTED_TEXT@;
          font-size: 11px;
          padding: 0;
      }
      QTableWidget#liveSourceMemoTable {
          alternate-background-color: @PANEL@;
          background: @CANVAS@;
          border: 1px solid @BORDER@;
          border-radius: 5px;
          color: @TEXT@;
          gridline-color: @BORDER@;
          outline: none;
          selection-background-color: @ACCENT@;
          selection-color: #ffffff;
      }
      QTableWidget#liveSourceMemoTable::item {
          border-bottom: 1px solid @BORDER@;
          padding: 8px 11px;
      }
      QTableWidget#liveSourceMemoTable::item:hover {
          background: @HOVER@;
      }
      QHeaderView::section {
          background: @PANEL_ALT@;
          border: none;
          border-bottom: 1px solid @BORDER@;
          color: @MUTED_TEXT@;
          font-size: 12px;
          font-weight: 600;
          padding: 9px 11px;
      }
      QScrollBar:vertical {
          background: @CANVAS@;
          border: none;
          margin: 0;
          width: 8px;
      }
      QScrollBar::handle:vertical {
          background: @SCROLL_HANDLE@;
          border-radius: 3px;
          min-height: 28px;
      }
      QScrollBar::handle:vertical:hover {
          background: @SCROLL_HOVER@;
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
          color: @MUTED_TEXT@;
          font-size: 11px;
      }
      QPushButton {
          background: @PANEL_ALT@;
          border: 1px solid @BORDER@;
          border-radius: 4px;
          color: @TEXT@;
          font-size: 13px;
          font-weight: 600;
          min-height: 34px;
          max-height: 36px;
          min-width: 82px;
          padding: 0 14px;
      }
      QPushButton#liveSourceMemoAddButton {
          background: @PANEL_ALT@;
          border-color: @ACCENT@;
          color: @ACCENT@;
      }
      QPushButton#liveSourceMemoAddButton:hover {
          background: @HOVER@;
          border-color: @ACCENT_HOVER@;
          color: @ACCENT_HOVER@;
      }
      QPushButton#liveSourceMemoDeleteButton {
          background: @DANGER_SURFACE@;
          border-color: @DANGER_BORDER@;
          color: @DANGER_TEXT@;
      }
      QPushButton#liveSourceMemoDeleteButton:hover:enabled {
          background: @DANGER_HOVER@;
          border-color: @DANGER_TEXT@;
          color: @DANGER_TEXT@;
      }
      QPushButton#liveSourceMemoDeleteButton:disabled {
          background: @PANEL_ALT@;
          border-color: @BORDER@;
          color: @MUTED_TEXT@;
      }
      QPushButton#liveSourceMemoReturnButton {
          background: @PANEL_ALT@;
          border-color: @BORDER@;
          color: @TEXT@;
      }
      QPushButton#liveSourceMemoReturnButton:hover {
          background: @HOVER@;
          border-color: @ACCENT@;
      }
      QPushButton#liveSourceMemoSaveButton {
          background: @ACCENT@;
          border-color: @ACCENT_HOVER@;
          color: #ffffff;
          min-width: 92px;
      }
      QPushButton#liveSourceMemoSaveButton:hover {
          background: @ACCENT_HOVER@;
          border-color: @ACCENT_HOVER@;
      }
      QPushButton:pressed {
          padding-top: 2px;
      }
  )");
  const QColor scrollHandle = palette.isDark ? palette.border.lighter(145)
                                              : palette.border.darker(112);
  const QColor scrollHover = palette.isDark ? scrollHandle.lighter(135)
                                             : scrollHandle.darker(125);
  const QColor dangerSurface =
      palette.isDark ? QColor(QStringLiteral("#321c21"))
                     : QColor(QStringLiteral("#fff0f1"));
  const QColor dangerHover =
      palette.isDark ? QColor(QStringLiteral("#48252c"))
                     : QColor(QStringLiteral("#f9dfe3"));
  const QColor dangerBorder =
      palette.isDark ? QColor(QStringLiteral("#68333d"))
                     : QColor(QStringLiteral("#e0adb4"));
  const QColor dangerText =
      palette.isDark ? QColor(QStringLiteral("#ef929d"))
                     : QColor(QStringLiteral("#9d3e4d"));
  style.replace(QStringLiteral("@WINDOW@"), palette.window.name());
  style.replace(QStringLiteral("@TEXT@"), palette.text.name());
  style.replace(QStringLiteral("@MUTED_TEXT@"), palette.mutedText.name());
  style.replace(QStringLiteral("@BORDER@"), palette.border.name());
  style.replace(QStringLiteral("@PANEL@"), palette.panel.name());
  style.replace(QStringLiteral("@PANEL_ALT@"), palette.panelAlt.name());
  style.replace(QStringLiteral("@CANVAS@"), palette.canvas.name());
  style.replace(QStringLiteral("@HOVER@"), palette.hover.name());
  style.replace(QStringLiteral("@ACCENT@"), palette.accent.name());
  style.replace(QStringLiteral("@ACCENT_HOVER@"),
                palette.accentHover.name());
  style.replace(QStringLiteral("@SCROLL_HANDLE@"), scrollHandle.name());
  style.replace(QStringLiteral("@SCROLL_HOVER@"), scrollHover.name());
  style.replace(QStringLiteral("@DANGER_SURFACE@"), dangerSurface.name());
  style.replace(QStringLiteral("@DANGER_HOVER@"), dangerHover.name());
  style.replace(QStringLiteral("@DANGER_BORDER@"), dangerBorder.name());
  style.replace(QStringLiteral("@DANGER_TEXT@"), dangerText.name());
  return style;
}

QString confirmationStyleSheet(const ThemeSettings& settings) {
  const UiThemePalette palette = resolvedThemePalette(settings);
  QString style = QStringLiteral(R"(
      QDialog {
          background: @PANEL@;
          color: @TEXT@;
          font-family: "Microsoft YaHei UI";
      }
      QLabel#memoConfirmationEyebrow {
          color: @MUTED_TEXT@;
          font-size: 10px;
      }
      QLabel#memoConfirmationTitle {
          color: @TEXT@;
          font-family: "Segoe UI Semibold", "Microsoft YaHei UI";
          font-size: 18px;
          font-weight: 600;
      }
      QLabel#memoConfirmationMessage {
          background: @CANVAS@;
          border: 1px solid @BORDER@;
          border-radius: 4px;
          color: @TEXT@;
          font-size: 13px;
          padding: 12px 13px;
      }
      QPushButton {
          background: @PANEL_ALT@;
          border: 1px solid @BORDER@;
          border-radius: 4px;
          color: @TEXT@;
          font-size: 13px;
          font-weight: 600;
          min-height: 34px;
          max-height: 36px;
          min-width: 96px;
          padding: 0 14px;
      }
      QPushButton:hover {
          background: @HOVER@;
          border-color: @ACCENT@;
      }
      QPushButton#memoConfirmationAcceptButton {
          background: @ACCENT@;
          border-color: @ACCENT_HOVER@;
          color: #ffffff;
      }
      QPushButton#memoConfirmationAcceptButton:hover {
          background: @ACCENT_HOVER@;
          border-color: @ACCENT_HOVER@;
      }
  )");
  style.replace(QStringLiteral("@PANEL@"), palette.panel.name());
  style.replace(QStringLiteral("@TEXT@"), palette.text.name());
  style.replace(QStringLiteral("@MUTED_TEXT@"), palette.mutedText.name());
  style.replace(QStringLiteral("@CANVAS@"), palette.canvas.name());
  style.replace(QStringLiteral("@BORDER@"), palette.border.name());
  style.replace(QStringLiteral("@PANEL_ALT@"), palette.panelAlt.name());
  style.replace(QStringLiteral("@HOVER@"), palette.hover.name());
  style.replace(QStringLiteral("@ACCENT@"), palette.accent.name());
  style.replace(QStringLiteral("@ACCENT_HOVER@"),
                palette.accentHover.name());
  return style;
}

bool showConfirmation(QWidget* const parent, const QString& objectName,
                      const QString& title, const QString& message,
                      const QString& detail, const QString& acceptText,
                      const QString& rejectText,
                      const ThemeSettings& themeSettings) {
  ThemeConfirmationDialog dialog(themeSettings, parent);
  dialog.setObjectName(objectName);
  dialog.setWindowTitle(title);
  dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  dialog.setWindowFlag(Qt::WindowCloseButtonHint, false);
  dialog.setModal(true);
  dialog.setFixedWidth(450);
  dialog.setStyleSheet(confirmationStyleSheet(themeSettings));
  dialog.setAttribute(Qt::WA_StyledBackground, true);

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

bool confirmsExplicitSave(QWidget* const parent,
                          const ThemeSettings& themeSettings) {
  return showConfirmation(
      parent, QStringLiteral("liveSourceMemoSaveConfirmation"),
      QStringLiteral("确认保存"),
      QStringLiteral("是否保存当前编辑的直播源？"),
      QStringLiteral("选择“确定”后，地址和备注会写入本机配置。"),
      QStringLiteral("确定"), QStringLiteral("取消"), themeSettings);
}

bool confirmsSaveBeforeClose(QWidget* const parent,
                             const ThemeSettings& themeSettings) {
  return showConfirmation(
      parent, QStringLiteral("liveSourceMemoCloseConfirmation"),
      QStringLiteral("还有未保存的修改"),
      QStringLiteral("你还没有保存，是否保存后关闭窗口？"),
      QStringLiteral("选择“否”会放弃本次未保存的修改。"),
      QStringLiteral("是，保存并关闭"), QStringLiteral("否，直接关闭"),
      themeSettings);
}

void showMissingAddressWarning(QWidget* const parent, const int row,
                               const ThemeSettings& themeSettings) {
  static_cast<void>(showConfirmation(
      parent, QStringLiteral("liveSourceMemoValidationWarning"),
      QStringLiteral("直播源地址不能为空"),
      QStringLiteral("第 %1 行填写了备注，但还没有直播源地址。")
          .arg(row + 1),
      QStringLiteral("请补全地址后再保存。"), QStringLiteral("知道了"),
      QString{}, themeSettings));
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
  applyTheme();
}

void LiveSourceMemoDialog::setThemeSettings(
    const ThemeSettings& settings) {
  themeSettings_ = normalizedThemeSettings(settings);
  applyTheme();
}

void LiveSourceMemoDialog::closeEvent(QCloseEvent* const event) {
  if (allowsClose_ || confirmClose()) {
    allowsClose_ = true;
    event->accept();
    return;
  }
  event->ignore();
}

void LiveSourceMemoDialog::paintEvent(QPaintEvent* const event) {
  QDialog::paintEvent(event);
  QPainter painter(this);
  painter.fillRect(rect(), resolvedThemePalette(themeSettings_).window);
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
  const UiThemePalette palette = resolvedThemePalette(themeSettings_);
  countBadge_->setText(QStringLiteral("%1 条记录").arg(table_->rowCount()));
  deleteButton_->setEnabled(table_->currentRow() >= 0);
  if (isDirty_) {
    statusLabel_->setText(QStringLiteral("有未保存的修改"));
    statusLabel_->setStyleSheet(
        QStringLiteral("color: %1;").arg(palette.secondary.name()));
  } else {
    statusLabel_->setText(
        QStringLiteral("已保存 %1 条  |  双击单元格编辑")
            .arg(savedMemos_.size()));
    statusLabel_->setStyleSheet(
        QStringLiteral("color: %1;").arg(palette.mutedText.name()));
  }
}

void LiveSourceMemoDialog::applyTheme() {
  const UiThemePalette palette = resolvedThemePalette(themeSettings_);
  setStyleSheet(dialogStyleSheet(themeSettings_));
  setAttribute(Qt::WA_StyledBackground, true);

  QPalette tablePalette = table_->palette();
  tablePalette.setColor(QPalette::Base, palette.canvas);
  tablePalette.setColor(QPalette::AlternateBase, palette.panel);
  tablePalette.setColor(QPalette::Text, palette.text);
  tablePalette.setColor(QPalette::Highlight, palette.accent);
  tablePalette.setColor(QPalette::HighlightedText, QColor(Qt::white));
  table_->setPalette(tablePalette);
  updateControls();
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
  if (asksForConfirmation &&
      !confirmsExplicitSave(this, themeSettings_)) {
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
  if (!confirmsSaveBeforeClose(this, themeSettings_)) {
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
      showMissingAddressWarning(this, row, themeSettings_);
      return false;
    }
    memos->append(LiveSourceMemo{sourceUrl, note});
  }
  return true;
}

}  // namespace mediahub::gui
