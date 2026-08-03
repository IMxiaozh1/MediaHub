#include "main_window.h"

namespace mediahub::gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("MediaHub"));
    resize(960, 600);
}

}
