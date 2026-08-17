#pragma once

#include <QPixmap>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QWidget>
#include <array>
#include <unordered_set>
#include <vector>

#include "mediahub/core/media_types.h"
#include "ui_theme.h"

class QEvent;
class QPainter;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;

namespace mediahub::gui {

class ThemeBackgroundWidget;

// 父控件绘制占位内容，每次视频会话使用只承载 vout 的独立原生子窗口。
class VideoOutputWidget final : public QWidget {
  Q_OBJECT

 public:
  explicit VideoOutputWidget(QWidget* parent = nullptr);

  [[nodiscard]] QSize sizeHint() const override;
  [[nodiscard]] QSize minimumSizeHint() const override;

  // 调用线程：GUI 主线程。只更新画布配色，不改变视频句柄或播放状态。
  void setPresentationMode(UiPresentationMode mode);
  // 调用线程：GUI 主线程。更新音频画布的配色和本地背景图缓存。
  void setThemeSettings(const ThemeSettings& settings);
  // 调用线程：GUI 主线程。共享整窗背景的裁切坐标，避免页面切换时图片错位。
  void setThemeBackgroundSource(ThemeBackgroundWidget* backgroundSource);
  // 调用线程：GUI 主线程。视频、音频波纹和静态占位三种画面互斥。
  void setPresentation(bool isVideoActive, bool isAudioVisualizationActive,
                       bool isAudioVisualizationPlaying, int progressValue,
                       QString mediaTitle, QString placeholderText);
  void setAudioWaveform(core::AudioWaveform waveform);
  [[nodiscard]] bool isVideoActive() const noexcept;
  [[nodiscard]] bool isAudioVisualizationActive() const noexcept;
  [[nodiscard]] bool isAudioVisualizationAnimating() const noexcept;
  [[nodiscard]] int audioVisualizationProgress() const noexcept;
  [[nodiscard]] int animationFrame() const noexcept;
  [[nodiscard]] float audioVisualizationIntensity() const noexcept;
  [[nodiscard]] core::AudioWaveform audioWaveform() const noexcept;
  [[nodiscard]] QString placeholderText() const;
  [[nodiscard]] UiPresentationMode presentationMode() const noexcept;
  // 调用线程：GUI 主线程。为下一次视频打开创建不会被旧 vout 占用的原生子窗口。
  [[nodiscard]] void* beginVideoSurfaceSession();
  // 调用线程：GUI 主线程。回收内核已确认不再使用的原生子窗口。
  void releaseVideoSurface(void* nativeHandle);

 signals:
  // 句柄只作为不透明值交给核心接口，GUI 层不暴露 Windows 类型。
  void surfaceReady(void* nativeHandle);

 protected:
  // 调用线程：GUI 主线程。窗口显示后才创建并发布原生句柄。
  void showEvent(QShowEvent* event) override;
  // 调用线程：GUI 主线程。句柄重建时重新发布，保证内核使用当前目标。
  bool event(QEvent* event) override;
  // 调用线程：GUI 主线程。所有仍存活的视频子窗口跟随画布尺寸。
  void resizeEvent(QResizeEvent* event) override;
  // 调用线程：GUI 主线程。视频活动时完全停止 Qt 绘制，避免覆盖 libVLC 画面。
  void paintEvent(QPaintEvent* event) override;

 private:
  void ensureBackgroundCache();
  void paintAudioVisualization(QPainter& painter);
  void publishSurface();

  QString placeholderText_{QStringLiteral("打开媒体后，画面会出现在这里")};
  QString mediaTitle_;
  QPixmap backgroundCache_;
  core::AudioWaveform audioWaveform_;
  std::array<float, core::kAudioWaveformSampleCount>
      displayedWaveformSamples_{};
  std::array<float, core::kAudioWaveformSampleCount> targetWaveformSamples_{};
  float displayedIntensity_{0.0F};
  void* publishedHandle_{nullptr};
  QWidget* activeVideoSurface_{nullptr};
  std::vector<QWidget*> videoSurfaces_;
  std::unordered_set<void*> releasedVideoSurfaces_;
  int progressValue_{0};
  int animationFrame_{0};
  int waveformTargetFramesRemaining_{0};
  UiPresentationMode presentationMode_{UiPresentationMode::LocalVideo};
  ThemeSettings themeSettings_;
  ThemeBackgroundWidget* themeBackgroundSource_{nullptr};
  QPoint cachedBackgroundOrigin_;
  QSize cachedBackgroundSourceSize_;
  bool isVideoActive_{false};
  bool isAudioVisualizationActive_{false};
  bool isAudioVisualizationPlaying_{false};
};

}  // namespace mediahub::gui
