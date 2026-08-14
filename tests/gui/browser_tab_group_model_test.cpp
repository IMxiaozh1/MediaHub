#include <QTest>

#include "browser_tab_group_model.h"

namespace mediahub::gui {
namespace {

class BrowserTabGroupModelTest final : public QObject {
    Q_OBJECT

 private slots:
    void createsAndUpdatesStableGroups();
    void rejectsInvalidAndDuplicateRestoredGroups();
};

void BrowserTabGroupModelTest::createsAndUpdatesStableGroups() {
    BrowserTabGroupModel model;
    const std::optional<QString> id =
        model.create(QStringLiteral("  工作   标签  "), QStringLiteral("#3D8F72"));
    QVERIFY(id.has_value());
    QCOMPARE(model.groups().size(), 1);
    QCOMPARE(model.groups().at(0).name, QStringLiteral("工作 标签"));
    QCOMPARE(model.groups().at(0).color, QStringLiteral("#3d8f72"));

    QVERIFY(model.rename(*id, QStringLiteral("直播")));
    QVERIFY(model.setColor(*id, QStringLiteral("#d97745")));
    QVERIFY(model.setCollapsed(*id, true));
    QCOMPARE(model.find(*id)->name, QStringLiteral("直播"));
    QCOMPARE(model.find(*id)->color, QStringLiteral("#d97745"));
    QVERIFY(model.find(*id)->isCollapsed);

    QVERIFY(model.remove(*id));
    QVERIFY(model.groups().isEmpty());
}

void BrowserTabGroupModelTest::rejectsInvalidAndDuplicateRestoredGroups() {
    BrowserTabGroupModel model;
    model.replace({{QStringLiteral("work"), QStringLiteral("工作"),
                    QStringLiteral("#256f62"), false},
                   {QStringLiteral("work"), QStringLiteral("重复"),
                    QStringLiteral("#ffffff"), false},
                   {QStringLiteral("invalid"), QStringLiteral("无效颜色"),
                    QStringLiteral("green"), false},
                   {QString{}, QStringLiteral("空 ID"),
                    QStringLiteral("#ffffff"), false}});
    QCOMPARE(model.groups().size(), 1);
    QCOMPARE(model.groups().at(0).id, QStringLiteral("work"));
    QVERIFY(!model.rename(QStringLiteral("missing"), QStringLiteral("名称")));
    QVERIFY(!model.setColor(QStringLiteral("work"), QStringLiteral("red")));
    QVERIFY(!model.create(QString{}, QStringLiteral("#ffffff")).has_value());
}

}  // namespace
}  // namespace mediahub::gui

QTEST_MAIN(mediahub::gui::BrowserTabGroupModelTest)

#include "browser_tab_group_model_test.moc"
