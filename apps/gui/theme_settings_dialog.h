#pragma once

#include <QDialog>

#include "theme_settings.h"

class QLabel;
class QSlider;
class QTimer;
class QToolButton;

namespace mediahub::gui {

// 主题设置窗口提供预设强调色、本地背景图和即时预览参数。
class ThemeSettingsDialog final : public QDialog {
  Q_OBJECT

 public:
  explicit ThemeSettingsDialog(const ThemeSettings& settings,
                               QWidget* parent = nullptr);

  [[nodiscard]] const ThemeSettings& settings() const noexcept;
  // 调用线程：GUI 主线程。验证图片可读取后更新预览，不修改原文件。
  Q_INVOKABLE bool setBackgroundImagePath(const QString& filePath);

 signals:
  void previewChanged(const ThemeSettings& settings);

 private:
  void chooseBackgroundImage();
  void refreshControls();
  void refreshDialogStyle();
  void refreshPresetPreviews();
  void applyCustomRgb();
  void scheduleInteractivePreview();
  void flushInteractivePreview();
  void emitPreview();

  ThemeSettings settings_;
  QLabel* backgroundFileLabel_{nullptr};
  QLabel* blurValueLabel_{nullptr};
  QLabel* opacityValueLabel_{nullptr};
  QLabel* statusLabel_{nullptr};
  QLabel* redValueLabel_{nullptr};
  QLabel* greenValueLabel_{nullptr};
  QLabel* blueValueLabel_{nullptr};
  QSlider* blurSlider_{nullptr};
  QSlider* opacitySlider_{nullptr};
  QSlider* redSlider_{nullptr};
  QSlider* greenSlider_{nullptr};
  QSlider* blueSlider_{nullptr};
  QTimer* previewTimer_{nullptr};
  QToolButton* customPresetButton_{nullptr};
};

}  // namespace mediahub::gui
