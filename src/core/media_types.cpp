#include "mediahub/core/media_types.h"

#include <algorithm>
#include <array>
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

bool equalsIgnoringAsciiCase(const std::string_view left,
                             const std::string_view right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](const char leftCharacter, const char rightCharacter) {
                          return std::tolower(
                                     static_cast<unsigned char>(leftCharacter)) ==
                                 std::tolower(static_cast<unsigned char>(
                                     rightCharacter));
                      });
}

bool isSupportedNetworkScheme(const std::string_view scheme) noexcept {
    constexpr std::array<std::string_view, 8> kSupportedSchemes{
        "http", "https", "rtsp", "rtmp", "rtmps", "udp", "rtp", "srt",
    };
    return std::any_of(kSupportedSchemes.begin(), kSupportedSchemes.end(),
                       [scheme](const std::string_view supported) {
                           return equalsIgnoringAsciiCase(scheme, supported);
                       });
}

bool hasValidSchemeSyntax(const std::string_view scheme) noexcept {
    if (scheme.empty() ||
        std::isalpha(static_cast<unsigned char>(scheme.front())) == 0) {
        return false;
    }
    return std::all_of(scheme.begin() + 1, scheme.end(), [](const char character) {
        return isSchemeCharacter(static_cast<unsigned char>(character));
    });
}

bool isDecimalPort(const std::string_view port) noexcept {
    return !port.empty() &&
           std::all_of(port.begin(), port.end(), [](const char character) {
               return std::isdigit(static_cast<unsigned char>(character)) != 0;
           });
}

bool hasValidAuthority(const std::string_view authority,
                       const bool allowsListenerTarget) noexcept {
    if (authority.empty()) {
        return false;
    }

    std::string_view endpoint = authority;
    const auto userInfoEnd = endpoint.find_last_of('@');
    if (userInfoEnd != std::string_view::npos) {
        endpoint.remove_prefix(userInfoEnd + 1);
    }
    if (endpoint.empty()) {
        return false;
    }

    if (allowsListenerTarget && endpoint.front() == ':') {
        return isDecimalPort(endpoint.substr(1));
    }

    if (endpoint.front() == '[') {
        const auto closingBracket = endpoint.find(']');
        if (closingBracket == std::string_view::npos || closingBracket == 1) {
            return false;
        }
        const auto suffix = endpoint.substr(closingBracket + 1);
        return suffix.empty() ||
               (suffix.front() == ':' && isDecimalPort(suffix.substr(1)));
    }

    const auto firstColon = endpoint.find(':');
    if (firstColon == std::string_view::npos) {
        return !endpoint.empty();
    }
    if (firstColon != endpoint.find_last_of(':') || firstColon == 0) {
        return false;
    }
    return isDecimalPort(endpoint.substr(firstColon + 1));
}

std::string inferNetworkDisplayName(const std::string_view source) {
    const auto schemeEnd = source.find("://");
    if (schemeEnd == std::string_view::npos) {
        return {};
    }

    const auto authorityStart = schemeEnd + 3;
    const auto privacyEnd = source.find_first_of("?#", authorityStart);
    const auto visibleEnd =
        privacyEnd == std::string_view::npos ? source.size() : privacyEnd;
    const auto authorityEnd = source.find('/', authorityStart);
    const auto boundedAuthorityEnd =
        authorityEnd == std::string_view::npos || authorityEnd > visibleEnd
            ? visibleEnd
            : authorityEnd;
    std::string_view authority =
        source.substr(authorityStart, boundedAuthorityEnd - authorityStart);
    const auto userInfoEnd = authority.find_last_of('@');
    if (userInfoEnd != std::string_view::npos) {
        authority.remove_prefix(userInfoEnd + 1);
    }

    std::string_view path;
    if (boundedAuthorityEnd < visibleEnd) {
        path = source.substr(boundedAuthorityEnd, visibleEnd - boundedAuthorityEnd);
        while (!path.empty() && path.back() == '/') {
            path.remove_suffix(1);
        }
    }
    const auto nameStart = path.find_last_of('/');
    const auto name =
        path.empty() ? std::string_view{}
                     : path.substr(nameStart == std::string_view::npos
                                       ? 0
                                       : nameStart + 1);
    if (!name.empty()) {
        return std::string(name);
    }
    return authority.empty() ? std::string("网络媒体") : std::string(authority);
}

}  // namespace

MediaSourceKind classifyMediaSource(const std::string_view source) noexcept {
    const auto separator = source.find("://");
    if (separator == std::string_view::npos || separator == 0) {
        return MediaSourceKind::LocalFile;
    }

    const auto scheme = source.substr(0, separator);
    if (!hasValidSchemeSyntax(scheme)) {
        return MediaSourceKind::LocalFile;
    }

    return isFileScheme(scheme) ? MediaSourceKind::LocalFile : MediaSourceKind::NetworkStream;
}

NetworkUrlValidationError validateNetworkUrl(
    const std::string_view source) noexcept {
    if (source.empty()) {
        return NetworkUrlValidationError::Empty;
    }
    if (std::any_of(source.begin(), source.end(), [](const char character) {
            const auto value = static_cast<unsigned char>(character);
            return value <= 0x20U || value == 0x7FU;
        })) {
        return NetworkUrlValidationError::ContainsWhitespace;
    }

    const auto schemeEnd = source.find("://");
    if (schemeEnd == std::string_view::npos ||
        !hasValidSchemeSyntax(source.substr(0, schemeEnd))) {
        return NetworkUrlValidationError::MissingScheme;
    }
    const auto scheme = source.substr(0, schemeEnd);
    if (!isSupportedNetworkScheme(scheme)) {
        return NetworkUrlValidationError::UnsupportedScheme;
    }

    const auto authorityStart = schemeEnd + 3;
    const auto authorityEnd = source.find_first_of("/?#", authorityStart);
    const auto authority = source.substr(
        authorityStart, authorityEnd == std::string_view::npos
                            ? std::string_view::npos
                            : authorityEnd - authorityStart);
    const bool allowsListenerTarget = equalsIgnoringAsciiCase(scheme, "udp") ||
                                      equalsIgnoringAsciiCase(scheme, "rtp");
    return hasValidAuthority(authority, allowsListenerTarget)
               ? NetworkUrlValidationError::None
               : NetworkUrlValidationError::MissingTarget;
}

MediaItem makeMediaItem(std::string source, std::string displayName) {
    const auto kind = classifyMediaSource(source);
    if (displayName.empty()) {
        if (kind == MediaSourceKind::NetworkStream) {
            displayName = inferNetworkDisplayName(source);
        } else {
            const std::string_view displaySource = source;
            const auto separator = displaySource.find_last_of("/\\");
            const auto nameStart =
                separator == std::string_view::npos ? 0 : separator + 1;
            const auto inferredName = displaySource.substr(nameStart);
            displayName =
                inferredName.empty() ? source : std::string(inferredName);
        }
    }

    return MediaItem{std::move(source), kind, std::move(displayName)};
}

}  // namespace mediahub::core
