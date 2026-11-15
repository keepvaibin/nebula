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

// Whether the machine runs on its battery, as the OS says (SYSTEM_POWER_STATUS: ACLineStatus 0 offline, 1 online, 255 not
// known); false when it cannot say.
[[nodiscard]] inline bool running_on_battery() noexcept {
    SYSTEM_POWER_STATUS status{};
    return GetSystemPowerStatus(&status) != FALSE && status.ACLineStatus == 0;
}

// The calling thread asks the OS for efficiency (EcoQoS: efficiency cores and lower clocks), or takes the request back.
inline void set_current_thread_efficiency(bool efficient) noexcept {
    THREAD_POWER_THROTTLING_STATE state{};
    state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = efficient ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0u;
    (void)SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
}

// Background work (the shape builder, loaders): the calling thread runs where the game's and the renderer's threads leave a
// core idle, and on battery asks for efficiency too (the frame threads never do: the frame's time is what the battery buys).
inline void lower_current_thread_priority() noexcept {
    (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    if (running_on_battery()) {
        set_current_thread_efficiency(true);
    }
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

// The process asks the OS for efficiency (EcoQoS: efficiency cores, lower clocks, and no wish to keep the timer fast), or goes
// back to the opt-out above. A thread that must keep its time (the audio device's) opts out on its own with
// set_current_thread_efficiency(false); that wins over the process. The same fallback as the opt-out: the combined request first,
// then execution speed alone.
inline void set_process_efficiency(bool efficient) noexcept {
    if (!efficient) {
        opt_out_of_power_throttling_process();
        return;
    }
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4u;
    state.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4u;
    if (!SetProcessInformation(
            GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state))) {
        state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        state.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        (void)SetProcessInformation(
            GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
    }
}

// The display (and with it the system) stays awake while held, or the OS's own idle timers rule again. Per thread: call both from
// the same thread (the host's main thread).
inline void keep_display_on(bool keep) noexcept {
    (void)SetThreadExecutionState(keep ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED) : ES_CONTINUOUS);
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
// Background work (the pipeline workers): the frame's threads run first wherever a core is contended. Apple: the class for
// long work nobody waits on (the frame's threads are user-interactive, perf_hint.cpp). Linux and Android: nice 10 (Android's
// THREAD_PRIORITY_BACKGROUND), about a tenth of a core's time beside a frame thread at nice 0 when both want it.
inline void lower_current_thread_priority() noexcept {
#if defined(__APPLE__)
    (void)pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#else
    (void)setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}
inline void opt_out_of_power_throttling_process() noexcept {}
}  // namespace galaxy::host
#endif
