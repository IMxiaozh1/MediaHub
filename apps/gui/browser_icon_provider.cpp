#include "browser_icon_provider.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <array>
#include <cmath>

namespace mediahub::gui {
namespace {

void drawChevron(QPainter& painter, const QRectF& bounds, const bool pointsRight) {
    const qreal direction = pointsRight ? 1.0 : -1.0;
    const QPointF center = bounds.center();
    QPainterPath path;
    path.moveTo(center.x() - direction * 4.0, center.y() - 5.0);
    path.lineTo(center.x() + direction * 1.0, center.y());
    path.lineTo(center.x() - direction * 4.0, center.y() + 5.0);
    painter.drawPath(path);
}

void drawIcon(QPainter& painter, const BrowserIcon type, const QRectF& bounds,
              const QColor& color) {
    constexpr qreal kPi = 3.14159265358979323846;
    QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const QPointF center = bounds.center();

    switch (type) {
        case BrowserIcon::Back:
        case BrowserIcon::Forward: {
            const bool pointsRight = type == BrowserIcon::Forward;
            drawChevron(painter, bounds, pointsRight);
            const qreal direction = pointsRight ? 1.0 : -1.0;
            painter.drawLine(QPointF(center.x() - direction * 4.0, center.y()),
                             QPointF(center.x() + direction * 5.0, center.y()));
            break;
        }
        case BrowserIcon::Reload: {
            painter.drawArc(bounds.adjusted(3.0, 3.0, -3.0, -3.0), 35 * 16,
                            285 * 16);
            QPainterPath arrow;
            arrow.moveTo(bounds.right() - 2.5, bounds.top() + 4.0);
            arrow.lineTo(bounds.right() - 7.0, bounds.top() + 3.5);
            arrow.lineTo(bounds.right() - 4.5, bounds.top() + 7.5);
            painter.drawPath(arrow);
            break;
        }
        case BrowserIcon::Stop:
            painter.drawRect(bounds.adjusted(5.0, 5.0, -5.0, -5.0));
            break;
        case BrowserIcon::Home: {
            QPainterPath home;
            home.moveTo(bounds.left() + 3.0, center.y() - 1.0);
            home.lineTo(center.x(), bounds.top() + 3.0);
            home.lineTo(bounds.right() - 3.0, center.y() - 1.0);
            home.moveTo(bounds.left() + 5.0, center.y() - 2.0);
            home.lineTo(bounds.left() + 5.0, bounds.bottom() - 3.0);
            home.lineTo(bounds.right() - 5.0, bounds.bottom() - 3.0);
            home.lineTo(bounds.right() - 5.0, center.y() - 2.0);
            painter.drawPath(home);
            break;
        }
        case BrowserIcon::Favorite:
        case BrowserIcon::FavoriteFilled: {
            QPainterPath star;
            constexpr int kPoints = 10;
            for (int index = 0; index < kPoints; ++index) {
                const qreal radius = index % 2 == 0 ? 7.0 : 3.2;
                const qreal angle = -kPi / 2.0 + index * kPi / 5.0;
                const QPointF point(center.x() + std::cos(angle) * radius,
                                    center.y() + std::sin(angle) * radius);
                index == 0 ? star.moveTo(point) : star.lineTo(point);
            }
            star.closeSubpath();
            if (type == BrowserIcon::FavoriteFilled) {
                painter.setBrush(color);
            }
            painter.drawPath(star);
            break;
        }
        case BrowserIcon::Audio:
        case BrowserIcon::AudioMuted: {
            QPainterPath speaker;
            speaker.moveTo(bounds.left() + 3.0, center.y() - 3.0);
            speaker.lineTo(bounds.left() + 6.0, center.y() - 3.0);
            speaker.lineTo(center.x(), center.y() - 6.0);
            speaker.lineTo(center.x(), center.y() + 6.0);
            speaker.lineTo(bounds.left() + 6.0, center.y() + 3.0);
            speaker.lineTo(bounds.left() + 3.0, center.y() + 3.0);
            speaker.closeSubpath();
            painter.drawPath(speaker);
            if (type == BrowserIcon::AudioMuted) {
                painter.drawLine(QPointF(center.x() + 2.0, center.y() - 4.0),
                                 QPointF(bounds.right() - 2.0,
                                         center.y() + 4.0));
                painter.drawLine(QPointF(bounds.right() - 2.0,
                                         center.y() - 4.0),
                                 QPointF(center.x() + 2.0,
                                         center.y() + 4.0));
            } else {
                painter.drawArc(bounds.adjusted(7.0, 4.0, -2.0, -4.0),
                                -55 * 16, 110 * 16);
            }
            break;
        }
        case BrowserIcon::Download:
            painter.drawLine(QPointF(center.x(), bounds.top() + 2.0),
                             QPointF(center.x(), bounds.bottom() - 6.0));
            painter.drawLine(QPointF(center.x(), bounds.bottom() - 5.0),
                             QPointF(center.x() - 4.0, bounds.bottom() - 9.0));
            painter.drawLine(QPointF(center.x(), bounds.bottom() - 5.0),
                             QPointF(center.x() + 4.0, bounds.bottom() - 9.0));
            painter.drawLine(QPointF(bounds.left() + 3.0, bounds.bottom() - 2.0),
                             QPointF(bounds.right() - 3.0,
                                     bounds.bottom() - 2.0));
            break;
        case BrowserIcon::More:
            painter.setBrush(color);
            painter.setPen(Qt::NoPen);
            for (int offset = -5; offset <= 5; offset += 5) {
                painter.drawEllipse(QPointF(center.x() + offset, center.y()),
                                    1.4, 1.4);
            }
            break;
        case BrowserIcon::TabSearch:
            painter.drawRoundedRect(bounds.adjusted(2.0, 3.0, -6.0, -5.0),
                                    2.0, 2.0);
            painter.drawEllipse(QRectF(center.x() - 1.0, center.y() - 1.0,
                                       7.0, 7.0));
            painter.drawLine(QPointF(center.x() + 4.5, center.y() + 4.5),
                             QPointF(bounds.right() - 1.5,
                                     bounds.bottom() - 1.5));
            break;
        case BrowserIcon::NewTab:
            painter.drawLine(QPointF(center.x(), bounds.top() + 3.0),
                             QPointF(center.x(), bounds.bottom() - 3.0));
            painter.drawLine(QPointF(bounds.left() + 3.0, center.y()),
                             QPointF(bounds.right() - 3.0, center.y()));
            break;
        case BrowserIcon::Close:
            painter.drawLine(bounds.topLeft() + QPointF(4.0, 4.0),
                             bounds.bottomRight() - QPointF(4.0, 4.0));
            painter.drawLine(bounds.topRight() + QPointF(-4.0, 4.0),
                             bounds.bottomLeft() + QPointF(4.0, -4.0));
            break;
        case BrowserIcon::SiteControl:
            for (int row = 0; row < 3; ++row) {
                const qreal y = bounds.top() + 4.0 + row * 5.0;
                const qreal knob = row == 1 ? bounds.left() + 12.0
                                            : bounds.left() + 7.0;
                painter.drawLine(QPointF(bounds.left() + 2.0, y),
                                 QPointF(bounds.right() - 2.0, y));
                painter.setBrush(color);
                painter.drawEllipse(QPointF(knob, y), 1.8, 1.8);
                painter.setBrush(Qt::NoBrush);
            }
            break;
        case BrowserIcon::History:
            painter.drawArc(bounds.adjusted(3.0, 3.0, -3.0, -3.0),
                            35 * 16, 300 * 16);
            painter.drawLine(center, QPointF(center.x(), center.y() - 4.0));
            painter.drawLine(center, QPointF(center.x() + 4.0, center.y()));
            painter.drawLine(QPointF(bounds.left() + 2.0, bounds.top() + 4.0),
                             QPointF(bounds.left() + 7.0, bounds.top() + 4.0));
            break;
        case BrowserIcon::Settings:
            painter.drawEllipse(bounds.adjusted(3.0, 3.0, -3.0, -3.0));
            painter.drawEllipse(QRectF(center.x() - 2.0, center.y() - 2.0,
                                       4.0, 4.0));
            break;
        case BrowserIcon::Permissions: {
            QPainterPath shield;
            shield.moveTo(center.x(), bounds.top() + 2.0);
            shield.lineTo(bounds.right() - 3.0, bounds.top() + 5.0);
            shield.lineTo(bounds.right() - 4.0, center.y() + 4.0);
            shield.lineTo(center.x(), bounds.bottom() - 2.0);
            shield.lineTo(bounds.left() + 4.0, center.y() + 4.0);
            shield.lineTo(bounds.left() + 3.0, bounds.top() + 5.0);
            shield.closeSubpath();
            painter.drawPath(shield);
            break;
        }
        case BrowserIcon::ClearData:
            painter.drawRect(bounds.adjusted(5.0, 6.0, -5.0, -2.0));
            painter.drawLine(QPointF(bounds.left() + 3.0, bounds.top() + 5.0),
                             QPointF(bounds.right() - 3.0, bounds.top() + 5.0));
            painter.drawLine(QPointF(center.x() - 3.0, bounds.top() + 2.0),
                             QPointF(center.x() + 3.0, bounds.top() + 2.0));
            break;
        case BrowserIcon::ZoomIn:
        case BrowserIcon::ZoomOut:
            painter.drawEllipse(bounds.adjusted(2.0, 2.0, -6.0, -6.0));
            painter.drawLine(QPointF(center.x() + 3.0, center.y() + 3.0),
                             QPointF(bounds.right() - 1.0,
                                     bounds.bottom() - 1.0));
            painter.drawLine(QPointF(bounds.left() + 5.0, center.y() - 2.0),
                             QPointF(center.x() + 2.0, center.y() - 2.0));
            if (type == BrowserIcon::ZoomIn) {
                painter.drawLine(QPointF(center.x() - 1.5, bounds.top() + 5.0),
                                 QPointF(center.x() - 1.5, center.y() + 2.0));
            }
            break;
        case BrowserIcon::Group:
            painter.drawRoundedRect(bounds.adjusted(2.0, 3.0, -5.0, -7.0),
                                    2.0, 2.0);
            painter.drawRoundedRect(bounds.adjusted(5.0, 7.0, -2.0, -3.0),
                                    2.0, 2.0);
            break;
        case BrowserIcon::FindPrevious:
            drawChevron(painter, bounds, false);
            break;
        case BrowserIcon::FindNext:
            drawChevron(painter, bounds, true);
            break;
    }
}

}  // namespace

QIcon BrowserIconProvider::icon(const BrowserIcon type, const QColor& color,
                                const int logicalSize) {
    static QHash<QString, QIcon> cache;
    const QString key = QStringLiteral("%1:%2:%3")
                            .arg(static_cast<int>(type))
                            .arg(color.rgba())
                            .arg(logicalSize);
    const auto found = cache.constFind(key);
    if (found != cache.cend()) {
        return found.value();
    }

    QIcon result;
    constexpr std::array<qreal, 4> kScaleFactors{1.0, 1.25, 1.5, 2.0};
    for (const qreal scale : kScaleFactors) {
        const int pixelSize = qRound(logicalSize * scale);
        QPixmap pixmap(pixelSize, pixelSize);
        pixmap.fill(Qt::transparent);
        pixmap.setDevicePixelRatio(scale);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(scale, scale);
        drawIcon(painter, type, QRectF(0.5, 0.5, logicalSize - 1.0,
                                      logicalSize - 1.0), color);
        result.addPixmap(pixmap);
    }
    cache.insert(key, result);
    return result;
}

}  // namespace mediahub::gui
