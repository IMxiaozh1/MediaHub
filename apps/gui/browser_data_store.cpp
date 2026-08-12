#include "browser_data_store.h"

#include <QSet>
#include <QSettings>
#include <QUrl>

#include <algorithm>

namespace mediahub::gui {
namespace {

constexpr int kBrowserDataVersion = 1;
constexpr int kMaximumHistoryEntries = 500;

QString normalizedStoredUrl(const QString& value) {
    const QUrl parsed(value.trimmed(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || (scheme != QStringLiteral("https") &&
                              scheme != QStringLiteral("http")) ||
        parsed.host().isEmpty() || !parsed.userName().isEmpty() ||
        !parsed.password().isEmpty()) {
        return {};
    }
    QUrl normalized = parsed;
    normalized.setScheme(scheme);
    normalized.setHost(parsed.host().toLower());
    return normalized.toString(QUrl::FullyEncoded);
}

QVector<BrowserHistoryEntry> normalizedHistory(
    const QVector<BrowserHistoryEntry>& history) {
    QVector<BrowserHistoryEntry> normalized;
    normalized.reserve(std::min(history.size(), kMaximumHistoryEntries));
    QSet<QString> knownUrls;
    for (const BrowserHistoryEntry& entry : history) {
        const QString url = normalizedStoredUrl(entry.url);
        if (url.isEmpty() || knownUrls.contains(url)) {
            continue;
        }
        knownUrls.insert(url);
        normalized.append(
            BrowserHistoryEntry{url, entry.title.trimmed(),
                                std::max<qint64>(0, entry.visitedAtMilliseconds)});
        if (normalized.size() == kMaximumHistoryEntries) {
            break;
        }
    }
    return normalized;
}

}  // namespace

QSettingsBrowserDataStore::QSettingsBrowserDataStore()
    : settings_(std::make_unique<QSettings>()) {}

QSettingsBrowserDataStore::QSettingsBrowserDataStore(
    const QString& settingsFilePath)
    : settings_(
          std::make_unique<QSettings>(settingsFilePath, QSettings::IniFormat)) {}

QSettingsBrowserDataStore::~QSettingsBrowserDataStore() = default;

QVector<BrowserHistoryEntry> QSettingsBrowserDataStore::loadHistory() {
    QVector<BrowserHistoryEntry> history;
    settings_->beginGroup(QStringLiteral("browserData"));
    if (settings_->value(QStringLiteral("version"), 0).toInt() !=
        kBrowserDataVersion) {
        settings_->endGroup();
        return history;
    }
    const int count = settings_->beginReadArray(QStringLiteral("history"));
    history.reserve(std::min(count, kMaximumHistoryEntries));
    for (int index = 0; index < count; ++index) {
        settings_->setArrayIndex(index);
        history.append(BrowserHistoryEntry{
            settings_->value(QStringLiteral("url")).toString(),
            settings_->value(QStringLiteral("title")).toString(),
            settings_->value(QStringLiteral("visitedAtMilliseconds"), 0)
                .toLongLong()});
    }
    settings_->endArray();
    settings_->endGroup();
    return normalizedHistory(history);
}

void QSettingsBrowserDataStore::saveHistory(
    const QVector<BrowserHistoryEntry>& history) {
    const QVector<BrowserHistoryEntry> normalized = normalizedHistory(history);
    settings_->beginGroup(QStringLiteral("browserData"));
    settings_->setValue(QStringLiteral("version"), kBrowserDataVersion);
    settings_->remove(QStringLiteral("history"));
    settings_->beginWriteArray(QStringLiteral("history"));
    for (int index = 0; index < normalized.size(); ++index) {
        const BrowserHistoryEntry& entry = normalized.at(index);
        settings_->setArrayIndex(index);
        settings_->setValue(QStringLiteral("url"), entry.url);
        settings_->setValue(QStringLiteral("title"), entry.title);
        settings_->setValue(QStringLiteral("visitedAtMilliseconds"),
                            entry.visitedAtMilliseconds);
    }
    settings_->endArray();
    settings_->endGroup();
    settings_->sync();
}

}  // namespace mediahub::gui
