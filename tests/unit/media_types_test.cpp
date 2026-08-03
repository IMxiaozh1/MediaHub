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

}  // namespace
}  // namespace mediahub::core
