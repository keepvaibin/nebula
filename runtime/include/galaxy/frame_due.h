// SPDX-License-Identifier: GPL-3.0-only
// The game's frame wait keeps its due moments on the runtime's own clock (platform_time.h qpc_now), in nanoseconds, from
// its first reading to its last. No std::chrono::steady_clock value ever enters it: on Apple steady_clock is
// CLOCK_MONOTONIC_RAW, which goes on counting while the Mac sleeps, and qpc_now() is CLOCK_UPTIME_RAW, which stops; a
// moment taken from one and read on the other is hours off on a Mac that has slept, and the frame wait slept that long
// (the 2.0.0 beta's Mac start that never reached its title). Where the clocks are the same (Windows, Linux, Android) it
// made no difference, which is why it was never seen there.
#pragma once

#include <algorithm>
#include <cstdint>

namespace galaxy {

class FrameDue {
public:
    // How far ahead a due moment may be, in the frame periods it lies ahead, and at least this long. The next frame is one
    // period ahead of the last (a display's phase correction adds less than a refresh), so a healthy wait is never more
    // than a period or two; 250 ms is a game at 4 frames a second. Anything further is a fault of the clock, not a wait.
    static constexpr std::int64_t kMostFrames = 8;
    static constexpr std::int64_t kMostNs = 250'000'000;

    // The moment the next frame is due, from `now_ns` (the runtime's clock in nanoseconds) and the period that holds now.
    // `cut` is set when the moment kept was further ahead than allowed: it is then replaced by one period from now, and
    // `ahead_ns` is how far ahead it was (for one log line).
    struct Due {
        std::int64_t ns = 0;
        bool cut = false;
        std::int64_t ahead_ns = 0;
    };

    [[nodiscard]] Due due(std::int64_t now_ns, std::int64_t period_ns) noexcept {
        if (!started_) {
            next_ns_ = now_ns + period_ns;
            started_ = true;
        }
        const std::int64_t ahead = next_ns_ - now_ns;
        if (ahead > std::max(kMostFrames * period_ns, kMostNs)) {
            next_ns_ = now_ns + period_ns;
            return {next_ns_, true, ahead};
        }
        return {next_ns_, false, ahead};
    }

    // The wait ended at `now_ns`: the next frame is one period after the one just due, or one period from now when the
    // wake-up came late (the steps it missed are skipped, not run to catch up).
    void waited(std::int64_t now_ns, std::int64_t period_ns) noexcept {
        next_ns_ += period_ns;
        if (now_ns >= next_ns_) next_ns_ = now_ns + period_ns;
    }

    // The next frame at a moment chosen by the caller (a display's own vsync, a phase correction): in the runtime's clock.
    void set_next(std::int64_t next_ns) noexcept {
        next_ns_ = next_ns;
        started_ = true;
    }

    // The moment now kept: one period after the wake-up from a suspension, or the display's frame.
    [[nodiscard]] std::int64_t next() const noexcept { return next_ns_; }

private:
    std::int64_t next_ns_ = 0;
    bool started_ = false;
};

}  // namespace galaxy
