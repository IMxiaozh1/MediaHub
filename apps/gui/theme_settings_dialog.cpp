#include "theme_settings_dialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <array>

#include "ui_theme.h"

namespace mediahub::gui {
namespace {

// 主题参数滑杆始终按鼠标绝对位置取值，不使用 Qt 默认分页点击。
class ThemeValueSlider final : public QSlider {
 public:
  explicit ThemeValueSlider(QWidget* const parent)
      : QSlider(Qt::Horizontal, parent) {}

 protected:
  void mousePressEvent(QMouseEvent* const event) override {
    if (event->button() != Qt::LeftButton || !isEnabled()) {
      QSlider::mousePressEvent(event);
      return;
    }

    isAbsoluteDrag_ = true;
    setSliderDown(true);
    setSliderPosition(valueFromPoint(event->pos()));
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent* const event) override {
    if (!isAbsoluteDrag_) {
      QSlider::mouseMoveEvent(event);
      return;
    }

    setSliderPosition(valueFromPoint(event->pos()));
    event->accept();
  }

  void mouseReleaseEvent(QMouseEvent* const event) override {
    if (!isAbsoluteDrag_ || event->button() != Qt::LeftButton) {
      QSlider::mouseReleaseEvent(event);
      return;
    }

    setSliderPosition(valueFromPoint(event->pos()));
    isAbsoluteDrag_ = false;
    setSliderDown(false);
    event->accept();
  }

 private:
  [[nodiscard]] int valueFromPoint(const QPoint& point) const {
    QStyleOptionSlider option;
    initStyleOption(&option);
    const int pixelSpan = qMax(width() - 1, 0);
    const int pixelPosition = qBound(0, point.x(), pixelSpan);
    return QStyle::sliderValueFromPosition(minimum(), maximum(), pixelPosition,
                                           pixelSpan, option.upsideDown);
  }

  bool isAbsoluteDrag_{false};
};

struct AccentPreset {
  const char* key;
  const char* objectName;
  const char* darkLabel;
  const char* lightLabel;
};

constexpr std::array<AccentPreset, 5> kAccentPresets{
    AccentPreset{"default", "themePresetDefaultButton", "石墨蓝", "云海蓝"},
    AccentPreset{"blue", "themePresetBlueButton", "深海", "晴空"},
    AccentPreset{"green", "themePresetGreenButton", "松林", "薄荷"},
    AccentPreset{"orange", "themePresetOrangeButton", "暖夜", "暖砂"},
    AccentPreset{"rose", "themePresetRoseButton", "酒红", "珊瑚"},
};

constexpr int kInteractivePreviewIntervalMilliseconds = 60;

QIcon palettePreviewIcon(const UiThemePalette& palette) {
  QPixmap pixmap(64, 38);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath outline;
  outline.addRoundedRect(QRectF(1.5, 1.5, 61.0, 35.0), 5.0, 5.0);
  painter.fillPath(outline, palette.window);
  painter.setPen(QPen(palette.border, 1.0));
  painter.drawPath(outline);
  painter.fillRect(QRectF(8.0, 8.0, 29.0, 22.0), palette.panel);
  painter.fillRect(QRectF(41.0, 8.0, 14.0, 8.0), palette.accent);
  painter.fillRect(QRectF(41.0, 19.0, 14.0, 11.0), palette.secondary);
  return QIcon(pixmap);
}

QString dialogStyleSheet(const UiThemePalette& palette) {
  QString style = QStringLiteral(R"(
      QDialog#themeSettingsDialog {
          background: @WINDOW@;
          color: @TEXT@;
          font-family: "Microsoft YaHei UI";
      }
      QLabel#themeSettingsTitle {
          color: @TEXT@;
          font-family: "Segoe UI Semibold", "Microsoft YaHei UI";
          font-size: 22px;
          font-weight: 600;
      }
      QLabel#themeSettingsIntroduction,
      QLabel#themeStatusLabel {
          color: @MUTED@;
          font-size: 11px;
      }
      QLabel[sectionTitle="true"] {
          color: @TEXT@;
          font-size: 13px;
          font-weight: 600;
      }
      QFrame[settingsSection="true"] {
          background: transparent;
          border: none;
          border-bottom: 1px solid @BORDER@;
      }
      QToolButton[appearanceMode="true"],
      QToolButton[themePreset="true"] {
          background: transparent;
          border: 1px solid transparent;
          border-radius: 4px;
          color: @MUTED@;
          font-size: 11px;
          min-width: 82px;
          padding: 6px 7px;
      }
      QToolButton[appearanceMode="true"] {
          min-width: 112px;
          min-height: 30px;
          font-size: 12px;
          font-weight: 600;
      }
      QToolButton[appearanceMode="true"]:hover,
      QToolButton[themePreset="true"]:hover {
          background: @HOVER@;
          border-color: @BORDER@;
          color: @TEXT@;
      }
      QToolButton[appearanceMode="true"]:checked,
      QToolButton[themePreset="true"]:checked {
          background: @ACCENT_SOFT@;
          border-color: @ACCENT@;
          color: @TEXT@;
      }
      QLabel#themeBackgroundFileLabel {
          background: @PANEL@;
          border: 1px solid @BORDER@;
          border-radius: 4px;
          color: @TEXT@;
          padding: 8px 10px;
      }
      QLabel[sliderName="true"] {
          color: @TEXT@;
          font-size: 12px;
          min-width: 72px;
      }
      QLabel[sliderValue="true"] {
          color: @ACCENT@;
          font-family: "Cascadia Mono";
          font-size: 11px;
          min-width: 42px;
      }
      QSlider::groove:horizontal {
          background: @BORDER@;
          border-radius: 2px;
          height: 4px;
      }
      QSlider::sub-page:horizontal {
          background: @ACCENT@;
          border-radius: 2px;
      }
      QSlider::handle:horizontal {
          background: @TEXT@;
          border: 2px solid @ACCENT@;
          border-radius: 6px;
          margin: -5px 0;
          width: 12px;
      }
      QPushButton {
          background: @PANEL@;
          border: 1px solid @BORDER@;
          border-radius: 4px;
          color: @TEXT@;
          font-size: 12px;
          font-weight: 600;
          min-height: 34px;
          max-height: 36px;
          min-width: 84px;
          padding: 0 14px;
      }
      QPushButton:hover {
          background: @HOVER@;
          border-color: @ACCENT@;
      }
      QPushButton#themeApplyButton {
          background: @ACCENT@;
          border-color: @ACCENT_HOVER@;
          color: #ffffff;
          min-width: 96px;
      }
      QPushButton#themeApplyButton:hover {
          background: @ACCENT_HOVER@;
      }
      QPushButton#themeRemoveBackgroundButton {
          background: transparent;
          color: @MUTED@;
      }
  )");
  const QColor accentSoft(palette.accent.red(), palette.accent.green(),
                          palette.accent.blue(), palette.isDark ? 48 : 34);
  style.replace(QStringLiteral("@WINDOW@"), palette.window.name());
  style.replace(QStringLiteral("@PANEL@"), palette.panel.name());
  style.replace(QStringLiteral("@TEXT@"), palette.text.name());
  style.replace(QStringLiteral("@MUTED@"), palette.mutedText.name());
  style.replace(QStringLiteral("@BORDER@"), palette.border.name());
  style.replace(QStringLiteral("@HOVER@"), palette.hover.name());
  style.replace(QStringLiteral("@ACCENT@"), palette.accent.name());
  style.replace(QStringLiteral("@ACCENT_HOVER@"),
                palette.accentHover.name());
  style.replace(QStringLiteral("@ACCENT_SOFT@"),
                QStringLiteral("rgba(%1, %2, %3, %4)")
                    .arg(accentSoft.red())
                    .arg(accentSoft.green())
                    .arg(accentSoft.blue())
                    .arg(accentSoft.alpha()));
  return style;
}

}  // namespace

ThemeSettingsDialog::ThemeSettingsDialog(const ThemeSettings& settings,
                                         QWidget* const parent)
    : QDialog(parent), settings_(normalizedThemeSettings(settings)) {
  setObjectName(QStringLiteral("themeSettingsDialog"));
  setWindowTitle(QStringLiteral("个性化主题"));
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  setModal(true);
  resize(720, 660);
  setMinimumSize(620, 600);

  previewTimer_ = new QTimer(this);
  previewTimer_->setObjectName(QStringLiteral("themePreviewTimer"));
  previewTimer_->setSingleShot(true);
  previewTimer_->setInterval(kInteractivePreviewIntervalMilliseconds);
  connect(previewTimer_, &QTimer::timeout, this,
          &ThemeSettingsDialog::flushInteractivePreview);

  auto* const layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 20, 24, 18);
  layout->setSpacing(10);
  auto* const title = new QLabel(QStringLiteral("个性化主题"), this);
  title->setObjectName(QStringLiteral("themeSettingsTitle"));
  auto* const introduction = new QLabel(
      QStringLiteral("选择完整界面配色，或使用本地图片打造自己的播放器。"), this);
  introduction->setObjectName(QStringLiteral("themeSettingsIntroduction"));
  layout->addWidget(title);
  layout->addWidget(introduction);

  auto* const modeSection = new QFrame(this);
  modeSection->setProperty("settingsSection", true);
  auto* const modeLayout = new QHBoxLayout(modeSection);
  modeLayout->setContentsMargins(4, 8, 4, 10);
  modeLayout->setSpacing(8);
  auto* const modeTitle = new QLabel(QStringLiteral("外观模式"), modeSection);
  modeTitle->setProperty("sectionTitle", true);
  modeLayout->addWidget(modeTitle);
  modeLayout->addStretch(1);
  auto* const modeButtonHost = new QWidget(modeSection);
  auto* const modeButtonLayout = new QHBoxLayout(modeButtonHost);
  modeButtonLayout->setContentsMargins(0, 0, 0, 0);
  modeButtonLayout->setSpacing(6);
  const auto addModeButton = [this, modeButtonHost, modeButtonLayout](
                                 const QString& objectName,
                                 const QString& modeKey,
                                 const QString& label) {
    auto* const button = new QToolButton(modeButtonHost);
    button->setObjectName(objectName);
    button->setProperty("appearanceMode", true);
    button->setProperty("modeKey", modeKey);
    button->setText(label);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QToolButton::clicked, this, [this, button] {
      settings_.appearanceMode = button->property("modeKey").toString();
      refreshControls();
      emitPreview();
    });
    modeButtonLayout->addWidget(button);
  };
  addModeButton(QStringLiteral("themeModeDarkButton"), QStringLiteral("dark"),
                QStringLiteral("深色模式"));
  addModeButton(QStringLiteral("themeModeLightButton"),
                QStringLiteral("light"), QStringLiteral("浅色模式"));
  modeLayout->addWidget(modeButtonHost);
  layout->addWidget(modeSection);

  auto* const colorSection = new QFrame(this);
  colorSection->setProperty("settingsSection", true);
  auto* const colorLayout = new QVBoxLayout(colorSection);
  colorLayout->setContentsMargins(4, 8, 4, 10);
  colorLayout->setSpacing(7);
  auto* const colorTitle = new QLabel(QStringLiteral("配色方案"), colorSection);
  colorTitle->setProperty("sectionTitle", true);
  colorLayout->addWidget(colorTitle);
  auto* const presetHost = new QWidget(colorSection);
  auto* const presetRow = new QHBoxLayout(presetHost);
  presetRow->setContentsMargins(0, 0, 0, 0);
  presetRow->setSpacing(6);
  for (const AccentPreset& preset : kAccentPresets) {
    auto* const button = new QToolButton(presetHost);
    button->setObjectName(QString::fromLatin1(preset.objectName));
    button->setProperty("themePreset", true);
    button->setProperty("accentKey", QString::fromLatin1(preset.key));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(64, 38));
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QToolButton::clicked, this, [this, button] {
      settings_.accentKey = button->property("accentKey").toString();
      refreshControls();
      emitPreview();
    });
    presetRow->addWidget(button);
  }
  customPresetButton_ = new QToolButton(presetHost);
  customPresetButton_->setObjectName(
      QStringLiteral("themePresetCustomButton"));
  customPresetButton_->setProperty("themePreset", true);
  customPresetButton_->setProperty("accentKey", QStringLiteral("custom"));
  customPresetButton_->setText(QStringLiteral("自定义"));
  customPresetButton_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  customPresetButton_->setIconSize(QSize(64, 38));
  customPresetButton_->setCheckable(true);
  customPresetButton_->setAutoExclusive(true);
  customPresetButton_->setCursor(Qt::PointingHandCursor);
  connect(customPresetButton_, &QToolButton::clicked, this, [this] {
    if (settings_.customAccentColor.isEmpty()) {
      settings_.customAccentColor = resolvedThemePalette(settings_).accent.name();
    }
    settings_.accentKey = QStringLiteral("custom");
    refreshControls();
    emitPreview();
  });
  presetRow->addWidget(customPresetButton_);
  presetRow->addStretch(1);
  colorLayout->addWidget(presetHost);
  layout->addWidget(colorSection);

  auto* const rgbSection = new QFrame(this);
  rgbSection->setProperty("settingsSection", true);
  auto* const rgbLayout = new QVBoxLayout(rgbSection);
  rgbLayout->setContentsMargins(4, 8, 4, 10);
  rgbLayout->setSpacing(6);
  auto* const rgbTitle = new QLabel(QStringLiteral("RGB 自定义"), rgbSection);
  rgbTitle->setProperty("sectionTitle", true);
  rgbLayout->addWidget(rgbTitle);
  const auto addRgbRow = [rgbSection, rgbLayout](
                             const QString& name, const QString& objectName,
                             QLabel** const valueLabel,
                             QSlider** const slider) {
    auto* const row = new QHBoxLayout();
    row->setSpacing(10);
    auto* const nameLabel = new QLabel(name, rgbSection);
    nameLabel->setProperty("sliderName", true);
    *slider = new ThemeValueSlider(rgbSection);
    (*slider)->setObjectName(objectName);
    (*slider)->setRange(0, 255);
    (*slider)->setSingleStep(1);
    (*slider)->setPageStep(16);
    *valueLabel = new QLabel(rgbSection);
    (*valueLabel)->setProperty("sliderValue", true);
    (*valueLabel)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(nameLabel);
    row->addWidget(*slider, 1);
    row->addWidget(*valueLabel);
    rgbLayout->addLayout(row);
  };
  addRgbRow(QStringLiteral("红色 R"), QStringLiteral("themeRedSlider"),
            &redValueLabel_, &redSlider_);
  addRgbRow(QStringLiteral("绿色 G"), QStringLiteral("themeGreenSlider"),
            &greenValueLabel_, &greenSlider_);
  addRgbRow(QStringLiteral("蓝色 B"), QStringLiteral("themeBlueSlider"),
            &blueValueLabel_, &blueSlider_);
  for (QSlider* const slider : {redSlider_, greenSlider_, blueSlider_}) {
    connect(slider, &QSlider::valueChanged, this,
            [this] { applyCustomRgb(); });
    connect(slider, &QSlider::sliderReleased, this,
            &ThemeSettingsDialog::flushInteractivePreview);
  }
  layout->addWidget(rgbSection);

  auto* const backgroundSection = new QFrame(this);
  backgroundSection->setProperty("settingsSection", true);
  auto* const backgroundLayout = new QVBoxLayout(backgroundSection);
  backgroundLayout->setContentsMargins(4, 8, 4, 10);
  backgroundLayout->setSpacing(8);
  auto* const backgroundTitle =
      new QLabel(QStringLiteral("自定义背景"), backgroundSection);
  backgroundTitle->setProperty("sectionTitle", true);
  backgroundLayout->addWidget(backgroundTitle);

  auto* const fileRow = new QHBoxLayout();
  fileRow->setSpacing(8);
  backgroundFileLabel_ = new QLabel(backgroundSection);
  backgroundFileLabel_->setObjectName(
      QStringLiteral("themeBackgroundFileLabel"));
  backgroundFileLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto* const chooseButton =
      new QPushButton(QStringLiteral("选择图片"), backgroundSection);
  chooseButton->setObjectName(QStringLiteral("themeChooseBackgroundButton"));
  auto* const removeButton =
      new QPushButton(QStringLiteral("移除"), backgroundSection);
  removeButton->setObjectName(QStringLiteral("themeRemoveBackgroundButton"));
  fileRow->addWidget(backgroundFileLabel_, 1);
  fileRow->addWidget(chooseButton);
  fileRow->addWidget(removeButton);
  backgroundLayout->addLayout(fileRow);

  const auto addBackgroundSliderRow = [backgroundSection, backgroundLayout](
                                          const QString& name,
                                          const QString& objectName,
                                          QLabel** const valueLabel,
                                          QSlider** const slider) {
    auto* const row = new QHBoxLayout();
    row->setSpacing(10);
    auto* const nameLabel = new QLabel(name, backgroundSection);
    nameLabel->setProperty("sliderName", true);
    *slider = new ThemeValueSlider(backgroundSection);
    (*slider)->setObjectName(objectName);
    (*slider)->setRange(0, 100);
    (*slider)->setSingleStep(1);
    (*slider)->setPageStep(10);
    *valueLabel = new QLabel(backgroundSection);
    (*valueLabel)->setProperty("sliderValue", true);
    (*valueLabel)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(nameLabel);
    row->addWidget(*slider, 1);
    row->addWidget(*valueLabel);
    backgroundLayout->addLayout(row);
  };
  addBackgroundSliderRow(QStringLiteral("模糊度"),
                         QStringLiteral("themeBlurSlider"),
                         &blurValueLabel_, &blurSlider_);
  addBackgroundSliderRow(QStringLiteral("背景清晰度"),
                         QStringLiteral("themeOpacitySlider"),
                         &opacityValueLabel_, &opacitySlider_);
  statusLabel_ = new QLabel(
      QStringLiteral("背景清晰度 0% 表示隐藏图片，100% 表示完整显示。"),
      backgroundSection);
  statusLabel_->setObjectName(QStringLiteral("themeStatusLabel"));
  backgroundLayout->addWidget(statusLabel_);
  layout->addWidget(backgroundSection, 1);

  auto* const footer = new QHBoxLayout();
  footer->setSpacing(8);
  auto* const resetButton =
      new QPushButton(QStringLiteral("恢复默认"), this);
  resetButton->setObjectName(QStringLiteral("themeResetButton"));
  auto* const cancelButton = new QPushButton(QStringLiteral("取消"), this);
  cancelButton->setObjectName(QStringLiteral("themeCancelButton"));
  auto* const applyButton = new QPushButton(QStringLiteral("应用"), this);
  applyButton->setObjectName(QStringLiteral("themeApplyButton"));
  applyButton->setDefault(true);
  footer->addWidget(resetButton);
  footer->addStretch(1);
  footer->addWidget(cancelButton);
  footer->addWidget(applyButton);
  layout->addLayout(footer);

  connect(chooseButton, &QPushButton::clicked, this,
          &ThemeSettingsDialog::chooseBackgroundImage);
  connect(removeButton, &QPushButton::clicked, this, [this] {
    settings_.backgroundImagePath.clear();
    refreshControls();
    emitPreview();
  });
  connect(blurSlider_, &QSlider::valueChanged, this, [this](const int value) {
    settings_.backgroundBlur = value;
    blurValueLabel_->setText(QStringLiteral("%1%").arg(value));
    if (!blurSlider_->isSliderDown()) {
      scheduleInteractivePreview();
    }
  });
  connect(opacitySlider_, &QSlider::valueChanged, this,
          [this](const int value) {
            settings_.backgroundOpacity = value;
            opacityValueLabel_->setText(QStringLiteral("%1%").arg(value));
            if (!opacitySlider_->isSliderDown()) {
              scheduleInteractivePreview();
            }
          });
  for (QSlider* const slider : {blurSlider_, opacitySlider_}) {
    connect(slider, &QSlider::sliderReleased, this,
            &ThemeSettingsDialog::flushInteractivePreview);
  }
  connect(resetButton, &QPushButton::clicked, this, [this] {
    settings_ = ThemeSettings{};
    refreshControls();
    emitPreview();
  });
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);

  refreshControls();
}

const ThemeSettings& ThemeSettingsDialog::settings() const noexcept {
  return settings_;
}

bool ThemeSettingsDialog::setBackgroundImagePath(const QString& filePath) {
  const QString normalizedPath = QFileInfo(filePath).absoluteFilePath();
  QImageReader reader(normalizedPath);
  reader.setAutoTransform(true);
  if (!reader.canRead()) {
    statusLabel_->setText(QStringLiteral("无法读取这张图片，请选择其他文件。"));
    return false;
  }
  settings_.backgroundImagePath = normalizedPath;
  refreshControls();
  emitPreview();
  return true;
}

void ThemeSettingsDialog::chooseBackgroundImage() {
  const QString filePath = QFileDialog::getOpenFileName(
      this, QStringLiteral("选择主题背景"), QString{},
      QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"));
  if (!filePath.isEmpty()) {
    static_cast<void>(setBackgroundImagePath(filePath));
  }
}

void ThemeSettingsDialog::refreshControls() {
  settings_ = normalizedThemeSettings(settings_);
  for (QToolButton* const button : findChildren<QToolButton*>()) {
    if (button->property("appearanceMode").toBool()) {
      button->setChecked(button->property("modeKey").toString() ==
                         settings_.appearanceMode);
    }
    if (button->property("themePreset").toBool()) {
      button->setChecked(button->property("accentKey").toString() ==
                         settings_.accentKey);
    }
  }

  const UiThemePalette palette = resolvedThemePalette(settings_);
  const QColor rgbColor = settings_.customAccentColor.isEmpty()
                              ? palette.accent
                              : QColor(settings_.customAccentColor);
  const QSignalBlocker redBlocker(redSlider_);
  const QSignalBlocker greenBlocker(greenSlider_);
  const QSignalBlocker blueBlocker(blueSlider_);
  const QSignalBlocker blurBlocker(blurSlider_);
  const QSignalBlocker opacityBlocker(opacitySlider_);
  redSlider_->setValue(rgbColor.red());
  greenSlider_->setValue(rgbColor.green());
  blueSlider_->setValue(rgbColor.blue());
  blurSlider_->setValue(settings_.backgroundBlur);
  opacitySlider_->setValue(settings_.backgroundOpacity);
  redValueLabel_->setText(QString::number(rgbColor.red()));
  greenValueLabel_->setText(QString::number(rgbColor.green()));
  blueValueLabel_->setText(QString::number(rgbColor.blue()));
  blurValueLabel_->setText(QStringLiteral("%1%").arg(settings_.backgroundBlur));
  opacityValueLabel_->setText(
      QStringLiteral("%1%").arg(settings_.backgroundOpacity));

  if (settings_.backgroundImagePath.isEmpty()) {
    backgroundFileLabel_->setText(QStringLiteral("未选择背景图片"));
    backgroundFileLabel_->setToolTip(QString{});
  } else {
    const QFileInfo fileInfo(settings_.backgroundImagePath);
    backgroundFileLabel_->setText(fileInfo.fileName());
    backgroundFileLabel_->setToolTip(settings_.backgroundImagePath);
  }
  refreshDialogStyle();
  refreshPresetPreviews();
}

void ThemeSettingsDialog::refreshDialogStyle() {
  setStyleSheet(dialogStyleSheet(resolvedThemePalette(settings_)));
}

void ThemeSettingsDialog::refreshPresetPreviews() {
  const bool isLight = settings_.appearanceMode == QStringLiteral("light");
  for (const AccentPreset& preset : kAccentPresets) {
    auto* const button =
        findChild<QToolButton*>(QString::fromLatin1(preset.objectName));
    if (button == nullptr) {
      continue;
    }
    ThemeSettings preview = settings_;
    preview.accentKey = QString::fromLatin1(preset.key);
    button->setText(QString::fromUtf8(isLight ? preset.lightLabel
                                              : preset.darkLabel));
    button->setIcon(palettePreviewIcon(resolvedThemePalette(preview)));
  }
  ThemeSettings customPreview = settings_;
  if (customPreview.customAccentColor.isEmpty()) {
    customPreview.customAccentColor = resolvedThemePalette(settings_).accent.name();
  }
  customPreview.accentKey = QStringLiteral("custom");
  customPresetButton_->setIcon(
      palettePreviewIcon(resolvedThemePalette(customPreview)));
}

void ThemeSettingsDialog::applyCustomRgb() {
  settings_.customAccentColor =
      QColor(redSlider_->value(), greenSlider_->value(), blueSlider_->value())
          .name(QColor::HexRgb);
  settings_.accentKey = QStringLiteral("custom");
  redValueLabel_->setText(QString::number(redSlider_->value()));
  greenValueLabel_->setText(QString::number(greenSlider_->value()));
  blueValueLabel_->setText(QString::number(blueSlider_->value()));
  customPresetButton_->setChecked(true);
  if (!redSlider_->isSliderDown() && !greenSlider_->isSliderDown() &&
      !blueSlider_->isSliderDown()) {
    scheduleInteractivePreview();
  }
}

void ThemeSettingsDialog::scheduleInteractivePreview() {
  // 合并高频滑杆事件，避免拖动时反复同步重算整个窗口的样式。
  if (!previewTimer_->isActive()) {
    previewTimer_->start();
  }
}

void ThemeSettingsDialog::flushInteractivePreview() {
  previewTimer_->stop();
  settings_ = normalizedThemeSettings(settings_);
  refreshDialogStyle();
  refreshPresetPreviews();
  emit previewChanged(settings_);
}

void ThemeSettingsDialog::emitPreview() {
  settings_ = normalizedThemeSettings(settings_);
  emit previewChanged(settings_);
}

}  // namespace mediahub::gui
