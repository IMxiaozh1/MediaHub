#include "app_state_store.h"

#include <QSettings>
#include <QSet>
#include <algorithm>
#include <utility>

namespace mediahub::gui {
namespace {

constexpr int kStateVersion = 1;
constexpr int kMaximumLiveUrlHistory = 20;
constexpr int kMaximumFavoriteLiveSources = 5000;
constexpr int kMaximumRecentLocalMedia = 20;

QString fromUtf8(const std::string& value) {
  return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::string utf8String(const QString& value) {
  const QByteArray bytes = value.toUtf8();
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QStringList normalizedUniqueStrings(const QStringList& values,
                                    const int maximumSize) {
  QStringList normalized;
  normalized.reserve(std::min(values.size(), maximumSize));
  for (const QString& value : values) {
    const QString text = value.trimmed();
    if (text.isEmpty() || normalized.contains(text)) {
      continue;
    }
    normalized.append(text);
    if (normalized.size() == maximumSize) {
      break;
    }
  }
  return normalized;
}

std::vector<LocalPlaybackRecord> normalizedRecentLocalMedia(
    const std::vector<LocalPlaybackRecord>& records) {
  std::vector<LocalPlaybackRecord> normalized;
  normalized.reserve(
      std::min(records.size(), static_cast<std::size_t>(kMaximumRecentLocalMedia)));
  QSet<QString> knownSources;
  for (const LocalPlaybackRecord& record : records) {
    if (record.item.kind != core::MediaSourceKind::LocalFile ||
        record.item.source.empty() || record.item.displayName.empty()) {
      continue;
    }
    const QString source = fromUtf8(record.item.source).trimmed();
    if (source.isEmpty() || knownSources.contains(source)) {
      continue;
    }
    knownSources.insert(source);
    LocalPlaybackRecord value = record;
    value.positionMilliseconds = std::max<qint64>(0, value.positionMilliseconds);
    if (value.durationMilliseconds <= 0) {
      value.durationMilliseconds = -1;
    } else {
      value.positionMilliseconds =
          std::min(value.positionMilliseconds, value.durationMilliseconds);
    }
    normalized.push_back(std::move(value));
    if (normalized.size() == kMaximumRecentLocalMedia) {
      break;
    }
  }
  return normalized;
}

QVector<LiveSourceMemo> normalizedMemos(
    const QVector<LiveSourceMemo>& memos) {
  QVector<LiveSourceMemo> normalized;
  normalized.reserve(memos.size());
  for (const LiveSourceMemo& memo : memos) {
    const QString sourceUrl = memo.sourceUrl.trimmed();
    if (sourceUrl.isEmpty()) {
      continue;
    }
    normalized.append(LiveSourceMemo{sourceUrl, memo.note.trimmed()});
  }
  return normalized;
}

}  // namespace

QSettingsAppStateStore::QSettingsAppStateStore()
    : settings_(std::make_unique<QSettings>()) {}

QSettingsAppStateStore::QSettingsAppStateStore(
    const QString& settingsFilePath)
    : settings_(
          std::make_unique<QSettings>(settingsFilePath, QSettings::IniFormat)) {}

QSettingsAppStateStore::~QSettingsAppStateStore() = default;

AppStateSnapshot QSettingsAppStateStore::load() {
  AppStateSnapshot snapshot;
  settings_->beginGroup(QStringLiteral("playbackState"));
  if (settings_->value(QStringLiteral("version"), 0).toInt() !=
      kStateVersion) {
    settings_->endGroup();
    return snapshot;
  }

  const int itemCount = settings_->beginReadArray(QStringLiteral("localPlaylist"));
  snapshot.localPlaylist.reserve(static_cast<std::size_t>(itemCount));
  for (int index = 0; index < itemCount; ++index) {
    settings_->setArrayIndex(index);
    const QString source = settings_->value(QStringLiteral("source")).toString();
    const QString displayName =
        settings_->value(QStringLiteral("displayName")).toString();
    if (source.isEmpty() || displayName.isEmpty()) {
      continue;
    }
    snapshot.localPlaylist.push_back(
        core::MediaItem{utf8String(source), core::MediaSourceKind::LocalFile,
                        utf8String(displayName)});
  }
  settings_->endArray();
  const int recentCount =
      settings_->beginReadArray(QStringLiteral("recentLocalMedia"));
  snapshot.recentLocalMedia.reserve(
      std::min(recentCount, kMaximumRecentLocalMedia));
  for (int index = 0; index < recentCount; ++index) {
    settings_->setArrayIndex(index);
    const QString source = settings_->value(QStringLiteral("source")).toString();
    const QString displayName =
        settings_->value(QStringLiteral("displayName")).toString();
    if (source.isEmpty() || displayName.isEmpty()) {
      continue;
    }
    snapshot.recentLocalMedia.push_back(LocalPlaybackRecord{
        core::MediaItem{utf8String(source), core::MediaSourceKind::LocalFile,
                        utf8String(displayName)},
        settings_->value(QStringLiteral("positionMilliseconds"), 0)
            .toLongLong(),
        settings_->value(QStringLiteral("durationMilliseconds"), -1)
            .toLongLong()});
  }
  settings_->endArray();
  snapshot.recentLocalMedia =
      normalizedRecentLocalMedia(snapshot.recentLocalMedia);
  snapshot.lastLivePlaylistUrl =
      settings_->value(QStringLiteral("lastLivePlaylistUrl"))
          .toString()
          .trimmed();
  snapshot.livePlaylistUrlHistory = normalizedUniqueStrings(
      settings_->value(QStringLiteral("livePlaylistUrlHistory")).toStringList(),
      kMaximumLiveUrlHistory);
  snapshot.favoriteLiveSourceUrls = normalizedUniqueStrings(
      settings_->value(QStringLiteral("favoriteLiveSourceUrls")).toStringList(),
      kMaximumFavoriteLiveSources);
  const int memoCount =
      settings_->beginReadArray(QStringLiteral("liveSourceMemos"));
  snapshot.liveSourceMemos.reserve(memoCount);
  for (int index = 0; index < memoCount; ++index) {
    settings_->setArrayIndex(index);
    const QString sourceUrl =
        settings_->value(QStringLiteral("sourceUrl")).toString().trimmed();
    if (sourceUrl.isEmpty()) {
      continue;
    }
    snapshot.liveSourceMemos.append(
        LiveSourceMemo{sourceUrl,
                       settings_->value(QStringLiteral("note"))
                           .toString()
                           .trimmed()});
  }
  settings_->endArray();
  settings_->endGroup();
  return snapshot;
}

void QSettingsAppStateStore::save(const AppStateSnapshot& snapshot) {
  settings_->beginGroup(QStringLiteral("playbackState"));
  settings_->remove(QString{});
  settings_->setValue(QStringLiteral("version"), kStateVersion);
  settings_->beginWriteArray(QStringLiteral("localPlaylist"));
  for (int index = 0;
       index < static_cast<int>(snapshot.localPlaylist.size()); ++index) {
    const auto& item = snapshot.localPlaylist.at(static_cast<std::size_t>(index));
    if (item.kind != core::MediaSourceKind::LocalFile || item.source.empty() ||
        item.displayName.empty()) {
      continue;
    }
    settings_->setArrayIndex(index);
    settings_->setValue(QStringLiteral("source"), fromUtf8(item.source));
    settings_->setValue(QStringLiteral("displayName"),
                        fromUtf8(item.displayName));
  }
  settings_->endArray();
  const std::vector<LocalPlaybackRecord> recentLocalMedia =
      normalizedRecentLocalMedia(snapshot.recentLocalMedia);
  settings_->beginWriteArray(QStringLiteral("recentLocalMedia"));
  for (int index = 0; index < static_cast<int>(recentLocalMedia.size());
       ++index) {
    const LocalPlaybackRecord& record =
        recentLocalMedia.at(static_cast<std::size_t>(index));
    settings_->setArrayIndex(index);
    settings_->setValue(QStringLiteral("source"), fromUtf8(record.item.source));
    settings_->setValue(QStringLiteral("displayName"),
                        fromUtf8(record.item.displayName));
    settings_->setValue(QStringLiteral("positionMilliseconds"),
                        record.positionMilliseconds);
    settings_->setValue(QStringLiteral("durationMilliseconds"),
                        record.durationMilliseconds);
  }
  settings_->endArray();
  settings_->setValue(QStringLiteral("lastLivePlaylistUrl"),
                      snapshot.lastLivePlaylistUrl.trimmed());
  settings_->setValue(QStringLiteral("livePlaylistUrlHistory"),
                      normalizedUniqueStrings(snapshot.livePlaylistUrlHistory,
                                              kMaximumLiveUrlHistory));
  settings_->setValue(
      QStringLiteral("favoriteLiveSourceUrls"),
      normalizedUniqueStrings(snapshot.favoriteLiveSourceUrls,
                              kMaximumFavoriteLiveSources));
  const QVector<LiveSourceMemo> liveSourceMemos =
      normalizedMemos(snapshot.liveSourceMemos);
  settings_->beginWriteArray(QStringLiteral("liveSourceMemos"));
  for (int index = 0; index < liveSourceMemos.size(); ++index) {
    const LiveSourceMemo& memo = liveSourceMemos.at(index);
    settings_->setArrayIndex(index);
    settings_->setValue(QStringLiteral("sourceUrl"), memo.sourceUrl);
    settings_->setValue(QStringLiteral("note"), memo.note);
  }
  settings_->endArray();
  settings_->endGroup();
  settings_->sync();
}

}  // namespace mediahub::gui
