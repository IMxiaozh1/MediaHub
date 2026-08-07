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
      {QStringLiteral("Ctrl+L"), QStringLiteral("打开网络地址")},
      {QStringLiteral("Space"), QStringLiteral("播放或暂停")},
      {QStringLiteral("F5"), QStringLiteral("刷新当前直播")},
      {QStringLiteral("F11"), QStringLiteral("进入或退出全屏")},
      {QStringLiteral("Esc"), QStringLiteral("退出全屏")},
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
  resize(680, 590);
  setMinimumSize(520, 430);
  setStyleSheet(QStringLiteral(R"(
      QDialog#shortcutHelpDialog {
          background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                      stop:0 #0d1412, stop:0.55 #111b17,
                                      stop:1 #17231e);
          color: #edf5f1;
          font-family: "Microsoft YaHei UI";
      }
      QFrame#shortcutHelpHeader {
          background: rgba(26, 40, 34, 228);
          border: 1px solid #30483e;
          border-radius: 14px;
      }
      QLabel#shortcutHelpEyebrow {
          color: #55dda2;
          font-family: "Bahnschrift SemiCondensed";
          font-size: 11px;
          font-weight: 700;
      }
      QLabel#shortcutHelpTitle {
          color: #f4faf7;
          font-size: 25px;
          font-weight: 700;
      }
      QLabel#shortcutHelpIntroduction {
          color: #98aaa2;
          font-size: 12px;
      }
      QLabel#shortcutHelpCountBadge {
          background: #213b31;
          border: 1px solid #32664f;
          border-radius: 14px;
          color: #7ce5b6;
          font-size: 11px;
          font-weight: 700;
          padding: 7px 11px;
      }
      QTableWidget#shortcutHelpTable {
          alternate-background-color: #15201c;
          background: #111916;
          border: 1px solid #2b3d35;
          border-radius: 10px;
          color: #dce8e2;
          gridline-color: transparent;
          outline: none;
      }
      QTableWidget#shortcutHelpTable::item {
          border-bottom: 1px solid #22322b;
          padding: 7px 12px;
      }
      QTableWidget#shortcutHelpTable::item:hover {
          background: #1b2d25;
          color: #ffffff;
      }
      QHeaderView::section {
          background: #1b2923;
          border: none;
          border-bottom: 1px solid #365044;
          color: #8fa59b;
          font-size: 11px;
          font-weight: 700;
          padding: 9px 12px;
      }
      QScrollBar:vertical {
          background: #101814;
          border: none;
          margin: 5px 3px;
          width: 9px;
      }
      QScrollBar::handle:vertical {
          background: #3f5f50;
          border-radius: 4px;
          min-height: 32px;
      }
      QScrollBar::handle:vertical:hover {
          background: #55b98a;
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
          color: #71847b;
          font-size: 11px;
      }
      QDialogButtonBox#shortcutHelpButtons QPushButton {
          background: #ffd166;
          border: 2px solid #ffe5a6;
          border-radius: 9px;
          color: #241900;
          font-size: 14px;
          font-weight: 700;
          min-width: 132px;
          padding: 11px 20px;
      }
      QDialogButtonBox#shortcutHelpButtons QPushButton:hover {
          background: #ffe292;
          border-color: #fff0c4;
      }
      QDialogButtonBox#shortcutHelpButtons QPushButton:pressed {
          background: #e9b848;
          border-color: #e9b848;
      }
  )"));

  auto* const layout = new QVBoxLayout(this);
  layout->setContentsMargins(22, 22, 22, 18);
  layout->setSpacing(14);

  auto* const header = new QFrame(this);
  header->setObjectName(QStringLiteral("shortcutHelpHeader"));
  auto* const headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(20, 16, 18, 16);
  headerLayout->setSpacing(18);

  auto* const titleLayout = new QVBoxLayout();
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(3);
  auto* const eyebrow =
      new QLabel(QStringLiteral("MEDIAHUB  /  KEY MAP"), header);
  eyebrow->setObjectName(QStringLiteral("shortcutHelpEyebrow"));
  auto* const title = new QLabel(QStringLiteral("快捷键速查"), header);
  title->setObjectName(QStringLiteral("shortcutHelpTitle"));
  auto* const introduction =
      new QLabel(QStringLiteral("把常用播放操作留在键盘上。"), header);
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
    shortcutItem->setForeground(QColor(QStringLiteral("#67e5ad")));
    shortcutItem->setBackground(QColor(QStringLiteral("#1b2c25")));
    table->setItem(row, 0, shortcutItem);

    auto* const operationItem =
        new QTableWidgetItem(descriptions.at(row).operation);
    operationItem->setForeground(QColor(QStringLiteral("#dce8e2")));
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
  okButton->setText(QStringLiteral("确定"));
  okButton->setCursor(Qt::PointingHandCursor);
  okButton->setDefault(true);
  okButton->setMinimumHeight(44);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  footerLayout->addWidget(buttons);
  layout->addLayout(footerLayout);
}

}  // namespace mediahub::gui
