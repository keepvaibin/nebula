// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace galaxy::host {

// Windows 11 may place a process or thread in EcoQoS (efficiency cores, low
// clocks) and ignore timeBeginPeriod, notably on hybrid-CPU laptops. The
// simulation, render and DSP threads are latency-critical real-time work, so
// opt the process and the calling thread out of execution-speed throttling
// and out of the timer-resolution override. Unsupported OS versions simply
// reject the request.
inline void opt_out_of_power_throttling_process() noexcept {
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    // PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION == 0x4.
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4u;
    state.StateMask = 0;
    (void)SetProcessInformation(
        GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
}

inline void opt_out_of_power_throttling_thread() noexcept {
    THREAD_POWER_THROTTLING_STATE state{};
    state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;
    (void)SetThreadInformation(
        GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
}

}  // namespace galaxy::host
#else
namespace galaxy::host {
inline void opt_out_of_power_throttling_process() noexcept {}
inline void opt_out_of_power_throttling_thread() noexcept {}
}  // namespace galaxy::host
#endif
