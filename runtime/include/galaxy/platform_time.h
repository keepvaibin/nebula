// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The runtime's high-resolution clock in ticks of qpc_frequency() per second: QueryPerformanceCounter on Windows,
// CLOCK_MONOTONIC nanoseconds elsewhere (Apple: CLOCK_UPTIME_RAW nanoseconds).

#include <cstdint>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#else
#include <time.h>
#endif

namespace galaxy {

[[nodiscard]] inline std::int64_t qpc_now() noexcept {
#if defined(_WIN32)
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
#elif defined(__APPLE__)
    // Mach absolute time in nanoseconds: the clock of the display link, of the presents' times and of steady_clock
    // there (CLOCK_MONOTONIC on Darwin also counts the time the machine slept).
    return static_cast<std::int64_t>(clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
#else
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;
#endif
}

[[nodiscard]] inline std::int64_t qpc_frequency() noexcept {
#if defined(_WIN32)
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::int64_t>(value.QuadPart);
    }();
    return frequency;
#else
    return 1000000000;
#endif
}

// Milliseconds since an arbitrary start (GetTickCount64 on Windows).
[[nodiscard]] inline std::uint64_t steady_ms() noexcept {
#if defined(_WIN32)
    return GetTickCount64();
#else
    return static_cast<std::uint64_t>(qpc_now() / 1000000);
#endif
}

// A cadence on the clock for work that must keep it whatever the picture rate (a report line, a poll of the display):
// due() is true at most once an interval. Counting pictures instead runs such work 50 times as often at an unlimited
// rate as at 60 a second. The first moment is at once when `at_start`, else one interval after the first call.
class ClockInterval {
public:
    explicit constexpr ClockInterval(std::int64_t ms, bool at_start = false) noexcept : ms_(ms), at_start_(at_start) {}
    [[nodiscard]] bool due() noexcept {
        const std::int64_t now = qpc_now();
        if (next_ == 0 && !at_start_) {
            next_ = now + interval();
            return false;
        }
        if (now < next_) {
            return false;
        }
        next_ = now + interval();
        return true;
    }
    // The next moment one interval from now (a moment taken for another reason).
    void restart() noexcept { next_ = qpc_now() + interval(); }

private:
    [[nodiscard]] std::int64_t interval() const noexcept { return qpc_frequency() / 1000 * ms_; }
    std::int64_t ms_;
    bool at_start_;
    std::int64_t next_ = 0;
};

// Microseconds since an arbitrary start, from the high-resolution clock.
[[nodiscard]] inline std::uint64_t steady_us() noexcept {
    const std::int64_t ticks = qpc_now();
    const std::int64_t frequency = qpc_frequency();
    return static_cast<std::uint64_t>(ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency);
}

}  // namespace galaxy
