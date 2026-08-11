#include "browser_navigation_policy.h"

#include <QUrl>

namespace mediahub::gui {

BrowserAddress normalizeBrowserAddress(const QString& input) {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QString candidate = trimmed.contains(QStringLiteral(":"))
                                  ? trimmed
                                  : QStringLiteral("https://") + trimmed;
    const QUrl url(candidate, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (url.isValid() &&
        (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) &&
        !url.host().isEmpty()) {
        return {BrowserAddressKind::Web, url.toString(QUrl::FullyEncoded)};
    }
    if (url.isValid() && scheme == QStringLiteral("file") && url.isLocalFile()) {
        return {BrowserAddressKind::LocalFile, url.toString(QUrl::FullyEncoded)};
    }
    if (scheme == QStringLiteral("javascript") || scheme == QStringLiteral("data") ||
        scheme == QStringLiteral("edge") || scheme == QStringLiteral("devtools")) {
        return {BrowserAddressKind::Blocked, {}};
    }
    if (url.isValid() && !scheme.isEmpty()) {
        return {BrowserAddressKind::ExternalProtocol, url.toString(QUrl::FullyEncoded)};
    }
    return {};
}

}  // namespace mediahub::gui
