#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "browser_startup_settings.h"

namespace mediahub::gui {
namespace {

class BrowserStartupSettingsTest final : public QObject {
    Q_OBJECT

 private slots:
    void defaultsToBingAndNormalizesStoredAddresses();
    void fallsBackWhenStartupPageListIsEmpty();
    void clearsOnlyBrowserStartupSettings();
};

void BrowserStartupSettingsTest::defaultsToBingAndNormalizesStoredAddresses() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    QSettingsBrowserStartupSettingsStore store(path);
    QCOMPARE(store.load().homeUrl, QStringLiteral("https://www.bing.com/"));
    QCOMPARE(store.load().mode, BrowserStartupMode::OpenBing);
    QCOMPARE(store.load().maximumTabCount, 20);

    BrowserStartupSettings settings;
    settings.homeUrl = QStringLiteral("https://Example.com/home?private=1#part");
    settings.mode = BrowserStartupMode::OpenStartupPages;
    settings.maximumTabCount = 101;
    settings.startupUrls = {
        QStringLiteral("https://One.example/a?token=x"),
        QStringLiteral("https://one.example/a#duplicate"),
        QStringLiteral("file:///C:/private.txt"),
        QStringLiteral("https://two.example/")};
    store.save(settings);

    const BrowserStartupSettings loaded = store.load();
    QCOMPARE(loaded.homeUrl, QStringLiteral("https://example.com/home"));
    QCOMPARE(loaded.mode, BrowserStartupMode::OpenStartupPages);
    QCOMPARE(loaded.maximumTabCount, 100);
    QCOMPARE(loaded.startupUrls,
             QVector<QString>({QStringLiteral("https://one.example/a"),
                               QStringLiteral("https://two.example/")}));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray contents = file.readAll();
    QVERIFY(!contents.contains("private"));
    QVERIFY(!contents.contains("token"));
}

void BrowserStartupSettingsTest::fallsBackWhenStartupPageListIsEmpty() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettingsBrowserStartupSettingsStore store(
        directory.filePath(QStringLiteral("settings.ini")));
    BrowserStartupSettings settings;
    settings.homeUrl = QStringLiteral("javascript:alert(1)");
    settings.mode = BrowserStartupMode::OpenStartupPages;
    settings.startupUrls = {QStringLiteral("https://user:pass@example.com/")};
    store.save(settings);

    const BrowserStartupSettings loaded = store.load();
    QCOMPARE(loaded.homeUrl, QStringLiteral("https://www.bing.com/"));
    QCOMPARE(loaded.mode, BrowserStartupMode::OpenBing);
    QVERIFY(loaded.startupUrls.isEmpty());
}

void BrowserStartupSettingsTest::clearsOnlyBrowserStartupSettings() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    QSettingsBrowserStartupSettingsStore store(path);
    BrowserStartupSettings settings;
    settings.mode = BrowserStartupMode::RestoreSession;
    settings.maximumTabCount = 1;
    store.save(settings);
    store.clear();

    QCOMPARE(store.load().mode, BrowserStartupMode::OpenBing);
    QCOMPARE(store.load().maximumTabCount, 20);
}

}  // namespace
}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserStartupSettingsTest)

#include "browser_startup_settings_test.moc"
