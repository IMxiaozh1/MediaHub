#pragma once

#include "mediahub/core/media_types.h"

namespace mediahub::core {

// 区分真实状态变化、幂等重复事件和被拒绝的非法事件。
enum class PlaybackTransitionResult {
    Changed,
    Unchanged,
    Rejected,
};

// 校验内核事件带来的状态变化。该对象由 GUI 主线程在事件桥接后驱动，不负责跨线程同步。
class PlaybackStateMachine {
public:
    // 调用线程：GUI 主线程。
    [[nodiscard]] PlaybackState state() const noexcept;

    // 调用线程：GUI 主线程。非法事件不会改变当前状态。
    [[nodiscard]] PlaybackTransitionResult transitionTo(PlaybackState nextState) noexcept;

    // 调用线程：任意线程。此函数不访问对象状态。
    [[nodiscard]] static bool canTransition(PlaybackState from,
                                            PlaybackState to) noexcept;

    // 调用线程：GUI 主线程。用于解除当前媒体后回到未加载状态。
    void reset() noexcept;

private:
    PlaybackState state_{PlaybackState::Idle};
};

}  // namespace mediahub::core
