#pragma once

#include "mediahub/core/media_types.h"

#include <chrono>
#include <optional>

namespace mediahub::core {

// 接收播放内核产生的异步事件。调用线程可能是任意内核线程，实现不得假定为 GUI
// 线程；回调不得抛出异常，也不得同步调用停止、释放或更换监听器等内核方法。
class PlayerEventListener {
public:
    virtual ~PlayerEventListener() = default;

    virtual void onStateChanged(PlaybackState state) noexcept = 0;
    virtual void onPositionChanged(PlaybackPosition position) noexcept = 0;
    virtual void onDurationChanged(
        std::optional<std::chrono::milliseconds> duration) noexcept = 0;
    virtual void onEndReached() noexcept = 0;
    virtual void onError(PlaybackError error) noexcept = 0;
};

// 与具体解码库无关的播放边界。控制方法只提交请求，完成结果必须以事件为准。
class PlayerEngine {
public:
    virtual ~PlayerEngine() = default;

    // 调用线程：应用控制线程。实现必须复制异步处理所需的媒体信息。
    virtual void open(MediaItem item) = 0;
    // 调用线程：应用控制线程。返回不代表已经进入 Playing。
    virtual void play() = 0;
    // 调用线程：应用控制线程。返回不代表已经进入 Paused。
    virtual void pause() = 0;
    // 调用线程：应用控制线程。返回不代表已经进入 Stopped。
    virtual void stop() = 0;
    // 调用线程：应用控制线程。目标位置使用毫秒，实际结果由位置事件确认。
    virtual void seek(std::chrono::milliseconds position) = 0;
    // 调用线程：应用控制线程。volume 的有效范围为 0 至 100。
    virtual void setVolume(int volume) = 0;
    // 调用线程：应用控制线程。
    virtual void setMuted(bool isMuted) = 0;
    // 调用线程：应用控制线程。句柄含义由具体内核解释，nullptr 表示解除输出目标。
    virtual void setVideoSurface(void* nativeHandle) = 0;

    // 调用线程：任意线程。实现必须返回线程安全的状态快照。
    [[nodiscard]] virtual PlaybackState state() const = 0;
    // 调用线程：任意线程。实现必须返回内部一致的位置快照。
    [[nodiscard]] virtual PlaybackPosition position() const = 0;
    // 调用线程：任意线程。空值表示总时长尚未确定或媒体没有固定时长。
    [[nodiscard]] virtual std::optional<std::chrono::milliseconds> duration() const = 0;
    // 调用线程：任意线程。
    [[nodiscard]] virtual bool isSeekable() const = 0;

    // 调用线程：应用控制线程。listener 不转移所有权，nullptr 用于解除监听。
    virtual void setEventListener(PlayerEventListener* listener) = 0;
};

}  // namespace mediahub::core
