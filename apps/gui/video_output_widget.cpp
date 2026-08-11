#include "video_output_widget.h"

#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QResizeEvent>
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

void VideoOutputWidget::setPresentationMode(const UiPresentationMode mode) {
  if (presentationMode_ == mode) {
    return;
  }
  presentationMode_ = mode;
  setProperty("themeMode", presentationModeKey(mode));
  backgroundCache_ = QPixmap{};
  if (!isVideoActive_) {
    update();
  }
}

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
  if (activeVideoSurface_ != nullptr) {
    if (isVideoActive_) {
      activeVideoSurface_->setGeometry(rect());
      activeVideoSurface_->show();
      activeVideoSurface_->raise();
    } else {
      activeVideoSurface_->hide();
    }
  }
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

UiPresentationMode VideoOutputWidget::presentationMode() const noexcept {
  return presentationMode_;
}

void* VideoOutputWidget::beginVideoSurfaceSession() {
  if (activeVideoSurface_ != nullptr) {
    activeVideoSurface_->hide();
    auto* const oldHandle = reinterpret_cast<void*>(
        static_cast<quintptr>(activeVideoSurface_->winId()));
    if (releasedVideoSurfaces_.erase(oldHandle) > 0) {
      const auto oldSurface = std::find(videoSurfaces_.begin(),
                                        videoSurfaces_.end(),
                                        activeVideoSurface_);
      if (oldSurface != videoSurfaces_.end()) {
        videoSurfaces_.erase(oldSurface);
      }
      delete activeVideoSurface_;
    }
  }

  auto* const surface = new QWidget(this);
  surface->setObjectName(
      QStringLiteral("videoSurface%1").arg(videoSurfaces_.size() + 1));
  surface->setAttribute(Qt::WA_NativeWindow);
  surface->setAttribute(Qt::WA_NoSystemBackground);
  surface->setAttribute(Qt::WA_OpaquePaintEvent);
  surface->setGeometry(rect());
  videoSurfaces_.push_back(surface);
  activeVideoSurface_ = surface;
  if (isVideoActive_) {
    surface->show();
    surface->raise();
  }
  const WId windowId = surface->winId();
  return reinterpret_cast<void*>(static_cast<quintptr>(windowId));
}

void VideoOutputWidget::releaseVideoSurface(void* const nativeHandle) {
  const auto releasedSurface = std::find_if(
      videoSurfaces_.begin(), videoSurfaces_.end(),
      [nativeHandle](QWidget* const surface) {
        return reinterpret_cast<void*>(static_cast<quintptr>(surface->winId())) ==
               nativeHandle;
      });
  if (releasedSurface == videoSurfaces_.end()) {
    return;
  }
  if (*releasedSurface == activeVideoSurface_) {
    releasedVideoSurfaces_.insert(nativeHandle);
    return;
  }
  delete *releasedSurface;
  videoSurfaces_.erase(releasedSurface);
}

void VideoOutputWidget::showEvent(QShowEvent* const event) {
  QWidget::showEvent(event);
  if (activeVideoSurface_ != nullptr && isVideoActive_) {
    activeVideoSurface_->show();
    activeVideoSurface_->raise();
  }
  publishSurface();
}

bool VideoOutputWidget::event(QEvent* const event) {
  const bool wasHandled = QWidget::event(event);
  if (event->type() == QEvent::WinIdChange && isVisible()) {
    publishSurface();
  }
  return wasHandled;
}

void VideoOutputWidget::resizeEvent(QResizeEvent* const event) {
  QWidget::resizeEvent(event);
  for (auto* const surface : videoSurfaces_) {
    surface->setGeometry(rect());
  }
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

  const QColor placeholderColor =
      presentationMode_ == UiPresentationMode::Live
          ? QColor(QStringLiteral("#d7b57f"))
          : QColor(QStringLiteral("#aeb8c0"));
  painter.setPen(placeholderColor);
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
  if (isAudioVisualizationActive_) {
    QLinearGradient background(bounds.topLeft(), bounds.bottomRight());
    background.setColorAt(0.0, QColor(QStringLiteral("#f7fbf9")));
    background.setColorAt(0.56, QColor(QStringLiteral("#e7f4ee")));
    background.setColorAt(1.0, QColor(QStringLiteral("#d9ebe3")));
    painter.fillRect(bounds, background);

    QRadialGradient glow(QPointF(width() * 0.7, height() * 0.22),
                         qMax(width(), height()) * 0.78);
    glow.setColorAt(0.0, QColor(39, 197, 126, 76));
    glow.setColorAt(0.46, QColor(48, 181, 132, 25));
    glow.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillRect(bounds, glow);

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(30, 176, 111, 26), 1.0));
    const qreal ringRadius = qMin(width(), height()) * 0.24;
    const QPointF ringCenter(width() * 0.5, height() * 0.48);
    painter.drawEllipse(ringCenter, ringRadius, ringRadius);
    painter.drawEllipse(ringCenter, ringRadius * 0.62, ringRadius * 0.62);
    painter.drawEllipse(ringCenter, ringRadius * 0.18, ringRadius * 0.18);
  } else {
    const bool isLive = presentationMode_ == UiPresentationMode::Live;
    QLinearGradient background(bounds.topLeft(), bounds.bottomRight());
    background.setColorAt(
        0.0, QColor(isLive ? QStringLiteral("#070707")
                           : QStringLiteral("#06080b")));
    background.setColorAt(
        1.0, QColor(isLive ? QStringLiteral("#1b1a18")
                           : QStringLiteral("#121923")));
    painter.fillRect(bounds, background);

    QRadialGradient glow(QPointF(width() * 0.5, height() * 0.78),
                         qMax(width(), height()) * 0.82);
    glow.setColorAt(0.0,
                    isLive ? QColor(222, 158, 66, 46)
                           : QColor(32, 190, 122, 38));
    glow.setColorAt(0.62, QColor(20, 28, 34, 14));
    glow.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.fillRect(bounds, glow);

    painter.setPen(isLive ? QColor(255, 255, 255, 11)
                          : QColor(120, 158, 176, 13));
    constexpr int kGridStep = 48;
    for (int x = 0; x < width(); x += kGridStep) {
      painter.drawLine(x, 0, x, height());
    }
    for (int y = 0; y < height(); y += kGridStep) {
      painter.drawLine(0, y, width(), y);
    }
  }
  backgroundCache_ = std::move(cache);
}

void VideoOutputWidget::paintAudioVisualization(QPainter& painter) {
  QFont titleFont = font();
  titleFont.setPointSize(15);
  titleFont.setWeight(QFont::DemiBold);
  painter.setFont(titleFont);
  painter.setPen(QColor(QStringLiteral("#183027")));
  const QString title = QFontMetrics(titleFont).elidedText(
      mediaTitle_, Qt::ElideRight, qMax(80, width() - 64));
  painter.drawText(QRectF(32, 24, width() - 64, 34),
                   Qt::AlignLeft | Qt::AlignVCenter, title);

  QFont modeFont = font();
  modeFont.setPixelSize(10);
  modeFont.setBold(true);
  modeFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
  painter.setFont(modeFont);
  painter.setPen(QColor(QStringLiteral("#168557")));
  painter.drawText(QRectF(32, 52, width() - 64, 18), Qt::AlignLeft,
                   QStringLiteral("LOCAL AUDIO  /  NOW PLAYING"));

  const QRectF waveBounds = QRectF(rect()).adjusted(28.0, 78.0, -28.0, -16.0);
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
  QPainterPath waveformFill = waveformPath;
  waveformFill.lineTo(waveBounds.bottomRight());
  waveformFill.lineTo(waveBounds.bottomLeft());
  waveformFill.closeSubpath();
  QLinearGradient waveFill(waveBounds.topLeft(), waveBounds.bottomLeft());
  waveFill.setColorAt(0.0, QColor(31, 190, 119, 68));
  waveFill.setColorAt(1.0, QColor(31, 190, 119, 3));
  painter.fillPath(waveformFill, waveFill);
  painter.setPen(QPen(QColor(QStringLiteral("#1fbe77")), 2.4, Qt::SolidLine,
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
