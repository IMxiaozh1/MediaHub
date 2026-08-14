#include "browser_session_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dpapi.h>
#endif

namespace mediahub::gui {
namespace {

constexpr char kFileMagic[] = "MHBS1";
constexpr qint64 kMaximumEncryptedBytes = 2 * 1024 * 1024;
constexpr int kMaximumTabs = 100;
constexpr int kMaximumClosedTabs = 20;
constexpr int kMaximumGroups = 20;
constexpr int kMaximumTitleLength = 512;
constexpr int kMaximumGroupIdLength = 128;
constexpr int kMaximumGroupNameLength = 64;
constexpr int kMaximumGroupColorLength = 32;
constexpr double kMinimumZoomFactor = 0.25;
constexpr double kMaximumZoomFactor = 5.0;

BrowserSessionTab normalizedTab(const BrowserSessionTab& tab) {
    return BrowserSessionTab{
        normalizeBrowserSessionUrl(tab.url),
        tab.title.trimmed().left(kMaximumTitleLength),
        tab.groupId.trimmed().left(kMaximumGroupIdLength),
        tab.isPinned,
        tab.isMuted,
        std::clamp(tab.zoomFactor, kMinimumZoomFactor, kMaximumZoomFactor)};
}

BrowserSessionGroup normalizedGroup(const BrowserSessionGroup& group) {
    return BrowserSessionGroup{
        group.id.trimmed().left(kMaximumGroupIdLength),
        group.name.trimmed().left(kMaximumGroupNameLength),
        group.color.trimmed().left(kMaximumGroupColorLength),
        group.isCollapsed};
}

BrowserSessionState normalizedState(const BrowserSessionState& state) {
    BrowserSessionState normalized;
    normalized.tabs.reserve(std::min(state.tabs.size(), kMaximumTabs));
    for (const BrowserSessionTab& tab : state.tabs) {
        BrowserSessionTab candidate = normalizedTab(tab);
        if (candidate.url.isEmpty()) {
            continue;
        }
        normalized.tabs.append(std::move(candidate));
        if (normalized.tabs.size() == kMaximumTabs) {
            break;
        }
    }
    normalized.closedTabs.reserve(
        std::min(state.closedTabs.size(), kMaximumClosedTabs));
    for (const BrowserSessionTab& tab : state.closedTabs) {
        BrowserSessionTab candidate = normalizedTab(tab);
        if (candidate.url.isEmpty()) {
            continue;
        }
        normalized.closedTabs.append(std::move(candidate));
        if (normalized.closedTabs.size() == kMaximumClosedTabs) {
            break;
        }
    }
    normalized.currentIndex = normalized.tabs.isEmpty()
                                  ? 0
                                  : std::clamp(state.currentIndex, 0,
                                               normalized.tabs.size() - 1);
    normalized.groups.reserve(std::min(state.groups.size(), kMaximumGroups));
    for (const BrowserSessionGroup& group : state.groups) {
        BrowserSessionGroup candidate = normalizedGroup(group);
        if (candidate.id.isEmpty() || candidate.name.isEmpty()) {
            continue;
        }
        const auto duplicate = std::find_if(
            normalized.groups.cbegin(), normalized.groups.cend(),
            [&candidate](const BrowserSessionGroup& existing) {
                return existing.id == candidate.id;
            });
        if (duplicate != normalized.groups.cend()) {
            continue;
        }
        normalized.groups.append(std::move(candidate));
        if (normalized.groups.size() == kMaximumGroups) {
            break;
        }
    }
    QSet<QString> validGroupIds;
    for (const BrowserSessionGroup& group : normalized.groups) {
        validGroupIds.insert(group.id);
    }
    for (BrowserSessionTab& tab : normalized.tabs) {
        if (!validGroupIds.contains(tab.groupId)) {
            tab.groupId.clear();
        }
    }
    for (BrowserSessionTab& tab : normalized.closedTabs) {
        if (!validGroupIds.contains(tab.groupId)) {
            tab.groupId.clear();
        }
    }
    return normalized;
}

QJsonObject tabToJson(const BrowserSessionTab& tab) {
    return QJsonObject{{QStringLiteral("url"), tab.url},
                       {QStringLiteral("title"), tab.title},
                       {QStringLiteral("groupId"), tab.groupId},
                       {QStringLiteral("pinned"), tab.isPinned},
                       {QStringLiteral("muted"), tab.isMuted},
                       {QStringLiteral("zoom"), tab.zoomFactor}};
}

QJsonObject groupToJson(const BrowserSessionGroup& group) {
    return QJsonObject{{QStringLiteral("id"), group.id},
                       {QStringLiteral("name"), group.name},
                       {QStringLiteral("color"), group.color},
                       {QStringLiteral("collapsed"), group.isCollapsed}};
}

QByteArray serializeState(const BrowserSessionState& state) {
    const BrowserSessionState normalized = normalizedState(state);
    QJsonArray tabs;
    for (const BrowserSessionTab& tab : normalized.tabs) {
        tabs.append(tabToJson(tab));
    }
    QJsonArray closedTabs;
    for (const BrowserSessionTab& tab : normalized.closedTabs) {
        closedTabs.append(tabToJson(tab));
    }
    QJsonArray groups;
    for (const BrowserSessionGroup& group : normalized.groups) {
        groups.append(groupToJson(group));
    }
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("version"), 2},
                             {QStringLiteral("currentIndex"),
                              normalized.currentIndex},
                             {QStringLiteral("tabs"), tabs},
                             {QStringLiteral("closedTabs"), closedTabs},
                             {QStringLiteral("groups"), groups}})
        .toJson(QJsonDocument::Compact);
}

BrowserSessionTab tabFromJson(const QJsonValue& value) {
    const QJsonObject object = value.toObject();
    return normalizedTab(BrowserSessionTab{
        object.value(QStringLiteral("url")).toString(),
        object.value(QStringLiteral("title")).toString(),
        object.value(QStringLiteral("groupId")).toString(),
        object.value(QStringLiteral("pinned")).toBool(),
        object.value(QStringLiteral("muted")).toBool(),
        object.value(QStringLiteral("zoom")).toDouble(1.0)});
}

BrowserSessionGroup groupFromJson(const QJsonValue& value) {
    const QJsonObject object = value.toObject();
    return normalizedGroup(BrowserSessionGroup{
        object.value(QStringLiteral("id")).toString(),
        object.value(QStringLiteral("name")).toString(),
        object.value(QStringLiteral("color")).toString(),
        object.value(QStringLiteral("collapsed")).toBool()});
}

std::optional<BrowserSessionState> parseState(const QByteArray& plainText) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(plainText, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    const int version = object.value(QStringLiteral("version")).toInt();
    if ((version != 1 && version != 2) ||
        !object.value(QStringLiteral("tabs")).isArray() ||
        !object.value(QStringLiteral("closedTabs")).isArray()) {
        return std::nullopt;
    }

    BrowserSessionState state;
    const QJsonArray tabs = object.value(QStringLiteral("tabs")).toArray();
    for (const QJsonValue& value : tabs) {
        const BrowserSessionTab tab = tabFromJson(value);
        if (!tab.url.isEmpty()) {
            state.tabs.append(tab);
        }
    }
    const QJsonArray closedTabs =
        object.value(QStringLiteral("closedTabs")).toArray();
    for (const QJsonValue& value : closedTabs) {
        const BrowserSessionTab tab = tabFromJson(value);
        if (!tab.url.isEmpty()) {
            state.closedTabs.append(tab);
        }
    }
    state.currentIndex = object.value(QStringLiteral("currentIndex")).toInt();
    if (version >= 2 && object.value(QStringLiteral("groups")).isArray()) {
        const QJsonArray groups = object.value(QStringLiteral("groups")).toArray();
        for (const QJsonValue& value : groups) {
            const BrowserSessionGroup group = groupFromJson(value);
            if (!group.id.isEmpty() && !group.name.isEmpty()) {
                state.groups.append(group);
            }
        }
    }
    return normalizedState(state);
}

#ifdef Q_OS_WIN
std::optional<QByteArray> protectBytes(const QByteArray& plainText) {
    DATA_BLOB input{
        static_cast<DWORD>(plainText.size()),
        reinterpret_cast<BYTE*>(const_cast<char*>(plainText.constData()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"MediaHub browser session", nullptr,
                          nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &output)) {
        return std::nullopt;
    }
    const QByteArray encrypted(reinterpret_cast<const char*>(output.pbData),
                               static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return encrypted;
}

std::optional<QByteArray> unprotectBytes(const QByteArray& encrypted) {
    DATA_BLOB input{
        static_cast<DWORD>(encrypted.size()),
        reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return std::nullopt;
    }
    const QByteArray plainText(reinterpret_cast<const char*>(output.pbData),
                               static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return plainText;
}
#endif

}  // namespace

QString normalizeBrowserSessionUrl(const QString& value) {
    const QUrl parsed(value.trimmed(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() ||
        (scheme != QStringLiteral("https") && scheme != QStringLiteral("http")) ||
        parsed.host().isEmpty() || !parsed.userName().isEmpty() ||
        !parsed.password().isEmpty()) {
        return {};
    }
    QUrl normalized = parsed;
    normalized.setScheme(scheme);
    normalized.setHost(parsed.host().toLower());
    normalized.setUserInfo({});
    return normalized.toString(QUrl::FullyEncoded);
}

DpapiBrowserSessionStore::DpapiBrowserSessionStore(QString filePath)
    : filePath_(std::move(filePath)) {}

std::optional<BrowserSessionState> DpapiBrowserSessionStore::load() {
#ifdef Q_OS_WIN
    QFile file(filePath_);
    if (!file.exists()) {
        return BrowserSessionState{};
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 5 ||
        file.size() > kMaximumEncryptedBytes) {
        return std::nullopt;
    }
    const QByteArray contents = file.readAll();
    if (!contents.startsWith(kFileMagic)) {
        return std::nullopt;
    }
    const std::optional<QByteArray> plainText =
        unprotectBytes(contents.mid(5));
    return plainText.has_value() ? parseState(*plainText) : std::nullopt;
#else
    return std::nullopt;
#endif
}

bool DpapiBrowserSessionStore::save(const BrowserSessionState& state) {
#ifdef Q_OS_WIN
    const std::optional<QByteArray> encrypted = protectBytes(serializeState(state));
    if (!encrypted.has_value()) {
        return false;
    }
    const QFileInfo fileInfo(filePath_);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        return false;
    }
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray contents = QByteArray(kFileMagic, 5) + *encrypted;
    return file.write(contents) == contents.size() && file.commit();
#else
    static_cast<void>(state);
    return false;
#endif
}

bool DpapiBrowserSessionStore::clear() {
    return !QFile::exists(filePath_) || QFile::remove(filePath_);
}

}  // namespace mediahub::gui
