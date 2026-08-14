#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

#include "browser_tab_group_dialog.h"
#include "ui_theme.h"

namespace mediahub::gui {
namespace {

class BrowserTabGroupDialogTest final : public QObject {
    Q_OBJECT

 private slots:
    void createsRenamesRecolorsAndTogglesGroup();
    void removesGroupWithStableIdSignal();
    void rejectsInvalidCreationAndUsesBrowserTheme();
};

void BrowserTabGroupDialogTest::createsRenamesRecolorsAndTogglesGroup() {
    BrowserTabGroupModel model;
    BrowserTabGroupDialog dialog(model);
    auto* const list = dialog.findChild<QListWidget*>(
        QStringLiteral("browserTabGroupList"));
    auto* const name = dialog.findChild<QLineEdit*>(
        QStringLiteral("browserTabGroupNameEdit"));
    auto* const colors = dialog.findChild<QComboBox*>(
        QStringLiteral("browserTabGroupColorCombo"));
    QVERIFY(list != nullptr);
    QVERIFY(name != nullptr);
    QVERIFY(colors != nullptr);
    QCOMPARE(colors->count(), 6);

    QSignalSpy changed(&dialog, &BrowserTabGroupDialog::groupsChanged);
    name->setText(QStringLiteral("  工作   标签  "));
    colors->setCurrentIndex(colors->findData(QStringLiteral("#256f62")));
    QTest::mouseClick(dialog.findChild<QPushButton*>(
                          QStringLiteral("browserTabGroupCreateButton")),
                      Qt::LeftButton);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.groups().size(), 1);
    QCOMPARE(model.groups().constFirst().name, QStringLiteral("工作 标签"));
    QCOMPARE(list->count(), 1);
    const QString id = model.groups().constFirst().id;

    name->setText(QStringLiteral("直播"));
    QTest::mouseClick(dialog.findChild<QPushButton*>(
                          QStringLiteral("browserTabGroupRenameButton")),
                      Qt::LeftButton);
    QCOMPARE(model.find(id)->name, QStringLiteral("直播"));

    colors->setCurrentIndex(colors->findData(QStringLiteral("#d97745")));
    QTest::mouseClick(dialog.findChild<QPushButton*>(
                          QStringLiteral("browserTabGroupRecolorButton")),
                      Qt::LeftButton);
    QCOMPARE(model.find(id)->color, QStringLiteral("#d97745"));

    auto* const toggle = dialog.findChild<QPushButton*>(
        QStringLiteral("browserTabGroupToggleCollapsedButton"));
    QTest::mouseClick(toggle, Qt::LeftButton);
    QVERIFY(model.find(id)->isCollapsed);
    QCOMPARE(toggle->text(), QStringLiteral("展开分组"));
    QTest::mouseClick(toggle, Qt::LeftButton);
    QVERIFY(!model.find(id)->isCollapsed);
    QCOMPARE(changed.count(), 5);
}

void BrowserTabGroupDialogTest::removesGroupWithStableIdSignal() {
    BrowserTabGroupModel model;
    model.replace({{QStringLiteral("work"), QStringLiteral("工作"),
                    QStringLiteral("#256f62"), false}});
    BrowserTabGroupDialog dialog(model);
    QSignalSpy changed(&dialog, &BrowserTabGroupDialog::groupsChanged);
    QSignalSpy removed(&dialog, &BrowserTabGroupDialog::groupRemoved);

    QTest::mouseClick(dialog.findChild<QPushButton*>(
                          QStringLiteral("browserTabGroupRemoveButton")),
                      Qt::LeftButton);

    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.constFirst().constFirst().toString(),
             QStringLiteral("work"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(model.groups().isEmpty());
    QVERIFY(!dialog.findChild<QPushButton*>(
                          QStringLiteral("browserTabGroupRemoveButton"))
                 ->isEnabled());
}

void BrowserTabGroupDialogTest::rejectsInvalidCreationAndUsesBrowserTheme() {
    BrowserTabGroupModel model;
    BrowserTabGroupDialog dialog(model);
    QSignalSpy changed(&dialog, &BrowserTabGroupDialog::groupsChanged);

    QTest::mouseClick(dialog.findChild<QPushButton*>(
                          QStringLiteral("browserTabGroupCreateButton")),
                      Qt::LeftButton);

    QCOMPARE(changed.count(), 0);
    QVERIFY(model.groups().isEmpty());
    auto* const status = dialog.findChild<QLabel*>(
        QStringLiteral("browserTabGroupStatusLabel"));
    QVERIFY(!status->isHidden());
    QCOMPARE(status->property("status").toString(), QStringLiteral("error"));

    const QString& styleSheet = mainWindowStyleSheet();
    QVERIFY(styleSheet.contains(QStringLiteral("#browserTabGroupDialog")));
    QVERIFY(styleSheet.contains(QStringLiteral("#browserTabGroupList")));
    QVERIFY(styleSheet.contains(QStringLiteral("#browserTabGroupNameEdit")));
    QVERIFY(styleSheet.contains(QStringLiteral("#browserTabGroupColorCombo")));
    QVERIFY(styleSheet.contains(QStringLiteral("#browserTabGroupRemoveButton")));
}

}  // namespace
}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserTabGroupDialogTest)

#include "browser_tab_group_dialog_test.moc"
