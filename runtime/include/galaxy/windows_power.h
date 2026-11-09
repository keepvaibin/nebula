// SPDX-License-Identifier: GPL-3.0-only
// Thread naming, the timer-resolution hold and the power-throttling opt-out for the host's threads.
#pragma once

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <atomic>

#include <cstddef>
#include <cstdio>

namespace galaxy::host {

// Names the calling thread for debuggers, profilers and minidumps, and logs the
// mapping once so a crash line's thread id identifies the thread's role.
inline void name_current_thread(const wchar_t* name) noexcept {
    (void)SetThreadDescription(GetCurrentThread(), name);
    char narrow[96]{};
    for (std::size_t i = 0; name[i] != L'\0' && i + 1u < sizeof(narrow); ++i) {
        narrow[i] = name[i] < 0x80 ? static_cast<char>(name[i]) : '?';
    }
    std::fprintf(stderr, "[thread] name=%s id=%lu\n", narrow,
                 static_cast<unsigned long>(GetCurrentThreadId()));
}

// timeBeginPeriod(1) while held; repeated calls with the same value do nothing.
inline void hold_high_resolution_timer(bool hold) noexcept {
    static std::atomic<bool> held{false};
    if (held.exchange(hold, std::memory_order_acq_rel) == hold) return;
    if (HMODULE winmm = LoadLibraryW(L"winmm.dll")) {
        using PeriodFn = UINT(WINAPI*)(UINT);
        if (auto call = reinterpret_cast<PeriodFn>(GetProcAddress(winmm, hold ? "timeBeginPeriod" : "timeEndPeriod"))) {
            (void)call(1);
        }
    }
}

// Request execution-speed QoS for the process, and retain its timer-resolution requests when Windows supports that
// policy. This does not select a core or override thermal/power limits. The timer flag requires a newer OS than
// execution-speed control, so the older policy is requested if the combined request is rejected. Unsupported OS
// versions may reject both.
inline void opt_out_of_power_throttling_process() noexcept {
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    // PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION == 0x4.
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4u;
    state.StateMask = 0;
    if (!SetProcessInformation(
            GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state))) {
        state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        (void)SetProcessInformation(
            GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
    }
}

}  // namespace galaxy::host
#else
#include <pthread.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <cstddef>

namespace galaxy::host {
// The thread's name in the platform's tools (top, perf, debuggers): its first 15 characters, as Linux keeps.
inline void name_current_thread(const wchar_t* name) noexcept {
    char narrow[16]{};
    for (std::size_t i = 0; name[i] != L'\0' && i + 1u < sizeof(narrow); ++i) {
        narrow[i] = name[i] < 0x80 ? static_cast<char>(name[i]) : '?';
    }
#if defined(__APPLE__)
    (void)pthread_setname_np(narrow);
#else
    (void)pthread_setname_np(pthread_self(), narrow);
#endif
}
inline void opt_out_of_power_throttling_process() noexcept {}
}  // namespace galaxy::host
#endif
