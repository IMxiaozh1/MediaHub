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
  playlist.add(
      std::vector{item("one.mp3"), item("two.mp4"), item("three.wav")});
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

  playlist.setMode(PlaybackMode::Shuffle);
  ASSERT_TRUE(playlist.advanceAfterEnd());
  EXPECT_NE(playlist.currentIndex(), 1U);
}

TEST(PlaylistTest, ShuffleNavigationAlwaysSelectsAnotherItem) {
  auto playlist = threeItems();
  playlist.setMode(PlaybackMode::Shuffle);

  const auto firstIndex = playlist.currentIndex();
  ASSERT_TRUE(playlist.selectNext());
  EXPECT_NE(playlist.currentIndex(), firstIndex);
  const auto secondIndex = playlist.currentIndex();
  ASSERT_TRUE(playlist.selectPrevious());
  EXPECT_NE(playlist.currentIndex(), secondIndex);
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

TEST(PlaylistTest, ReportsNavigationBoundariesForEmptyAndSingleItemLists) {
  Playlist playlist;
  for (const auto mode : {PlaybackMode::Sequential, PlaybackMode::LoopAll,
                          PlaybackMode::LoopOne, PlaybackMode::Shuffle}) {
    playlist.setMode(mode);
    EXPECT_EQ(playlist.previousIndex(), std::nullopt);
    EXPECT_EQ(playlist.nextIndex(), std::nullopt);
    EXPECT_FALSE(playlist.advanceAfterEnd());
  }

  playlist.add(item("only.mp3"));
  playlist.setMode(PlaybackMode::LoopAll);
  EXPECT_EQ(playlist.previousIndex(), std::nullopt);
  EXPECT_EQ(playlist.nextIndex(), std::nullopt);
  EXPECT_TRUE(playlist.advanceAfterEnd());
  EXPECT_EQ(playlist.currentIndex(), 0U);

  playlist.setMode(PlaybackMode::Shuffle);
  EXPECT_TRUE(playlist.advanceAfterEnd());
  EXPECT_EQ(playlist.currentIndex(), 0U);
}

TEST(PlaylistTest, RemovingItemsAroundSelectionPreservesTheSameCurrentMedia) {
  auto playlist = threeItems();
  ASSERT_TRUE(playlist.select(1));

  ASSERT_TRUE(playlist.remove(2));
  ASSERT_NE(playlist.currentItem(), nullptr);
  EXPECT_EQ(playlist.currentIndex(), 1U);
  EXPECT_EQ(playlist.currentItem()->displayName, "two.mp4");

  playlist.add(item("four.ogg"));
  ASSERT_TRUE(playlist.remove(0));
  ASSERT_NE(playlist.currentItem(), nullptr);
  EXPECT_EQ(playlist.currentIndex(), 0U);
  EXPECT_EQ(playlist.currentItem()->displayName, "two.mp4");
}

TEST(PlaylistTest, ReordersItemsWhilePreservingTheCurrentMedia) {
  auto playlist = threeItems();
  ASSERT_TRUE(playlist.select(1));

  ASSERT_TRUE(playlist.moveItem(1, 0));
  EXPECT_EQ(playlist.at(0).displayName, "two.mp4");
  EXPECT_EQ(playlist.currentIndex(), 0U);
  ASSERT_TRUE(playlist.moveItem(2, 0));
  EXPECT_EQ(playlist.at(0).displayName, "three.wav");
  EXPECT_EQ(playlist.currentIndex(), 1U);
  EXPECT_EQ(playlist.currentItem()->displayName, "two.mp4");
  EXPECT_FALSE(playlist.moveItem(3, 0));
  EXPECT_FALSE(playlist.moveItem(0, 3));
}

TEST(PlaylistTest, RenamesOnlyThePlaylistDisplayName) {
  auto playlist = threeItems();
  const std::string originalSource = playlist.at(1).source;

  ASSERT_TRUE(playlist.renameItem(1, "列表中的新名字"));
  EXPECT_EQ(playlist.at(1).displayName, "列表中的新名字");
  EXPECT_EQ(playlist.at(1).source, originalSource);
  EXPECT_FALSE(playlist.renameItem(1, ""));
  EXPECT_FALSE(playlist.renameItem(3, "越界"));
}

}  // namespace
}  // namespace mediahub::core
