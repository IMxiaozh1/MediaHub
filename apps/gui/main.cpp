#include <QApplication>
#include <QMessageBox>
#include <QTimer>

#include "engine_event_bridge.h"
#include "main_window.h"
#include "mediahub/engine_vlc/vlc_player_engine.h"
#include "player_presenter.h"

#include <cstdlib>
#include <exception>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MediaHub"));
    QApplication::setOrganizationName(QStringLiteral("MediaHub"));

    try {
        mediahub::engine_vlc::VlcPlayerEngine engine;
        mediahub::gui::EngineEventBridge eventBridge;
        mediahub::gui::MainWindow mainWindow;
        mediahub::gui::PlayerPresenter presenter(engine, eventBridge, mainWindow);
        mainWindow.show();
        const QStringList arguments = application.arguments();
        if (arguments.size() > 1) {
            const QStringList initialMediaPaths = arguments.mid(1);
            QTimer::singleShot(0, &presenter, [&presenter, initialMediaPaths] {
                presenter.addLocalFiles(initialMediaPaths);
            });
        }

        return application.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr,
                              QStringLiteral("MediaHub 启动失败"),
                              QString::fromUtf8(error.what()));
        return EXIT_FAILURE;
    }
}
