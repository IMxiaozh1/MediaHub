#include "browser_profile_directory.h"

#include <QDir>
#include <QFileInfo>

namespace mediahub::gui {

QString makeBrowserProfileDirectory(const QString& appLocalDataLocation) {
    if (appLocalDataLocation.isEmpty() ||
        !QFileInfo(appLocalDataLocation).isAbsolute()) {
        return {};
    }
    return QDir(appLocalDataLocation)
        .filePath(QStringLiteral("WebView2/Profile-v1"));
}

}  // namespace mediahub::gui
