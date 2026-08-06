#pragma once

#include <QAbstractListModel>
#include <functional>

namespace mediahub::core {
struct MediaItem;
class Playlist;
}

namespace mediahub::gui {

// 把纯核心播放列表映射为只读 Qt 列表，不复制媒体项。
class PlaylistModel final : public QAbstractListModel {
public:
    using MarkedPredicate = std::function<bool(const core::MediaItem&)>;
    using FavoritePredicate = std::function<bool(const core::MediaItem&)>;
    static constexpr int kMarkedRole = Qt::UserRole + 1;
    static constexpr int kFavoriteRole = Qt::UserRole + 2;

    explicit PlaylistModel(core::Playlist& playlist, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

    // 调用线程：GUI 主线程。核心列表发生变化后通知视图重新读取轻量数据。
    void refresh();
    // 调用线程：GUI 主线程。仅刷新单个条目的状态，保持列表滚动位置不变。
    void refreshItem(int row);
    // 调用线程：GUI 主线程。为直播模型注入会话内标记状态。
    void setMarkedPredicate(MarkedPredicate predicate);
    // 调用线程：GUI 主线程。为直播模型注入会话内收藏状态。
    void setFavoritePredicate(FavoritePredicate predicate);

private:
    [[nodiscard]] bool isMarked(const QModelIndex& index) const;
    [[nodiscard]] bool isFavorite(const QModelIndex& index) const;

    core::Playlist& playlist_;
    MarkedPredicate markedPredicate_;
    FavoritePredicate favoritePredicate_;
};

}  // namespace mediahub::gui
