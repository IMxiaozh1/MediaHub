#pragma once

#include "engine_event_bridge.h"
#include "mediahub/logging/logger.h"
#include "mediahub/core/playback_state_machine.h"
#include "mediahub/core/player_engine.h"
#include "mediahub/core/playlist.h"
#include "playlist_model.h"
#include "player_view_state.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace mediahub::gui {

class MainWindow;

// 集中处理用户命令、播放状态机和控件快照，不接触任何具体播放内核类型。
class PlayerPresenter final : public QObject {
    Q_OBJECT

public:
    PlayerPresenter(core::PlayerEngine& engine,
                    EngineEventBridge& eventBridge,
                    MainWindow& window,
                    QObject* parent = nullptr,
                    logging::Logger* logger = nullptr);
    ~PlayerPresenter() override;

    // 调用线程：GUI 主线程。路径来自文件选择器，使用 UTF-8 交给核心接口。
    void openLocalFile(const QString& filePath);
    // 调用线程：GUI 主线程。保持输入顺序追加文件并播放本批次第一项。
    void addLocalFiles(const QStringList& filePaths);
    // 调用线程：GUI 主线程。可重复调用，用于关闭窗口时断开事件并停止内核。
    void shutdown() noexcept;

signals:
    // 供界面观测与自动化测试确认最终应用的业务状态。
    void stateApplied(core::PlaybackState state);

private:
    // 调用线程：GUI 主线程。以下方法只由主线程输入或队列事件调用。
    void requestPlay();
    void requestPause();
    void requestStop();
    void beginSeek();
    void previewSeek(int progressValue);
    void commitSeek(int progressValue);
    void requestVolume(int volume);
    void toggleMuted();
    void requestPrevious();
    void requestNext();
    void activatePlaylistItem(int row);
    void removePlaylistItem(int row);
    void changePlaybackMode(int modeIndex);
    void attachVideoSurface(void* nativeHandle);
    void handleStateChanged(core::PlaybackState state);
    void handlePositionChanged(core::PlaybackPosition position);
    void handleDurationChanged(OptionalDuration duration);
    void handleEndReached();
    void handleError(core::PlaybackError error);
    void openCurrentPlaylistItem();
    void render();
    [[nodiscard]] PlayerViewState makeViewState() const;

    core::PlayerEngine& engine_;
    EngineEventBridge& eventBridge_;
    MainWindow& window_;
    logging::Logger* logger_{nullptr};
    core::PlaybackStateMachine stateMachine_;
    core::Playlist playlist_;
    PlaylistModel playlistModel_;
    core::PlaybackPosition position_;
    std::optional<std::chrono::milliseconds> seekPreviewPosition_;
    QString mediaName_{QStringLiteral("未选择媒体")};
    int volume_{100};
    bool isAutoPlayPending_{false};
    bool isSeeking_{false};
    bool isMuted_{false};
    bool isVideoMedia_{false};
    bool isPreparingMedia_{false};
    bool isShuttingDown_{false};
};

}  // namespace mediahub::gui
