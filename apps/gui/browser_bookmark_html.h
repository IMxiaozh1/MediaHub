#pragma once

#include <QByteArray>
#include <QVector>

#include "browser_data_store.h"

namespace mediahub::gui {

// 收藏导入先生成临时结果，调用方确认成功后再一次性写入持久化存储。
struct BrowserBookmarkImportResult {
    QVector<BrowserFavoriteEntry> favorites;
    int rejectedEntries{0};
    bool isInputTooLarge{false};
};

// 解析常见浏览器使用的 Netscape Bookmark HTML，过滤非 HTTP(S) 和敏感地址。
[[nodiscard]] BrowserBookmarkImportResult importBrowserBookmarksHtml(
    const QByteArray& html);

// 导出标准 Bookmark HTML；地址会再次规范化，不输出浏览历史或其他宿主状态。
[[nodiscard]] QByteArray exportBrowserBookmarksHtml(
    const QVector<BrowserFavoriteEntry>& favorites);

}  // namespace mediahub::gui
