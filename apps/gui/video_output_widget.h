#pragma once

#include <QSize>
#include <QString>
#include <QWidget>

class QEvent;
class QPaintEvent;
class QShowEvent;

namespace mediahub::gui {

// 提供稳定的原生视频输出目标；占位内容由控件自身绘制，不在视频上叠加子控件。
class VideoOutputWidget final : public QWidget {
    Q_OBJECT

public:
    explicit VideoOutputWidget(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

    // 调用线程：GUI 主线程。active 为 false 时立即用占位内容覆盖上一帧。
    void setPresentation(bool isVideoActive, QString placeholderText);
    [[nodiscard]] bool isVideoActive() const noexcept;
    [[nodiscard]] QString placeholderText() const;

signals:
    // 句柄只作为不透明值交给核心接口，GUI 层不暴露 Windows 类型。
    void surfaceReady(void* nativeHandle);

protected:
    // 调用线程：GUI 主线程。窗口显示后才创建并发布原生句柄。
    void showEvent(QShowEvent* event) override;
    // 调用线程：GUI 主线程。句柄重建时重新发布，保证内核使用当前目标。
    bool event(QEvent* event) override;
    // 调用线程：GUI 主线程。视频活动时完全停止 Qt 绘制，避免覆盖 libVLC 画面。
    void paintEvent(QPaintEvent* event) override;

private:
    void publishSurface();

    QString placeholderText_{QStringLiteral("打开媒体后，画面会出现在这里")};
    void* publishedHandle_{nullptr};
    bool isVideoActive_{false};
};

}  // namespace mediahub::gui
