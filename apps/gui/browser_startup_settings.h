#pragma once

#include <QString>
#include <QVector>

#include <memory>

class QSettings;

namespace mediahub::gui {

enum class BrowserStartupMode {
    OpenBing,
    RestoreSession,
    OpenStartupPages,
};

struct BrowserStartupSettings {
    QString homeUrl{QStringLiteral("https://www.bing.com/")};
    BrowserStartupMode mode{BrowserStartupMode::OpenBing};
    QVector<QString> startupUrls;
    int maximumTabCount{20};
};

class BrowserStartupSettingsStore {
 public:
    virtual ~BrowserStartupSettingsStore() = default;

    [[nodiscard]] virtual BrowserStartupSettings load() = 0;
    virtual void save(const BrowserStartupSettings& settings) = 0;
    virtual void clear() = 0;
};

// 启动设置只保存经隐私规范化的地址，不保存查询、片段或网页会话内容。
class QSettingsBrowserStartupSettingsStore final
    : public BrowserStartupSettingsStore {
 public:
    QSettingsBrowserStartupSettingsStore();
    explicit QSettingsBrowserStartupSettingsStore(const QString& settingsFilePath);
    ~QSettingsBrowserStartupSettingsStore() override;

    [[nodiscard]] BrowserStartupSettings load() override;
    void save(const BrowserStartupSettings& settings) override;
    void clear() override;

 private:
    std::unique_ptr<QSettings> settings_;
};

}  // namespace mediahub::gui
