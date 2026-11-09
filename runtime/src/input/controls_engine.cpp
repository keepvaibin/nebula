// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/controls_engine.h"
#include "galaxy/pointer_control.h"

namespace galaxy::host::controls {

// In a tilt section (tilt.h) the virtual remote's acceleration is the controller's tilt or the stick's; elsewhere it
// lies still. Pointed up or down it loses the screen (remote_faces_screen).
void apply_tilt_accel(galaxy::input::PlayerInput& input,
                      galaxy::input::tilt::Tilt& state,
                      galaxy::input::tilt::Section section,
                      const galaxy::input::tilt::Vec3* controller_up,
                      float stick_x,
                      float stick_y,
                      bool recenter) {
    const galaxy::input::tilt::Tilt::Output tilt =
        state.update(section, controller_up, stick_x, stick_y, recenter);
    if (tilt.active) {
        input.gravity = tilt.game;
    }
    input.pointer_on = input.pointer_on && galaxy::input::remote_faces_screen(input.gravity);
}

#if !defined(NEBULA_DEV)
NativeInputScriptOverlay build_native_input_script_overlay(std::uint64_t, const ScriptServices&) {
    return {};
}

NativeInputReplayLog load_native_input_replay_log(const std::string&) {
    return {};
}

const NativeInputReplayLog* active_native_input_replay_log() {
    return nullptr;
}

const NativeInputReplaySample* native_input_replay_sample_for_frame(std::uint64_t) {
    return nullptr;
}

galaxy::input::PlayerInput build_replayed_input(std::uint64_t, const NativeInputReplaySample&,
                                                const NativeInputScriptOverlay&, galaxy::RuntimeInputMode,
                                                galaxy::input::tilt::Section,
                                                const galaxy::input::NativeMousePointerProjection&, const void*) {
    return {};
}
#endif

}  // namespace galaxy::host::controls
