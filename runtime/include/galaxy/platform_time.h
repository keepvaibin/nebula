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

// Microseconds since an arbitrary start, from the high-resolution clock.
[[nodiscard]] inline std::uint64_t steady_us() noexcept {
    const std::int64_t ticks = qpc_now();
    const std::int64_t frequency = qpc_frequency();
    return static_cast<std::uint64_t>(ticks / frequency * 1000000 + ticks % frequency * 1000000 / frequency);
}

}  // namespace galaxy
