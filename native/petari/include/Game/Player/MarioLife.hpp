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

    /// @brief The health losses of a press (MarioActor::setPress): the console's six, the most health it had, or as many
    /// as the health is when a mod's rules give more (GameRules::playerHealth, lifeUpHealth), so a press always ends at
    /// none as the game over that follows it expects. With six fixed, a health of 9 was left at 3 through the game over
    /// (N-3021). Each loss goes through decLife(0) as before (its refusal while invincible, the damage event).
    constexpr u32 cPressLosses = 6;
    template < class Actor >
    inline void loseHealthToPress(Actor& rActor) {
        const u32 losses = rActor.mHealth > cPressLosses ? rActor.mHealth : cPressLosses;
        for (u32 loss = 0; loss < losses; loss++) {
            rActor.decLife(0);
        }
    }
}  // namespace MR
