#include "browser_data_model.h"

#include <utility>

namespace mediahub::gui {

BrowserDataModel::BrowserDataModel(BrowserDataStore* const dataStore) noexcept
    : dataStore_(dataStore) {}

bool BrowserDataModel::isAvailable() const noexcept {
    return dataStore_ != nullptr;
}

const QVector<BrowserHistoryEntry>& BrowserDataModel::history() {
    if (!hasLoadedHistory_) {
        hasLoadedHistory_ = true;
        if (dataStore_ != nullptr) {
            history_ = normalizeBrowserHistoryEntries(dataStore_->loadHistory());
        }
    }
    return history_;
}

const QVector<BrowserFavoriteEntry>& BrowserDataModel::favorites() {
    if (!hasLoadedFavorites_) {
        hasLoadedFavorites_ = true;
        if (dataStore_ != nullptr) {
            favorites_ =
                normalizeBrowserFavoriteEntries(dataStore_->loadFavorites());
        }
    }
    return favorites_;
}

void BrowserDataModel::replaceHistory(QVector<BrowserHistoryEntry> history) {
    history_ = normalizeBrowserHistoryEntries(history);
    hasLoadedHistory_ = true;
    hasPendingHistoryWrite_ = false;
    if (dataStore_ != nullptr) {
        dataStore_->saveHistory(history_);
    }
}

void BrowserDataModel::replaceHistoryDeferred(
    QVector<BrowserHistoryEntry> history) {
    history_ = normalizeBrowserHistoryEntries(history);
    hasLoadedHistory_ = true;
    hasPendingHistoryWrite_ = dataStore_ != nullptr;
}

void BrowserDataModel::flushPendingHistory() {
    if (!hasPendingHistoryWrite_ || dataStore_ == nullptr) {
        return;
    }
    dataStore_->saveHistory(history_);
    hasPendingHistoryWrite_ = false;
}

void BrowserDataModel::replaceFavorites(
    QVector<BrowserFavoriteEntry> favorites) {
    favorites_ = normalizeBrowserFavoriteEntries(favorites);
    hasLoadedFavorites_ = true;
    if (dataStore_ != nullptr) {
        dataStore_->saveFavorites(favorites_);
    }
}

}  // namespace mediahub::gui
