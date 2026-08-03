#pragma once

#include "player_view_state.h"

#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>

class QAbstractItemModel;
class QAction;
class QComboBox;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QListView;
class QPushButton;
class QEvent;
class QSlider;
class QVBoxLayout;

namespace mediahub::gui {

class VideoOutputWidget;

// 主窗口只负责布局、采集用户输入和渲染展示快照。
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    // 调用线程：GUI 主线程。parent 按 Qt 对象树规则持有本窗口。
    explicit MainWindow(QWidget* parent = nullptr);

    // 调用线程：GUI 主线程。所有控件启用状态均直接使用 presenter 的快照。
    void applyViewState(const PlayerViewState& viewState);
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
    void stopRequested();
    void seekStarted();
    void seekPreviewRequested(int progressValue);
    void seekRequested(int progressValue);
    void volumeRequested(int volume);
    void muteToggled();
    void previousRequested();
    void nextRequested();
    void playlistItemActivated(int row);
    void removePlaylistItemRequested(int row);
    void playbackModeRequested(int modeIndex);
    void videoSurfaceReady(void* nativeHandle);
    void closing();

protected:
    // 调用线程：GUI 主线程。先通知 presenter 关闭事件链，再继续默认关闭流程。
    void closeEvent(QCloseEvent* event) override;
    // 调用线程：GUI 主线程。同步全屏动作与按钮文字。
    void changeEvent(QEvent* event) override;
    // 调用线程：GUI 主线程。只接受包含本地文件的拖放数据。
    void dragEnterEvent(QDragEnterEvent* event) override;
    // 调用线程：GUI 主线程。保持拖入文件的原始顺序并交给 presenter。
    void dropEvent(QDropEvent* event) override;

private:
    void chooseLocalFile();
    void toggleFullScreen();
    void exitFullScreen();
    void updateFullScreenText();

    QAction* openAction_{nullptr};
    QAction* fullScreenAction_{nullptr};
    QPushButton* openButton_{nullptr};
    QPushButton* playButton_{nullptr};
    QPushButton* pauseButton_{nullptr};
    QPushButton* stopButton_{nullptr};
    QPushButton* fullScreenButton_{nullptr};
    QPushButton* muteButton_{nullptr};
    QPushButton* previousButton_{nullptr};
    QPushButton* nextButton_{nullptr};
    QPushButton* removePlaylistButton_{nullptr};
    QSlider* progressSlider_{nullptr};
    QSlider* volumeSlider_{nullptr};
    QComboBox* playbackModeCombo_{nullptr};
    QListView* playlistView_{nullptr};
    VideoOutputWidget* videoOutput_{nullptr};
    QLabel* mediaNameLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* errorLabel_{nullptr};
    QLabel* positionLabel_{nullptr};
    QLabel* volumeLabel_{nullptr};
    QVBoxLayout* rootLayout_{nullptr};
    QList<QWidget*> fullScreenChrome_;
};

}  // namespace mediahub::gui
