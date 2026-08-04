#include "video_output_widget.h"

#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QShowEvent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace mediahub::gui {

namespace {

constexpr int kVisualizationProgressMaximum = 1000;
constexpr int kWaveformTargetFrameCount = 6;
constexpr std::size_t kWaveformControlPointCount = 24;
constexpr float kWaveformShapeResponse = 0.48F;
constexpr double kPi = 3.14159265358979323846;

std::array<float, core::kAudioWaveformSampleCount> normalizedWaveformShape(
    const std::array<float, core::kAudioWaveformSampleCount>& samples) {
  std::array<float, kWaveformControlPointCount> controlPoints{};
  for (std::size_t controlIndex = 0; controlIndex < kWaveformControlPointCount;
       ++controlIndex) {
    const std::size_t begin =
        controlIndex * samples.size() / kWaveformControlPointCount;
    const std::size_t end =
        (controlIndex + 1) * samples.size() / kWaveformControlPointCount;
    double sumOfSquares = 0.0;
    for (std::size_t sampleIndex = begin; sampleIndex < end; ++sampleIndex) {
      const double sample = static_cast<double>(samples[sampleIndex]);
      sumOfSquares += sample * sample;
    }
    controlPoints[controlIndex] = static_cast<float>(
        std::sqrt(sumOfSquares / static_cast<double>(end - begin)));
  }

  std::array<float, core::kAudioWaveformSampleCount> normalized{};
  float peak = 0.0F;
  for (const float controlPoint : controlPoints) {
    peak = std::max(peak, controlPoint);
  }
  if (peak < 0.0001F) {
    return normalized;
  }

  for (std::size_t index = 0; index < normalized.size(); ++index) {
    const double controlPosition =
        static_cast<double>(index) /
        static_cast<double>(normalized.size() - 1) *
        static_cast<double>(kWaveformControlPointCount - 1);
    const std::size_t leftControl = static_cast<std::size_t>(controlPosition);
    const std::size_t rightControl =
        std::min(leftControl + 1, kWaveformControlPointCount - 1);
    const double controlFraction =
        controlPosition - static_cast<double>(leftControl);
    const double smoothFraction =
        controlFraction * controlFraction * (3.0 - 2.0 * controlFraction);
    normalized[index] = static_cast<float>(
        (static_cast<double>(controlPoints[leftControl]) *
             (1.0 - smoothFraction) +
         static_cast<double>(controlPoints[rightControl]) * smoothFraction) /
        static_cast<double>(peak));
  }
  return normalized;
}

}  // namespace

VideoOutputWidget::VideoOutputWidget(QWidget* const parent) : QWidget(parent) {
  setObjectName(QStringLiteral("videoOutputWidget"));
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMinimumHeight(180);
}

QSize VideoOutputWidget::sizeHint() const { return QSize(720, 405); }

QSize VideoOutputWidget::minimumSizeHint() const { return QSize(320, 180); }

void VideoOutputWidget::setPresentation(const bool isVideoActive,
                                        const bool isAudioVisualizationActive,
                                        const bool isAudioVisualizationPlaying,
                                        const int progressValue,
                                        QString mediaTitle,
                                        QString placeholderText) {
  const bool showsAudioVisualization =
      isAudioVisualizationActive && !isVideoActive;
  const bool mediaChanged = mediaTitle_ != mediaTitle;
  const bool audioModeChanged =
      isAudioVisualizationActive_ != showsAudioVisualization;
  const int boundedProgress =
      qBound(0, progressValue, kVisualizationProgressMaximum);
  if (isVideoActive_ == isVideoActive &&
      isAudioVisualizationActive_ == showsAudioVisualization &&
      isAudioVisualizationPlaying_ == isAudioVisualizationPlaying &&
      progressValue_ == boundedProgress && mediaTitle_ == mediaTitle &&
      placeholderText_ == placeholderText) {
    return;
  }

  isVideoActive_ = isVideoActive;
  isAudioVisualizationActive_ = showsAudioVisualization;
  isAudioVisualizationPlaying_ =
      showsAudioVisualization && isAudioVisualizationPlaying;
  progressValue_ = boundedProgress;
  mediaTitle_ = std::move(mediaTitle);
  placeholderText_ = std::move(placeholderText);
  if (mediaChanged || audioModeChanged) {
    audioWaveform_ = {};
    displayedWaveformSamples_.fill(0.0F);
    targetWaveformSamples_.fill(0.0F);
    displayedIntensity_ = 0.0F;
    animationFrame_ = 0;
    waveformTargetFramesRemaining_ = 0;
  }

  // 视频活动时画布由 libVLC 直接绘制，Qt 不再擦除或声明自己覆盖全部像素。
  setAttribute(Qt::WA_NoSystemBackground, isVideoActive_);
  setAttribute(Qt::WA_OpaquePaintEvent, !isVideoActive_);
  if (isVideoActive_) {
    return;
  }
  update();
}

void VideoOutputWidget::setAudioWaveform(core::AudioWaveform waveform) {
  audioWaveform_ = std::move(waveform);
  const float targetIntensity =
      std::clamp(audioWaveform_.intensity, 0.0F, 1.0F);
  const float response = targetIntensity > displayedIntensity_ ? 0.52F : 0.38F;
  displayedIntensity_ += response * (targetIntensity - displayedIntensity_);

  if (waveformTargetFramesRemaining_ == 0) {
    targetWaveformSamples_ = normalizedWaveformShape(audioWaveform_.samples);
    waveformTargetFramesRemaining_ = kWaveformTargetFrameCount;
  }
  --waveformTargetFramesRemaining_;
  for (std::size_t index = 0; index < displayedWaveformSamples_.size();
       ++index) {
    displayedWaveformSamples_[index] +=
        kWaveformShapeResponse *
        (targetWaveformSamples_[index] - displayedWaveformSamples_[index]);
  }

  ++animationFrame_;
  if (isAudioVisualizationActive_ && isVisible()) {
    update();
  }
}

bool VideoOutputWidget::isVideoActive() const noexcept {
  return isVideoActive_;
}

bool VideoOutputWidget::isAudioVisualizationActive() const noexcept {
  return isAudioVisualizationActive_;
}

bool VideoOutputWidget::isAudioVisualizationAnimating() const noexcept {
  return isAudioVisualizationActive_ && isAudioVisualizationPlaying_;
}

int VideoOutputWidget::audioVisualizationProgress() const noexcept {
  return progressValue_;
}

int VideoOutputWidget::animationFrame() const noexcept {
  return animationFrame_;
}

float VideoOutputWidget::audioVisualizationIntensity() const noexcept {
  return displayedIntensity_;
}

core::AudioWaveform VideoOutputWidget::audioWaveform() const noexcept {
  return audioWaveform_;
}

QString VideoOutputWidget::placeholderText() const { return placeholderText_; }

void VideoOutputWidget::showEvent(QShowEvent* const event) {
  QWidget::showEvent(event);
  publishSurface();
}

bool VideoOutputWidget::event(QEvent* const event) {
  const bool wasHandled = QWidget::event(event);
  if (event->type() == QEvent::WinIdChange && isVisible()) {
    publishSurface();
  }
  return wasHandled;
}

void VideoOutputWidget::paintEvent(QPaintEvent* const event) {
  Q_UNUSED(event);

  if (isVideoActive_) {
    return;
  }

  ensureBackgroundCache();
  QPainter painter(this);
  painter.drawPixmap(0, 0, backgroundCache_);

  if (isAudioVisualizationActive_) {
    painter.setRenderHint(QPainter::Antialiasing);
    paintAudioVisualization(painter);
    return;
  }

  painter.setPen(QColor(QStringLiteral("#e8e1d2")));
  QFont messageFont = font();
  messageFont.setPointSize(13);
  messageFont.setWeight(QFont::DemiBold);
  painter.setFont(messageFont);
  painter.drawText(rect().adjusted(32, 24, -32, -24),
                   Qt::AlignCenter | Qt::TextWordWrap, placeholderText_);
}

void VideoOutputWidget::ensureBackgroundCache() {
  const qreal pixelRatio = devicePixelRatioF();
  const QSize pixelSize(qMax(1, qRound(width() * pixelRatio)),
                        qMax(1, qRound(height() * pixelRatio)));
  if (!backgroundCache_.isNull() &&
      backgroundCache_.devicePixelRatioF() == pixelRatio &&
      backgroundCache_.size() == pixelSize) {
    return;
  }

  QPixmap cache(pixelSize);
  cache.setDevicePixelRatio(pixelRatio);
  cache.fill(Qt::transparent);
  QPainter painter(&cache);
  const QRectF bounds(QPointF(0.0, 0.0), QSizeF(size()));
  QLinearGradient background(bounds.topLeft(), bounds.bottomRight());
  background.setColorAt(0.0, QColor(QStringLiteral("#102f2d")));
  background.setColorAt(1.0, QColor(QStringLiteral("#183e3a")));
  painter.fillRect(bounds, background);

  QRadialGradient glow(QPointF(width() * 0.52, height() * 0.5),
                       qMax(width(), height()) * 0.74);
  glow.setColorAt(0.0, QColor(44, 139, 125, 82));
  glow.setColorAt(0.58, QColor(20, 75, 70, 32));
  glow.setColorAt(1.0, QColor(10, 35, 34, 0));
  painter.fillRect(bounds, glow);

  painter.setPen(QColor(QStringLiteral("#315a54")));
  constexpr int kGridStep = 36;
  for (int x = -height(); x < width(); x += kGridStep) {
    painter.drawLine(x, height(), x + height(), 0);
  }
  backgroundCache_ = std::move(cache);
}

void VideoOutputWidget::paintAudioVisualization(QPainter& painter) {
  QFont titleFont = font();
  titleFont.setPointSize(16);
  titleFont.setWeight(QFont::DemiBold);
  painter.setFont(titleFont);
  painter.setPen(QColor(QStringLiteral("#f3ead7")));
  const QString title = QFontMetrics(titleFont).elidedText(
      mediaTitle_, Qt::ElideRight, qMax(80, width() - 64));
  painter.drawText(QRectF(32, 24, width() - 64, 34),
                   Qt::AlignLeft | Qt::AlignVCenter, title);

  const QRectF waveBounds = QRectF(rect()).adjusted(28.0, 68.0, -28.0, -10.0);
  if (waveBounds.width() <= 1.0 || waveBounds.height() <= 1.0) {
    return;
  }

  const double baselineY = waveBounds.bottom();
  const double amplitude =
      waveBounds.height() * 0.92 *
      std::pow(std::clamp(static_cast<double>(displayedIntensity_), 0.0, 1.0),
               0.55);
  QPainterPath waveformPath;
  constexpr std::size_t kCurvePointCount = 192;
  for (std::size_t index = 0; index < kCurvePointCount; ++index) {
    const double normalized =
        static_cast<double>(index) / static_cast<double>(kCurvePointCount - 1);
    const double edgeEnvelope =
        std::pow(std::max(0.0, std::sin(normalized * kPi)), 0.42);
    const double samplePosition =
        normalized * static_cast<double>(displayedWaveformSamples_.size() - 1);
    const std::size_t leftSample = static_cast<std::size_t>(samplePosition);
    const std::size_t rightSample =
        std::min(leftSample + 1, displayedWaveformSamples_.size() - 1);
    const double sampleFraction =
        samplePosition - static_cast<double>(leftSample);
    const double wave =
        static_cast<double>(displayedWaveformSamples_[leftSample]) *
            (1.0 - sampleFraction) +
        static_cast<double>(displayedWaveformSamples_[rightSample]) *
            sampleFraction;
    const QPointF point(waveBounds.left() + waveBounds.width() * normalized,
                        baselineY - amplitude * edgeEnvelope * wave);
    if (index == 0) {
      waveformPath.moveTo(point);
    } else {
      waveformPath.lineTo(point);
    }
  }
  painter.setPen(QPen(QColor(QStringLiteral("#72ddc5")), 2.4, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
  painter.drawPath(waveformPath);
}

void VideoOutputWidget::publishSurface() {
  const WId windowId = winId();
  auto* const nativeHandle =
      reinterpret_cast<void*>(static_cast<quintptr>(windowId));
  if (nativeHandle != nullptr && nativeHandle != publishedHandle_) {
    publishedHandle_ = nativeHandle;
    emit surfaceReady(nativeHandle);
  }
}

}  // namespace mediahub::gui
