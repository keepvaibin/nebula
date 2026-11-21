// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The runtime's high-resolution clock in ticks of qpc_frequency() per second: QueryPerformanceCounter on Windows,
// CLOCK_MONOTONIC nanoseconds elsewhere (Apple: CLOCK_UPTIME_RAW nanoseconds).

#include <cstdint>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#if defined(_M_X64) || defined(__x86_64__)
#include <intrin.h>
#define GALAXY_PROCESS_CYCLES 1
#endif
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

// Ticks of the clock as nanoseconds, a span or a moment, either sign. Whole seconds and the rest apart: ticks times
// 10^9 overflows 64 bits past 9.2 seconds where a tick is a nanosecond (everywhere but Windows).
[[nodiscard]] inline std::int64_t qpc_to_ns(std::int64_t ticks) noexcept {
    const std::int64_t frequency = qpc_frequency();
    return ticks / frequency * 1000000000 + ticks % frequency * 1000000000 / frequency;
}

// The processor time this process has used, all threads, over a span the caller measures: take a stamp at each end
// and ask process_cpu_ns_between. Windows on x64 reads QueryProcessCycleTime (exact; GetProcessTimes is sampled at
// the clock tick) and turns cycles into time by the time stamp counter's own rate over the same span (the counter and
// the clock above read at both ends); that needs a counter that runs at one rate (invariant: CPUID 0x80000007 EDX bit
// 8), else, and on Windows on another processor family, the time is not known. Elsewhere it is
// clock_gettime(CLOCK_PROCESS_CPUTIME_ID).
struct ProcessCpuStamp {
    std::int64_t qpc = 0;        // qpc_now() at the stamp
    std::uint64_t counter = 0;   // the time stamp counter (Windows on x64)
    std::uint64_t cycles = 0;    // the process's cycles (Windows on x64), else its processor time in nanoseconds
    bool known = false;
};

// Whether process_cpu_stamp gives a processor time on this machine.
[[nodiscard]] inline bool process_cpu_known() noexcept {
#if defined(GALAXY_PROCESS_CYCLES)
    static const bool invariant = [] {
        int regs[4] = {};
        __cpuid(regs, static_cast<int>(0x80000000u));
        if (static_cast<unsigned>(regs[0]) < 0x80000007u) return false;
        __cpuid(regs, 0x80000007);
        return ((regs[3] >> 8) & 1) != 0;
    }();
    return invariant;
#elif defined(_WIN32)
    return false;
#else
    return true;
#endif
}

[[nodiscard]] inline ProcessCpuStamp process_cpu_stamp() noexcept {
    ProcessCpuStamp s;
    if (!process_cpu_known()) return s;
#if defined(GALAXY_PROCESS_CYCLES)
    ULONG64 cycles = 0;
    s.qpc = qpc_now();
    s.counter = __rdtsc();
    if (!QueryProcessCycleTime(GetCurrentProcess(), &cycles)) return s;
    s.cycles = cycles;
    s.known = true;
#elif !defined(_WIN32)
    timespec now{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) != 0) return s;
    s.qpc = qpc_now();
    s.cycles = static_cast<std::uint64_t>(now.tv_sec) * 1000000000u + static_cast<std::uint64_t>(now.tv_nsec);
    s.known = true;
#endif
    return s;
}

// Cycles used over a span as nanoseconds: the counter ticked `counter_ticks` while the clock ticked `qpc_ticks` (at
// `frequency` a second), so the counter runs at counter_ticks / (qpc_ticks / frequency) a second. -1: not known (an
// empty span).
[[nodiscard]] inline std::int64_t cycles_to_ns(std::uint64_t cycles, std::uint64_t counter_ticks, std::int64_t qpc_ticks,
                                               std::int64_t frequency) noexcept {
    if (counter_ticks == 0 || qpc_ticks <= 0 || frequency <= 0) return -1;
    const long double span_seconds = static_cast<long double>(qpc_ticks) / static_cast<long double>(frequency);
    const long double counter_hz = static_cast<long double>(counter_ticks) / span_seconds;
    return static_cast<std::int64_t>(static_cast<long double>(cycles) / counter_hz * 1e9L + 0.5L);
}

// The processor time between two stamps in nanoseconds, -1 when either is not known or the span is empty.
[[nodiscard]] inline std::int64_t process_cpu_ns_between(const ProcessCpuStamp& from, const ProcessCpuStamp& to) noexcept {
    if (!from.known || !to.known || to.cycles < from.cycles) return -1;
#if defined(GALAXY_PROCESS_CYCLES)
    if (to.counter <= from.counter) return -1;
    return cycles_to_ns(to.cycles - from.cycles, to.counter - from.counter, to.qpc - from.qpc, qpc_frequency());
#else
    return static_cast<std::int64_t>(to.cycles - from.cycles);
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
