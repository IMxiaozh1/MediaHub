#include "playlist_model.h"

#include <QString>

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
  const QString displayName = QString::fromUtf8(
      item.displayName.data(), static_cast<int>(item.displayName.size()));
  if (role == Qt::UserRole) {
    return displayName;
  }
  if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) {
    return {};
  }

  const bool isCurrent = playlist_.currentIndex() == row;
  return QStringLiteral("%1%2. %3")
      .arg(isCurrent ? QStringLiteral("▶ ") : QStringLiteral("   "))
      .arg(index.row() + 1)
      .arg(displayName);
}

void PlaylistModel::refresh() {
  beginResetModel();
  endResetModel();
}

}  // namespace mediahub::gui
