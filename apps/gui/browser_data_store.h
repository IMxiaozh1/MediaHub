#pragma once

#include <QString>
#include <QVector>

#include <memory>

class QSettings;

namespace mediahub::gui {

// 返回允许进入 QSettings 的 HTTP(S) 地址；移除用户信息、查询参数和片段。
[[nodiscard]] QString normalizeStoredBrowserUrl(const QString& value);

// 网页成功导航记录；只保存可见元数据，不包含请求参数之外的网页内容或凭据。
struct BrowserHistoryEntry {
    QString url;
    QString title;
    qint64 visitedAtMilliseconds{0};
};

// 收藏夹只保存用户可见元数据，备注由用户主动输入。
struct BrowserFavoriteEntry {
    QString url;
    QString title;
    QString note;
};

// 内置浏览器自己的持久化边界，不参与播放器状态恢复。
class BrowserDataStore {
 public:
    virtual ~BrowserDataStore() = default;

    [[nodiscard]] virtual QVector<BrowserHistoryEntry> loadHistory() = 0;
    virtual void saveHistory(const QVector<BrowserHistoryEntry>& history) = 0;
    [[nodiscard]] virtual QVector<BrowserFavoriteEntry> loadFavorites() = 0;
    virtual void saveFavorites(
        const QVector<BrowserFavoriteEntry>& favorites) = 0;
};

// 把网页历史保存到 MediaHub 自己的 QSettings 配置组。
class QSettingsBrowserDataStore final : public BrowserDataStore {
 public:
    QSettingsBrowserDataStore();
    // 指定 INI 文件仅用于隔离自动化测试和诊断。
    explicit QSettingsBrowserDataStore(const QString& settingsFilePath);
    ~QSettingsBrowserDataStore() override;

    [[nodiscard]] QVector<BrowserHistoryEntry> loadHistory() override;
    void saveHistory(const QVector<BrowserHistoryEntry>& history) override;
    [[nodiscard]] QVector<BrowserFavoriteEntry> loadFavorites() override;
    void saveFavorites(
        const QVector<BrowserFavoriteEntry>& favorites) override;

 private:
    std::unique_ptr<QSettings> settings_;
};

}  // namespace mediahub::gui
