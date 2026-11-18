// SPDX-License-Identifier: GPL-3.0-only
// MR::stepHealthTo (Game/Player/MarioLife.hpp, N-3015): MarioActor::changeMaxLife moves the health to the new most one
// point at a time and stops when a step is refused. MR::loseHealthToPress (N-3021): a press takes all the health, more
// than the console's six when a mod's rules give more. A Life Up's demo starts in a later step than its pickup; a press
// between them puts the player in TimeWait with no health, incLife refuses there, and the console's loop never ended.
// The test's actor refuses as the game's does; past cCallBound calls the actor gives the health itself, so a loop that
// never ends shows as a failure, not a hang.
#include "Game/Player/MarioLife.hpp"

#include <stdio.h>

namespace {
    int sFailures = 0;
    constexpr u32 cCallBound = 1000;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    // MarioActor's decLife(0) and incLife(1), with its refusals: invincible, and a nerve that takes no change.
    struct Actor {
        u32 mHealth = 0;
        u32 mMaxHealth = 0;
        bool mInvincible = false;
        bool mNerveChanges = true;
        u32 mCalls = 0;

        void giveUp() {
            if (++mCalls >= cCallBound) {
                mHealth = mMaxHealth;
            }
        }
        void decLife(u16) {
            if (!mInvincible && mHealth != 0) {
                mHealth--;
            }
            giveUp();
        }
        void incLife(u32 amt) {
            if (mNerveChanges) {
                mHealth = mHealth + amt >= mMaxHealth ? mMaxHealth : mHealth + amt;
            }
            giveUp();
        }
    };

    Actor changeMaxLife(u32 health, u32 max, bool invincible, bool nerveChanges) {
        Actor actor;
        actor.mHealth = health;
        actor.mMaxHealth = max;
        actor.mInvincible = invincible;
        actor.mNerveChanges = nerveChanges;
        MR::stepHealthTo(actor, max);
        return actor;
    }
}  // namespace

int main() {
    // A Life Up at 3 of 3: three steps up to 6.
    Actor up = changeMaxLife(3, 6, false, true);
    expect(up.mHealth == 6 && up.mCalls == 3, "a Life Up at full health takes three steps to 6");
    // Back to 3 from 6 (a mod's own most): three steps down.
    Actor down = changeMaxLife(6, 3, false, true);
    expect(down.mHealth == 3 && down.mCalls == 3, "a lower most takes three steps down");
    // Pressed between the pickup and the demo: no health, the nerve takes no change. One refused step, then it stops.
    Actor pressed = changeMaxLife(0, 6, false, false);
    expect(pressed.mCalls == 1, "a refused step up ends the loop (the console looped for ever)");
    expect(pressed.mHealth == 0, "and the pressed player keeps no health");
    // Invincible above the new most: decLife refuses.
    Actor star = changeMaxLife(6, 3, true, true);
    expect(star.mCalls == 1, "a refused step down ends the loop");
    expect(star.mHealth == 6, "and the health stays");
    // Already there: no step.
    Actor same = changeMaxLife(6, 6, false, false);
    expect(same.mCalls == 0, "health at the most takes no step");
    // A press (N-3021): the console's health (6 at most) takes its six losses, as the console did.
    Actor pressedSix;
    pressedSix.mHealth = 3;
    MR::loseHealthToPress(pressedSix);
    expect(pressedSix.mHealth == 0 && pressedSix.mCalls == 6, "a press at 3 takes the console's six losses");
    // A mod's rules give 9: the press takes all of it.
    Actor pressedNine;
    pressedNine.mHealth = 9;
    MR::loseHealthToPress(pressedNine);
    expect(pressedNine.mHealth == 0, "a press at a mod's 9 leaves no health (six fixed left 3 through the game over)");
    // Invincible: every loss refused, as before.
    Actor pressedStar;
    pressedStar.mHealth = 3;
    pressedStar.mInvincible = true;
    MR::loseHealthToPress(pressedStar);
    expect(pressedStar.mHealth == 3 && pressedStar.mCalls == 6, "an invincible press loses nothing");
    if (sFailures == 0) {
        printf("native_mario_life_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
