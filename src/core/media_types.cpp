#include "mediahub/core/media_types.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace mediahub::core {
namespace {

bool isSchemeCharacter(const unsigned char character) noexcept {
    return std::isalnum(character) != 0 || character == '+' || character == '-' ||
           character == '.';
}

bool isFileScheme(const std::string_view scheme) noexcept {
    constexpr std::string_view kFileScheme = "file";
    return scheme.size() == kFileScheme.size() &&
           std::equal(scheme.begin(), scheme.end(), kFileScheme.begin(),
                      [](const char left, const char right) {
                          return std::tolower(static_cast<unsigned char>(left)) == right;
                      });
}

}  // namespace

MediaSourceKind classifyMediaSource(const std::string_view source) noexcept {
    const auto separator = source.find("://");
    if (separator == std::string_view::npos || separator == 0) {
        return MediaSourceKind::LocalFile;
    }

    const auto scheme = source.substr(0, separator);
    const auto first = static_cast<unsigned char>(scheme.front());
    if (std::isalpha(first) == 0 ||
        !std::all_of(scheme.begin() + 1, scheme.end(), [](const char character) {
            return isSchemeCharacter(static_cast<unsigned char>(character));
        })) {
        return MediaSourceKind::LocalFile;
    }

    return isFileScheme(scheme) ? MediaSourceKind::LocalFile : MediaSourceKind::NetworkStream;
}

MediaItem makeMediaItem(std::string source, std::string displayName) {
    const auto kind = classifyMediaSource(source);
    if (displayName.empty()) {
        std::string_view displaySource = source;
        if (kind == MediaSourceKind::NetworkStream) {
            displaySource = displaySource.substr(0, displaySource.find_first_of("?#"));
        }

        const auto separator = displaySource.find_last_of("/\\");
        const auto nameStart = separator == std::string_view::npos ? 0 : separator + 1;
        const auto inferredName = displaySource.substr(nameStart);
        displayName = inferredName.empty() ? source : std::string(inferredName);
    }

    return MediaItem{std::move(source), kind, std::move(displayName)};
}

}  // namespace mediahub::core
