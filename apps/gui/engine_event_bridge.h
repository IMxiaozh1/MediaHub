#pragma once

#include "mediahub/core/player_engine.h"

#include <QMetaType>
#include <QObject>

#include <atomic>
#include <chrono>
#include <optional>

namespace mediahub::gui {

using OptionalDuration = std::optional<std::chrono::milliseconds>;

// 把任意内核线程产生的值类型事件转交给 Qt 信号系统，不承载业务逻辑。
class EngineEventBridge final : public QObject, public core::PlayerEventListener {
    Q_OBJECT

public:
    explicit EngineEventBridge(QObject* parent = nullptr);

    // 调用线程：GUI 主线程。关闭后到达的内核事件会被直接丢弃。
    void deactivate() noexcept;

    // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
    void onStateChanged(core::PlaybackState state) noexcept override;
    // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
    void onPositionChanged(core::PlaybackPosition position) noexcept override;
    // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
    void onDurationChanged(OptionalDuration duration) noexcept override;
    // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
    void onEndReached() noexcept override;
    // 调用线程：任意内核线程。函数只检查关闭标记并发出值类型信号。
    void onError(core::PlaybackError error) noexcept override;

signals:
    void stateChanged(core::PlaybackState state);
    void positionChanged(core::PlaybackPosition position);
    void durationChanged(mediahub::gui::OptionalDuration duration);
    void endReached();
    void errorOccurred(core::PlaybackError error);

private:
    std::atomic_bool isActive_{true};
};

}  // namespace mediahub::gui

Q_DECLARE_METATYPE(mediahub::core::PlaybackState)
Q_DECLARE_METATYPE(mediahub::core::PlaybackPosition)
Q_DECLARE_METATYPE(mediahub::core::PlaybackError)
Q_DECLARE_METATYPE(mediahub::gui::OptionalDuration)
