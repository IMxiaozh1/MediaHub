#include "playlist_model.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QString>
#include <utility>

#include "mediahub/core/playlist.h"

namespace mediahub::gui {

PlaylistModel::PlaylistModel(core::Playlist& playlist, QObject* const parent)
    : QAbstractListModel(parent), playlist_(playlist) {}

int PlaylistModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(playlist_.size());
}

QVariant PlaylistModel::data(const QModelIndex& index, const int role) const {
  if (!index.isValid() || index.row() < 0 ||
      static_cast<std::size_t>(index.row()) >= playlist_.size()) {
    return {};
  }
  const auto row = static_cast<std::size_t>(index.row());
  const auto& item = playlist_.at(row);
  const bool isItemMarked = isMarked(index);
  const bool isItemFavorite = isFavorite(index);
  if (role == kMarkedRole) {
    return isItemMarked;
  }
  if (role == kFavoriteRole) {
    return isItemFavorite;
  }
  const QString displayName = QString::fromUtf8(
      item.displayName.data(), static_cast<int>(item.displayName.size()));
  if (role == Qt::UserRole) {
    return displayName;
  }
  if (isItemMarked && role == Qt::ToolTipRole) {
    return QStringLiteral("此直播源不可用，右键可取消标记");
  }
  if (isItemMarked && role == Qt::ForegroundRole) {
    return QBrush(QColor(QStringLiteral("#9b2f1f")));
  }
  if (isItemMarked && role == Qt::BackgroundRole) {
    return QBrush(QColor(QStringLiteral("#fff0e8")));
  }
  if (isItemMarked && role == Qt::FontRole) {
    QFont font;
    font.setBold(true);
    return font;
  }
  if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) {
    return {};
  }

  const bool isCurrent = playlist_.currentIndex() == row;
  const QString favoritePrefix =
      isItemFavorite ? QStringLiteral("★ ") : QString{};
  return QStringLiteral("%1%2%3. %4")
      .arg(favoritePrefix,
           isItemMarked
               ? QStringLiteral("【不可用】 ")
               : (isCurrent ? QStringLiteral("▶ ") : QStringLiteral("   ")))
      .arg(index.row() + 1)
      .arg(displayName);
}

Qt::ItemFlags PlaylistModel::flags(const QModelIndex& index) const {
  auto itemFlags = QAbstractListModel::flags(index);
  if (isMarked(index)) {
    itemFlags &= ~(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
  }
  return itemFlags;
}

void PlaylistModel::refresh() {
  beginResetModel();
  endResetModel();
}

void PlaylistModel::refreshItem(const int row) {
  if (row < 0 || static_cast<std::size_t>(row) >= playlist_.size()) {
    return;
  }
  const QModelIndex itemIndex = index(row, 0);
  emit dataChanged(itemIndex, itemIndex);
}

void PlaylistModel::setMarkedPredicate(MarkedPredicate predicate) {
  markedPredicate_ = std::move(predicate);
  refresh();
}

void PlaylistModel::setFavoritePredicate(FavoritePredicate predicate) {
  favoritePredicate_ = std::move(predicate);
  refresh();
}

bool PlaylistModel::isMarked(const QModelIndex& index) const {
  return markedPredicate_ && index.isValid() && index.row() >= 0 &&
         static_cast<std::size_t>(index.row()) < playlist_.size() &&
         markedPredicate_(playlist_.at(static_cast<std::size_t>(index.row())));
}

bool PlaylistModel::isFavorite(const QModelIndex& index) const {
  return favoritePredicate_ && index.isValid() && index.row() >= 0 &&
         static_cast<std::size_t>(index.row()) < playlist_.size() &&
         favoritePredicate_(
             playlist_.at(static_cast<std::size_t>(index.row())));
}

}  // namespace mediahub::gui
