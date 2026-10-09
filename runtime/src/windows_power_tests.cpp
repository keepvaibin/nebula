#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <array>
#include <cstdio>

namespace {
std::array<PROCESS_POWER_THROTTLING_STATE, 2> requests{};
unsigned process_calls{}, thread_calls{};
bool accept_combined{}, accept_fallback{}, valid{true};

BOOL fake_process_information(HANDLE process, PROCESS_INFORMATION_CLASS kind,
    LPVOID value, DWORD size) {
    valid &= process == GetCurrentProcess() && kind == ProcessPowerThrottling &&
        size == sizeof(PROCESS_POWER_THROTTLING_STATE) && process_calls < requests.size();
    if (!valid) return FALSE;
    requests[process_calls] = *static_cast<PROCESS_POWER_THROTTLING_STATE*>(value);
    return (process_calls++ == 0u ? accept_combined : accept_fallback) ? TRUE : FALSE;
}

BOOL fake_thread_information(HANDLE thread, THREAD_INFORMATION_CLASS kind,
    LPVOID value, DWORD size) {
    const auto& state = *static_cast<THREAD_POWER_THROTTLING_STATE*>(value);
    valid &= thread == GetCurrentThread() && kind == ThreadPowerThrottling &&
        size == sizeof(state) && state.Version == THREAD_POWER_THROTTLING_CURRENT_VERSION &&
        state.ControlMask == THREAD_POWER_THROTTLING_EXECUTION_SPEED && state.StateMask == 0u;
    ++thread_calls;
    return FALSE;  // Unsupported thread policy must remain nonfatal.
}
}  // namespace

// Test the request/fallback contract without changing this process's OS policy.
#define SetProcessInformation fake_process_information
#define SetThreadInformation fake_thread_information
#include "galaxy/windows_power.h"
#undef SetProcessInformation
#undef SetThreadInformation

int main() {
    bool passed = true;
    for (unsigned scenario = 0u; scenario < 3u; ++scenario) {
        process_calls = 0u;
        accept_combined = scenario == 0u;
        accept_fallback = scenario == 1u;
        galaxy::host::opt_out_of_power_throttling_process();
        passed &= process_calls == (accept_combined ? 1u : 2u);
        passed &= requests[0].Version == PROCESS_POWER_THROTTLING_CURRENT_VERSION &&
            requests[0].ControlMask == (PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4u) &&
            requests[0].StateMask == 0u;
        if (!accept_combined) {
            passed &= requests[1].Version == PROCESS_POWER_THROTTLING_CURRENT_VERSION &&
                requests[1].ControlMask == PROCESS_POWER_THROTTLING_EXECUTION_SPEED &&
                requests[1].StateMask == 0u;
        }
    }
    galaxy::host::opt_out_of_power_throttling_thread();
    passed &= valid && thread_calls == 1u;
    std::puts(passed ? "Windows power policy fallback checks passed" :
        "Windows power policy fallback checks FAILED");
    return passed ? 0 : 1;
}
