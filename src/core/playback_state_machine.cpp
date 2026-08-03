#include "mediahub/core/playback_state_machine.h"

namespace mediahub::core {

PlaybackState PlaybackStateMachine::state() const noexcept {
    return state_;
}

PlaybackTransitionResult PlaybackStateMachine::transitionTo(
    const PlaybackState nextState) noexcept {
    if (nextState == state_) {
        return PlaybackTransitionResult::Unchanged;
    }
    if (!canTransition(state_, nextState)) {
        return PlaybackTransitionResult::Rejected;
    }

    state_ = nextState;
    return PlaybackTransitionResult::Changed;
}

bool PlaybackStateMachine::canTransition(const PlaybackState from,
                                         const PlaybackState to) noexcept {
    if (from == to) {
        return true;
    }

    switch (from) {
    case PlaybackState::Idle:
        return to == PlaybackState::Opening || to == PlaybackState::Failed;
    case PlaybackState::Opening:
        return to == PlaybackState::Buffering || to == PlaybackState::Playing ||
               to == PlaybackState::Paused || to == PlaybackState::Stopped ||
               to == PlaybackState::Ended || to == PlaybackState::Failed;
    case PlaybackState::Buffering:
        return to == PlaybackState::Opening || to == PlaybackState::Playing ||
               to == PlaybackState::Paused || to == PlaybackState::Stopped ||
               to == PlaybackState::Ended || to == PlaybackState::Failed;
    case PlaybackState::Playing:
        return to == PlaybackState::Opening || to == PlaybackState::Buffering ||
               to == PlaybackState::Paused || to == PlaybackState::Stopped ||
               to == PlaybackState::Ended || to == PlaybackState::Failed;
    case PlaybackState::Paused:
        return to == PlaybackState::Opening || to == PlaybackState::Buffering ||
               to == PlaybackState::Playing || to == PlaybackState::Stopped ||
               to == PlaybackState::Ended || to == PlaybackState::Failed;
    case PlaybackState::Stopped:
        return to == PlaybackState::Opening || to == PlaybackState::Buffering ||
               to == PlaybackState::Playing || to == PlaybackState::Failed;
    case PlaybackState::Ended:
        return to == PlaybackState::Opening || to == PlaybackState::Buffering ||
               to == PlaybackState::Playing || to == PlaybackState::Stopped ||
               to == PlaybackState::Failed;
    case PlaybackState::Failed:
        return to == PlaybackState::Opening || to == PlaybackState::Stopped;
    }

    return false;
}

void PlaybackStateMachine::reset() noexcept {
    state_ = PlaybackState::Idle;
}

}  // namespace mediahub::core
