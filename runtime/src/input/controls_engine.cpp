// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/controls_engine.h"
#include "galaxy/host_extension.h"
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

NativeInputScriptOverlay extension_controls(std::uint64_t frame) {
    NativeInputScriptOverlay overlay{};
    NebulaExtensionControls given{};
    if (!galaxy::extensions::controls(NebulaExtensionFrame{frame}, given)) {
        return overlay;
    }
    const auto held = [&given](std::uint32_t button) { return (given.buttons & button) != 0u; };
    overlay.buttons.a = held(NEBULA_EXT_BUTTON_A);
    overlay.buttons.b = held(NEBULA_EXT_BUTTON_B);
    overlay.buttons.one = held(NEBULA_EXT_BUTTON_ONE);
    overlay.buttons.two = held(NEBULA_EXT_BUTTON_TWO);
    overlay.buttons.plus = held(NEBULA_EXT_BUTTON_PLUS);
    overlay.buttons.minus = held(NEBULA_EXT_BUTTON_MINUS);
    overlay.buttons.home = held(NEBULA_EXT_BUTTON_HOME);
    overlay.buttons.up = held(NEBULA_EXT_BUTTON_UP);
    overlay.buttons.down = held(NEBULA_EXT_BUTTON_DOWN);
    overlay.buttons.left = held(NEBULA_EXT_BUTTON_LEFT);
    overlay.buttons.right = held(NEBULA_EXT_BUTTON_RIGHT);
    overlay.buttons.c = held(NEBULA_EXT_BUTTON_C);
    overlay.buttons.z = held(NEBULA_EXT_BUTTON_Z);
    overlay.shake = given.shake != 0u;
    overlay.has_stick = given.has_stick != 0u;
    overlay.stick_x = given.stick_x;
    overlay.stick_y = given.stick_y;
    overlay.has_pointer = given.has_pointer != 0u;
    overlay.pointer_x = given.pointer_x;
    overlay.pointer_y = given.pointer_y;
    return overlay;
}

}  // namespace galaxy::host::controls
