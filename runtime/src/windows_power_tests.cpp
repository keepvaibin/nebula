#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <array>
#include <cstdio>

namespace {
std::array<PROCESS_POWER_THROTTLING_STATE, 2> requests{};
unsigned process_calls{};
bool accept_combined{}, accept_fallback{}, valid{true};

BOOL fake_process_information(HANDLE process, PROCESS_INFORMATION_CLASS kind,
    LPVOID value, DWORD size) {
    valid &= process == GetCurrentProcess() && kind == ProcessPowerThrottling &&
        size == sizeof(PROCESS_POWER_THROTTLING_STATE) && process_calls < requests.size();
    if (!valid) return FALSE;
    requests[process_calls] = *static_cast<PROCESS_POWER_THROTTLING_STATE*>(value);
    return (process_calls++ == 0u ? accept_combined : accept_fallback) ? TRUE : FALSE;
}
}  // namespace

// Test the request/fallback contract without changing this process's OS policy.
#define SetProcessInformation fake_process_information
#include "galaxy/windows_power.h"
#undef SetProcessInformation

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
    std::puts(passed ? "Windows power policy fallback checks passed" :
        "Windows power policy fallback checks FAILED");
    return passed ? 0 : 1;
}
