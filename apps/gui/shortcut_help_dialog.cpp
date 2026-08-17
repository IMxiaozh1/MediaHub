#include "shortcut_help_dialog.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace mediahub::gui {

const QVector<ShortcutHelpEntry>& ShortcutHelpDialog::entries() {
  static const QVector<ShortcutHelpEntry> kEntries{
      {QStringLiteral("Ctrl+O"), QStringLiteral("打开本地媒体文件")},
      {QStringLiteral("Ctrl+L"), QStringLiteral("网页模式：聚焦并全选地址；直播模式：打开网络地址")},
      {QStringLiteral("Space"), QStringLiteral("播放或暂停")},
      {QStringLiteral("Ctrl+R"), QStringLiteral("网页模式：刷新当前网页")},
      {QStringLiteral("F5"), QStringLiteral("网页模式：刷新当前网页；直播模式：刷新当前直播")},
      {QStringLiteral("F11"), QStringLiteral("进入或退出全屏")},
      {QStringLiteral("Esc"), QStringLiteral("优先退出网页全屏，否则退出播放器全屏")},
      {QStringLiteral("Alt+左键"), QStringLiteral("网页后退")},
      {QStringLiteral("Alt+右键"), QStringLiteral("网页前进")},
      {QStringLiteral("上键"), QStringLiteral("音量增加 5%")},
      {QStringLiteral("下键"), QStringLiteral("音量降低 5%")},
      {QStringLiteral("左键"), QStringLiteral("按当前步长后退")},
      {QStringLiteral("右键"), QStringLiteral("按当前步长前进")},
      {QStringLiteral("长按右键"), QStringLiteral("临时 2.0 倍速播放")},
      {QStringLiteral("Ctrl+左键"), QStringLiteral("上一项")},
      {QStringLiteral("Ctrl+右键"), QStringLiteral("下一项")},
      {QStringLiteral("Ctrl+下键"), QStringLiteral("静音")},
      {QStringLiteral("Ctrl+上键"), QStringLiteral("恢复声音")},
      {QStringLiteral("Ctrl+M"), QStringLiteral("打开直播源窗口")},
      {QStringLiteral("Ctrl+S"),
       QStringLiteral("在直播源窗口中保存（仅该窗口）")},
      {QStringLiteral("Ctrl+Q"), QStringLiteral("退出程序")},
  };
  return kEntries;
}

ShortcutHelpDialog::ShortcutHelpDialog(QWidget* const parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("shortcutHelpDialog"));
  setWindowTitle(QStringLiteral("MediaHub 快捷键"));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setModal(true);
  resize(700, 600);
  setMinimumSize(560, 460);
  setStyleSheet(QStringLiteral(R"(
      QDialog#shortcutHelpDialog {
          background: #121417;
          color: #e7e9ec;
          font-family: "Microsoft YaHei UI";
      }
      QFrame#shortcutHelpHeader {
          background: transparent;
          border: none;
          border-bottom: 1px solid #2a2e34;
      }
      QLabel#shortcutHelpEyebrow {
          color: #7f8790;
          font-size: 10px;
      }
      QLabel#shortcutHelpTitle {
          color: #f1f3f5;
          font-family: "Segoe UI Semibold", "Microsoft YaHei UI";
          font-size: 22px;
          font-weight: 600;
      }
      QLabel#shortcutHelpIntroduction {
          color: #89919a;
          font-size: 12px;
      }
      QLabel#shortcutHelpCountBadge {
          background: transparent;
          border: none;
          color: #89919a;
          font-size: 11px;
          padding: 0;
      }
      QTableWidget#shortcutHelpTable {
          alternate-background-color: #15181c;
          background: #101216;
          border: 1px solid #2a2e34;
          border-radius: 5px;
          color: #d7dbe0;
          gridline-color: transparent;
          outline: none;
      }
      QTableWidget#shortcutHelpTable::item {
          border-bottom: 1px solid #22262c;
          padding: 7px 11px;
      }
      QTableWidget#shortcutHelpTable::item:hover {
          background: #1d2228;
          color: #ffffff;
      }
      QHeaderView::section {
          background: #181b20;
          border: none;
          border-bottom: 1px solid #30353c;
          color: #9299a2;
          font-size: 11px;
          font-weight: 600;
          padding: 9px 12px;
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
      QLabel#shortcutHelpFooterHint {
          color: #747c85;
          font-size: 11px;
      }
  )"));

  auto* const layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 20, 22, 18);
  layout->setSpacing(12);

  auto* const header = new QFrame(this);
  header->setObjectName(QStringLiteral("shortcutHelpHeader"));
  auto* const headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(0, 0, 0, 14);
  headerLayout->setSpacing(16);

  auto* const titleLayout = new QVBoxLayout();
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(3);
  auto* const eyebrow = new QLabel(header);
  eyebrow->setObjectName(QStringLiteral("shortcutHelpEyebrow"));
  eyebrow->hide();
  auto* const title = new QLabel(QStringLiteral("快捷键"), header);
  title->setObjectName(QStringLiteral("shortcutHelpTitle"));
  auto* const introduction =
      new QLabel(QStringLiteral("播放器和网页模式可用的键盘操作"), header);
  introduction->setObjectName(QStringLiteral("shortcutHelpIntroduction"));
  titleLayout->addWidget(eyebrow);
  titleLayout->addWidget(title);
  titleLayout->addWidget(introduction);
  headerLayout->addLayout(titleLayout, 1);

  const auto& descriptions = entries();
  auto* const countBadge = new QLabel(
      QStringLiteral("%1 项操作").arg(descriptions.size()), header);
  countBadge->setObjectName(QStringLiteral("shortcutHelpCountBadge"));
  countBadge->setAlignment(Qt::AlignCenter);
  countBadge->setMaximumHeight(24);
  headerLayout->addWidget(countBadge, 0, Qt::AlignVCenter);
  layout->addWidget(header);

  auto* const table = new QTableWidget(descriptions.size(), 2, this);
  table->setObjectName(QStringLiteral("shortcutHelpTable"));
  table->setAccessibleName(QStringLiteral("快捷键列表"));
  table->setHorizontalHeaderLabels(
      {QStringLiteral("快捷键"), QStringLiteral("操作")});
  table->verticalHeader()->setVisible(false);
  table->verticalHeader()->setDefaultSectionSize(40);
  table->horizontalHeader()->setFixedHeight(38);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table->setAlternatingRowColors(true);
  table->setShowGrid(false);
  table->setCornerButtonEnabled(false);
  table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setFocusPolicy(Qt::NoFocus);
  QFont shortcutFont(QStringLiteral("Cascadia Mono"));
  shortcutFont.setStyleHint(QFont::Monospace);
  shortcutFont.setBold(true);
  for (int row = 0; row < descriptions.size(); ++row) {
    auto* const shortcutItem =
        new QTableWidgetItem(descriptions.at(row).shortcut);
    shortcutItem->setTextAlignment(Qt::AlignCenter);
    shortcutItem->setFont(shortcutFont);
    shortcutItem->setForeground(QColor(QStringLiteral("#78baf0")));
    shortcutItem->setBackground(QColor(QStringLiteral("#171b20")));
    table->setItem(row, 0, shortcutItem);

    auto* const operationItem =
        new QTableWidgetItem(descriptions.at(row).operation);
    operationItem->setForeground(QColor(QStringLiteral("#d7dbe0")));
    table->setItem(row, 1, operationItem);
  }
  layout->addWidget(table, 1);

  auto* const footerLayout = new QHBoxLayout();
  footerLayout->setContentsMargins(2, 0, 0, 0);
  auto* const footerHint = new QLabel(
      QStringLiteral("快捷键在播放器窗口激活时生效"), this);
  footerHint->setObjectName(QStringLiteral("shortcutHelpFooterHint"));
  footerLayout->addWidget(footerHint);
  footerLayout->addStretch(1);
  auto* const buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok, Qt::Horizontal, this);
  buttons->setObjectName(QStringLiteral("shortcutHelpButtons"));
  auto* const okButton = buttons->button(QDialogButtonBox::Ok);
  okButton->setObjectName(QStringLiteral("shortcutHelpOkButton"));
  okButton->setText(QStringLiteral("确定"));
  okButton->setCursor(Qt::PointingHandCursor);
  okButton->setDefault(true);
  okButton->setMinimumSize(96, 34);
  okButton->setMaximumHeight(36);
  okButton->setStyleSheet(QStringLiteral(R"(
      QPushButton#shortcutHelpOkButton {
          background-color: #2f78b7;
          border: 1px solid #438dca;
          border-radius: 4px;
          color: #ffffff;
          font-family: "Microsoft YaHei UI";
          font-size: 13px;
          font-weight: 600;
          min-height: 34px;
          min-width: 96px;
          max-height: 36px;
          padding: 0 18px;
      }
      QPushButton#shortcutHelpOkButton:hover {
          background-color: #3d89c8;
          border-color: #65a8dd;
      }
      QPushButton#shortcutHelpOkButton:pressed {
          background-color: #26679f;
          border-color: #397cad;
      }
      QPushButton#shortcutHelpOkButton:focus {
          border-color: #8bc6f3;
      }
  )"));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  footerLayout->addWidget(buttons);
  layout->addLayout(footerLayout);
}

}  // namespace mediahub::gui
