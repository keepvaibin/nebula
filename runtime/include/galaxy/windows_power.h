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

// Request execution-speed QoS for the process and calling thread, and retain
// the process's timer-resolution requests when Windows supports that policy.
// This does not select a core or override thermal/power limits. The timer flag
// requires a newer OS than execution-speed control, so retry the older policy
// if the combined request is rejected. Unsupported OS versions may reject both.
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
