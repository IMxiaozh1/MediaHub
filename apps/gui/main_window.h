#pragma once

#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>

#include "mediahub/core/media_types.h"
#include "player_view_state.h"

class QAbstractItemModel;
class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QListView;
class QMenu;
class QPoint;
class QPushButton;
class QFrame;
class QEvent;
class QSlider;
class QStackedLayout;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace mediahub::gui {

class VideoOutputWidget;
class LyricsView;
struct LyricsResult;

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
  // 调用线程：GUI 主线程。model 由 presenter 持有且生命周期覆盖本窗口。
  void setPlaylistModel(QAbstractItemModel* model);

 signals:
  void localFilesSelected(const QStringList& filePaths);
  void playRequested();
  void pauseRequested();
  void playbackToggleRequested();
  void stopRequested();
  void seekStarted();
  void seekPreviewRequested(int progressValue);
  void seekRequested(int progressValue);
  void volumeRequested(int volume);
  void volumeStepRequested(int delta);
  void seekRelativeRequested(int seconds);
  void playbackRateRequested(double rate);
  void temporaryFastPlaybackRequested(bool enabled);
  void muteToggled();
  void lyricsToggled();
  void previousRequested();
  void nextRequested();
  void playlistItemActivated(int row);
  void playlistItemsRemoveRequested(QList<int> rows);
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
  // 调用线程：GUI 主线程。区分右键轻按与长按，并吞掉系统自动重复事件。
  bool eventFilter(QObject* watched, QEvent* event) override;
  // 调用线程：GUI 主线程。只接受包含本地文件的拖放数据。
  void dragEnterEvent(QDragEnterEvent* event) override;
  // 调用线程：GUI 主线程。保持拖入文件的原始顺序并交给 presenter。
  void dropEvent(QDropEvent* event) override;

 private:
  void chooseLocalFile();
  void toggleFullScreen();
  void exitFullScreen();
  void updateFullScreenText();
  void togglePlaylistVisibility();
  void updatePlaylistToggleAppearance();
  void showPlaylistContextMenu(const QPoint& position);
  void renameContextPlaylistItem();
  void selectPlaylistRow(int row);

  QAction* openAction_{nullptr};
  QAction* fullScreenAction_{nullptr};
  QPushButton* openButton_{nullptr};
  QAction* playlistPlayAction_{nullptr};
  QAction* playlistRenameAction_{nullptr};
  QAction* playlistMoveUpAction_{nullptr};
  QAction* playlistMoveDownAction_{nullptr};
  QAction* playlistMoveTopAction_{nullptr};
  QAction* playlistRemoveAction_{nullptr};
  QSlider* progressSlider_{nullptr};
  QSlider* volumeSlider_{nullptr};
  QToolButton* playPauseButton_{nullptr};
  QToolButton* stopButton_{nullptr};
  QToolButton* fullScreenButton_{nullptr};
  QToolButton* previousButton_{nullptr};
  QToolButton* nextButton_{nullptr};
  QToolButton* volumeButton_{nullptr};
  QToolButton* lyricsButton_{nullptr};
  QToolButton* keyboardSeekStepButton_{nullptr};
  QToolButton* playbackRateButton_{nullptr};
  QToolButton* playbackModeButton_{nullptr};
  QToolButton* playlistToggleButton_{nullptr};
  QListView* playlistView_{nullptr};
  QMenu* playlistContextMenu_{nullptr};
  QFrame* playlistPanel_{nullptr};
  VideoOutputWidget* videoOutput_{nullptr};
  LyricsView* lyricsView_{nullptr};
  QStackedLayout* mediaDisplayStack_{nullptr};
  QLabel* mediaNameLabel_{nullptr};
  QLabel* statusLabel_{nullptr};
  QLabel* errorLabel_{nullptr};
  QLabel* positionLabel_{nullptr};
  QLabel* volumeLabel_{nullptr};
  QVBoxLayout* rootLayout_{nullptr};
  QTimer* rightKeyHoldTimer_{nullptr};
  QList<QWidget*> fullScreenChrome_;
  QList<int> playlistContextRows_;
  int keyboardSeekStepSeconds_{5};
  bool isPlaylistExpanded_{true};
  bool canEditPlaylist_{false};
  bool isRightKeyPressed_{false};
  bool isRightKeyHoldActive_{false};
};

}  // namespace mediahub::gui
