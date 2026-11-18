#pragma once

#include <nebula_types.h>

namespace MR {
    /// @brief The most turns of turnUpWhileBlocked: each halves the angle between the front and `up`, so after 32 the
    /// front is `up` to a float's precision (pi / 2^32) and a turn more changes nothing.
    constexpr u32 cRunTurnMost = 32;

    /// @brief DemoRabbit::updateRun's turn away from the map: while `rIsBlocked(front)`, the front turns half way to `up`
    /// (the two added and halved, its length kept), at most cRunTurnMost times; the front after the last turn stays. The
    /// console turned until the way was clear: with the map still in the way straight up (the rabbit under a ledge or a
    /// ceiling, chased into a wall by the player) or no gravity (the front then never turns), its loop never ended and
    /// the game hung (N-3018).
    template < class Vector, class Blocked >
    inline void turnUpWhileBlocked(Vector& rFront, const Vector& rUp, Blocked&& rIsBlocked) {
        for (u32 turn = 0; turn < cRunTurnMost && rIsBlocked(rFront); turn++) {
            const f32 length = rFront.length();
            rFront = (rFront + rUp) / 2.0f;
            rFront.setLength(length);
        }
    }
}  // namespace MR
