// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/native_input.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/tilt.h"

namespace galaxy::host::controls {
// Controls a host extension holds (host_extension.h), added to the devices'.
struct NativeInputScriptOverlay {
    galaxy::input::ButtonState buttons{};
    bool has_stick = false;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    bool has_pointer = false;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
    bool shake = false;
};

// An XInput trigger is down from this far (of 255).
inline constexpr std::uint8_t kXInputTriggerThreshold = 96u;

// The host extensions' controls at game frame `frame` (none without one).
NativeInputScriptOverlay extension_controls(std::uint64_t frame);
void apply_tilt_accel(galaxy::input::PlayerInput& input, galaxy::input::tilt::Tilt& state,
                      galaxy::input::tilt::Section section, const galaxy::input::tilt::Vec3* controller_up,
                      float stick_x, float stick_y, bool recenter);
}  // namespace galaxy::host::controls
