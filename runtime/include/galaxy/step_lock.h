// SPDX-License-Identifier: GPL-3.0-only
// Whether the game's step follows the display's refreshes (Windows' frame phase, the Android and Apple display clocks)
// or the host's own clock at exactly 60 steps a second.
//
// In-between pictures off: the step locks to a display within 0.2% of a whole number of refreshes a step. A 59.94 Hz
// display runs the game 0.1% slower, as the console did, with no doubled or dropped picture every 17 s; a panel further
// off (61 Hz, 50, 75, 144) keeps exactly 60 and its pictures are paced by the display, as at any other rate. Sound
// follows the audio device's clock, not the step, so a locked step moves game sounds against the music by 0.2% at
// most. In-between pictures on: the step stays at exactly 60, since blended pictures fill any rate; only a display at
// an exact multiple (to 0.02%, the measured rate's own noise) ties the step to its refreshes, so one refresh shows each
// step's own picture.
#pragma once

#include <cstdint>

namespace galaxy {

inline constexpr std::int64_t kStepLockNear = 500;    // 1/500: 0.2%
inline constexpr std::int64_t kStepLockExact = 5000;  // 1/5000: 0.02%

// The refreshes in a step of `period_ns` when the step follows the display, else 0. A variable refresh display
// (`variable`, render/variable_refresh.h) follows the pictures, so the step is never locked to it: the host's clock runs
// the 60 steps (I-007).
[[nodiscard]] constexpr std::int64_t step_lock_refreshes(std::int64_t period_ns, std::int64_t refresh_ns,
                                                         bool in_between, bool variable) noexcept {
    if (variable || period_ns <= 0 || refresh_ns <= 0) {
        return 0;
    }
    const std::int64_t count = (period_ns + refresh_ns / 2) / refresh_ns;
    if (count < 1) {
        return 0;
    }
    const std::int64_t gap = count * refresh_ns - period_ns;
    const std::int64_t off = gap < 0 ? -gap : gap;
    return off * (in_between ? kStepLockExact : kStepLockNear) <= period_ns ? count : 0;
}

}  // namespace galaxy
