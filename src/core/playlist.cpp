#include "mediahub/core/playlist.h"

#include <utility>

namespace mediahub::core {

void Playlist::add(MediaItem item) {
    items_.push_back(std::move(item));
    if (!currentIndex_.has_value()) {
        currentIndex_ = 0;
    }
}

void Playlist::add(std::vector<MediaItem> items) {
    const bool wasEmpty = items_.empty();
    items_.reserve(items_.size() + items.size());
    for (auto& item : items) {
        items_.push_back(std::move(item));
    }
    if (wasEmpty && !items_.empty()) {
        currentIndex_ = 0;
    }
}

bool Playlist::remove(const std::size_t index) {
    if (index >= items_.size()) {
        return false;
    }

    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    if (items_.empty()) {
        currentIndex_.reset();
    } else if (currentIndex_.has_value()) {
        if (index < *currentIndex_) {
            --*currentIndex_;
        } else if (*currentIndex_ >= items_.size()) {
            currentIndex_ = items_.size() - 1;
        }
    }
    return true;
}

void Playlist::clear() noexcept {
    items_.clear();
    currentIndex_.reset();
}

bool Playlist::empty() const noexcept {
    return items_.empty();
}

std::size_t Playlist::size() const noexcept {
    return items_.size();
}

const MediaItem& Playlist::at(const std::size_t index) const {
    return items_.at(index);
}

std::optional<std::size_t> Playlist::currentIndex() const noexcept {
    return currentIndex_;
}

const MediaItem* Playlist::currentItem() const noexcept {
    return currentIndex_.has_value() ? &items_[*currentIndex_] : nullptr;
}

bool Playlist::select(const std::size_t index) noexcept {
    if (index >= items_.size()) {
        return false;
    }
    currentIndex_ = index;
    return true;
}

void Playlist::setMode(const PlaybackMode mode) noexcept {
    mode_ = mode;
}

PlaybackMode Playlist::mode() const noexcept {
    return mode_;
}

std::optional<std::size_t> Playlist::previousIndex() const noexcept {
    if (!currentIndex_.has_value() || items_.empty()) {
        return std::nullopt;
    }
    if (*currentIndex_ > 0) {
        return *currentIndex_ - 1;
    }
    if (mode_ == PlaybackMode::LoopAll && items_.size() > 1) {
        return items_.size() - 1;
    }
    return std::nullopt;
}

std::optional<std::size_t> Playlist::nextIndex() const noexcept {
    if (!currentIndex_.has_value() || items_.empty()) {
        return std::nullopt;
    }
    if (*currentIndex_ + 1 < items_.size()) {
        return *currentIndex_ + 1;
    }
    if (mode_ == PlaybackMode::LoopAll && items_.size() > 1) {
        return 0;
    }
    return std::nullopt;
}

bool Playlist::selectPrevious() noexcept {
    const auto target = previousIndex();
    return target.has_value() && select(*target);
}

bool Playlist::selectNext() noexcept {
    const auto target = nextIndex();
    return target.has_value() && select(*target);
}

bool Playlist::advanceAfterEnd() noexcept {
    if (!currentIndex_.has_value() || items_.empty()) {
        return false;
    }
    if (mode_ == PlaybackMode::LoopOne) {
        return true;
    }
    if (*currentIndex_ + 1 < items_.size()) {
        ++*currentIndex_;
        return true;
    }
    if (mode_ == PlaybackMode::LoopAll) {
        currentIndex_ = 0;
        return true;
    }
    return false;
}

}  // namespace mediahub::core
