#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mediahub/core/media_types.h"

namespace mediahub::core {

// 保存有序媒体项与当前选择，并集中实现四种播放模式的导航规则。
class Playlist {
 public:
  void add(MediaItem item);
  void add(std::vector<MediaItem> items);
  [[nodiscard]] bool remove(std::size_t index);
  [[nodiscard]] bool moveItem(std::size_t from, std::size_t to);
  [[nodiscard]] bool renameItem(std::size_t index, std::string displayName);
  void clear() noexcept;

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const MediaItem& at(std::size_t index) const;
  [[nodiscard]] std::optional<std::size_t> currentIndex() const noexcept;
  [[nodiscard]] const MediaItem* currentItem() const noexcept;
  [[nodiscard]] bool select(std::size_t index) noexcept;

  void setMode(PlaybackMode mode) noexcept;
  [[nodiscard]] PlaybackMode mode() const noexcept;
  [[nodiscard]] std::optional<std::size_t> previousIndex() const noexcept;
  [[nodiscard]] std::optional<std::size_t> nextIndex() const noexcept;
  [[nodiscard]] bool selectPrevious() noexcept;
  [[nodiscard]] bool selectNext() noexcept;
  [[nodiscard]] bool advanceAfterEnd() noexcept;

 private:
  [[nodiscard]] bool selectShuffleTarget() noexcept;

  std::vector<MediaItem> items_;
  std::optional<std::size_t> currentIndex_;
  PlaybackMode mode_{PlaybackMode::Sequential};
  std::uint32_t shuffleState_{0x9e3779b9U};
};

}  // namespace mediahub::core
