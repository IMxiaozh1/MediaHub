#include "mediahub/core/playback_state_machine.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace mediahub::core {
namespace {

constexpr std::array kStates{
    PlaybackState::Idle,      PlaybackState::Opening, PlaybackState::Buffering,
    PlaybackState::Playing,   PlaybackState::Paused,  PlaybackState::Stopped,
    PlaybackState::Ended,     PlaybackState::Failed,
};

constexpr std::array<std::array<bool, kStates.size()>, kStates.size()> kExpectedTransitions{{
    {{true, true, false, false, false, false, false, true}},
    {{false, true, true, true, true, true, true, true}},
    {{false, true, true, true, true, true, true, true}},
    {{false, true, true, true, true, true, true, true}},
    {{false, true, true, true, true, true, true, true}},
    {{false, true, true, true, false, true, false, true}},
    {{false, true, true, true, false, true, true, true}},
    {{false, true, false, false, false, true, false, true}},
}};

void moveToState(PlaybackStateMachine& machine, const PlaybackState state) {
    if (state == PlaybackState::Idle) {
        return;
    }
    if (state != PlaybackState::Failed) {
        ASSERT_EQ(machine.transitionTo(PlaybackState::Opening),
                  PlaybackTransitionResult::Changed);
    }
    ASSERT_NE(machine.transitionTo(state), PlaybackTransitionResult::Rejected);
}

TEST(PlaybackStateMachineTest, StartsIdle) {
    const PlaybackStateMachine machine;
    EXPECT_EQ(machine.state(), PlaybackState::Idle);
}

TEST(PlaybackStateMachineTest, ImplementsTheCompleteTransitionTable) {
    for (std::size_t fromIndex = 0; fromIndex < kStates.size(); ++fromIndex) {
        for (std::size_t toIndex = 0; toIndex < kStates.size(); ++toIndex) {
            SCOPED_TRACE(::testing::Message()
                         << "from=" << fromIndex << ", to=" << toIndex);

            PlaybackStateMachine machine;
            moveToState(machine, kStates[fromIndex]);

            const bool isExpected = kExpectedTransitions[fromIndex][toIndex];
            EXPECT_EQ(PlaybackStateMachine::canTransition(kStates[fromIndex], kStates[toIndex]),
                      isExpected);

            const auto result = machine.transitionTo(kStates[toIndex]);
            if (!isExpected) {
                EXPECT_EQ(result, PlaybackTransitionResult::Rejected);
                EXPECT_EQ(machine.state(), kStates[fromIndex]);
            } else if (fromIndex == toIndex) {
                EXPECT_EQ(result, PlaybackTransitionResult::Unchanged);
                EXPECT_EQ(machine.state(), kStates[fromIndex]);
            } else {
                EXPECT_EQ(result, PlaybackTransitionResult::Changed);
                EXPECT_EQ(machine.state(), kStates[toIndex]);
            }
        }
    }
}

TEST(PlaybackStateMachineTest, ResetReturnsEveryStateToIdle) {
    for (const auto state : kStates) {
        PlaybackStateMachine machine;
        moveToState(machine, state);
        machine.reset();
        EXPECT_EQ(machine.state(), PlaybackState::Idle);
    }
}

}  // namespace
}  // namespace mediahub::core
