#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace mediahub::gui {

// 在内存中按网站来源缓存经过校验的 PNG 图标，不保存页面路径或查询参数。
class BrowserFaviconCache final {
 public:
    BrowserFaviconCache() = default;

    // 调用线程：GUI 主线程；未命中或来源无效时返回空字节数组。
    [[nodiscard]] QByteArray lookup(const QString& urlOrOrigin) const;
    // 调用线程：GUI 主线程；校验失败时保留该来源已有的安全图标。
    bool put(const QString& urlOrOrigin, const QByteArray& pngBytes);
    // 调用线程：GUI 主线程。
    void clear();

    [[nodiscard]] int size() const;
    [[nodiscard]] static QString normalizeOrigin(const QString& value);

 private:
    struct Entry {
        QString origin;
        QByteArray pngBytes;
    };

    [[nodiscard]] static bool isSafePng(const QByteArray& pngBytes);

    QVector<Entry> entries_;
};

}  // namespace mediahub::gui
