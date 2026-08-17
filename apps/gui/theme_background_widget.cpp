#include "theme_background_widget.h"

#include <QImageReader>
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOption>
#include <algorithm>

#include "ui_theme.h"

namespace mediahub::gui {

ThemeBackgroundWidget::ThemeBackgroundWidget(QWidget* const parent)
    : QWidget(parent) {
  setAttribute(Qt::WA_StyledBackground, true);
}

void ThemeBackgroundWidget::setThemeSettings(const ThemeSettings& settings) {
  const ThemeSettings normalized = normalizedThemeSettings(settings);
  const bool reloadsImage =
      normalized.backgroundImagePath != settings_.backgroundImagePath;
  const bool rebuildsImage =
      reloadsImage || normalized.backgroundBlur != settings_.backgroundBlur;
  settings_ = normalized;
  if (reloadsImage) {
    loadOriginalImage();
  }
  if (rebuildsImage) {
    renderedImage_ = {};
    renderedSize_ = {};
  }
  update();
}

QImage ThemeBackgroundWidget::alignedBackgroundFor(const QWidget* target) {
  if (target == nullptr || originalImage_.isNull() ||
      settings_.backgroundOpacity == 0 || target->size().isEmpty()) {
    return {};
  }
  if (renderedImage_.isNull() || renderedSize_ != size()) {
    rebuildCache();
  }
  if (renderedImage_.isNull()) {
    return {};
  }

  const QRect targetRect(target->mapTo(this, QPoint(0, 0)), target->size());
  const QRect sourceRect = targetRect.intersected(rect());
  if (sourceRect.isEmpty()) {
    return {};
  }
  QImage aligned(target->size(), QImage::Format_ARGB32_Premultiplied);
  aligned.fill(Qt::transparent);
  const QRect destinationRect(sourceRect.topLeft() - targetRect.topLeft(),
                              sourceRect.size());
  QPainter painter(&aligned);
  painter.drawImage(destinationRect, renderedImage_, sourceRect);
  return aligned;
}

void ThemeBackgroundWidget::paintEvent(QPaintEvent* const event) {
  Q_UNUSED(event);
  QStyleOption option;
  option.initFrom(this);
  QPainter painter(this);
  style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
  if (originalImage_.isNull() || settings_.backgroundOpacity == 0) {
    return;
  }
  if (renderedImage_.isNull() || renderedSize_ != size()) {
    rebuildCache();
  }
  if (renderedImage_.isNull()) {
    return;
  }
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.setOpacity(settings_.backgroundOpacity / 100.0);
  painter.drawImage(rect(), renderedImage_);
  painter.setOpacity(1.0);
  const UiThemePalette palette = resolvedThemePalette(settings_);
  QColor atmosphere = palette.isDark ? palette.window : palette.chrome;
  atmosphere.setAlpha(palette.isDark ? 32 : 18);
  painter.fillRect(rect(), atmosphere);
}

void ThemeBackgroundWidget::resizeEvent(QResizeEvent* const event) {
  QWidget::resizeEvent(event);
  renderedImage_ = {};
  renderedSize_ = {};
}

void ThemeBackgroundWidget::loadOriginalImage() {
  originalImage_ = {};
  if (settings_.backgroundImagePath.isEmpty()) {
    return;
  }
  QImageReader reader(settings_.backgroundImagePath);
  reader.setAutoTransform(true);
  originalImage_ = reader.read();
}

void ThemeBackgroundWidget::rebuildCache() {
  renderedImage_ = {};
  renderedSize_ = size();
  if (originalImage_.isNull() || width() <= 0 || height() <= 0) {
    return;
  }

  QImage covered = originalImage_.scaled(
      size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  const int left = std::max(0, (covered.width() - width()) / 2);
  const int top = std::max(0, (covered.height() - height()) / 2);
  covered = covered.copy(left, top, width(), height());

  if (settings_.backgroundBlur > 0) {
    const int reduction = 1 + settings_.backgroundBlur / 8;
    const QSize reducedSize(std::max(24, width() / reduction),
                            std::max(24, height() / reduction));
    covered = covered.scaled(reducedSize, Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation)
                  .scaled(size(), Qt::IgnoreAspectRatio,
                          Qt::SmoothTransformation);
  }
  renderedImage_ = std::move(covered);
}

}  // namespace mediahub::gui
