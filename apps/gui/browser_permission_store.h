#pragma once

#include <QVector>
#include <QString>

#include <optional>

#include "browser_types.h"

namespace mediahub::gui {

// 网站权限的持久化状态；Ask 表示每次请求都交给用户确认。
enum class BrowserPermissionState {
    Ask,
    Allow,
    Block,
};

struct BrowserPermissionEntry {
    QString origin;
    BrowserPermissionKind kind{BrowserPermissionKind::Other};
    BrowserPermissionState state{BrowserPermissionState::Ask};
};

// 只保存规范化 HTTP(S) 来源及权限状态，不保存路径、查询参数或凭据。
class BrowserPermissionStore final {
 public:
    explicit BrowserPermissionStore(const QString& filePath = {});

    // 调用线程：GUI 主线程；读取失败时不覆盖当前已加载的数据。
    bool reload();

    [[nodiscard]] QVector<BrowserPermissionEntry> entries() const;
    // 缺少记录时返回 Ask，便于调用方按浏览器默认语义处理。
    [[nodiscard]] BrowserPermissionState stateFor(
        const QString& origin, BrowserPermissionKind kind) const;

    // 调用线程：GUI 主线程；写入失败时不修改内存中的旧记录。
    bool set(const QString& origin, BrowserPermissionKind kind,
             BrowserPermissionState state);
    // 调用线程：GUI 主线程；删除不存在的记录视为成功。
    bool remove(const QString& origin, BrowserPermissionKind kind);
    // 调用线程：GUI 主线程；使用原子替换清空全部记录。
    bool clear();

    [[nodiscard]] QString filePath() const;

    [[nodiscard]] static QString normalizeOrigin(const QString& value);
    [[nodiscard]] static QString permissionName(BrowserPermissionKind kind);
    [[nodiscard]] static QString stateName(BrowserPermissionState state);
    [[nodiscard]] static bool isPersistableKind(BrowserPermissionKind kind);
    [[nodiscard]] static bool isPersistableState(BrowserPermissionKind kind,
                                                  BrowserPermissionState state);

 private:
    [[nodiscard]] bool writeSnapshot(
        const QVector<BrowserPermissionEntry>& entries) const;
    [[nodiscard]] static bool sameKey(const BrowserPermissionEntry& left,
                                      const QString& origin,
                                      BrowserPermissionKind kind);

    QString filePath_;
    QVector<BrowserPermissionEntry> entries_;
};

}  // namespace mediahub::gui
