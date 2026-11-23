// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A wait to a moment on the runtime's clock (platform_time.h qpc_now) that ends on the moment, not when the OS timer
// gets round to it: the OS timer for most of the wait, then a short spin for the rest. Even a high-resolution waitable
// timer (Windows 10 1803+) wakes its thread a few hundred microseconds to over a millisecond late at times, and a
// thread woken late starts its frame late: the game's step and every picture's moment would carry that lateness. The
// spin's length (the margin) is the timer's own lateness as this thread has seen it: up at once to the latest late wake
// with a guard, down slowly while wakes are on time, held between kLeastMargin and kMostMargin. Each thread keeps its
// own waiter (timer and margin).

#include "galaxy/platform_time.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <intrin.h>
#if !defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#else
#include <cerrno>
#include <time.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif
#endif

namespace galaxy {

// The margin's bounds and guard, in microseconds.
inline constexpr std::int64_t kLeastMarginUs = 100;
inline constexpr std::int64_t kMostMarginUs = 2000;
inline constexpr std::int64_t kMarginGuardUs = 100;
inline constexpr std::int64_t kFirstMarginUs = 1000;

// The margin after a timer wake `late_us` past the moment it was set for (0 or less: on time or early): the late wake
// with its guard when that is more, else 1/16 of the way down to it.
[[nodiscard]] constexpr std::int64_t next_wait_margin_us(std::int64_t margin_us, std::int64_t late_us) noexcept {
    const std::int64_t wanted = std::clamp(std::max<std::int64_t>(late_us, 0) + kMarginGuardUs, kLeastMarginUs, kMostMarginUs);
    if (wanted >= margin_us) return wanted;
    return std::max(wanted, margin_us - (margin_us - wanted + 15) / 16);
}

class PreciseWait {
public:
    PreciseWait() noexcept {
#if defined(_WIN32)
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
#elif defined(__linux__)
        // Linux and Android let a sleep end up to the thread's timer slack late (50 us by default, more for a
        // background thread): this thread's is the least, so its sleeps end on time and the spin stays at its least.
        (void)prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);
#endif
    }
    ~PreciseWait() {
#if defined(_WIN32)
        if (timer_ != nullptr) CloseHandle(timer_);
#endif
    }
    PreciseWait(const PreciseWait&) = delete;
    PreciseWait& operator=(const PreciseWait&) = delete;

    // Returns at `due` (qpc_now() ticks) or as soon after as the thread runs; at once when it has passed.
    void until(std::int64_t due) noexcept {
        const std::int64_t frequency = qpc_frequency();
        std::int64_t now = qpc_now();
        if (now >= due) return;
        const std::int64_t target = due - margin_us_ * frequency / 1'000'000;
        if (target > now) {
            sleep_for_ticks(target - now, frequency);
            now = qpc_now();
            margin_us_ = next_wait_margin_us(margin_us_, (now - target) * 1'000'000 / frequency);
        }
        while (now < due) {
            pause();
            now = qpc_now();
        }
    }

    // The thread's own waiter.
    static PreciseWait& for_this_thread() noexcept {
        thread_local PreciseWait wait;
        return wait;
    }

    [[nodiscard]] std::int64_t margin_us() const noexcept { return margin_us_; }

private:
    static void pause() noexcept {
#if defined(_WIN32)
        YieldProcessor();
#elif defined(__x86_64__) || defined(__i386__)
        _mm_pause();
#elif defined(__aarch64__)
        __asm__ __volatile__("yield");
#endif
    }

    void sleep_for_ticks(std::int64_t ticks, std::int64_t frequency) noexcept {
#if defined(_WIN32)
        if (timer_ != nullptr) {
            LARGE_INTEGER relative;
            // 100 ns units, negative for a relative time.
            relative.QuadPart = -std::max<std::int64_t>(1, ticks * 10'000'000 / frequency);
            if (SetWaitableTimer(timer_, &relative, 0, nullptr, nullptr, FALSE)) {
                (void)WaitForSingleObject(timer_, INFINITE);
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds(ticks * 1'000'000'000 / frequency));
#else
        // qpc_now() is CLOCK_MONOTONIC (Apple: CLOCK_UPTIME_RAW) nanoseconds: a relative sleep of that length.
        (void)frequency;
        timespec length{};
        length.tv_sec = static_cast<time_t>(ticks / 1'000'000'000);
        length.tv_nsec = static_cast<long>(ticks % 1'000'000'000);
        // Interrupted by a signal: the rest of it.
        while (nanosleep(&length, &length) != 0 && errno == EINTR) {
        }
#endif
    }

#if defined(_WIN32)
    HANDLE timer_ = nullptr;
#endif
    std::int64_t margin_us_ = kFirstMarginUs;
};

// A moment on the qpc_now() clock given in nanoseconds of that clock (qpc_to_ns of a qpc_now() reading, plus lengths of
// time), in qpc_now()'s ticks. Never a std::chrono::steady_clock value: on Apple steady_clock is CLOCK_MONOTONIC_RAW, which
// counts the time the Mac sleeps, and qpc_now() is CLOCK_UPTIME_RAW, which does not (frame_due.h).
[[nodiscard]] inline std::int64_t qpc_from_ns(std::int64_t ns) noexcept {
    const std::int64_t frequency = qpc_frequency();
    if (frequency == 1'000'000'000) return ns;
    return ns / 1'000'000'000 * frequency + ns % 1'000'000'000 * frequency / 1'000'000'000;
}

}  // namespace galaxy
