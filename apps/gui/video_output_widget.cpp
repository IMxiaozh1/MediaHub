#include "video_output_widget.h"

#include <QColor>
#include <QEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>

#include <utility>

namespace mediahub::gui {

VideoOutputWidget::VideoOutputWidget(QWidget* const parent) : QWidget(parent) {
    setObjectName(QStringLiteral("videoOutputWidget"));
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumHeight(180);
}

QSize VideoOutputWidget::sizeHint() const {
    return QSize(720, 405);
}

QSize VideoOutputWidget::minimumSizeHint() const {
    return QSize(320, 180);
}

void VideoOutputWidget::setPresentation(const bool isVideoActive,
                                        QString placeholderText) {
    if (isVideoActive_ == isVideoActive && placeholderText_ == placeholderText) {
        return;
    }

    isVideoActive_ = isVideoActive;
    placeholderText_ = std::move(placeholderText);

    // 视频活动时画布由 libVLC 直接绘制，Qt 不再擦除或声明自己覆盖全部像素。
    setAttribute(Qt::WA_NoSystemBackground, isVideoActive_);
    setAttribute(Qt::WA_OpaquePaintEvent, !isVideoActive_);
    if (isVideoActive_) {
        return;
    }
    repaint();
}

bool VideoOutputWidget::isVideoActive() const noexcept {
    return isVideoActive_;
}

QString VideoOutputWidget::placeholderText() const {
    return placeholderText_;
}

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

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QLinearGradient background(rect().topLeft(), rect().bottomRight());
    background.setColorAt(0.0, QColor(QStringLiteral("#102f2d")));
    background.setColorAt(1.0, QColor(QStringLiteral("#183e3a")));
    painter.fillRect(rect(), background);

    painter.setPen(QColor(QStringLiteral("#315a54")));
    constexpr int kGridStep = 36;
    for (int x = -height(); x < width(); x += kGridStep) {
        painter.drawLine(x, height(), x + height(), 0);
    }

    painter.setPen(QColor(QStringLiteral("#e8e1d2")));
    QFont messageFont = font();
    messageFont.setPointSize(13);
    messageFont.setWeight(QFont::DemiBold);
    painter.setFont(messageFont);
    painter.drawText(rect().adjusted(32, 24, -32, -24),
                     Qt::AlignCenter | Qt::TextWordWrap,
                     placeholderText_);
}

void VideoOutputWidget::publishSurface() {
    const WId windowId = winId();
    auto* const nativeHandle = reinterpret_cast<void*>(static_cast<quintptr>(windowId));
    if (nativeHandle != nullptr && nativeHandle != publishedHandle_) {
        publishedHandle_ = nativeHandle;
        emit surfaceReady(nativeHandle);
    }
}

}  // namespace mediahub::gui
