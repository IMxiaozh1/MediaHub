#include "browser_favicon_cache.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QUrl>

#include <algorithm>

namespace mediahub::gui {
namespace {

constexpr int kMaximumEntries = 100;
constexpr int kMaximumPngBytes = 256 * 1024;
constexpr int kMaximumImageDimension = 512;
constexpr auto kPngSignature = "\x89PNG\r\n\x1a\n";
constexpr int kPngSignatureSize = 8;

}  // namespace

QByteArray BrowserFaviconCache::lookup(const QString& urlOrOrigin) const {
    const QString origin = normalizeOrigin(urlOrOrigin);
    if (origin.isEmpty()) {
        return {};
    }
    const auto iterator = std::find_if(
        entries_.cbegin(), entries_.cend(), [&origin](const Entry& entry) {
            return entry.origin == origin;
        });
    return iterator == entries_.cend() ? QByteArray{} : iterator->pngBytes;
}

bool BrowserFaviconCache::put(const QString& urlOrOrigin,
                              const QByteArray& pngBytes) {
    const QString origin = normalizeOrigin(urlOrOrigin);
    if (origin.isEmpty() || !isSafePng(pngBytes)) {
        return false;
    }

    const auto iterator = std::find_if(
        entries_.begin(), entries_.end(), [&origin](const Entry& entry) {
            return entry.origin == origin;
        });
    if (iterator != entries_.end()) {
        entries_.erase(iterator);
    } else if (entries_.size() >= kMaximumEntries) {
        entries_.removeFirst();
    }
    entries_.append(Entry{origin, pngBytes});
    return true;
}

void BrowserFaviconCache::clear() {
    entries_.clear();
}

int BrowserFaviconCache::size() const {
    return entries_.size();
}

QString BrowserFaviconCache::normalizeOrigin(const QString& value) {
    const QUrl parsed(value.trimmed(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || parsed.isRelative() || parsed.host().isEmpty() ||
        (scheme != QStringLiteral("http") &&
         scheme != QStringLiteral("https")) ||
        !parsed.userName().isEmpty() || !parsed.password().isEmpty()) {
        return {};
    }

    QUrl normalized;
    normalized.setScheme(scheme);
    normalized.setHost(parsed.host().toLower());
    const int port = parsed.port(-1);
    if (port >= 0 &&
        !((scheme == QStringLiteral("http") && port == 80) ||
          (scheme == QStringLiteral("https") && port == 443))) {
        normalized.setPort(port);
    }
    return normalized.toString(QUrl::FullyEncoded);
}

bool BrowserFaviconCache::isSafePng(const QByteArray& pngBytes) {
    if (pngBytes.size() < kPngSignatureSize ||
        pngBytes.size() > kMaximumPngBytes ||
        pngBytes.left(kPngSignatureSize) !=
            QByteArray::fromRawData(kPngSignature, kPngSignatureSize)) {
        return false;
    }

    QBuffer buffer;
    buffer.setData(pngBytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return false;
    }
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    if (!reader.canRead() ||
        reader.format().toLower() != QByteArrayLiteral("png")) {
        return false;
    }
    const QSize size = reader.size();
    if (!size.isValid() || size.isEmpty() ||
        size.width() > kMaximumImageDimension ||
        size.height() > kMaximumImageDimension) {
        return false;
    }
    const QImage image = reader.read();
    return !image.isNull() && image.size() == size &&
           image.width() <= kMaximumImageDimension &&
           image.height() <= kMaximumImageDimension;
}

}  // namespace mediahub::gui
