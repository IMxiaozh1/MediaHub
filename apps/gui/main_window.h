#pragma once

#include <QMainWindow>

namespace mediahub::gui {

// 阶段 2 的最小主窗口，只验证 Qt 工程骨架和生命周期。
class MainWindow final : public QMainWindow {
public:
    // 调用线程：GUI 主线程。parent 按 Qt 对象树规则持有本窗口。
    explicit MainWindow(QWidget* parent = nullptr);
};

}
