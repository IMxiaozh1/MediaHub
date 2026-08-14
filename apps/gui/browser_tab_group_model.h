#pragma once

#include <QString>
#include <QVector>

#include <optional>

#include "browser_session_store.h"

namespace mediahub::gui {

// 标签分组模型维护稳定 ID 和界面元数据，不拥有标签或浏览器 Controller。
class BrowserTabGroupModel final {
 public:
    [[nodiscard]] const QVector<BrowserSessionGroup>& groups() const noexcept;
    void replace(const QVector<BrowserSessionGroup>& groups);

    [[nodiscard]] std::optional<QString> create(const QString& name,
                                                const QString& color);
    bool rename(const QString& id, const QString& name);
    bool setColor(const QString& id, const QString& color);
    bool setCollapsed(const QString& id, bool isCollapsed);
    bool remove(const QString& id);

    [[nodiscard]] const BrowserSessionGroup* find(const QString& id) const;
    [[nodiscard]] static QString normalizeName(const QString& value);
    [[nodiscard]] static QString normalizeColor(const QString& value);

 private:
    [[nodiscard]] int findIndex(const QString& id) const noexcept;

    QVector<BrowserSessionGroup> groups_;
};

}  // namespace mediahub::gui
