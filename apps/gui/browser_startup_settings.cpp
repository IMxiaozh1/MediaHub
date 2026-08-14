#include "browser_startup_settings.h"

#include <QSettings>
#include <QSet>

#include <algorithm>

#include "browser_data_store.h"

namespace mediahub::gui {
namespace {

const QString kDefaultHomeUrl = QStringLiteral("https://www.bing.com/");
constexpr int kMaximumStartupUrls = 20;
constexpr int kDefaultMaximumTabCount = 20;
constexpr int kMinimumMaximumTabCount = 5;
constexpr int kMaximumMaximumTabCount = 100;

BrowserStartupSettings normalizedSettings(
    const BrowserStartupSettings& settings) {
    BrowserStartupSettings normalized;
    normalized.homeUrl = normalizeStoredBrowserUrl(settings.homeUrl);
    if (normalized.homeUrl.isEmpty()) {
        normalized.homeUrl = kDefaultHomeUrl;
    }
    normalized.mode = settings.mode;
    normalized.maximumTabCount = std::clamp(
        settings.maximumTabCount, kMinimumMaximumTabCount,
        kMaximumMaximumTabCount);
    QSet<QString> knownUrls;
    for (const QString& value : settings.startupUrls) {
        const QString url = normalizeStoredBrowserUrl(value);
        if (url.isEmpty() || knownUrls.contains(url)) {
            continue;
        }
        knownUrls.insert(url);
        normalized.startupUrls.append(url);
        if (normalized.startupUrls.size() == kMaximumStartupUrls) {
            break;
        }
    }
    if (normalized.mode == BrowserStartupMode::OpenStartupPages &&
        normalized.startupUrls.isEmpty()) {
        normalized.mode = BrowserStartupMode::OpenBing;
    }
    return normalized;
}

int storedMode(const BrowserStartupMode mode) {
    switch (mode) {
        case BrowserStartupMode::OpenBing:
            return 0;
        case BrowserStartupMode::RestoreSession:
            return 1;
        case BrowserStartupMode::OpenStartupPages:
            return 2;
    }
    return 0;
}

BrowserStartupMode loadedMode(const int value) {
    switch (value) {
        case 1:
            return BrowserStartupMode::RestoreSession;
        case 2:
            return BrowserStartupMode::OpenStartupPages;
        default:
            return BrowserStartupMode::OpenBing;
    }
}

}  // namespace

QSettingsBrowserStartupSettingsStore::QSettingsBrowserStartupSettingsStore()
    : settings_(std::make_unique<QSettings>()) {}

QSettingsBrowserStartupSettingsStore::QSettingsBrowserStartupSettingsStore(
    const QString& settingsFilePath)
    : settings_(
          std::make_unique<QSettings>(settingsFilePath, QSettings::IniFormat)) {}

QSettingsBrowserStartupSettingsStore::~QSettingsBrowserStartupSettingsStore() =
    default;

BrowserStartupSettings QSettingsBrowserStartupSettingsStore::load() {
    settings_->beginGroup(QStringLiteral("browserStartup"));
    BrowserStartupSettings settings;
    settings.homeUrl = settings_->value(QStringLiteral("homeUrl"),
                                        kDefaultHomeUrl)
                           .toString();
    settings.mode = loadedMode(settings_->value(QStringLiteral("mode"), 0).toInt());
    settings.maximumTabCount = settings_->value(QStringLiteral("maximumTabCount"),
                                                 kDefaultMaximumTabCount)
                                   .toInt();
    const int count = settings_->beginReadArray(QStringLiteral("startupUrls"));
    settings.startupUrls.reserve(std::min(count, kMaximumStartupUrls));
    for (int index = 0; index < count; ++index) {
        settings_->setArrayIndex(index);
        settings.startupUrls.append(settings_->value(QStringLiteral("url")).toString());
    }
    settings_->endArray();
    settings_->endGroup();
    return normalizedSettings(settings);
}

void QSettingsBrowserStartupSettingsStore::save(
    const BrowserStartupSettings& settings) {
    const BrowserStartupSettings normalized = normalizedSettings(settings);
    settings_->beginGroup(QStringLiteral("browserStartup"));
    settings_->setValue(QStringLiteral("homeUrl"), normalized.homeUrl);
    settings_->setValue(QStringLiteral("mode"), storedMode(normalized.mode));
    settings_->setValue(QStringLiteral("maximumTabCount"),
                        normalized.maximumTabCount);
    settings_->remove(QStringLiteral("startupUrls"));
    settings_->beginWriteArray(QStringLiteral("startupUrls"));
    for (int index = 0; index < normalized.startupUrls.size(); ++index) {
        settings_->setArrayIndex(index);
        settings_->setValue(QStringLiteral("url"),
                            normalized.startupUrls.at(index));
    }
    settings_->endArray();
    settings_->endGroup();
    settings_->sync();
}

void QSettingsBrowserStartupSettingsStore::clear() {
    settings_->remove(QStringLiteral("browserStartup"));
    settings_->sync();
}

}  // namespace mediahub::gui
