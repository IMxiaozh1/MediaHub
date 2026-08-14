#include "browser_permission_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace mediahub::gui {
namespace {

constexpr int kPermissionDataVersion = 1;
constexpr int kMaximumPermissionEntries = 500;
constexpr qint64 kMaximumPermissionFileBytes = 2 * 1024 * 1024;

QString defaultPermissionPath() {
    QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (directory.isEmpty()) {
        directory = QDir::tempPath();
    }
    return QDir(directory).filePath(QStringLiteral("browser-permissions.json"));
}

std::optional<BrowserPermissionKind> permissionKindFromInt(const int value) {
    if (value < static_cast<int>(BrowserPermissionKind::Camera) ||
        value > static_cast<int>(BrowserPermissionKind::ClipboardRead)) {
        return std::nullopt;
    }
    return static_cast<BrowserPermissionKind>(value);
}

std::optional<BrowserPermissionState> permissionStateFromInt(const int value) {
    if (value < static_cast<int>(BrowserPermissionState::Ask) ||
        value > static_cast<int>(BrowserPermissionState::Block)) {
        return std::nullopt;
    }
    return static_cast<BrowserPermissionState>(value);
}

}  // namespace

BrowserPermissionStore::BrowserPermissionStore(const QString& filePath)
    : filePath_(filePath.trimmed().isEmpty() ? defaultPermissionPath()
                                              : filePath.trimmed()) {
    static_cast<void>(reload());
}

bool BrowserPermissionStore::reload() {
    QFile file(filePath_);
    if (!file.exists()) {
        entries_.clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly) ||
        file.size() > kMaximumPermissionFileBytes) {
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt(0) !=
        kPermissionDataVersion) {
        return false;
    }
    const QJsonValue recordsValue = root.value(QStringLiteral("permissions"));
    if (!recordsValue.isArray() || recordsValue.toArray().size() >
                                      kMaximumPermissionEntries) {
        return false;
    }

    QVector<BrowserPermissionEntry> parsed;
    for (const QJsonValue& value : recordsValue.toArray()) {
        if (!value.isObject()) {
            return false;
        }
        const QJsonObject object = value.toObject();
        const QString origin = normalizeOrigin(
            object.value(QStringLiteral("origin")).toString());
        const auto kind = permissionKindFromInt(
            object.value(QStringLiteral("kind")).toInt(-1));
        const auto state = permissionStateFromInt(
            object.value(QStringLiteral("state")).toInt(-1));
        if (!kind.has_value() || !state.has_value() || origin.isEmpty() ||
            !isPersistableState(*kind, *state) ||
            *state == BrowserPermissionState::Ask) {
            return false;
        }
        if (std::any_of(parsed.cbegin(), parsed.cend(),
                        [&origin, kind](const BrowserPermissionEntry& entry) {
                            return entry.origin == origin && entry.kind == *kind;
                        })) {
            return false;
        }
        parsed.append(BrowserPermissionEntry{origin, *kind, *state});
    }
    entries_ = std::move(parsed);
    return true;
}

QVector<BrowserPermissionEntry> BrowserPermissionStore::entries() const {
    return entries_;
}

BrowserPermissionState BrowserPermissionStore::stateFor(
    const QString& origin, const BrowserPermissionKind kind) const {
    const QString normalized = normalizeOrigin(origin);
    if (normalized.isEmpty() || !isPersistableKind(kind)) {
        return BrowserPermissionState::Ask;
    }
    const auto iterator = std::find_if(
        entries_.cbegin(), entries_.cend(), [&normalized, kind](const auto& entry) {
            return sameKey(entry, normalized, kind);
        });
    return iterator == entries_.cend() ? BrowserPermissionState::Ask
                                       : iterator->state;
}

bool BrowserPermissionStore::set(const QString& origin,
                                 const BrowserPermissionKind kind,
                                 const BrowserPermissionState state) {
    const QString normalized = normalizeOrigin(origin);
    if (normalized.isEmpty() || !isPersistableState(kind, state)) {
        return false;
    }
    if (state == BrowserPermissionState::Ask) {
        return remove(normalized, kind);
    }
    QVector<BrowserPermissionEntry> candidate = entries_;
    const auto iterator = std::find_if(
        candidate.begin(), candidate.end(), [&normalized, kind](const auto& entry) {
            return sameKey(entry, normalized, kind);
        });
    if (iterator == candidate.end()) {
        if (candidate.size() >= kMaximumPermissionEntries) {
            return false;
        }
        candidate.append(BrowserPermissionEntry{normalized, kind, state});
    } else {
        iterator->state = state;
    }
    if (!writeSnapshot(candidate)) {
        return false;
    }
    entries_ = std::move(candidate);
    return true;
}

bool BrowserPermissionStore::remove(const QString& origin,
                                    const BrowserPermissionKind kind) {
    const QString normalized = normalizeOrigin(origin);
    if (normalized.isEmpty() || !isPersistableKind(kind)) {
        return false;
    }
    QVector<BrowserPermissionEntry> candidate;
    candidate.reserve(entries_.size());
    bool found = false;
    for (const BrowserPermissionEntry& entry : entries_) {
        if (sameKey(entry, normalized, kind)) {
            found = true;
        } else {
            candidate.append(entry);
        }
    }
    if (!found) {
        return true;
    }
    if (!writeSnapshot(candidate)) {
        return false;
    }
    entries_ = std::move(candidate);
    return true;
}

bool BrowserPermissionStore::clear() {
    const QVector<BrowserPermissionEntry> candidate;
    if (!writeSnapshot(candidate)) {
        return false;
    }
    entries_.clear();
    return true;
}

QString BrowserPermissionStore::filePath() const {
    return filePath_;
}

QString BrowserPermissionStore::normalizeOrigin(const QString& value) {
    const QUrl parsed(value.trimmed(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || (scheme != QStringLiteral("http") &&
                              scheme != QStringLiteral("https")) ||
        parsed.host().isEmpty() || !parsed.userName().isEmpty() ||
        !parsed.password().isEmpty() || parsed.isRelative()) {
        return {};
    }
    QUrl normalized;
    normalized.setScheme(scheme);
    normalized.setHost(parsed.host().toLower());
    if (parsed.port() >= 0 &&
        !((scheme == QStringLiteral("http") && parsed.port() == 80) ||
          (scheme == QStringLiteral("https") && parsed.port() == 443))) {
        normalized.setPort(parsed.port());
    }
    return normalized.toString(QUrl::FullyEncoded);
}

QString BrowserPermissionStore::permissionName(const BrowserPermissionKind kind) {
    switch (kind) {
    case BrowserPermissionKind::Camera:
        return QStringLiteral("摄像头");
    case BrowserPermissionKind::Microphone:
        return QStringLiteral("麦克风");
    case BrowserPermissionKind::Geolocation:
        return QStringLiteral("位置信息");
    case BrowserPermissionKind::Notifications:
        return QStringLiteral("通知");
    case BrowserPermissionKind::ScreenCapture:
        return QStringLiteral("屏幕捕获");
    case BrowserPermissionKind::ClipboardRead:
        return QStringLiteral("读取剪贴板");
    case BrowserPermissionKind::Other:
        return QStringLiteral("其他");
    }
    return QStringLiteral("其他");
}

QString BrowserPermissionStore::stateName(const BrowserPermissionState state) {
    switch (state) {
    case BrowserPermissionState::Ask:
        return QStringLiteral("询问");
    case BrowserPermissionState::Allow:
        return QStringLiteral("允许");
    case BrowserPermissionState::Block:
        return QStringLiteral("阻止");
    }
    return QStringLiteral("询问");
}

bool BrowserPermissionStore::isPersistableKind(
    const BrowserPermissionKind kind) {
    return kind != BrowserPermissionKind::Other;
}

bool BrowserPermissionStore::isPersistableState(
    const BrowserPermissionKind kind, const BrowserPermissionState state) {
    return isPersistableKind(kind) &&
           (kind != BrowserPermissionKind::ScreenCapture ||
            state != BrowserPermissionState::Allow);
}

bool BrowserPermissionStore::writeSnapshot(
    const QVector<BrowserPermissionEntry>& entries) const {
    QJsonArray permissions;
    for (const BrowserPermissionEntry& entry : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("origin"), entry.origin);
        object.insert(QStringLiteral("kind"), static_cast<int>(entry.kind));
        object.insert(QStringLiteral("state"), static_cast<int>(entry.state));
        permissions.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kPermissionDataVersion);
    root.insert(QStringLiteral("permissions"), permissions);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > kMaximumPermissionFileBytes) {
        return false;
    }

    const QFileInfo fileInfo(filePath_);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        return false;
    }
    QSaveFile file(filePath_);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit()) {
        return false;
    }
    return true;
}

bool BrowserPermissionStore::sameKey(const BrowserPermissionEntry& left,
                                      const QString& origin,
                                      const BrowserPermissionKind kind) {
    return left.origin == origin && left.kind == kind;
}

}  // namespace mediahub::gui
