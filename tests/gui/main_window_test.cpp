#include "main_window.h"

#include <QCoreApplication>
#include <QTest>

namespace mediahub::gui {

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void hasExpectedInitialState();
    void canBeShownAndClosed();
};

void MainWindowTest::hasExpectedInitialState() {
    MainWindow window;

    QCOMPARE(window.windowTitle(), QStringLiteral("MediaHub"));
    QCOMPARE(window.size(), QSize(960, 600));
    QVERIFY(!window.isVisible());
}

void MainWindowTest::canBeShownAndClosed() {
    MainWindow window;

    window.show();
    QCoreApplication::processEvents();
    QVERIFY(window.isVisible());

    window.close();
    QCoreApplication::processEvents();
    QVERIFY(!window.isVisible());
}

}

QTEST_MAIN(mediahub::gui::MainWindowTest)

#include "main_window_test.moc"
