#pragma once

#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>
#include <vector>

#include "live_source_memo.h"
#include "mediahub/core/media_types.h"
#include "player_view_state.h"
#include "ui_theme.h"
#include "window_icon_manager.h"

class QAbstractItemModel;
class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QPoint;
class QPushButton;
class QFrame;
class QEvent;
class QResizeEvent;
class QSlider;
class QStackedLayout;
class QTabBar;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace mediahub::gui {

class VideoOutputWidget;
class LyricsView;
struct LyricsResult;

struct RecentLocalMediaItem {
  QString filePath;
  QString label;
};

// 主窗口只负责布局、采集用户输入和渲染展示快照。
class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  // 调用线程：GUI 主线程。parent 按 Qt 对象树规则持有本窗口。
  explicit MainWindow(QWidget* parent = nullptr);

  // 调用线程：GUI 主线程。所有控件启用状态均直接使用 presenter 的快照。
  void applyViewState(const PlayerViewState& viewState);
  // 调用线程：GUI 主线程。只更新音频画布，不重复渲染整行播放控件。
  void setAudioWaveform(core::AudioWaveform waveform);
  // 调用线程：GUI 主线程。歌词查询不阻塞播放，界面先切换为加载状态。
  void showLyricsLoading();
  // 调用线程：GUI 主线程。显示当前媒体的同步歌词、普通歌词或失败状态。
  void setLyricsResult(const LyricsResult& result);
  // 调用线程：GUI 主线程。切换媒体时清除上一首歌词。
  void clearLyrics();
  // 调用线程：GUI 主线程。错误以内联方式展示，不阻塞事件循环。
  void showPlaybackError(const QString& message);
  // 调用线程：GUI 主线程。
  void clearPlaybackError();
  // 调用线程：GUI 主线程。两个 model 均由 presenter 持有且生命周期覆盖本窗口。
  void setPlaylistModels(QAbstractItemModel* localModel,
                         QAbstractItemModel* liveModel);
  // 调用线程：GUI 主线程。完整地址只保留在当前窗口内，用于下次输入时选择。
  void setRecentNetworkUrls(const QStringList& urls);
  [[nodiscard]] const QStringList& recentNetworkUrls() const noexcept;
  // 调用线程：GUI 主线程。Ctrl+L 识别清单后同步显示到直播清单输入框。
  void setLivePlaylistUrl(const QString& url);
  [[nodiscard]] QString livePlaylistUrl() const;
  // 调用线程：GUI 主线程。历史只用于选择和删除，不会自动载入清单。
  void setLivePlaylistHistoryUrls(const QStringList& urls);
  // 调用线程：GUI 主线程。直播源备忘只显示和编辑，不触发播放或网络请求。
  void setLiveSourceMemos(const QVector<LiveSourceMemo>& memos);
  // 调用线程：GUI 主线程。最近播放菜单只展示本机记录，选择后再交给 presenter。
  void setRecentLocalMedia(const QVector<RecentLocalMediaItem>& items);

 signals:
  void localFilesSelected(const QStringList& filePaths);
  void networkUrlSelected(const QString& url);
  void livePlaylistLoadRequested(const QString& url);
  void livePlaylistLoadCancelRequested();
  void livePlaylistHistoryChanged(const QStringList& urls);
  void liveSourceMemosChanged(const QVector<LiveSourceMemo>& memos);
  void recentLocalMediaSelected(const QString& filePath);
  void recentLocalMediaClearRequested();
  void playlistKindSelected(int kindIndex);
  void playRequested();
  void pauseRequested();
  void playbackToggleRequested();
  void stopRequested();
  void networkRefreshRequested();
  void seekStarted();
  void seekPreviewRequested(int progressValue);
  void seekRequested(int progressValue);
  void volumeRequested(int volume);
  void volumeStepRequested(int delta);
  void seekRelativeRequested(int seconds);
  void playbackRateRequested(double rate);
  void temporaryFastPlaybackRequested(bool enabled);
  void muteToggled();
  void muteStateRequested(bool isMuted);
  void lyricsToggled();
  void previousRequested();
  void nextRequested();
  void playlistItemActivated(int row);
  void playlistItemsRemoveRequested(QList<int> rows);
  void livePlaylistMarkToggled(int row);
  void livePlaylistFavoriteToggled(int row);
  void playlistItemMoveRequested(int row, int targetRow);
  void playlistItemRenameRequested(int row, const QString& displayName);
  void playbackModeRequested(int modeIndex);
  void videoSurfaceReady(void* nativeHandle);
  void closing();

 protected:
  // 调用线程：GUI 主线程。先通知 presenter 关闭事件链，再继续默认关闭流程。
  void closeEvent(QCloseEvent* event) override;
  // 调用线程：GUI 主线程。同步全屏动作与按钮文字。
  void changeEvent(QEvent* event) override;
  // 调用线程：GUI 主线程。窗口跨过响应式阈值时更新播放列表排版。
  void resizeEvent(QResizeEvent* event) override;
  // 调用线程：GUI 主线程。处理快捷键长按，并阻止右键双击激活列表项。
  bool eventFilter(QObject* watched, QEvent* event) override;
  // 调用线程：GUI 主线程。只接受包含本地文件的拖放数据。
  void dragEnterEvent(QDragEnterEvent* event) override;
  // 调用线程：GUI 主线程。保持拖入文件的原始顺序并交给 presenter。
  void dropEvent(QDropEvent* event) override;

 private:
  void chooseLocalFile();
  void chooseNetworkUrl();
  void showLiveUrlHistory();
  void showLiveSourceMemo();
  void showShortcutHelp();
  void toggleFullScreen();
  void exitFullScreen();
  void updateFullScreenText();
  void togglePlaylistVisibility();
  void updatePlaylistToggleAppearance();
  void updatePlaylistResponsiveStyle();
  void applyPresentationMode(UiPresentationMode mode);
  void showPlaylistContextMenu(const QPoint& position);
  void renameContextPlaylistItem();
  void selectPlaylistRow(int row);
  void showPlaylistKind(int kindIndex);
  void applyLivePlaylistFilter();

  WindowIconManager windowIconManager_;
  QAction* openAction_{nullptr};
  QAction* openNetworkAction_{nullptr};
  QAction* fullScreenAction_{nullptr};
  QPushButton* openButton_{nullptr};
  QAction* playlistPlayAction_{nullptr};
  QAction* playlistPauseAction_{nullptr};
  QAction* playlistStopAction_{nullptr};
  QAction* playlistRenameAction_{nullptr};
  QAction* playlistMoveUpAction_{nullptr};
  QAction* playlistMoveDownAction_{nullptr};
  QAction* playlistMoveTopAction_{nullptr};
  QAction* playlistRemoveAction_{nullptr};
  QAction* livePlaylistPlaybackAction_{nullptr};
  QAction* livePlaylistStopAction_{nullptr};
  QAction* livePlaylistMarkAction_{nullptr};
  QAction* livePlaylistFavoriteAction_{nullptr};
  QMenu* recentLocalMediaMenu_{nullptr};
  QSlider* progressSlider_{nullptr};
  QSlider* volumeSlider_{nullptr};
  QToolButton* playPauseButton_{nullptr};
  QToolButton* stopButton_{nullptr};
  QToolButton* networkRefreshButton_{nullptr};
  QToolButton* fullScreenButton_{nullptr};
  QToolButton* previousButton_{nullptr};
  QToolButton* nextButton_{nullptr};
  QToolButton* volumeButton_{nullptr};
  QToolButton* lyricsButton_{nullptr};
  QToolButton* keyboardSeekStepButton_{nullptr};
  QToolButton* playbackRateButton_{nullptr};
  QToolButton* playbackModeButton_{nullptr};
  QToolButton* playlistToggleButton_{nullptr};
  QToolButton* livePlaylistHistoryButton_{nullptr};
  QTabBar* playlistKindTabs_{nullptr};
  QLineEdit* livePlaylistUrlEdit_{nullptr};
  QLineEdit* livePlaylistSearchEdit_{nullptr};
  QPushButton* livePlaylistLoadButton_{nullptr};
  QPushButton* livePlaylistLocateButton_{nullptr};
  QListView* playlistView_{nullptr};
  QMenu* playlistContextMenu_{nullptr};
  QMenu* livePlaylistContextMenu_{nullptr};
  QFrame* playlistPanel_{nullptr};
  QFrame* livePlaylistTools_{nullptr};
  QFrame* headerPanel_{nullptr};
  QFrame* mediaCard_{nullptr};
  QFrame* transportPanel_{nullptr};
  QWidget* centralSurface_{nullptr};
  QWidget* mediaDisplay_{nullptr};
  VideoOutputWidget* videoOutput_{nullptr};
  LyricsView* lyricsView_{nullptr};
  QStackedLayout* mediaDisplayStack_{nullptr};
  QLabel* mediaNameLabel_{nullptr};
  QLabel* statusLabel_{nullptr};
  QLabel* errorLabel_{nullptr};
  QLabel* positionLabel_{nullptr};
  QLabel* volumeLabel_{nullptr};
  QLabel* livePlaylistStatusLabel_{nullptr};
  QLabel* playlistTitleLabel_{nullptr};
  QLabel* eyebrowLabel_{nullptr};
  QLabel* titleLabel_{nullptr};
  QLabel* subtitleLabel_{nullptr};
  QLabel* modeBadgeLabel_{nullptr};
  QVBoxLayout* rootLayout_{nullptr};
  QTimer* rightKeyHoldTimer_{nullptr};
  QList<QWidget*> fullScreenChrome_;
  QList<int> playlistContextRows_;
  QStringList recentNetworkUrls_;
  QStringList livePlaylistHistoryUrls_;
  QVector<LiveSourceMemo> liveSourceMemos_;
  QString playlistResponsiveSize_;
  QAbstractItemModel* localPlaylistModel_{nullptr};
  QAbstractItemModel* livePlaylistModel_{nullptr};
  int keyboardSeekStepSeconds_{5};
  std::optional<UiPresentationMode> presentationMode_;
  int currentPlaylistIndex_{-1};
  int currentLivePlaybackIndex_{-1};
  bool isPlaylistExpanded_{true};
  bool isLivePlaylistActive_{false};
  bool isLivePlaylistLoading_{false};
  bool canEditPlaylist_{false};
  bool canPlayCurrentItem_{false};
  bool canPauseCurrentItem_{false};
  bool canStopCurrentItem_{false};
  bool isCurrentPlaybackInActivePlaylist_{false};
  bool isRightKeyPressed_{false};
  bool isRightKeyHoldActive_{false};
};

}  // namespace mediahub::gui
