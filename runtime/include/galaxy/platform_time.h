// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The runtime's high-resolution clock in ticks of qpc_frequency() per second: QueryPerformanceCounter on Windows,
// CLOCK_MONOTONIC nanoseconds elsewhere.

#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <time.h>
#endif

namespace galaxy {

[[nodiscard]] inline std::int64_t qpc_now() noexcept {
#if defined(_WIN32)
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
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

}  // namespace galaxy
