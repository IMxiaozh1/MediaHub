#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace mediahub::gui {

// 会话地址会保留查询和片段，但只允许不带用户名密码的 HTTP(S) 地址。
[[nodiscard]] QString normalizeBrowserSessionUrl(const QString& value);

struct BrowserSessionTab {
    QString url;
    QString title;
    QString groupId;
    bool isPinned{false};
    bool isMuted{false};
    double zoomFactor{1.0};
};

// 标签分组只保存界面元数据；成员关系由 BrowserSessionTab::groupId 表达。
struct BrowserSessionGroup {
    QString id;
    QString name;
    QString color;
    bool isCollapsed{false};
};

struct BrowserSessionState {
    QVector<BrowserSessionTab> tabs;
    QVector<BrowserSessionTab> closedTabs;
    int currentIndex{0};
    QVector<BrowserSessionGroup> groups;
};

// 浏览器会话使用独立加密文件，不进入 QSettings 或播放器状态快照。
class BrowserSessionStore {
 public:
    virtual ~BrowserSessionStore() = default;

    [[nodiscard]] virtual std::optional<BrowserSessionState> load() = 0;
    virtual bool save(const BrowserSessionState& state) = 0;
    virtual bool clear() = 0;
};

// Windows 实现使用当前用户范围 DPAPI，并以原子替换方式更新会话文件。
class DpapiBrowserSessionStore final : public BrowserSessionStore {
 public:
    explicit DpapiBrowserSessionStore(QString filePath);

    [[nodiscard]] std::optional<BrowserSessionState> load() override;
    bool save(const BrowserSessionState& state) override;
    bool clear() override;

 private:
    QString filePath_;
};

}  // namespace mediahub::gui
