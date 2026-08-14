#pragma once

#include <QVector>

#include "browser_data_store.h"

namespace mediahub::gui {

// 浏览器资料模型在 GUI 主线程惰性加载并缓存历史与收藏元数据。
class BrowserDataModel final {
 public:
    explicit BrowserDataModel(BrowserDataStore* dataStore = nullptr) noexcept;

    [[nodiscard]] bool isAvailable() const noexcept;
    // 调用线程：GUI 主线程。首次访问读取持久化存储，后续直接返回缓存。
    [[nodiscard]] const QVector<BrowserHistoryEntry>& history();
    // 调用线程：GUI 主线程。首次访问读取持久化存储，后续直接返回缓存。
    [[nodiscard]] const QVector<BrowserFavoriteEntry>& favorites();
    // 调用线程：GUI 主线程。先更新有界缓存，再同步交给持久化边界。
    void replaceHistory(QVector<BrowserHistoryEntry> history);
    // 调用线程：GUI 主线程。只更新有界缓存，由页面在合并窗口结束后写盘。
    void replaceHistoryDeferred(QVector<BrowserHistoryEntry> history);
    // 调用线程：GUI 主线程。仅在历史缓存变化后写入一次持久化边界。
    void flushPendingHistory();
    // 调用线程：GUI 主线程。先更新有界缓存，再同步交给持久化边界。
    void replaceFavorites(QVector<BrowserFavoriteEntry> favorites);

 private:
    BrowserDataStore* dataStore_{nullptr};
    QVector<BrowserHistoryEntry> history_;
    QVector<BrowserFavoriteEntry> favorites_;
    bool hasLoadedHistory_{false};
    bool hasLoadedFavorites_{false};
    bool hasPendingHistoryWrite_{false};
};

}  // namespace mediahub::gui
