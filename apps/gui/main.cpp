#include <QApplication>
#include <QMessageBox>
#include <QTimer>

#include "engine_event_bridge.h"
#include "main_window.h"
#include "mediahub/engine_vlc/vlc_player_engine.h"
#include "player_presenter.h"

#include <cstdlib>
#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MediaHub"));
    QApplication::setOrganizationName(QStringLiteral("MediaHub"));

    mediahub::logging::Logger logger(std::clog);
    logger.log(mediahub::logging::LogLevel::Info, "application", "started");
    try {
        int exitCode = EXIT_FAILURE;
        {
            mediahub::engine_vlc::VlcPlayerEngine engine;
            logger.log(mediahub::logging::LogLevel::Info, "engine", "initialized");
            mediahub::gui::EngineEventBridge eventBridge;
            mediahub::gui::MainWindow mainWindow;
            mediahub::gui::PlayerPresenter presenter(
                engine, eventBridge, mainWindow, nullptr, &logger);
            mainWindow.show();
            const QStringList arguments = application.arguments();
            if (arguments.size() > 1) {
                const QStringList initialMediaPaths = arguments.mid(1);
                QTimer::singleShot(0, &presenter, [&presenter, initialMediaPaths] {
                    presenter.addLocalFiles(initialMediaPaths);
                });
            }

            exitCode = application.exec();
        }
        logger.log(mediahub::logging::LogLevel::Info,
                   "application",
                   "stopped",
                   {{"exit_code", std::to_string(exitCode)}});
        return exitCode;
    } catch (const std::exception& error) {
        logger.log(mediahub::logging::LogLevel::Error,
                   "application",
                   "startup_failed",
                   {{"detail", error.what()}});
        QMessageBox::critical(nullptr,
                              QStringLiteral("MediaHub 启动失败"),
                              QString::fromUtf8(error.what()));
        return EXIT_FAILURE;
    }
}
