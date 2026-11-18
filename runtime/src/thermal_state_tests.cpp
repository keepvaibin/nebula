// SPDX-License-Identifier: GPL-3.0-only
// The system's heat (thermal_state.h, P-019): Android's and Apple's ladders; on the Mac and Android a heat is read, on
// Windows and Linux none is (Unknown, no headroom).
#include "galaxy/thermal_state.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++g_failures;
    }
}

}  // namespace

int main() {
    using galaxy::host::Heat;
    using galaxy::host::heat_of_android_status;
    using galaxy::host::heat_of_apple_state;

    // ATHERMAL_STATUS_ERROR, NONE ... SHUTDOWN, and a value past the ladder.
    check(heat_of_android_status(-1) == Heat::Unknown, "Android error status is Unknown");
    check(heat_of_android_status(0) == Heat::None, "Android none");
    check(heat_of_android_status(1) == Heat::Light, "Android light");
    check(heat_of_android_status(2) == Heat::Moderate, "Android moderate");
    check(heat_of_android_status(3) == Heat::Severe, "Android severe");
    check(heat_of_android_status(4) == Heat::Critical, "Android critical");
    check(heat_of_android_status(5) == Heat::Emergency, "Android emergency");
    check(heat_of_android_status(6) == Heat::Shutdown, "Android shutdown");
    check(heat_of_android_status(7) == Heat::Unknown, "Android status past the ladder is Unknown");

    // NSProcessInfoThermalStateNominal, Fair, Serious, Critical.
    check(heat_of_apple_state(0) == Heat::None, "Apple nominal");
    check(heat_of_apple_state(1) == Heat::Light, "Apple fair");
    check(heat_of_apple_state(2) == Heat::Severe, "Apple serious");
    check(heat_of_apple_state(3) == Heat::Critical, "Apple critical");
    check(heat_of_apple_state(4) == Heat::Unknown, "Apple state past the ladder is Unknown");

    // Every heat has its own name.
    for (int a = 0; a <= 7; ++a) {
        for (int b = a + 1; b <= 7; ++b) {
            check(std::strcmp(galaxy::host::heat_name(static_cast<Heat>(a)), galaxy::host::heat_name(static_cast<Heat>(b))) != 0,
                  "two heats share a name");
        }
    }

    galaxy::host::thermal_watch_start();
    const Heat heat = galaxy::host::thermal_heat();
#if defined(__APPLE__)
    check(heat != Heat::Unknown, "the Mac reports its thermal state");
#elif defined(__ANDROID__)
    check(heat != Heat::Unknown, "Android reports its thermal status");
    std::printf("headroom: %.3f\n", static_cast<double>(galaxy::host::thermal_headroom()));
#else
    check(heat == Heat::Unknown, "Windows and Linux report no heat");
    check(std::isnan(galaxy::host::thermal_headroom()), "Windows and Linux report no headroom");
#endif
    std::printf("heat: %s\n", galaxy::host::heat_name(heat));

    if (g_failures != 0) return 1;
    std::printf("thermal_state_tests: all passed\n");
    return 0;
}
