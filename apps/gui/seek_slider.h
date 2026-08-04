#pragma once

#include <QSlider>

class QMouseEvent;
class QPoint;

namespace mediahub::gui {

// 在保留标准手柄拖动行为的同时，支持单击轨道直接定位。
class SeekSlider final : public QSlider {
  Q_OBJECT

 public:
  explicit SeekSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

 protected:
  void mousePressEvent(QMouseEvent* event) override;

 private:
  [[nodiscard]] int valueFromPoint(const QPoint& point) const;
};

}  // namespace mediahub::gui
