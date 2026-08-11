#include <QApplication>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <cstdlib>
#include <exception>
#include <iostream>

#include "app_state_store.h"
#include "browser_profile_directory.h"
#include "engine_event_bridge.h"
#include "main_window.h"
#include "mediahub/browser_webview2/webview2_browser_backend.h"
#include "mediahub/engine_vlc/vlc_player_engine.h"
#include "player_presenter.h"
#include "shutdown_watchdog.h"

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  QApplication::setApplicationName(QStringLiteral("MediaHub"));
  QApplication::setOrganizationName(QStringLiteral("MediaHub"));

  mediahub::logging::Logger logger(std::clog);
  logger.log(mediahub::logging::LogLevel::Info, "application", "started");
  try {
    int exitCode = EXIT_FAILURE;
    mediahub::gui::ShutdownWatchdog shutdownWatchdog;
    {
      mediahub::engine_vlc::VlcPlayerEngine engine;
      logger.log(mediahub::logging::LogLevel::Info, "engine", "initialized");
      mediahub::browser_webview2::WebView2BrowserBackend browserBackend(&logger);
      const QString browserProfileDirectory =
          mediahub::gui::makeBrowserProfileDirectory(
              QStandardPaths::writableLocation(
                  QStandardPaths::AppLocalDataLocation));
      mediahub::gui::EngineEventBridge eventBridge;
      mediahub::gui::MainWindow mainWindow(&browserBackend,
                                           browserProfileDirectory);
      mediahub::gui::QSettingsAppStateStore appStateStore;
      QObject::connect(&mainWindow, &mediahub::gui::MainWindow::closing,
                       &mainWindow,
                       [&shutdownWatchdog] { shutdownWatchdog.arm(); });
      mediahub::gui::PlayerPresenter presenter(engine, eventBridge, mainWindow,
                                               nullptr, &logger, nullptr,
                                               nullptr, &appStateStore);
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
    shutdownWatchdog.complete();
    logger.log(mediahub::logging::LogLevel::Info, "application", "stopped",
               {{"exit_code", std::to_string(exitCode)}});
    return exitCode;
  } catch (const std::exception& error) {
    logger.log(mediahub::logging::LogLevel::Error, "application",
               "startup_failed", {{"detail", error.what()}});
    QMessageBox::critical(nullptr, QStringLiteral("MediaHub 启动失败"),
                          QString::fromUtf8(error.what()));
    return EXIT_FAILURE;
  }
}
