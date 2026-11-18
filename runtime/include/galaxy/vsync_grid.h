// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "galaxy/step_lock.h"

// The display's vsync grid as the game's frame clock, from the vsync callbacks a platform delivers (Android's
// Choreographer: platform/sdl/display_clock.cpp; Apple's display link: platform/apple/display_clock_apple.mm): the
// latest vsync, the interval between vsyncs, and the frame slots on that grid. Arithmetic on one monotonic nanosecond
// clock; the caller holds its own lock around it. Tested in runtime/src/vsync_grid_tests.cpp.

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace galaxy {

struct VsyncGrid {
    std::int64_t vsync_ns = 0;     // the latest vsync; 0 before the first
    std::int64_t interval_ns = 0;  // between vsyncs; 0 until known

    // Bounds of a vsync interval: 500 Hz to 20 Hz. A gap past the longest is the display out of sight or asleep.
    static constexpr std::int64_t kShortest = 2000000;
    static constexpr std::int64_t kLongest = 50000000;

    // A vsync at `vsync`; `stated`: the display's period as the system gives it, or 0. The system's period stands when
    // it gives one. Else each gap is a whole number of intervals (callbacks the platform skipped are no new rate) and
    // the interval follows the gaps, an eighth of each difference; a gap that is no whole number of intervals is the
    // display's new rate, and so are three gaps in a row of the same whole number above one (the rate fell).
    void note(std::int64_t vsync, std::int64_t stated) noexcept {
        if (vsync_ns != 0 && vsync <= vsync_ns) {
            return;  // the same vsync again, or one out of order
        }
        const std::int64_t delta = vsync_ns != 0 ? vsync - vsync_ns : 0;
        vsync_ns = vsync;
        if (stated > 0) {
            interval_ns = stated;
            same_run_ = 0;
            return;
        }
        if (delta < kShortest) {
            return;  // the first vsync, or two callbacks of one vsync
        }
        if (interval_ns == 0) {
            if (delta <= kLongest) {
                interval_ns = delta;
            }
            return;
        }
        const std::int64_t whole = (delta + interval_ns / 2) / interval_ns;
        const std::int64_t error = delta - whole * interval_ns;
        if (whole >= 1 && std::llabs(error) <= interval_ns / 8) {
            if (whole == 1) {
                same_run_ = 0;
                interval_ns += error / 8;
                return;
            }
            same_run_ = whole == same_whole_ ? same_run_ + 1 : 1;
            same_whole_ = whole;
            if (same_run_ >= 3 && delta <= kLongest) {
                interval_ns = delta;
                same_run_ = 0;
                return;
            }
            interval_ns += error / whole / 8;
            return;
        }
        same_run_ = 0;
        if (delta <= kLongest) {
            interval_ns = delta;
        }
    }

    // The frame slot after `last` (0: none yet) for frames `period` apart, at `now`: the first vsync at least one frame
    // period (less half a vsync) after the last slot, or from now when the clock fell behind (a suspended app, a stalled
    // thread), as a display skips the fields it missed. `frame_period`: the period in whole vsyncs. False, and the host's
    // own clock runs the frames, when there is no grid (no vsync yet, or none for a quarter second: out of sight, the
    // display asleep) or the step does not lock to it (galaxy/step_lock.h: `in_between`, in-between pictures on;
    // 59.94, 60, 120 or 240 Hz keep the game on the grid with them off; at 50, 61, 75 or 90 Hz the host's clock runs it).
    [[nodiscard]] bool next_slot(std::int64_t period, std::int64_t last, std::int64_t now, bool in_between,
                                 std::int64_t& slot, std::int64_t& frame_period) const noexcept {
        if (vsync_ns == 0 || interval_ns == 0 || now - vsync_ns > 250000000) {
            return false;
        }
        const std::int64_t vsyncs = step_lock_refreshes(period, interval_ns, in_between);
        if (vsyncs == 0) {
            return false;
        }
        std::int64_t earliest = last + period - interval_ns / 2;
        if (last == 0 || earliest < now - period) {
            earliest = now;
        }
        const std::int64_t ahead = earliest - vsync_ns + interval_ns - 1;
        const std::int64_t steps = ahead > 0 ? ahead / interval_ns : 0;
        slot = vsync_ns + steps * interval_ns;
        frame_period = vsyncs * interval_ns;
        return true;
    }

private:
    std::int64_t same_whole_ = 0;  // the whole number of intervals the last gaps above one were
    std::int64_t same_run_ = 0;    // how many in a row
};

}  // namespace galaxy
