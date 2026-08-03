#pragma once

#include "player_view_state.h"

#include <QMainWindow>
#include <QString>

class QAction;
class QCloseEvent;
class QLabel;
class QPushButton;

namespace mediahub::gui {

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

signals:
    void localFileSelected(const QString& filePath);
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void closing();

protected:
    // 调用线程：GUI 主线程。先通知 presenter 关闭事件链，再继续默认关闭流程。
    void closeEvent(QCloseEvent* event) override;

private:
    void chooseLocalFile();

    QAction* openAction_{nullptr};
    QPushButton* openButton_{nullptr};
    QPushButton* playButton_{nullptr};
    QPushButton* pauseButton_{nullptr};
    QPushButton* stopButton_{nullptr};
    QLabel* mediaNameLabel_{nullptr};
    QLabel* statusLabel_{nullptr};
    QLabel* errorLabel_{nullptr};
};

}  // namespace mediahub::gui
