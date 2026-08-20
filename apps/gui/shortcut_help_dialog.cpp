#include "shortcut_help_dialog.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "native_window_theme.h"
#include "ui_theme.h"

namespace mediahub::gui {
namespace {

QString shortcutDialogStyleSheet(const ThemeSettings& settings) {
  const UiThemePalette palette = resolvedThemePalette(settings);
  QString style = QStringLiteral(R"(
      #shortcutHelpDialog {
          background: @WINDOW@;
          color: @TEXT@;
          font-family: "Microsoft YaHei UI";
      }
      QFrame#shortcutHelpHeader {
          background: transparent;
          border: none;
          border-bottom: 1px solid @BORDER@;
      }
      QLabel#shortcutHelpEyebrow {
          color: @MUTED_TEXT@;
          font-size: 10px;
      }
      QLabel#shortcutHelpTitle {
          color: @TEXT@;
          font-family: "Segoe UI Semibold", "Microsoft YaHei UI";
          font-size: 22px;
          font-weight: 600;
      }
      QLabel#shortcutHelpIntroduction,
      QLabel#shortcutHelpCountBadge {
          background: transparent;
          border: none;
          color: @MUTED_TEXT@;
          font-size: 11px;
          padding: 0;
      }
      QLabel#shortcutHelpIntroduction {
          font-size: 12px;
      }
      QTableWidget#shortcutHelpTable {
          alternate-background-color: @PANEL@;
          background: @CANVAS@;
          border: 1px solid @BORDER@;
          border-radius: 5px;
          color: @TEXT@;
          gridline-color: transparent;
          outline: none;
      }
      QTableWidget#shortcutHelpTable::item {
          background: @CANVAS@;
          border-bottom: 1px solid @BORDER@;
          padding: 7px 11px;
      }
      QTableWidget#shortcutHelpTable::item:alternate {
          background: @PANEL@;
      }
      QTableWidget#shortcutHelpTable::item:hover {
          background: @HOVER@;
          color: @TEXT@;
      }
      QHeaderView::section {
          background: @PANEL_ALT@;
          border: none;
          border-bottom: 1px solid @BORDER@;
          color: @MUTED_TEXT@;
          font-size: 11px;
          font-weight: 600;
          padding: 9px 12px;
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
      QLabel#shortcutHelpFooterHint {
          color: @MUTED_TEXT@;
          font-size: 11px;
      }
  )");
  const QColor scrollHandle = palette.isDark ? palette.border.lighter(145)
                                              : palette.border.darker(112);
  const QColor scrollHover = palette.isDark ? scrollHandle.lighter(135)
                                             : scrollHandle.darker(125);
  style.replace(QStringLiteral("@WINDOW@"), palette.window.name());
  style.replace(QStringLiteral("@TEXT@"), palette.text.name());
  style.replace(QStringLiteral("@MUTED_TEXT@"), palette.mutedText.name());
  style.replace(QStringLiteral("@BORDER@"), palette.border.name());
  style.replace(QStringLiteral("@PANEL@"), palette.panel.name());
  style.replace(QStringLiteral("@PANEL_ALT@"), palette.panelAlt.name());
  style.replace(QStringLiteral("@CANVAS@"), palette.canvas.name());
  style.replace(QStringLiteral("@HOVER@"), palette.hover.name());
  style.replace(QStringLiteral("@SCROLL_HANDLE@"), scrollHandle.name());
  style.replace(QStringLiteral("@SCROLL_HOVER@"), scrollHover.name());
  return style;
}

QString shortcutHeaderStyleSheet(const ThemeSettings& settings) {
  const UiThemePalette palette = resolvedThemePalette(settings);
  return QStringLiteral(R"(
      QHeaderView {
          background: %1;
      }
      QHeaderView::section {
          background: %1;
          border: none;
          border-bottom: 1px solid %2;
          color: %3;
          font-size: 11px;
          font-weight: 600;
          padding: 9px 12px;
      }
  )")
      .arg(palette.panelAlt.name(), palette.border.name(),
           palette.mutedText.name());
}

QString shortcutOkButtonStyleSheet(const ThemeSettings& settings) {
  const UiThemePalette palette = resolvedThemePalette(settings);
  QString style = QStringLiteral(R"(
      QPushButton#shortcutHelpOkButton {
          background-color: @ACCENT@;
          border: 1px solid @ACCENT_HOVER@;
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
          background-color: @ACCENT_HOVER@;
          border-color: @FOCUS@;
      }
      QPushButton#shortcutHelpOkButton:pressed {
          background-color: @ACCENT_PRESSED@;
          border-color: @ACCENT_PRESSED@;
      }
      QPushButton#shortcutHelpOkButton:focus {
          border-color: @FOCUS@;
      }
  )");
  const QColor pressed = palette.accent.darker(112);
  const QColor focus = palette.accentHover.lighter(125);
  style.replace(QStringLiteral("@ACCENT@"), palette.accent.name());
  style.replace(QStringLiteral("@ACCENT_HOVER@"),
                palette.accentHover.name());
  style.replace(QStringLiteral("@ACCENT_PRESSED@"), pressed.name());
  style.replace(QStringLiteral("@FOCUS@"), focus.name());
  return style;
}

}  // namespace

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

  table_ = new QTableWidget(descriptions.size(), 2, this);
  table_->setObjectName(QStringLiteral("shortcutHelpTable"));
  table_->setAccessibleName(QStringLiteral("快捷键列表"));
  table_->setHorizontalHeaderLabels(
      {QStringLiteral("快捷键"), QStringLiteral("操作")});
  table_->verticalHeader()->setVisible(false);
  table_->verticalHeader()->setDefaultSectionSize(40);
  table_->horizontalHeader()->setFixedHeight(38);
  table_->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->setAlternatingRowColors(true);
  table_->setShowGrid(false);
  table_->setCornerButtonEnabled(false);
  table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionMode(QAbstractItemView::NoSelection);
  table_->setFocusPolicy(Qt::NoFocus);
  QFont shortcutFont(QStringLiteral("Cascadia Mono"));
  shortcutFont.setStyleHint(QFont::Monospace);
  shortcutFont.setBold(true);
  for (int row = 0; row < descriptions.size(); ++row) {
    auto* const shortcutItem =
        new QTableWidgetItem(descriptions.at(row).shortcut);
    shortcutItem->setTextAlignment(Qt::AlignCenter);
    shortcutItem->setFont(shortcutFont);
    table_->setItem(row, 0, shortcutItem);

    auto* const operationItem =
        new QTableWidgetItem(descriptions.at(row).operation);
    table_->setItem(row, 1, operationItem);
  }
  layout->addWidget(table_, 1);

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
  okButton_ = buttons->button(QDialogButtonBox::Ok);
  okButton_->setObjectName(QStringLiteral("shortcutHelpOkButton"));
  okButton_->setText(QStringLiteral("确定"));
  okButton_->setCursor(Qt::PointingHandCursor);
  okButton_->setDefault(true);
  okButton_->setMinimumSize(96, 34);
  okButton_->setMaximumHeight(36);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  footerLayout->addWidget(buttons);
  layout->addLayout(footerLayout);
  applyTheme();
}

void ShortcutHelpDialog::setThemeSettings(const ThemeSettings& settings) {
  themeSettings_ = normalizedThemeSettings(settings);
  applyTheme();
}

void ShortcutHelpDialog::paintEvent(QPaintEvent* const event) {
  QDialog::paintEvent(event);
  QPainter painter(this);
  painter.fillRect(rect(), resolvedThemePalette(themeSettings_).window);
}

void ShortcutHelpDialog::applyTheme() {
  const UiThemePalette palette = resolvedThemePalette(themeSettings_);
  const QString dialogStyle = shortcutDialogStyleSheet(themeSettings_);
  setStyleSheet(dialogStyle);
  setAttribute(Qt::WA_StyledBackground, true);
  setNativeDarkTitleBar(this, palette.isDark);
  table_->setStyleSheet(dialogStyle);
  table_->viewport()->setObjectName(QStringLiteral("shortcutHelpViewport"));
  table_->viewport()->setStyleSheet(
      QStringLiteral("QWidget#shortcutHelpViewport { background-color: %1; }")
          .arg(palette.canvas.name()));
  table_->horizontalHeader()->setStyleSheet(
      shortcutHeaderStyleSheet(themeSettings_));
  applyTablePalette(table_, palette);
  okButton_->setStyleSheet(shortcutOkButtonStyleSheet(themeSettings_));
  for (int row = 0; row < table_->rowCount(); ++row) {
    table_->item(row, 0)->setForeground(palette.accent);
    table_->item(row, 0)->setBackground(palette.panelAlt);
    table_->item(row, 1)->setForeground(palette.text);
  }
}

}  // namespace mediahub::gui
