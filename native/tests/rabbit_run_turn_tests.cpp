// SPDX-License-Identifier: GPL-3.0-only
// MR::turnUpWhileBlocked (Game/NPC/RabbitRunTurn.hpp, N-3018): DemoRabbit turns its front up away from the map at most
// cRunTurnMost times. The console turned until the way was clear, and with the map in the way straight up, or no gravity,
// it never was. The test's map stops blocking itself past cProbeBound probes, so a loop that never ends shows as a
// failure, not a hang.
#include "Game/NPC/RabbitRunTurn.hpp"

#include <math.h>
#include <stdio.h>

namespace {
    int sFailures = 0;
    constexpr u32 cProbeBound = 1000;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    // The game's Vec3f as the turn uses it.
    struct Vec {
        f32 x, y, z;
        f32 length() const { return sqrtf(x * x + y * y + z * z); }
        void setLength(f32 rLength) {
            const f32 now = length();
            if (now != 0.0f) {
                x *= rLength / now;
                y *= rLength / now;
                z *= rLength / now;
            }
        }
        Vec operator+(const Vec& rOther) const { return {x + rOther.x, y + rOther.y, z + rOther.z}; }
        Vec operator/(f32 div) const { return {x / div, y / div, z / div}; }
    };

    // A map that blocks a front with y under `clearAbove`; `probes` counts the calls.
    struct Map {
        f32 clearAbove;
        u32 probes = 0;
        bool operator()(const Vec& rFront) {
            return ++probes < cProbeBound && rFront.y < clearAbove;
        }
    };
}  // namespace

int main() {
    const Vec up = {0.0f, 1.0f, 0.0f};
    // A wall ahead, clear at 45 degrees up: one turn from level (to 45 degrees), then clear.
    Vec front = {0.0f, 0.0f, 1.0f};
    Map wall{0.7f};
    MR::turnUpWhileBlocked(front, up, wall);
    expect(wall.probes == 2, "a wall clear at 45 degrees up takes one turn");
    expect(fabsf(front.length() - 1.0f) < 1e-6f, "the turn keeps the front's length");
    // A ceiling too: the map never clears. The front ends up, after the most turns.
    Vec blocked = {0.0f, 0.0f, 1.0f};
    Map ceiling{2.0f};
    MR::turnUpWhileBlocked(blocked, up, ceiling);
    expect(ceiling.probes == MR::cRunTurnMost, "a way blocked straight up stops after the most turns (the console turned for ever)");
    expect(fabsf(blocked.y - 1.0f) < 1e-6f, "and the front points up");
    // No gravity: the front never turns; the turns still end.
    Vec still = {1.0f, 0.0f, 0.0f};
    Map nowhere{2.0f};
    MR::turnUpWhileBlocked(still, Vec{0.0f, 0.0f, 0.0f}, nowhere);
    expect(nowhere.probes == MR::cRunTurnMost, "with no gravity the turns end");
    // A clear way: no turn.
    Vec clear = {0.0f, 0.0f, 1.0f};
    Map open{-2.0f};
    MR::turnUpWhileBlocked(clear, up, open);
    expect(open.probes == 1 && clear.z == 1.0f, "a clear way takes no turn");
    if (sFailures == 0) {
        printf("native_rabbit_run_turn_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
