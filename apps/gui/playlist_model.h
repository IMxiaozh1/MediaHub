#pragma once

#include <QAbstractListModel>

namespace mediahub::core {
class Playlist;
}

namespace mediahub::gui {

// 把纯核心播放列表映射为只读 Qt 列表，不复制媒体项。
class PlaylistModel final : public QAbstractListModel {
public:
    explicit PlaylistModel(core::Playlist& playlist, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;

    // 调用线程：GUI 主线程。核心列表发生变化后通知视图重新读取轻量数据。
    void refresh();

private:
    core::Playlist& playlist_;
};

}  // namespace mediahub::gui
