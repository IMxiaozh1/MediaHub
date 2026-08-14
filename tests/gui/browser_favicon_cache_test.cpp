#include <QBuffer>
#include <QImage>
#include <QTest>

#include "browser_favicon_cache.h"

namespace mediahub::gui {
namespace {

QByteArray makePng(const int width = 16, const int height = 16) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(32, 120, 180, 255));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return bytes;
}

class BrowserFaviconCacheTest final : public QObject {
    Q_OBJECT

 private slots:
    void normalizesOnlyHttpOrigins();
    void storesAndLooksUpByOrigin();
    void rejectsUnsafePngWithoutReplacingExistingIcon();
    void evictsTheLeastRecentlyWrittenOrigin();
    void clearsAllEntries();
};

void BrowserFaviconCacheTest::normalizesOnlyHttpOrigins() {
    QCOMPARE(BrowserFaviconCache::normalizeOrigin(
                 QStringLiteral(" HTTPS://Example.COM:443/private?q=x#part ")),
             QStringLiteral("https://example.com"));
    QCOMPARE(BrowserFaviconCache::normalizeOrigin(
                 QStringLiteral("http://Example.COM:8080/path")),
             QStringLiteral("http://example.com:8080"));
    QVERIFY(BrowserFaviconCache::normalizeOrigin(
                QStringLiteral("https://user:secret@example.com/path"))
                .isEmpty());
    QVERIFY(BrowserFaviconCache::normalizeOrigin(
                QStringLiteral("file:///C:/private/icon.png"))
                .isEmpty());
    QVERIFY(BrowserFaviconCache::normalizeOrigin(
                QStringLiteral("javascript:alert(1)"))
                .isEmpty());
}

void BrowserFaviconCacheTest::storesAndLooksUpByOrigin() {
    BrowserFaviconCache cache;
    const QByteArray icon = makePng();
    QVERIFY(!icon.isEmpty());

    QVERIFY(cache.put(QStringLiteral("https://Example.com/page?token=x"), icon));
    QCOMPARE(cache.lookup(QStringLiteral("https://example.com/another")), icon);
    QCOMPARE(cache.size(), 1);
    QVERIFY(cache.lookup(QStringLiteral("https://other.example")).isEmpty());
    QVERIFY(cache.lookup(QStringLiteral("file:///private/icon.png")).isEmpty());
}

void BrowserFaviconCacheTest::rejectsUnsafePngWithoutReplacingExistingIcon() {
    BrowserFaviconCache cache;
    const QByteArray icon = makePng();
    QVERIFY(cache.put(QStringLiteral("https://safe.example"), icon));

    QByteArray damaged = icon;
    damaged.truncate(16);
    QVERIFY(!cache.put(QStringLiteral("https://safe.example"), damaged));
    QVERIFY(!cache.put(QStringLiteral("https://jpeg.example"),
                       QByteArray("not a png")));
    QByteArray oversized = icon;
    oversized.append(QByteArray(256 * 1024 + 1 - oversized.size(), '\0'));
    QVERIFY(!cache.put(QStringLiteral("https://large-file.example"), oversized));
    QVERIFY(!cache.put(QStringLiteral("https://large-image.example"),
                       makePng(513, 1)));

    QCOMPARE(cache.lookup(QStringLiteral("https://safe.example/path")), icon);
    QCOMPARE(cache.size(), 1);
}

void BrowserFaviconCacheTest::evictsTheLeastRecentlyWrittenOrigin() {
    BrowserFaviconCache cache;
    const QByteArray icon = makePng();
    QVERIFY(!icon.isEmpty());
    for (int index = 0; index < 100; ++index) {
        QVERIFY(cache.put(QStringLiteral("https://site-%1.example").arg(index),
                          icon));
    }
    QCOMPARE(cache.size(), 100);

    QVERIFY(cache.put(QStringLiteral("https://site-0.example/new"), icon));
    QVERIFY(cache.put(QStringLiteral("https://site-100.example"), icon));

    QCOMPARE(cache.size(), 100);
    QVERIFY(!cache.lookup(QStringLiteral("https://site-0.example")).isEmpty());
    QVERIFY(cache.lookup(QStringLiteral("https://site-1.example")).isEmpty());
    QVERIFY(!cache.lookup(QStringLiteral("https://site-100.example")).isEmpty());
}

void BrowserFaviconCacheTest::clearsAllEntries() {
    BrowserFaviconCache cache;
    QVERIFY(cache.put(QStringLiteral("https://one.example"), makePng()));
    QVERIFY(cache.put(QStringLiteral("https://two.example"), makePng()));

    cache.clear();

    QCOMPARE(cache.size(), 0);
    QVERIFY(cache.lookup(QStringLiteral("https://one.example")).isEmpty());
    QVERIFY(cache.lookup(QStringLiteral("https://two.example")).isEmpty());
}

}  // namespace
}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserFaviconCacheTest)
#include "browser_favicon_cache_test.moc"
