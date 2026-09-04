#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QTest>

#include "browser_icon_provider.h"

namespace mediahub::gui {
namespace {

QRect opaqueBounds(const QImage& image) {
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y).alpha() == 0) {
                continue;
            }
            bounds |= QRect(x, y, 1, 1);
        }
    }
    return bounds;
}

class BrowserIconProviderTest final : public QObject {
    Q_OBJECT

 private slots:
    void highDpiIconsAreNotClippedByDuplicateScaling();
};

void BrowserIconProviderTest::highDpiIconsAreNotClippedByDuplicateScaling() {
    const QList<BrowserIcon> types{BrowserIcon::Back, BrowserIcon::FavoriteFilled,
                                   BrowserIcon::Download};
    for (const BrowserIcon type : types) {
        const QPixmap pixmap = BrowserIconProvider::icon(
                                   type, QColor(QStringLiteral("#344454")), 18)
                                   .pixmap(QSize(18, 18), 1.5);
        QVERIFY(!pixmap.isNull());
        const QRect bounds = opaqueBounds(pixmap.toImage());
        QVERIFY(!bounds.isNull());
        QVERIFY2(bounds.right() < pixmap.width() - 1,
                 "高 DPI 图标右侧不应被裁切");
        QVERIFY2(bounds.bottom() < pixmap.height() - 1,
                 "高 DPI 图标底部不应被裁切");
    }
}

}  // namespace
}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserIconProviderTest)
#include "browser_icon_provider_test.moc"
