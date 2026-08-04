#include "seek_slider.h"

#include <QMouseEvent>
#include <QPoint>
#include <QStyle>
#include <QStyleOptionSlider>

namespace mediahub::gui {

SeekSlider::SeekSlider(const Qt::Orientation orientation, QWidget* const parent)
    : QSlider(orientation, parent) {}

void SeekSlider::mousePressEvent(QMouseEvent* const event) {
  if (event->button() != Qt::LeftButton || !isEnabled()) {
    QSlider::mousePressEvent(event);
    return;
  }

  QStyleOptionSlider option;
  initStyleOption(&option);
  const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option,
                                               QStyle::SC_SliderHandle, this);
  if (handle.contains(event->pos())) {
    QSlider::mousePressEvent(event);
    return;
  }

  // 复用标准信号顺序，使轨道单击与一次完整的手柄拖动具有相同语义。
  setSliderDown(true);
  setSliderPosition(valueFromPoint(event->pos()));
  setSliderDown(false);
  event->accept();
}

int SeekSlider::valueFromPoint(const QPoint& point) const {
  QStyleOptionSlider option;
  initStyleOption(&option);
  const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option,
                                               QStyle::SC_SliderGroove, this);
  const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option,
                                               QStyle::SC_SliderHandle, this);

  int sliderMinimum = 0;
  int sliderMaximum = 0;
  int pixelPosition = 0;
  if (orientation() == Qt::Horizontal) {
    sliderMinimum = groove.x();
    sliderMaximum = groove.right() - handle.width() + 1;
    pixelPosition = point.x() - handle.width() / 2;
  } else {
    sliderMinimum = groove.y();
    sliderMaximum = groove.bottom() - handle.height() + 1;
    pixelPosition = point.y() - handle.height() / 2;
  }

  return QStyle::sliderValueFromPosition(
      minimum(), maximum(), pixelPosition - sliderMinimum,
      qMax(sliderMaximum - sliderMinimum, 0), option.upsideDown);
}

}  // namespace mediahub::gui
