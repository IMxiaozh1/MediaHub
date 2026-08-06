#include "mediahub/core/m3u.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace mediahub::core {
namespace {

LiveChannel makeChannel(std::string name, std::string streamUrl) {
    return LiveChannel{
        std::move(name), "新闻",    std::move(streamUrl), "https://example.test/logo.png",
        "news01",        "标准频道"};
}

TEST(M3uParseTest, ParsesSupportedFieldsAndPreservesOrder) {
    const std::string content =
        "#EXTM3U x-tvg-url=\"https://example.test/epg.xml\"\n"
        "#EXTINF:-1 tvg-id=\"news01\" tvg-name=\"标准新闻\" "
        "tvg-logo=\"https://example.test/news.png\" group-title=\"新闻\",我的新闻频道\n"
        "https://example.test/live/news.m3u8\n"
        "#EXTINF:-1 group-title=\"摄像头\",局域网摄像头\n"
        "rtsp://192.0.2.10:554/live\n";

    const auto result = parseM3u(content);

    EXPECT_TRUE(result.issues.empty());
    EXPECT_EQ(result.skippedChannelCount, 0U);
    EXPECT_EQ(result.duplicateChannelCount, 0U);
    EXPECT_EQ(result.library.epgUrl, "https://example.test/epg.xml");
    ASSERT_EQ(result.library.channels.size(), 2U);
    EXPECT_EQ(result.library.channels[0],
              (LiveChannel{"我的新闻频道", "新闻", "https://example.test/live/news.m3u8",
                           "https://example.test/news.png", "news01", "标准新闻"}));
    EXPECT_EQ(result.library.channels[1].name, "局域网摄像头");
    EXPECT_EQ(result.library.channels[1].category, "摄像头");
    EXPECT_EQ(result.library.channels[1].streamUrl, "rtsp://192.0.2.10:554/live");
}

TEST(M3uParseTest, AcceptsBomCrLfCommentsAndDefaultCategory) {
    const std::string content =
        "\xEF\xBB\xBF#EXTM3U\r\n\r\n# 普通注释\r\n"
        "#EXTINF:-1,中文频道\r\nhttps://example.test/live\r\n";

    const auto result = parseM3u(content);

    ASSERT_TRUE(result.issues.empty());
    ASSERT_EQ(result.library.channels.size(), 1U);
    EXPECT_EQ(result.library.channels[0].name, "中文频道");
    EXPECT_EQ(result.library.channels[0].category, kUncategorizedChannelCategory);
}

TEST(M3uParseTest, PreservesCommasAndEscapedQuotesInMetadata) {
    const std::string content =
        "#EXTM3U\n"
        "#EXTINF:-1 tvg-name=\"标准\\\"频道\" group-title=\"新闻,地方\",名称,含逗号\n"
        "https://example.test/live\n";

    const auto result = parseM3u(content);

    ASSERT_TRUE(result.issues.empty());
    ASSERT_EQ(result.library.channels.size(), 1U);
    EXPECT_EQ(result.library.channels[0].name, "名称,含逗号");
    EXPECT_EQ(result.library.channels[0].category, "新闻,地方");
    EXPECT_EQ(result.library.channels[0].epgName, "标准\"频道");
}

TEST(M3uParseTest, RejectsMissingHeaderWithoutInterpretingChannels) {
    const std::string content =
        "#EXTINF:-1,频道\n"
        "https://user:secret@example.test/live?token=private\n";

    const auto result = parseM3u(content);

    EXPECT_TRUE(result.library.channels.empty());
    ASSERT_EQ(result.issues.size(), 1U);
    EXPECT_EQ(result.issues[0], (M3uParseIssue{1U, M3uParseIssueKind::MissingHeader}));
}

TEST(M3uParseTest, ReportsOrphanAddressAsMissingMetadata) {
    const auto result = parseM3u("#EXTM3U\nhttps://example.test/orphan\n");

    EXPECT_TRUE(result.library.channels.empty());
    EXPECT_EQ(result.skippedChannelCount, 1U);
    ASSERT_EQ(result.issues.size(), 1U);
    EXPECT_EQ(result.issues[0], (M3uParseIssue{2U, M3uParseIssueKind::MissingChannelMetadata}));
}

TEST(M3uParseTest, SkipsMalformedMetadataAndContinuesWithNextChannel) {
    const std::string content =
        "#EXTM3U\n"
        "#EXTINF:-1 group-title=\"新闻\"\n"
        "https://example.test/skipped\n"
        "#EXTINF:-1,有效频道\n"
        "https://example.test/valid\n";

    const auto result = parseM3u(content);

    EXPECT_EQ(result.skippedChannelCount, 1U);
    ASSERT_EQ(result.issues.size(), 1U);
    EXPECT_EQ(result.issues[0], (M3uParseIssue{2U, M3uParseIssueKind::MalformedChannelMetadata}));
    ASSERT_EQ(result.library.channels.size(), 1U);
    EXPECT_EQ(result.library.channels[0].name, "有效频道");
}

TEST(M3uParseTest, ReportsMissingChannelName) {
    const auto result =
        parseM3u("#EXTM3U\n#EXTINF:-1 group-title=\"新闻\",  \nhttps://example.test/live\n");

    EXPECT_EQ(result.skippedChannelCount, 1U);
    ASSERT_EQ(result.issues.size(), 1U);
    EXPECT_EQ(result.issues[0], (M3uParseIssue{2U, M3uParseIssueKind::MissingChannelName}));
}

TEST(M3uParseTest, ReportsMissingStreamBeforeTheNextEntryAndAtEndOfFile) {
    const std::string content =
        "#EXTM3U\n"
        "#EXTINF:-1,第一个缺地址\n"
        "#EXTINF:-1,有效频道\n"
        "https://example.test/valid\n"
        "#EXTINF:-1,最后缺地址\n";

    const auto result = parseM3u(content);

    EXPECT_EQ(result.skippedChannelCount, 2U);
    ASSERT_EQ(result.issues.size(), 2U);
    EXPECT_EQ(result.issues[0], (M3uParseIssue{2U, M3uParseIssueKind::MissingStreamUrl}));
    EXPECT_EQ(result.issues[1], (M3uParseIssue{5U, M3uParseIssueKind::MissingStreamUrl}));
    ASSERT_EQ(result.library.channels.size(), 1U);
    EXPECT_EQ(result.library.channels[0].name, "有效频道");
}

TEST(M3uParseTest, SkipsAddressesRejectedByTheExistingNetworkValidator) {
    const std::string content =
        "#EXTM3U\n"
        "#EXTINF:-1,本地文件不是直播\nfile:///C:/Media/video.mp4\n"
        "#EXTINF:-1,协议不支持\ncustom://example.test/live\n";

    const auto result = parseM3u(content);

    EXPECT_TRUE(result.library.channels.empty());
    EXPECT_EQ(result.skippedChannelCount, 2U);
    ASSERT_EQ(result.issues.size(), 2U);
    EXPECT_EQ(result.issues[0].kind, M3uParseIssueKind::InvalidStreamUrl);
    EXPECT_EQ(result.issues[1].kind, M3uParseIssueKind::InvalidStreamUrl);
}

TEST(M3uParseTest, SkipsExactAddressDuplicatesButKeepsSameNameWithAnotherAddress) {
    const std::string content =
        "#EXTM3U\n"
        "#EXTINF:-1,同名频道\nhttps://example.test/one\n"
        "#EXTINF:-1,重复地址的新名称\nhttps://example.test/one\n"
        "#EXTINF:-1,同名频道\nhttps://example.test/two\n";

    const auto result = parseM3u(content);

    EXPECT_EQ(result.skippedChannelCount, 0U);
    EXPECT_EQ(result.duplicateChannelCount, 1U);
    ASSERT_EQ(result.issues.size(), 1U);
    EXPECT_EQ(result.issues[0], (M3uParseIssue{5U, M3uParseIssueKind::DuplicateStreamUrl}));
    ASSERT_EQ(result.library.channels.size(), 2U);
    EXPECT_EQ(result.library.channels[0].name, "同名频道");
    EXPECT_EQ(result.library.channels[1].name, "同名频道");
    EXPECT_NE(result.library.channels[0].streamUrl, result.library.channels[1].streamUrl);
}

TEST(M3uSerializationTest, WritesAnEmptyLibraryAsOneHeaderLine) {
    const auto result = serializeM3u(LiveChannelLibrary{});

    EXPECT_TRUE(result.isSuccess());
    EXPECT_EQ(result.content, "#EXTM3U\n");
}

TEST(M3uSerializationTest, WritesStableFieldOrderAndEscapesAttributeQuotes) {
    LiveChannel channel = makeChannel("名称,含逗号", "https://example.test/live");
    channel.epgName = "标准\"频道";
    const LiveChannelLibrary library{"https://example.test/epg.xml", {channel}};

    const auto result = serializeM3u(library);

    ASSERT_TRUE(result.isSuccess());
    const std::string expected =
        "#EXTM3U x-tvg-url=\"https://example.test/epg.xml\"\n\n"
        "#EXTINF:-1 tvg-id=\"news01\" tvg-name=\"标准\\\"频道\" "
        "tvg-logo=\"https://example.test/logo.png\" group-title=\"新闻\",名称,含逗号\n"
        "https://example.test/live\n";
    EXPECT_EQ(result.content, expected);
}

TEST(M3uSerializationTest, RejectsAnEmptyChannelName) {
    auto channel = makeChannel("  ", "https://example.test/live");

    const auto result = serializeM3u(LiveChannelLibrary{"", {channel}});

    EXPECT_FALSE(result.isSuccess());
    EXPECT_TRUE(result.content.empty());
    EXPECT_EQ(result.issue,
              (M3uSerializationIssue{M3uSerializationIssueKind::EmptyChannelName, 0U}));
}

TEST(M3uSerializationTest, RejectsAnInvalidStreamAddress) {
    auto channel = makeChannel("频道", "file:///C:/Media/video.mp4");

    const auto result = serializeM3u(LiveChannelLibrary{"", {channel}});

    EXPECT_EQ(result.issue,
              (M3uSerializationIssue{M3uSerializationIssueKind::InvalidStreamUrl, 0U}));
}

TEST(M3uSerializationTest, RejectsDuplicateStreamAddresses) {
    const auto first = makeChannel("频道一", "https://example.test/live");
    const auto second = makeChannel("频道二", "https://example.test/live");

    const auto result = serializeM3u(LiveChannelLibrary{"", {first, second}});

    EXPECT_EQ(result.issue,
              (M3uSerializationIssue{M3uSerializationIssueKind::DuplicateStreamUrl, 1U}));
}

TEST(M3uSerializationTest, RejectsLineBreakInjectionWithoutProducingContent) {
    auto channel = makeChannel("频道\n#EXTINF:-1,注入", "https://example.test/live");

    const auto result = serializeM3u(LiveChannelLibrary{"", {channel}});

    EXPECT_TRUE(result.content.empty());
    EXPECT_EQ(result.issue,
              (M3uSerializationIssue{M3uSerializationIssueKind::ContainsLineBreak, 0U}));
}

TEST(M3uRoundTripTest, PreservesEverySupportedFieldAndChannelOrder) {
    auto first = makeChannel("频道一", "https://example.test/one");
    first.logoUrl = R"(C:\图标\"频道一".png)";
    LiveChannel second{"频道二", "", "udp://@239.0.0.1:1234", "", "", ""};
    LiveChannelLibrary library{"https://example.test/epg.xml?lang=zh&region=cn", {first, second}};
    library.channels[1].category = std::string(kUncategorizedChannelCategory);

    const auto serialized = serializeM3u(library);
    ASSERT_TRUE(serialized.isSuccess());
    const auto parsed = parseM3u(serialized.content);

    EXPECT_TRUE(parsed.issues.empty());
    EXPECT_EQ(parsed.library, library);
}

}  // namespace
}  // namespace mediahub::core
