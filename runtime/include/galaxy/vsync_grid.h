// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "galaxy/frame_due.h"
#include "galaxy/step_lock.h"

// The display's vsync grid as the game's frame clock, from the vsync callbacks a platform delivers (Android's
// Choreographer: platform/sdl/display_clock.cpp; Apple's display link: platform/apple/display_clock_apple.mm): the
// latest vsync, the interval between vsyncs, and the frame slots on that grid. Arithmetic on one monotonic nanosecond
// clock; the caller holds its own lock around it. Tested in runtime/src/vsync_grid_tests.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
    // period (less half a vsync) after the last slot. A clock that fell behind keeps its slots, as the host's clock keeps
    // its due moments (FrameDue::catch_up, P-274): a slot already past is returned, so the frames it missed run back to
    // back; further behind than FrameDue::kCatchUpNs (a suspended app, a load) the slots start again from now, and
    // `dropped`, when given, is set to the time dropped. `frame_period`: the period in whole vsyncs. False, and the host's
    // own clock runs the frames, when there is no grid (no vsync yet, or none for a quarter second: out of sight, the
    // display asleep) or the step does not lock to it (galaxy/step_lock.h: `in_between`, in-between pictures on;
    // 59.94, 60, 120 or 240 Hz keep the game on the grid with them off; at 50, 61, 75 or 90 Hz the host's clock runs it;
    // `variable`, a display whose refreshes follow the pictures: never locked, I-007).
    [[nodiscard]] bool next_slot(std::int64_t period, std::int64_t last, std::int64_t now, bool in_between,
                                 bool variable, std::int64_t& slot, std::int64_t& frame_period,
                                 std::int64_t* dropped = nullptr) const noexcept {
        if (vsync_ns == 0 || interval_ns == 0 || now - vsync_ns > 250000000) {
            return false;
        }
        const std::int64_t vsyncs = step_lock_refreshes(period, interval_ns, in_between, variable);
        if (vsyncs == 0) {
            return false;
        }
        std::int64_t earliest = last + period - interval_ns / 2;
        if (last == 0 || earliest < now - FrameDue::kCatchUpNs) {
            if (last != 0 && dropped != nullptr) {
                *dropped = now - earliest;
            }
            earliest = now;
        }
        // The first vsync at or after `earliest`, before the latest one while the clock catches up.
        const std::int64_t from = earliest - vsync_ns;
        const std::int64_t steps = from >= 0 ? (from + interval_ns - 1) / interval_ns : -(-from / interval_ns);
        slot = vsync_ns + steps * interval_ns;
        frame_period = vsyncs * interval_ns;
        return true;
    }

private:
    std::int64_t same_whole_ = 0;  // the whole number of intervals the last gaps above one were
    std::int64_t same_run_ = 0;    // how many in a row
};

// A display's refreshes from when its presents completed (Linux, I-005: platform/sdl/display_clock.cpp): the stamps of
// presents the render thread saw complete (present wait) or present timing's own times, fitted to the display mode's
// period. Presents complete on refreshes but not on every one, and a stamp taken by a thread that woke comes after its
// refresh, never before. So each gap between stamps is a whole number of the mode's periods (1 to kMostRefreshes, within
// a quarter period; a stamp off that grid is a late wake and is left out, a longer gap a pause from which the grid
// starts again). The period is the least-squares slope of the last kPoints stamps against their refresh numbers, kept
// within 2 % of the mode's: a wake's delay on one stamp moves it by that delay over the refreshes the points span,
// where the median of single gaps moves by the delay over one. The latest refresh follows the stamps on that period,
// taken at once from a stamp earlier than predicted and an eighth of the way toward a later one (an exact time is
// taken as it is). Arithmetic on one monotonic nanosecond clock; the caller holds its own lock.
// Tested in runtime/src/vsync_grid_tests.cpp.
struct PresentStampFit {
    std::int64_t vsync_ns = 0;     // the latest refresh on the fitted grid; 0 before the first stamp
    std::int64_t interval_ns = 0;  // the fitted period; 0 until the mode's period is known

    static constexpr std::int64_t kMostRefreshes = 16;  // a longer gap: a pause (out of sight, a load)
    static constexpr std::size_t kPoints = 32;          // the stamps the period is fitted over

    // The display mode's period (0: not known). Another mode starts the fit again.
    void mode(std::int64_t mode_ns) noexcept {
        if (mode_ns == mode_ns_) {
            return;
        }
        *this = PresentStampFit{};
        mode_ns_ = mode_ns;
        interval_ns = mode_ns;
    }

    // A present completed at `stamp` (`exact`: the presentation engine's own time, else the stamp of a thread that saw
    // it complete). False when the stamp was not taken (no mode, the same refresh again, a gap off the grid).
    bool note(std::int64_t stamp, bool exact) noexcept {
        if (mode_ns_ <= 0) {
            return false;
        }
        if (vsync_ns == 0 || last_stamp_ == 0) {
            restart(stamp);
            return true;
        }
        const std::int64_t raw = stamp - last_stamp_;
        const std::int64_t whole = (raw + interval_ns / 2) / interval_ns;
        if (whole < 1) {
            return false;  // the same refresh again, or a stamp out of order
        }
        if (whole > kMostRefreshes) {
            restart(stamp);  // a pause: the grid starts again from this stamp, on the period fitted so far
            return false;
        }
        if (std::llabs(raw - whole * interval_ns) > interval_ns / 4) {
            return false;  // a late wake: left out, the next gap read from the stamp before it
        }
        last_stamp_ = stamp;
        refresh_ += whole;
        add(stamp);
        if (count_ >= 4u) {
            // The slope of stamp on refresh number, both from the oldest point held (doubles: spans of seconds).
            const Point& oldest = points_[(next_ + kPoints - count_) % kPoints];
            double mean_x = 0.0, mean_y = 0.0;
            for (std::size_t i = 0; i < count_; ++i) {
                const Point& p = points_[(next_ + kPoints - count_ + i) % kPoints];
                mean_x += static_cast<double>(p.refresh - oldest.refresh);
                mean_y += static_cast<double>(p.stamp - oldest.stamp);
            }
            mean_x /= static_cast<double>(count_);
            mean_y /= static_cast<double>(count_);
            double xx = 0.0, xy = 0.0;
            for (std::size_t i = 0; i < count_; ++i) {
                const Point& p = points_[(next_ + kPoints - count_ + i) % kPoints];
                const double x = static_cast<double>(p.refresh - oldest.refresh) - mean_x;
                xx += x * x;
                xy += x * (static_cast<double>(p.stamp - oldest.stamp) - mean_y);
            }
            if (xx > 0.0) {
                interval_ns = std::clamp(static_cast<std::int64_t>(std::llround(xy / xx)), mode_ns_ - mode_ns_ / 50,
                                         mode_ns_ + mode_ns_ / 50);
            }
        }
        // The refresh the stamp belongs to, predicted on the fitted period from the latest.
        const std::int64_t from = stamp - vsync_ns;
        const std::int64_t predicted = vsync_ns + (from + interval_ns / 2) / interval_ns * interval_ns;
        vsync_ns = exact || stamp < predicted ? stamp : predicted + (stamp - predicted) / 8;
        return true;
    }

private:
    struct Point {
        std::int64_t refresh = 0;  // refreshes since the grid started
        std::int64_t stamp = 0;
    };
    void restart(std::int64_t stamp) noexcept {
        vsync_ns = stamp;
        last_stamp_ = stamp;
        refresh_ = 0;
        count_ = 0;
        add(stamp);
    }
    void add(std::int64_t stamp) noexcept {
        points_[next_] = {refresh_, stamp};
        next_ = (next_ + 1u) % kPoints;
        count_ = std::min(count_ + 1u, kPoints);
    }
    std::int64_t mode_ns_ = 0;
    std::int64_t last_stamp_ = 0;
    std::int64_t refresh_ = 0;
    std::array<Point, kPoints> points_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

}  // namespace galaxy
