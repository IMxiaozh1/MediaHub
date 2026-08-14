#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

#include "browser_permission_dialog.h"
#include "browser_permission_store.h"
#include "ui_theme.h"

namespace mediahub::gui {

class BrowserPermissionTest final : public QObject {
    Q_OBJECT

 private slots:
    void normalizesOnlyHttpOrigins();
    void persistsAndQueriesPermissionStates();
    void neverPersistsScreenCaptureAllow();
    void keepsOldStateWhenAtomicWriteFails();
    void searchesEditsAndDeletesEntries();
    void usesBrowserAuxiliaryTheme();
};

void BrowserPermissionTest::normalizesOnlyHttpOrigins() {
    QCOMPARE(BrowserPermissionStore::normalizeOrigin(
                 QStringLiteral(" HTTPS://Example.COM:443/private?q=secret#token ")),
             QStringLiteral("https://example.com"));
    QCOMPARE(BrowserPermissionStore::normalizeOrigin(
                 QStringLiteral("http://Example.COM:8080/private")),
             QStringLiteral("http://example.com:8080"));
    QVERIFY(BrowserPermissionStore::normalizeOrigin(
                QStringLiteral("https://user:secret@example.com/path"))
                .isEmpty());
    QVERIFY(BrowserPermissionStore::normalizeOrigin(
                QStringLiteral("file:///C:/private.txt"))
                .isEmpty());
    QVERIFY(BrowserPermissionStore::normalizeOrigin(
                QStringLiteral("javascript:alert(1)"))
                .isEmpty());
    QVERIFY(BrowserPermissionStore::normalizeOrigin(
                QStringLiteral("https:///missing-host"))
                .isEmpty());
}

void BrowserPermissionTest::persistsAndQueriesPermissionStates() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(
        QStringLiteral("permissions.json"));
    BrowserPermissionStore writer(path);

    QVERIFY(writer.set(QStringLiteral("https://Example.com/private?token=x"),
                       BrowserPermissionKind::Camera,
                       BrowserPermissionState::Allow));
    QVERIFY(writer.set(QStringLiteral("https://example.com/another"),
                       BrowserPermissionKind::Microphone,
                       BrowserPermissionState::Block));
    QVERIFY(writer.set(QStringLiteral("https://notify.example"),
                       BrowserPermissionKind::Notifications,
                       BrowserPermissionState::Allow));

    BrowserPermissionStore reader(path);
    QCOMPARE(reader.entries().size(), 3);
    QCOMPARE(reader.stateFor(QStringLiteral("https://example.com/ignored"),
                             BrowserPermissionKind::Camera),
             BrowserPermissionState::Allow);
    QCOMPARE(reader.stateFor(QStringLiteral("https://example.com"),
                             BrowserPermissionKind::Microphone),
             BrowserPermissionState::Block);
    QCOMPARE(reader.stateFor(QStringLiteral("https://missing.example"),
                             BrowserPermissionKind::Camera),
             BrowserPermissionState::Ask);
    QVERIFY(reader.set(QStringLiteral("https://notify.example"),
                       BrowserPermissionKind::Notifications,
                       BrowserPermissionState::Ask));
    QCOMPARE(reader.entries().size(), 2);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    file.close();
    QVERIFY(!bytes.contains("private"));
    QVERIFY(!bytes.contains("token"));
    QVERIFY(!bytes.contains("secret"));

    QVERIFY(reader.clear());
    QVERIFY(reader.entries().isEmpty());
    BrowserPermissionStore cleared(path);
    QVERIFY(cleared.entries().isEmpty());
}

void BrowserPermissionTest::neverPersistsScreenCaptureAllow() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    BrowserPermissionStore store(
        temporaryDirectory.filePath(QStringLiteral("permissions.json")));

    QVERIFY(!store.set(QStringLiteral("https://capture.example"),
                       BrowserPermissionKind::ScreenCapture,
                       BrowserPermissionState::Allow));
    QVERIFY(store.set(QStringLiteral("https://capture.example"),
                      BrowserPermissionKind::ScreenCapture,
                      BrowserPermissionState::Block));
    QCOMPARE(store.stateFor(QStringLiteral("https://capture.example/path"),
                            BrowserPermissionKind::ScreenCapture),
             BrowserPermissionState::Block);
    QVERIFY(!store.set(QStringLiteral("https://unknown.example"),
                       BrowserPermissionKind::Other,
                       BrowserPermissionState::Block));
}

void BrowserPermissionTest::keepsOldStateWhenAtomicWriteFails() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(
        QStringLiteral("permissions.json"));
    BrowserPermissionStore store(path);
    QVERIFY(store.set(QStringLiteral("https://safe.example"),
                      BrowserPermissionKind::Camera,
                      BrowserPermissionState::Block));

    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkdir(path));
    QVERIFY(!store.set(QStringLiteral("https://safe.example"),
                       BrowserPermissionKind::Camera,
                       BrowserPermissionState::Allow));
    QCOMPARE(store.stateFor(QStringLiteral("https://safe.example"),
                            BrowserPermissionKind::Camera),
             BrowserPermissionState::Block);
    QVERIFY(!store.clear());
    QCOMPARE(store.entries().size(), 1);
}

void BrowserPermissionTest::searchesEditsAndDeletesEntries() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    BrowserPermissionStore store(
        temporaryDirectory.filePath(QStringLiteral("permissions.json")));
    QVERIFY(store.set(QStringLiteral("https://camera.example/private"),
                      BrowserPermissionKind::Camera,
                      BrowserPermissionState::Allow));
    QVERIFY(store.set(QStringLiteral("https://voice.example/private"),
                      BrowserPermissionKind::Microphone,
                      BrowserPermissionState::Allow));
    QVERIFY(store.set(QStringLiteral("https://capture.example/private"),
                      BrowserPermissionKind::ScreenCapture,
                      BrowserPermissionState::Block));

    BrowserPermissionManagementDialog dialog(store);
    QCOMPARE(dialog.visibleEntryCount(), 3);
    auto* const search = dialog.findChild<QLineEdit*>(
        QStringLiteral("browserPermissionSearchEdit"));
    auto* const table = dialog.findChild<QTableWidget*>(
        QStringLiteral("browserPermissionTable"));
    auto* const combo = dialog.findChild<QComboBox*>(
        QStringLiteral("browserPermissionStateCombo"));
    auto* const save = dialog.findChild<QPushButton*>(
        QStringLiteral("browserPermissionSaveButton"));
    auto* const remove = dialog.findChild<QPushButton*>(
        QStringLiteral("browserPermissionRemoveButton"));
    QVERIFY(search != nullptr);
    QVERIFY(table != nullptr);
    QVERIFY(combo != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(remove != nullptr);

    search->setText(QStringLiteral("麦克风"));
    QCOMPARE(dialog.visibleEntryCount(), 1);
    int visibleRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->isRowHidden(row)) {
            visibleRow = row;
            break;
        }
    }
    QVERIFY(visibleRow >= 0);
    table->selectRow(visibleRow);
    combo->setCurrentIndex(combo->findData(
        static_cast<int>(BrowserPermissionState::Block)));
    save->click();
    QCOMPARE(store.stateFor(QStringLiteral("https://voice.example/secret"),
                            BrowserPermissionKind::Microphone),
             BrowserPermissionState::Block);
    QCOMPARE(dialog.statusText(), QStringLiteral("权限设置已更新。"));

    search->setText(QStringLiteral("capture.example"));
    QCOMPARE(dialog.visibleEntryCount(), 1);
    visibleRow = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (!table->isRowHidden(row)) {
            visibleRow = row;
            break;
        }
    }
    QVERIFY(visibleRow >= 0);
    table->selectRow(visibleRow);
    remove->click();
    QCOMPARE(store.stateFor(QStringLiteral("https://capture.example"),
                            BrowserPermissionKind::ScreenCapture),
             BrowserPermissionState::Ask);
    QCOMPARE(dialog.statusText(), QStringLiteral("权限记录已删除。"));
    QVERIFY(!dialog.statusText().contains(QStringLiteral("capture.example")));
}

void BrowserPermissionTest::usesBrowserAuxiliaryTheme() {
    const QString& styleSheet = mainWindowStyleSheet();
    QVERIFY(styleSheet.contains(
        QStringLiteral("#browserPermissionManagementDialog")));
    QVERIFY(styleSheet.contains(QStringLiteral("#browserPermissionSearchEdit")));
    QVERIFY(styleSheet.contains(QStringLiteral("#browserPermissionTable")));
    QVERIFY(styleSheet.contains(
        QStringLiteral("#browserPermissionStateCombo")));
    QVERIFY(styleSheet.contains(
        QStringLiteral("#browserPermissionSaveButton")));
    QVERIFY(styleSheet.contains(
        QStringLiteral("#browserPermissionRemoveButton")));
}

}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserPermissionTest)

#include "browser_permission_test.moc"
