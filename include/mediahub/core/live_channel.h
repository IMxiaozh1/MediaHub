#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mediahub::core {

inline constexpr std::string_view kUncategorizedChannelCategory = "未分类";

// 用户维护的直播频道。所有文本均为 UTF-8；streamUrl 可能包含敏感参数，不得写入日志。
struct LiveChannel {
    std::string name;
    std::string category;
    std::string streamUrl;
    std::string logoUrl;
    std::string epgId;
    std::string epgName;

    bool operator==(const LiveChannel&) const = default;
};

// 单个本地 M3U 频道库；频道顺序与文件顺序一致。
struct LiveChannelLibrary {
    std::string epgUrl;
    std::vector<LiveChannel> channels;

    bool operator==(const LiveChannelLibrary&) const = default;
};

}  // namespace mediahub::core
