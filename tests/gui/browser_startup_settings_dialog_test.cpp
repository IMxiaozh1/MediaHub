#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

#include "browser_startup_settings_dialog.h"

namespace mediahub::gui {
namespace {

class BrowserStartupSettingsDialogTest final : public QObject {
    Q_OBJECT

 private slots:
    void editsHomeStartupModeAndOrderedPages();
};

void BrowserStartupSettingsDialogTest::editsHomeStartupModeAndOrderedPages() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettingsBrowserStartupSettingsStore store(
        directory.filePath(QStringLiteral("settings.ini")));
    BrowserStartupSettingsDialog dialog(store);
    dialog.setCurrentTabUrls(
        {QStringLiteral("https://one.example/a?private=1"),
         QStringLiteral("https://two.example/b#part")},
        1);

    auto* const home =
        dialog.findChild<QLineEdit*>(QStringLiteral("browserHomeUrlEdit"));
    auto* const mode = dialog.findChild<QComboBox*>(
        QStringLiteral("browserStartupModeCombo"));
    auto* const list = dialog.findChild<QListWidget*>(
        QStringLiteral("browserStartupUrlsList"));
    auto* const tabLimit = dialog.findChild<QSpinBox*>(
        QStringLiteral("browserMaximumTabCountSpin"));
    QVERIFY(home != nullptr);
    QVERIFY(mode != nullptr);
    QVERIFY(list != nullptr);
    QVERIFY(tabLimit != nullptr);

    home->setText(QStringLiteral("https://home.example/path?secret=x"));
    tabLimit->setValue(35);
    mode->setCurrentIndex(2);
    QTest::mouseClick(dialog.findChild<QPushButton*>(
                          QStringLiteral("browserStartupAddCurrentButton")),
                      Qt::LeftButton);
    QTest::mouseClick(dialog.findChild<QPushButton*>(
                          QStringLiteral("browserStartupAddAllButton")),
                      Qt::LeftButton);
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->text(), QStringLiteral("https://two.example/b"));
    QCOMPARE(list->item(1)->text(), QStringLiteral("https://one.example/a"));
    list->setCurrentRow(1);
    QTest::mouseClick(dialog.findChild<QPushButton*>(
                          QStringLiteral("browserStartupMoveUpButton")),
                      Qt::LeftButton);
    QCOMPARE(list->item(0)->text(), QStringLiteral("https://one.example/a"));

    QSignalSpy saved(&dialog, &BrowserStartupSettingsDialog::settingsSaved);
    QTest::mouseClick(dialog.findChild<QPushButton*>(
                          QStringLiteral("browserStartupSaveButton")),
                      Qt::LeftButton);
    QCOMPARE(saved.count(), 1);
    const BrowserStartupSettings settings = store.load();
    QCOMPARE(settings.homeUrl, QStringLiteral("https://home.example/path"));
    QCOMPARE(settings.maximumTabCount, 35);
    QCOMPARE(settings.mode, BrowserStartupMode::OpenStartupPages);
    QCOMPARE(settings.startupUrls.size(), 2);
    QCOMPARE(settings.startupUrls.at(0),
             QStringLiteral("https://one.example/a"));
}

}  // namespace
}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserStartupSettingsDialogTest)

#include "browser_startup_settings_dialog_test.moc"
