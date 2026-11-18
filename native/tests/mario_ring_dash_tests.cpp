// SPDX-License-Identifier: GPL-3.0-only
// The dash ring times of MarioSwim and MarioFoo (N-3023): their passRing copies the ring's placement times
// (DashRing::mBoostTime and mMaxDuration, s32 arguments a mod's stage sets) into the swim's and the flight's timers.
// They were u16, so a ring time past 65535 wrapped: 65536 gave no dash. The timers must hold every time an s32
// argument gives.
#include "Game/MapObj/DashRing.hpp"
#include "Game/Player/MarioFoo.hpp"
#include "Game/Player/MarioSwim.hpp"

#include <limits>
#include <stdio.h>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    template < class Timer, class Argument >
    constexpr bool holdsEveryTime() {
        return static_cast< unsigned long long >(std::numeric_limits< Timer >::max()) >=
               static_cast< unsigned long long >(std::numeric_limits< Argument >::max());
    }
}  // namespace

int main() {
    expect(holdsEveryTime< decltype(MarioSwim::mRingDashTimer), decltype(DashRing::mBoostTime) >(),
           "mRingDashTimer holds every boost time a ring's placement gives");
    expect(holdsEveryTime< decltype(MarioSwim::mRingDashMaxDuration), decltype(DashRing::mMaxDuration) >(),
           "mRingDashMaxDuration holds every duration");
    expect(holdsEveryTime< decltype(MarioFoo::mRingBoostTimer), decltype(DashRing::mBoostTime) >(),
           "the flight's mRingBoostTimer holds every boost time");
    expect(holdsEveryTime< decltype(MarioFoo::mRingBoostLength), decltype(DashRing::mMaxDuration) >(),
           "the flight's mRingBoostLength holds every duration");
    if (sFailures == 0) {
        printf("native_mario_ring_dash_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
