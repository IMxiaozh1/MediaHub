#include "browser_bookmark_html.h"

#include <QRegularExpression>
#include <QSet>
#include <QTextDocumentFragment>

#include <algorithm>

namespace mediahub::gui {
namespace {

constexpr qsizetype kMaximumBookmarkHtmlBytes = 8 * 1024 * 1024;
constexpr int kMaximumImportedFavorites = 5000;

QString plainTextFromHtml(const QString& html) {
    return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

QString capturedAttributeValue(const QRegularExpressionMatch& match) {
    for (int index = 1; index <= 3; ++index) {
        if (match.capturedStart(index) >= 0) {
            return plainTextFromHtml(match.captured(index));
        }
    }
    return {};
}

QString followingDescription(const QString& source, const qsizetype anchorEnd,
                             const qsizetype nextAnchorStart) {
    const QString following = source.mid(
        anchorEnd, std::max<qsizetype>(0, nextAnchorStart - anchorEnd));
    static const QRegularExpression descriptionPattern(
        QStringLiteral("<dd\\b[^>]*>([\\s\\S]*?)(?=<(?:dt|dl|p|h[1-6]|/dl)\\b|$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = descriptionPattern.match(following);
    return match.hasMatch() ? plainTextFromHtml(match.captured(1)) : QString{};
}

}  // namespace

BrowserBookmarkImportResult importBrowserBookmarksHtml(const QByteArray& html) {
    BrowserBookmarkImportResult result;
    if (html.size() > kMaximumBookmarkHtmlBytes) {
        result.isInputTooLarge = true;
        return result;
    }

    const QString source = QString::fromUtf8(html);
    static const QRegularExpression anchorPattern(
        QStringLiteral("<a\\b([^>]*)>([\\s\\S]*?)</a\\s*>"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression hrefPattern(
        QStringLiteral("\\bhref\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"),
        QRegularExpression::CaseInsensitiveOption);

    QVector<QRegularExpressionMatch> anchors;
    auto iterator = anchorPattern.globalMatch(source);
    while (iterator.hasNext()) {
        anchors.append(iterator.next());
    }

    QSet<QString> knownUrls;
    result.favorites.reserve(
        std::min(anchors.size(), kMaximumImportedFavorites));
    for (int index = 0; index < anchors.size(); ++index) {
        const QRegularExpressionMatch& anchor = anchors.at(index);
        const QRegularExpressionMatch href =
            hrefPattern.match(anchor.captured(1));
        const QString normalizedUrl =
            normalizeStoredBrowserUrl(capturedAttributeValue(href));
        if (!href.hasMatch() || normalizedUrl.isEmpty() ||
            knownUrls.contains(normalizedUrl) ||
            result.favorites.size() == kMaximumImportedFavorites) {
            ++result.rejectedEntries;
            continue;
        }

        const qsizetype nextAnchorStart =
            index + 1 < anchors.size() ? anchors.at(index + 1).capturedStart()
                                       : source.size();
        result.favorites.append(BrowserFavoriteEntry{
            normalizedUrl, plainTextFromHtml(anchor.captured(2)),
            followingDescription(source, anchor.capturedEnd(),
                                 nextAnchorStart)});
        knownUrls.insert(normalizedUrl);
    }
    return result;
}

QByteArray exportBrowserBookmarksHtml(
    const QVector<BrowserFavoriteEntry>& favorites) {
    QString html = QStringLiteral(
        "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
        "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
        "<TITLE>MediaHub 收藏夹</TITLE>\n"
        "<H1>MediaHub 收藏夹</H1>\n"
        "<DL><p>\n");
    QSet<QString> knownUrls;
    int exportedCount = 0;
    for (const BrowserFavoriteEntry& favorite : favorites) {
        const QString normalizedUrl = normalizeStoredBrowserUrl(favorite.url);
        if (normalizedUrl.isEmpty() || knownUrls.contains(normalizedUrl) ||
            exportedCount == kMaximumImportedFavorites) {
            continue;
        }
        knownUrls.insert(normalizedUrl);
        const QString title = favorite.title.trimmed().isEmpty()
                                  ? normalizedUrl
                                  : favorite.title.trimmed();
        html += QStringLiteral("    <DT><A HREF=\"%1\">%2</A>\n")
                    .arg(normalizedUrl.toHtmlEscaped(), title.toHtmlEscaped());
        if (!favorite.note.trimmed().isEmpty()) {
            html += QStringLiteral("    <DD>%1\n")
                        .arg(favorite.note.trimmed().toHtmlEscaped());
        }
        ++exportedCount;
    }
    html += QStringLiteral("</DL><p>\n");
    return html.toUtf8();
}

}  // namespace mediahub::gui
