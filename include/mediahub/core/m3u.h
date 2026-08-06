#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mediahub/core/live_channel.h"

namespace mediahub::core {

// M3U 解析问题只携带行号和稳定类别，避免诊断信息泄漏完整直播地址。
enum class M3uParseIssueKind {
    MissingHeader,
    MissingChannelMetadata,
    MalformedChannelMetadata,
    MissingChannelName,
    MissingStreamUrl,
    InvalidStreamUrl,
    DuplicateStreamUrl,
};

struct M3uParseIssue {
    std::size_t lineNumber{0};
    M3uParseIssueKind kind{M3uParseIssueKind::MalformedChannelMetadata};

    bool operator==(const M3uParseIssue&) const = default;
};

// accepted 数量等于 library.channels.size()；重复和其他跳过项分别统计。
struct M3uParseResult {
    LiveChannelLibrary library;
    std::vector<M3uParseIssue> issues;
    std::size_t skippedChannelCount{0};
    std::size_t duplicateChannelCount{0};
};

// 相对地址解析器返回可交给播放内核的绝对地址；空结果表示该地址无法安全补全。
using M3uStreamUrlResolver =
    std::function<std::optional<std::string>(std::string_view)>;

// 解析 MediaHub 支持的 UTF-8 M3U 子集。缺少有效文件头时不继续解释后续内容。
[[nodiscard]] M3uParseResult parseM3u(std::string_view content);

// 使用调用方提供的基准补全相对条目；核心层仍负责协议白名单和重复检查。
[[nodiscard]] M3uParseResult parseM3u(
    std::string_view content, const M3uStreamUrlResolver& streamUrlResolver);

enum class M3uSerializationIssueKind {
    EmptyChannelName,
    InvalidStreamUrl,
    DuplicateStreamUrl,
    ContainsLineBreak,
};

// channelIndex 为空表示问题位于频道库级元数据，否则指向 channels 中的元素。
struct M3uSerializationIssue {
    M3uSerializationIssueKind kind{M3uSerializationIssueKind::EmptyChannelName};
    std::optional<std::size_t> channelIndex;

    bool operator==(const M3uSerializationIssue&) const = default;
};

// 序列化失败时 content 为空，调用方不得覆盖当前有效文件。
struct M3uSerializationResult {
    std::string content;
    std::optional<M3uSerializationIssue> issue;

    [[nodiscard]] bool isSuccess() const noexcept;
};

// 生成规范化 LF 文本；字段顺序固定，便于安全保存和外部文本编辑。
[[nodiscard]] M3uSerializationResult serializeM3u(const LiveChannelLibrary& library);

}  // namespace mediahub::core
