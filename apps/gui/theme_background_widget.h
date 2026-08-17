#pragma once

#include <QImage>
#include <QWidget>

#include "theme_settings.h"

class QPaintEvent;
class QResizeEvent;

namespace mediahub::gui {

// 绘制播放器外壳背景，并缓存按窗口尺寸处理后的本地图片。
class ThemeBackgroundWidget final : public QWidget {
 public:
  explicit ThemeBackgroundWidget(QWidget* parent = nullptr);

  // 调用线程：GUI 主线程。图片只读取，不修改原文件。
  void setThemeSettings(const ThemeSettings& settings);
  // 调用线程：GUI 主线程。返回与整窗背景同坐标裁切的目标区域。
  [[nodiscard]] QImage alignedBackgroundFor(const QWidget* target);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void loadOriginalImage();
  void rebuildCache();

  ThemeSettings settings_;
  QImage originalImage_;
  QImage renderedImage_;
  QSize renderedSize_;
};

}  // namespace mediahub::gui
