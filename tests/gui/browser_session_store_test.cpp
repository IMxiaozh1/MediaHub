#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "browser_session_store.h"

namespace mediahub::gui {
namespace {

class BrowserSessionStoreTest final : public QObject {
    Q_OBJECT

 private slots:
    void normalizesOnlySafeRestorableAddresses();
    void encryptsRoundTripsAndClearsSession();
    void rejectsCorruptedOrOversizedSessionFiles();
};

void BrowserSessionStoreTest::normalizesOnlySafeRestorableAddresses() {
    QCOMPARE(normalizeBrowserSessionUrl(
                 QStringLiteral(" HTTPS://Example.com/a?q=secret#part ")),
             QStringLiteral("https://example.com/a?q=secret#part"));
    QVERIFY(normalizeBrowserSessionUrl(
                QStringLiteral("https://user:pass@example.com/private"))
                .isEmpty());
    QVERIFY(normalizeBrowserSessionUrl(QStringLiteral("file:///C:/private"))
                .isEmpty());
}

void BrowserSessionStoreTest::encryptsRoundTripsAndClearsSession() {
#ifndef Q_OS_WIN
    QSKIP("DPAPI 会话存储只在 Windows 上提供");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("session.bin"));
    DpapiBrowserSessionStore store(path);
    BrowserSessionState state;
    state.tabs = {
        {QStringLiteral("https://example.com/watch?token=sensitive#position"),
         QStringLiteral("播放页"), QStringLiteral("media"), true, true, 1.25},
        {QStringLiteral("javascript:alert(1)"), QStringLiteral("危险"), {},
         false, false, 1.0}};
    state.closedTabs = {{QStringLiteral("https://closed.example/path"),
                         QStringLiteral("已关闭"), {}, false, false, 8.0}};
    for (int index = 1; index < 25; ++index) {
        state.closedTabs.append(
            {QStringLiteral("https://closed%1.example/path").arg(index),
             QStringLiteral("已关闭 %1").arg(index), {}, false, false,
             1.0});
    }
    state.currentIndex = 99;
    state.groups = {{QStringLiteral("media"), QStringLiteral("媒体"),
                     QStringLiteral("#3d8f72"), true},
                    {QStringLiteral("media"), QStringLiteral("重复"),
                     QStringLiteral("#ffffff"), false},
                    {QString{}, QStringLiteral("无效"), QString{}, false}};

    QVERIFY(store.save(state));
    QFile encryptedFile(path);
    QVERIFY(encryptedFile.open(QIODevice::ReadOnly));
    const QByteArray encrypted = encryptedFile.readAll();
    QVERIFY(encrypted.startsWith("MHBS1"));
    QVERIFY(!encrypted.contains("example.com"));
    QVERIFY(!encrypted.contains("sensitive"));
    encryptedFile.close();

    const std::optional<BrowserSessionState> loaded = store.load();
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->tabs.size(), 1);
    QCOMPARE(loaded->tabs.at(0).url,
             QStringLiteral("https://example.com/watch?token=sensitive#position"));
    QCOMPARE(loaded->currentIndex, 0);
    QCOMPARE(loaded->closedTabs.size(), 20);
    QCOMPARE(loaded->closedTabs.at(0).zoomFactor, 5.0);
    QCOMPARE(loaded->closedTabs.constLast().url,
             QStringLiteral("https://closed19.example/path"));
    QCOMPARE(loaded->groups.size(), 1);
    QCOMPARE(loaded->groups.at(0).id, QStringLiteral("media"));
    QCOMPARE(loaded->groups.at(0).name, QStringLiteral("媒体"));
    QCOMPARE(loaded->groups.at(0).color, QStringLiteral("#3d8f72"));
    QVERIFY(loaded->groups.at(0).isCollapsed);

    QVERIFY(store.clear());
    QVERIFY(!QFile::exists(path));
#endif
}

void BrowserSessionStoreTest::rejectsCorruptedOrOversizedSessionFiles() {
#ifndef Q_OS_WIN
    QSKIP("DPAPI 会话存储只在 Windows 上提供");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("session.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("MHBS1broken"), 11);
    file.close();

    DpapiBrowserSessionStore store(path);
    QVERIFY(!store.load().has_value());

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(QByteArray(2 * 1024 * 1024 + 1, 'x')),
             2 * 1024 * 1024 + 1);
    file.close();
    QVERIFY(!store.load().has_value());
#endif
}

}  // namespace
}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserSessionStoreTest)

#include "browser_session_store_test.moc"
