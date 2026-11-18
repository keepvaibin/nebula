#pragma once

#include <nebula_types.h>

namespace MR {
    /// @brief Moves the player's health to `max` one point at a time through the actor's own decLife(0) and incLife(1)
    /// (their sound, the damage event, the most falling back after a Life Up), as MarioActor::changeMaxLife does. It stops
    /// when a step changes nothing: decLife refuses while the player is invincible, incLife while the actor's nerve takes
    /// no change (a press or a game over between a Life Up's pickup and its demo). The console looped there for ever and
    /// the game hung (N-3015).
    template < class Actor >
    inline void stepHealthTo(Actor& rActor, u32 max) {
        while (rActor.mHealth != max) {
            const u32 before = rActor.mHealth;
            if (before > max) {
                rActor.decLife(0);
            } else {
                rActor.incLife(1);
            }
            if (rActor.mHealth == before) {
                break;
            }
        }
    }
}  // namespace MR
