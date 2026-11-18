// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The system's heat as the platform reports it (P-019): Android's thermal status and headroom (android/thermal.h,
// Android 11 and 12), Apple's NSProcessInfo thermalState. Windows and Linux report none (Unknown). Each change is
// logged once ("[heat] ..."), so a run's log says when the system began to throttle. Read by dynamic resolution
// (docs/plan 01-AGENT-1-ENGINE.md 6.4 item 7).

#include <cstdint>

namespace galaxy::host {

// Android's ladder (ATHERMAL_STATUS_NONE to _SHUTDOWN); Apple's four states take None, Light, Severe and Critical.
enum class Heat : std::uint8_t { Unknown, None, Light, Moderate, Severe, Critical, Emergency, Shutdown };

// ATHERMAL_STATUS_* (-1 error, 0 none ... 6 shutdown).
[[nodiscard]] constexpr Heat heat_of_android_status(int status) noexcept {
    return status >= 0 && status <= 6 ? static_cast<Heat>(status + 1) : Heat::Unknown;
}
// NSProcessInfoThermalState (0 nominal, 1 fair, 2 serious, 3 critical). Apple's serious is where performance is cut,
// as Android's severe; fair is raised heat with no cut yet, as light.
[[nodiscard]] constexpr Heat heat_of_apple_state(long state) noexcept {
    switch (state) {
        case 0: return Heat::None;
        case 1: return Heat::Light;
        case 2: return Heat::Severe;
        case 3: return Heat::Critical;
        default: return Heat::Unknown;
    }
}
[[nodiscard]] const char* heat_name(Heat heat) noexcept;

// Once, at the host's start: the starting heat logged; on Android every later change is logged as the system reports
// it (a listener). Apple's host calls thermal_heat_changed from its thermal-state notification.
void thermal_watch_start() noexcept;
// The system says its heat changed: read it now (logged when it differs).
void thermal_heat_changed() noexcept;
// The newest heat; read from the system at most once a second where no listener keeps it. Any thread.
[[nodiscard]] Heat thermal_heat() noexcept;
// Android 12 and later: the system's headroom now, 1.0 where it throttles (Severe), read at most once a second (the
// system answers nothing more often). Not a number elsewhere, or when the system gives none.
[[nodiscard]] float thermal_headroom() noexcept;

}  // namespace galaxy::host
