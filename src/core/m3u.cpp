#include "mediahub/core/m3u.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mediahub/core/media_types.h"

namespace mediahub::core {
namespace {

constexpr std::string_view kHeaderTag = "#EXTM3U";
constexpr std::string_view kChannelTag = "#EXTINF";
constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

struct SourceLine {
    std::string_view text;
    std::size_t number{0};
};

struct PendingChannel {
    std::size_t metadataLine{0};
    std::optional<LiveChannel> channel;
};

using Attributes = std::unordered_map<std::string, std::string>;

bool isAsciiSpace(const char character) noexcept { return character == ' ' || character == '\t'; }

std::string_view trimAscii(std::string_view text) noexcept {
    while (!text.empty() && isAsciiSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isAsciiSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool startsWithTag(const std::string_view line, const std::string_view tag) noexcept {
    return line == tag || (line.size() > tag.size() && line.substr(0, tag.size()) == tag &&
                           isAsciiSpace(line[tag.size()]));
}

std::vector<SourceLine> splitLines(const std::string_view content) {
    std::vector<SourceLine> lines;
    std::size_t start = 0;
    std::size_t lineNumber = 1;
    while (true) {
        const auto end = content.find('\n', start);
        auto line = content.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (lineNumber == 1 && line.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
            line.remove_prefix(kUtf8Bom.size());
        }
        lines.push_back(SourceLine{line, lineNumber});
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
        ++lineNumber;
    }
    return lines;
}

std::string lowerAscii(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return lowered;
}

Attributes parseAttributes(const std::string_view text) {
    Attributes attributes;
    std::size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() && isAsciiSpace(text[position])) {
            ++position;
        }
        const auto keyStart = position;
        while (position < text.size() && !isAsciiSpace(text[position]) && text[position] != '=') {
            ++position;
        }
        const auto key = text.substr(keyStart, position - keyStart);
        const auto keyEnd = position;
        while (position < text.size() && isAsciiSpace(text[position])) {
            ++position;
        }
        if (key.empty() || position >= text.size() || text[position] != '=') {
            position = keyEnd;
            while (position < text.size() && isAsciiSpace(text[position])) {
                ++position;
            }
            continue;
        }

        ++position;
        while (position < text.size() && isAsciiSpace(text[position])) {
            ++position;
        }

        std::string value;
        if (position < text.size() && text[position] == '"') {
            ++position;
            while (position < text.size()) {
                const char character = text[position++];
                if (character == '"') {
                    break;
                }
                if (character == '\\' && position < text.size() &&
                    (text[position] == '\\' || text[position] == '"')) {
                    value.push_back(text[position++]);
                    continue;
                }
                value.push_back(character);
            }
        } else {
            const auto valueStart = position;
            while (position < text.size() && !isAsciiSpace(text[position])) {
                ++position;
            }
            value.assign(text.substr(valueStart, position - valueStart));
        }
        attributes.insert_or_assign(lowerAscii(key), std::move(value));
    }
    return attributes;
}

std::optional<std::string> findAttribute(const Attributes& attributes, const std::string_view key) {
    const auto iterator = attributes.find(std::string(key));
    if (iterator == attributes.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::optional<std::size_t> findMetadataSeparator(const std::string_view text) noexcept {
    bool isQuoted = false;
    bool isEscaped = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (isQuoted && isEscaped) {
            isEscaped = false;
            continue;
        }
        if (isQuoted && character == '\\') {
            isEscaped = true;
            continue;
        }
        if (character == '"') {
            isQuoted = !isQuoted;
            continue;
        }
        if (!isQuoted && character == ',') {
            return index;
        }
    }
    return std::nullopt;
}

void recordSkipped(M3uParseResult& result, const std::size_t lineNumber,
                   const M3uParseIssueKind kind) {
    result.issues.push_back(M3uParseIssue{lineNumber, kind});
    ++result.skippedChannelCount;
}

void flushMissingStream(M3uParseResult& result, const std::optional<PendingChannel>& pending) {
    if (pending.has_value() && pending->channel.has_value()) {
        recordSkipped(result, pending->metadataLine, M3uParseIssueKind::MissingStreamUrl);
    }
}

PendingChannel parseChannelLine(const std::string_view line, const std::size_t lineNumber,
                                M3uParseResult& result) {
    PendingChannel pending{lineNumber, std::nullopt};
    const std::string_view prefix = "#EXTINF:";
    if (line.substr(0, prefix.size()) != prefix) {
        recordSkipped(result, lineNumber, M3uParseIssueKind::MalformedChannelMetadata);
        return pending;
    }

    const auto body = line.substr(prefix.size());
    const auto separator = findMetadataSeparator(body);
    if (!separator.has_value()) {
        recordSkipped(result, lineNumber, M3uParseIssueKind::MalformedChannelMetadata);
        return pending;
    }

    const auto name = trimAscii(body.substr(*separator + 1));
    if (name.empty()) {
        recordSkipped(result, lineNumber, M3uParseIssueKind::MissingChannelName);
        return pending;
    }

    const auto attributes = parseAttributes(body.substr(0, *separator));
    LiveChannel channel;
    channel.name = std::string(name);
    channel.category = findAttribute(attributes, "group-title")
                           .value_or(std::string(kUncategorizedChannelCategory));
    if (channel.category.empty()) {
        channel.category = kUncategorizedChannelCategory;
    }
    channel.logoUrl = findAttribute(attributes, "tvg-logo").value_or("");
    channel.epgId = findAttribute(attributes, "tvg-id").value_or("");
    channel.epgName = findAttribute(attributes, "tvg-name").value_or("");
    pending.channel = std::move(channel);
    return pending;
}

bool containsLineBreak(const std::string_view text) noexcept {
    return text.find_first_of("\r\n") != std::string_view::npos;
}

std::string escapeAttributeValue(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

void appendAttribute(std::string& content, const std::string_view key,
                     const std::string_view value) {
    if (value.empty()) {
        return;
    }
    content.push_back(' ');
    content.append(key);
    content.append("=\"");
    content.append(escapeAttributeValue(value));
    content.push_back('"');
}

M3uSerializationResult serializationFailure(const M3uSerializationIssueKind kind,
                                            const std::optional<std::size_t> channelIndex) {
    return M3uSerializationResult{"", M3uSerializationIssue{kind, channelIndex}};
}

bool channelMetadataContainsLineBreak(const LiveChannel& channel) noexcept {
    return containsLineBreak(channel.name) || containsLineBreak(channel.category) ||
           containsLineBreak(channel.logoUrl) || containsLineBreak(channel.epgId) ||
           containsLineBreak(channel.epgName);
}

}  // namespace

M3uParseResult parseM3u(const std::string_view content) {
    return parseM3u(content, {});
}

M3uParseResult parseM3u(
    const std::string_view content,
    const M3uStreamUrlResolver& streamUrlResolver) {
    M3uParseResult result;
    const auto lines = splitLines(content);
    const auto header = std::find_if(lines.begin(), lines.end(), [](const SourceLine& line) {
        return !trimAscii(line.text).empty();
    });
    if (header == lines.end() || !startsWithTag(trimAscii(header->text), kHeaderTag)) {
        const auto lineNumber = header == lines.end() ? 1U : header->number;
        result.issues.push_back(M3uParseIssue{lineNumber, M3uParseIssueKind::MissingHeader});
        return result;
    }

    const auto headerText = trimAscii(header->text);
    const auto headerAttributes = parseAttributes(headerText.substr(kHeaderTag.size()));
    result.library.epgUrl = findAttribute(headerAttributes, "x-tvg-url").value_or("");

    std::unordered_set<std::string> streamUrls;
    std::optional<PendingChannel> pending;
    for (auto line = header + 1; line != lines.end(); ++line) {
        const auto text = trimAscii(line->text);
        if (text.empty()) {
            continue;
        }
        if (text.substr(0, kChannelTag.size()) == kChannelTag) {
            flushMissingStream(result, pending);
            pending = parseChannelLine(text, line->number, result);
            continue;
        }
        if (text.front() == '#') {
            continue;
        }
        if (!pending.has_value()) {
            recordSkipped(result, line->number, M3uParseIssueKind::MissingChannelMetadata);
            continue;
        }
        if (!pending->channel.has_value()) {
            pending.reset();
            continue;
        }
        std::optional<std::string> resolvedUrl;
        std::string_view streamUrl = text;
        if (validateNetworkUrl(streamUrl) != NetworkUrlValidationError::None &&
            streamUrlResolver) {
            resolvedUrl = streamUrlResolver(text);
            if (resolvedUrl.has_value()) {
                streamUrl = *resolvedUrl;
            }
        }
        if (validateNetworkUrl(streamUrl) != NetworkUrlValidationError::None) {
            recordSkipped(result, line->number, M3uParseIssueKind::InvalidStreamUrl);
            pending.reset();
            continue;
        }
        if (!streamUrls.insert(std::string(streamUrl)).second) {
            result.issues.push_back(
                M3uParseIssue{line->number, M3uParseIssueKind::DuplicateStreamUrl});
            ++result.duplicateChannelCount;
            pending.reset();
            continue;
        }

        pending->channel->streamUrl = std::string(streamUrl);
        result.library.channels.push_back(std::move(*pending->channel));
        pending.reset();
    }
    flushMissingStream(result, pending);
    return result;
}

bool M3uSerializationResult::isSuccess() const noexcept { return !issue.has_value(); }

M3uSerializationResult serializeM3u(const LiveChannelLibrary& library) {
    if (containsLineBreak(library.epgUrl)) {
        return serializationFailure(M3uSerializationIssueKind::ContainsLineBreak, std::nullopt);
    }

    std::unordered_set<std::string> streamUrls;
    for (std::size_t index = 0; index < library.channels.size(); ++index) {
        const auto& channel = library.channels[index];
        if (trimAscii(channel.name).empty()) {
            return serializationFailure(M3uSerializationIssueKind::EmptyChannelName, index);
        }
        if (validateNetworkUrl(channel.streamUrl) != NetworkUrlValidationError::None) {
            return serializationFailure(M3uSerializationIssueKind::InvalidStreamUrl, index);
        }
        if (!streamUrls.insert(channel.streamUrl).second) {
            return serializationFailure(M3uSerializationIssueKind::DuplicateStreamUrl, index);
        }
        if (channelMetadataContainsLineBreak(channel)) {
            return serializationFailure(M3uSerializationIssueKind::ContainsLineBreak, index);
        }
    }

    std::string content(kHeaderTag);
    appendAttribute(content, "x-tvg-url", library.epgUrl);
    content.push_back('\n');
    for (const auto& channel : library.channels) {
        content.push_back('\n');
        content.append("#EXTINF:-1");
        appendAttribute(content, "tvg-id", channel.epgId);
        appendAttribute(content, "tvg-name", channel.epgName);
        appendAttribute(content, "tvg-logo", channel.logoUrl);
        appendAttribute(content, "group-title",
                        channel.category.empty() ? kUncategorizedChannelCategory
                                                 : std::string_view(channel.category));
        content.push_back(',');
        content.append(channel.name);
        content.push_back('\n');
        content.append(channel.streamUrl);
        content.push_back('\n');
    }
    return M3uSerializationResult{std::move(content), std::nullopt};
}

}  // namespace mediahub::core
