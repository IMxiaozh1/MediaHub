#include "mediahub/core/playlist.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace mediahub::core {
namespace {

MediaItem item(const std::string& name) {
    return MediaItem{"C:/Media/" + name, MediaSourceKind::LocalFile, name};
}

Playlist threeItems() {
    Playlist playlist;
    playlist.add(std::vector{item("one.mp3"), item("two.mp4"), item("three.wav")});
    return playlist;
}

TEST(PlaylistTest, PreservesOrderDuplicatesAndFirstSelection) {
    Playlist playlist;
    playlist.add(item("same.mp3"));
    playlist.add(item("same.mp3"));

    ASSERT_EQ(playlist.size(), 2U);
    ASSERT_EQ(playlist.currentIndex(), 0U);
    EXPECT_EQ(playlist.at(0).displayName, "same.mp3");
    EXPECT_EQ(playlist.at(1).displayName, "same.mp3");
    ASSERT_NE(playlist.currentItem(), nullptr);
    EXPECT_EQ(playlist.currentItem()->displayName, "same.mp3");
}

TEST(PlaylistTest, RejectsInvalidSelectionWithoutChangingCurrentItem) {
    auto playlist = threeItems();
    ASSERT_TRUE(playlist.select(1));
    EXPECT_FALSE(playlist.select(3));
    EXPECT_EQ(playlist.currentIndex(), 1U);
}

TEST(PlaylistTest, ManualNavigationOnlyWrapsInLoopAllMode) {
    auto playlist = threeItems();
    EXPECT_FALSE(playlist.selectPrevious());
    ASSERT_TRUE(playlist.select(2));
    EXPECT_FALSE(playlist.selectNext());

    playlist.setMode(PlaybackMode::LoopOne);
    EXPECT_FALSE(playlist.selectNext());
    playlist.setMode(PlaybackMode::LoopAll);
    ASSERT_TRUE(playlist.selectNext());
    EXPECT_EQ(playlist.currentIndex(), 0U);
    ASSERT_TRUE(playlist.selectPrevious());
    EXPECT_EQ(playlist.currentIndex(), 2U);
}

TEST(PlaylistTest, AdvancesAfterEndAccordingToPlaybackMode) {
    auto playlist = threeItems();
    ASSERT_TRUE(playlist.select(2));
    EXPECT_FALSE(playlist.advanceAfterEnd());
    EXPECT_EQ(playlist.currentIndex(), 2U);

    playlist.setMode(PlaybackMode::LoopAll);
    ASSERT_TRUE(playlist.advanceAfterEnd());
    EXPECT_EQ(playlist.currentIndex(), 0U);

    playlist.setMode(PlaybackMode::LoopOne);
    ASSERT_TRUE(playlist.select(1));
    ASSERT_TRUE(playlist.advanceAfterEnd());
    EXPECT_EQ(playlist.currentIndex(), 1U);
}

TEST(PlaylistTest, RemovingItemsKeepsCurrentIndexValid) {
    auto playlist = threeItems();
    ASSERT_TRUE(playlist.select(1));
    ASSERT_TRUE(playlist.remove(0));
    EXPECT_EQ(playlist.currentIndex(), 0U);
    EXPECT_EQ(playlist.currentItem()->displayName, "two.mp4");

    ASSERT_TRUE(playlist.remove(0));
    EXPECT_EQ(playlist.currentIndex(), 0U);
    EXPECT_EQ(playlist.currentItem()->displayName, "three.wav");
    ASSERT_TRUE(playlist.remove(0));
    EXPECT_TRUE(playlist.empty());
    EXPECT_EQ(playlist.currentIndex(), std::nullopt);
    EXPECT_EQ(playlist.currentItem(), nullptr);
    EXPECT_FALSE(playlist.remove(0));
}

TEST(PlaylistTest, ClearRemovesSelectionButPreservesMode) {
    auto playlist = threeItems();
    playlist.setMode(PlaybackMode::LoopAll);
    playlist.clear();

    EXPECT_TRUE(playlist.empty());
    EXPECT_EQ(playlist.currentIndex(), std::nullopt);
    EXPECT_EQ(playlist.mode(), PlaybackMode::LoopAll);
}

}  // namespace
}  // namespace mediahub::core
