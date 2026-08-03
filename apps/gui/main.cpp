#include <QApplication>

#include "main_window.h"

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    mediahub::gui::MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
