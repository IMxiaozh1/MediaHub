#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <chrono>
#include <memory>
#include <optional>
#include <unordered_set>

#include "engine_event_bridge.h"
#include "live_playlist_service.h"
#include "live_source_memo.h"
#include "lyrics_service.h"
#include "mediahub/core/playback_state_machine.h"
#include "mediahub/core/player_engine.h"
#include "mediahub/core/playlist.h"
#include "mediahub/logging/logger.h"
#include "player_view_state.h"
#include "playlist_model.h"

class QTimer;

namespace mediahub::gui {

class MainWindow;
class AppStateStore;

// 集中处理用户命令、播放状态机和控件快照，不接触任何具体播放内核类型。
class PlayerPresenter final : public QObject {
  Q_OBJECT

 public:
  PlayerPresenter(core::PlayerEngine& engine, EngineEventBridge& eventBridge,
                  MainWindow& window, QObject* parent = nullptr,
                  logging::Logger* logger = nullptr,
                  LyricsService* lyricsService = nullptr,
                  LivePlaylistService* livePlaylistService = nullptr,
                  AppStateStore* appStateStore = nullptr);
  ~PlayerPresenter() override;

  // 调用线程：GUI 主线程。路径来自文件选择器，使用 UTF-8 交给核心接口。
  void openLocalFile(const QString& filePath);
  // 调用线程：GUI 主线程。保持输入顺序追加文件并播放本批次第一项。
  void addLocalFiles(const QStringList& filePaths);
  // 调用线程：GUI 主线程。校验用户提供的直播地址后加入列表并播放。
  void openNetworkUrl(const QString& url);
  // 调用线程：GUI 主线程。可重复调用，用于关闭窗口时断开事件并停止内核。
  void shutdown() noexcept;

 signals:
  // 供界面观测与自动化测试确认最终应用的业务状态。
  void stateApplied(core::PlaybackState state);

 private:
  enum class PlaylistKind {
    Local,
    Live,
  };

  // 调用线程：GUI 主线程。以下方法只由主线程输入或队列事件调用。
  void requestPlay();
  void requestPause();
  void togglePlayback();
  void requestStop();
  void requestNetworkRefresh();
  void beginSeek();
  void previewSeek(int progressValue);
  void commitSeek(int progressValue);
  void submitSeek(std::chrono::milliseconds target);
  void requestRelativeSeek(int seconds);
  void requestVolume(int volume);
  void requestVolumeStep(int delta);
  void requestMuted(bool isMuted);
  void requestPlaybackRate(double rate);
  void requestTemporaryFastPlayback(bool enabled);
  void applyPlaybackRate(double rate);
  void toggleMuted();
  void toggleLyrics();
  void requestPrevious();
  void requestNext();
  void activatePlaylistItem(int row);
  void removePlaylistItems(QList<int> rows);
  void toggleLivePlaylistMark(int row);
  void toggleLivePlaylistFavorite(int row);
  void movePlaylistItem(int row, int targetRow);
  void renamePlaylistItem(int row, const QString& displayName);
  void changePlaybackMode(int modeIndex);
  void changePlaylistKind(int kindIndex);
  void requestLivePlaylistLoad(const QString& playlistUrl);
  void cancelLivePlaylistLoad();
  void startLivePlaylistLoad(const QString& playlistUrl,
                             bool fallsBackToDirectPlayback);
  void openDirectNetworkUrl(const QString& normalizedUrl);
  void handleLivePlaylistLoaded(LivePlaylistLoadResult result);
  void handleLivePlaylistFailure(LivePlaylistLoadError error);
  void attachVideoSurface(void* nativeHandle);
  void handleStateChanged(core::PlaybackState state);
  void handlePositionChanged(core::PlaybackPosition position);
  void handleDurationChanged(OptionalDuration duration);
  void handleBufferingChanged(int percentage);
  void handleAudioWaveformChanged(core::AudioWaveform waveform);
  void handleLyricsResult(LyricsResult result);
  void handleEndReached();
  void handleError(core::PlaybackError error);
  void handleNetworkOpenTimeout();
  void rememberNetworkUrl(const QString& url);
  void rememberLivePlaylistUrl(const QString& url);
  void updateLivePlaylistHistory(const QStringList& urls);
  void updateLiveSourceMemos(const QVector<LiveSourceMemo>& memos);
  void restoreAppState();
  void persistAppState() noexcept;
  void openCurrentPlaylistItem(PlaylistKind playlistKind,
                               bool isNetworkRefresh = false);
  void openCurrentPlaybackItem(bool isNetworkRefresh = false);
  [[nodiscard]] core::Playlist& playlist(PlaylistKind playlistKind) noexcept;
  [[nodiscard]] const core::Playlist& playlist(
      PlaylistKind playlistKind) const noexcept;
  [[nodiscard]] PlaylistModel& playlistModel(
      PlaylistKind playlistKind) noexcept;
  [[nodiscard]] bool isLivePlaylistItemMarked(std::size_t index) const;
  [[nodiscard]] bool selectAdjacentPlaylistItem(PlaylistKind playlistKind,
                                                bool selectsNext);
  void render();
  [[nodiscard]] PlayerViewState makeViewState() const;

  core::PlayerEngine& engine_;
  EngineEventBridge& eventBridge_;
  MainWindow& window_;
  logging::Logger* logger_{nullptr};
  std::unique_ptr<LyricsService> ownedLyricsService_;
  LyricsService* lyricsService_{nullptr};
  std::unique_ptr<LivePlaylistService> ownedLivePlaylistService_;
  LivePlaylistService* livePlaylistService_{nullptr};
  AppStateStore* appStateStore_{nullptr};
  QTimer* networkOpenTimeoutTimer_{nullptr};
  core::PlaybackStateMachine stateMachine_;
  core::Playlist localPlaylist_;
  core::Playlist livePlaylist_;
  std::unordered_set<std::string> markedLiveSources_;
  std::unordered_set<std::string> favoriteLiveSources_;
  PlaylistModel localPlaylistModel_;
  PlaylistModel livePlaylistModel_;
  std::optional<core::MediaItem> currentPlaybackItem_;
  std::optional<PlaylistKind> currentPlaybackKind_;
  PlaylistKind activePlaylistKind_{PlaylistKind::Local};
  core::PlaybackPosition position_;
  std::optional<std::chrono::milliseconds> seekPreviewPosition_;
  std::optional<std::chrono::milliseconds> pendingRestartPosition_;
  QString mediaName_{QStringLiteral("未选择媒体")};
  QString currentSourcePath_;
  QStringList recentNetworkUrls_;
  QStringList livePlaylistUrlHistory_;
  QVector<LiveSourceMemo> liveSourceMemos_;
  QString lastLivePlaylistUrl_;
  QString activeLivePlaylistRequestUrl_;
  QString pendingPlaylistProbeUrl_;
  QString livePlaylistStatusText_{QStringLiteral("输入远程 M3U/M3U8 清单 URL")};
  int volume_{100};
  int bufferingPercentage_{0};
  double playbackRate_{1.0};
  double lastAppliedPlaybackRate_{1.0};
  bool isAutoPlayPending_{false};
  bool isSeeking_{false};
  bool isMuted_{false};
  bool isLyricsVisible_{false};
  bool isLyricsLoading_{false};
  bool hasLyricsResult_{false};
  bool isVideoMedia_{false};
  bool isNetworkMedia_{false};
  bool isNetworkOpenPending_{false};
  bool isNetworkOpenCancelled_{false};
  bool isNetworkOpenTimedOut_{false};
  bool ignoresCancelledNetworkEvents_{false};
  bool isNetworkRefreshPending_{false};
  bool isNetworkDisconnected_{false};
  bool isLivePlaylistLoading_{false};
  bool isPreparingMedia_{false};
  bool isRestartPlayRequested_{false};
  bool isTemporaryFastPlayback_{false};
  // 只有当前媒体实际开始播放后，它的结束事件才有资格推进列表。
  bool hasCurrentMediaStarted_{false};
  bool isShuttingDown_{false};
};

}  // namespace mediahub::gui
