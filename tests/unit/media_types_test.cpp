#include "mediahub/core/media_types.h"

#include <gtest/gtest.h>

namespace mediahub::core {
namespace {

TEST(MediaSourceClassificationTest, TreatsPathsAndFileUrisAsLocalFiles) {
    EXPECT_EQ(classifyMediaSource(R"(C:\媒体\song.mp3)"), MediaSourceKind::LocalFile);
    EXPECT_EQ(classifyMediaSource("relative/video.mp4"), MediaSourceKind::LocalFile);
    EXPECT_EQ(classifyMediaSource("file:///C:/Media/song.mp3"), MediaSourceKind::LocalFile);
    EXPECT_EQ(classifyMediaSource("FILE:///C:/Media/song.mp3"), MediaSourceKind::LocalFile);
}

TEST(MediaSourceClassificationTest, TreatsValidNonFileUrisAsNetworkStreams) {
    EXPECT_EQ(classifyMediaSource("https://example.test/live.m3u8"),
              MediaSourceKind::NetworkStream);
    EXPECT_EQ(classifyMediaSource("rtsp://example.test/live"), MediaSourceKind::NetworkStream);
    EXPECT_EQ(classifyMediaSource("custom+media://host/resource"),
              MediaSourceKind::NetworkStream);
}

TEST(MediaSourceClassificationTest, RejectsMalformedUriSchemes) {
    EXPECT_EQ(classifyMediaSource("1http://example.test/live"), MediaSourceKind::LocalFile);
    EXPECT_EQ(classifyMediaSource("bad_scheme://example.test/live"),
              MediaSourceKind::LocalFile);
    EXPECT_EQ(classifyMediaSource("://example.test/live"), MediaSourceKind::LocalFile);
}

TEST(NetworkUrlValidationTest, AcceptsEverySupportedStreamingScheme) {
    constexpr std::array<std::string_view, 8> kAddresses{
        "http://example.test/live.m3u8", "HTTPS://example.test/live.m3u8",
        "rtsp://example.test/camera", "rtmp://example.test/live/channel",
        "rtmps://example.test/live/channel", "udp://@239.0.0.1:1234",
        "rtp://@:5004", "srt://example.test:9000?mode=caller",
    };
    for (const auto address : kAddresses) {
        EXPECT_EQ(validateNetworkUrl(address), NetworkUrlValidationError::None)
            << address;
    }
}

TEST(NetworkUrlValidationTest, RejectsUnsafeOrIncompleteAddresses) {
    EXPECT_EQ(validateNetworkUrl(""), NetworkUrlValidationError::Empty);
    EXPECT_EQ(validateNetworkUrl(" https://example.test/live"),
              NetworkUrlValidationError::ContainsWhitespace);
    EXPECT_EQ(validateNetworkUrl("https://example.test/live stream"),
              NetworkUrlValidationError::ContainsWhitespace);
    EXPECT_EQ(validateNetworkUrl("C:/Media/live.m3u8"),
              NetworkUrlValidationError::MissingScheme);
    EXPECT_EQ(validateNetworkUrl("file:///C:/Media/live.m3u8"),
              NetworkUrlValidationError::UnsupportedScheme);
    EXPECT_EQ(validateNetworkUrl("custom://example.test/live"),
              NetworkUrlValidationError::UnsupportedScheme);
    EXPECT_EQ(validateNetworkUrl("https:///live.m3u8"),
              NetworkUrlValidationError::MissingTarget);
    EXPECT_EQ(validateNetworkUrl("udp://@:not-a-port"),
              NetworkUrlValidationError::MissingTarget);
}

TEST(MediaItemTest, InfersDisplayNameWithoutLosingSourceKind) {
    const auto localItem = makeMediaItem(R"(C:\Media\song.mp3)");
    EXPECT_EQ(localItem.source, R"(C:\Media\song.mp3)");
    EXPECT_EQ(localItem.kind, MediaSourceKind::LocalFile);
    EXPECT_EQ(localItem.displayName, "song.mp3");

    const auto streamItem = makeMediaItem("https://example.test/live/channel.m3u8?quality=high");
    EXPECT_EQ(streamItem.kind, MediaSourceKind::NetworkStream);
    EXPECT_EQ(streamItem.displayName, "channel.m3u8");
}

TEST(MediaItemTest, PreservesExplicitDisplayName) {
    const auto item = makeMediaItem("https://example.test/live", "测试直播");
    EXPECT_EQ(item.displayName, "测试直播");
}

TEST(MediaItemTest, KeepsStableNamesForRootQueriesAndTrailingSeparators) {
    const auto rootStream = makeMediaItem("https://example.test/?token=private#live");
    EXPECT_EQ(rootStream.kind, MediaSourceKind::NetworkStream);
    EXPECT_EQ(rootStream.displayName, "example.test");

    const auto trailingLocal = makeMediaItem(R"(C:\Media\)");
    EXPECT_EQ(trailingLocal.kind, MediaSourceKind::LocalFile);
    EXPECT_EQ(trailingLocal.displayName, trailingLocal.source);

    const auto emptySource = makeMediaItem("");
    EXPECT_EQ(emptySource.kind, MediaSourceKind::LocalFile);
    EXPECT_TRUE(emptySource.displayName.empty());
}

TEST(MediaItemTest, RemovesCredentialsAndPrivateUrlPartsFromNetworkName) {
    const auto root = makeMediaItem(
        "https://user:password@example.test:8443/?token=private#live");
    EXPECT_EQ(root.source,
              "https://user:password@example.test:8443/?token=private#live");
    EXPECT_EQ(root.displayName, "example.test:8443");

    const auto channel = makeMediaItem(
        "https://user:password@example.test/live/channel.m3u8?token=private");
    EXPECT_EQ(channel.displayName, "channel.m3u8");
    EXPECT_EQ(channel.displayName.find("password"), std::string::npos);
    EXPECT_EQ(channel.displayName.find("token"), std::string::npos);
}

}  // namespace
}  // namespace mediahub::core
