// SPDX-License-Identifier: GPL-3.0-only
// The native game's host services on SDL3 platforms (native_host.h, native_audio.h): the game's sound through SDL's
// audio stream, player 1's input from the on-screen controls, the finger on the picture, keys and gamepads (with the
// same bindings and tuning as Windows), vibration, and the device data the game reads.

#include "galaxy/render_api.h"
#include "sdl_host.h"

#include "galaxy/content_viewport.h"
#include "galaxy/input_bindings.h"
#include "galaxy/audio_mixer.h"
#include "galaxy/native_audio.h"
#include "galaxy/game_paths.h"
#include "galaxy/native_input.h"
#include "galaxy/platform_time.h"
#include "galaxy/pointer_control.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/tilt.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>

namespace galaxy::host {
namespace {

namespace in = galaxy::input;

// The finger's pointer stays where it was lifted this long, so the cursor does not blink between touches.
constexpr std::uint64_t kTouchPointerHoldMs = 1500;
// A pad or key pointer at rest hides after this long, as the Wii's does when the remote points away.
constexpr std::uint64_t kStickPointerHoldMs = 3000;
// A tap in a menu is A, counted in the game's frames so it lands at any frame rate: the game takes A on what the pointer
// is over, which it finds some frames after the pointer arrives (InputPointer wants five valid samples, then the menu
// its hover), and reads the remote once a frame.
constexpr std::uint64_t kTapHoverFrames = 8;
constexpr std::uint64_t kTapPressFrames = 3;
// An on-screen button's press reaches the game for at least this many of its frames, however quick the finger.
constexpr std::uint64_t kTouchPressFrames = 3;
// The game's star pointer mode on the title (StarPointerMode_Title): its prompt asks for A and B, so the controls stay.
constexpr std::int32_t kStarPointerModeTitle = 4;
constexpr float kTriggerThreshold = 96.0f / 255.0f;  // the Windows host's XInput trigger threshold

const in::ControlsConfig& live_controls() {
    static in::ControlsConfig config = in::get_controls_config();
    static std::uint64_t generation = in::controls_config_generation();
    if (const std::uint64_t now = in::controls_config_generation(); now != generation) {
        config = in::get_controls_config();
        generation = now;
    }
    return config;
}

// The on-screen controls as the bindings' output.
in::LogicalInput touch_logic(const sdl::InputState& state) {
    in::LogicalInput logic{};
    const auto press = [&logic](in::GameInput target) { logic.pressed |= in::game_input_bit(target); };
    const std::uint32_t b = state.touch_buttons;
    if (b & sdl::kTouchA) press(in::GameInput::A);
    if (b & sdl::kTouchB) press(in::GameInput::B);
    if (b & sdl::kTouchZ) press(in::GameInput::Z);
    if (b & sdl::kTouchC) press(in::GameInput::C);
    if (b & sdl::kTouchShake) press(in::GameInput::Shake);
    if (b & sdl::kTouchMinus) press(in::GameInput::Minus);
    if (b & sdl::kTouchPlus) press(in::GameInput::Plus);
    if (b & sdl::kTouchHome) press(in::GameInput::Home);
    if (b & sdl::kTouchOne) press(in::GameInput::One);
    if (b & sdl::kTouchTwo) press(in::GameInput::Two);
    if (b & sdl::kTouchUp) press(in::GameInput::DpadUp);
    if (b & sdl::kTouchDown) press(in::GameInput::DpadDown);
    if (b & sdl::kTouchLeft) press(in::GameInput::DpadLeft);
    if (b & sdl::kTouchRight) press(in::GameInput::DpadRight);
    logic.stick_x = state.touch_stick_x;
    logic.stick_y = state.touch_stick_y;
    return logic;
}

// A point in window pixels as the pointer (-1..1 across the picture, y down), through the rectangle the picture was last drawn in.
bool project_window_point(const sdl::InputState& state, float x, float y, float& out_x, float& out_y) {
    const auto content = galaxy::g_presentation_content.read();
    if (!content || content->surface_width != state.window_width || content->surface_height != state.window_height) {
        return false;
    }
    const in::NativeMousePointerProjection projection = in::project_native_mouse_pointer({
        state.window_focused, true, true, static_cast<int>(x), static_cast<int>(y), state.window_width,
        state.window_height, content->viewport});
    if (!projection.visible) {
        return false;
    }
    out_x = projection.x;
    out_y = projection.y;
    return true;
}

std::uint32_t g_rumble_pad = 0;  // the gamepad the player used last (SDL id; 0: none)
float g_rumble_strength = 1.0f;

}  // namespace

in::PlayerInput native_game_input(std::uint64_t frame, std::int32_t star_pointer_mode, std::uint8_t tilt_section) {
    // A menu that takes the screen takes the keys and pads on the window thread. The game draws it under its own
    // pointer, so the finger and the mouse still reach the game, as the mouse does on Windows.
    const bool captured = galaxy::render::native_window_input_captured();
    const in::PlayContext context = captured ? in::PlayContext::Menu
        : star_pointer_mode >= 0 && static_cast<std::uint32_t>(star_pointer_mode) < in::kStarPointerModeCount
        ? in::play_context_from_star_pointer_mode(static_cast<std::uint32_t>(star_pointer_mode))
        : in::PlayContext::Play;
    sdl::set_menu_context(context == in::PlayContext::Menu && (captured || star_pointer_mode != kStarPointerModeTitle));
    const in::tilt::Section section = tilt_section == 1u ? in::tilt::Section::StarBall
        : tilt_section == 2u ? in::tilt::Section::Ray
        : in::tilt::Section::None;
    sdl::InputState state = sdl::take_input_state();
    static std::array<std::uint64_t, 32> s_touch_press_end{};
    for (std::uint32_t bit = 0; bit < s_touch_press_end.size(); ++bit) {
        if (captured) {
            s_touch_press_end[bit] = 0u;  // a press that opened the menu does not outlast it
        } else if ((state.touch_buttons & (1u << bit)) != 0u) {
            if (s_touch_press_end[bit] <= frame) {
                s_touch_press_end[bit] = frame + kTouchPressFrames;
            }
        } else if (frame < s_touch_press_end[bit]) {
            state.touch_buttons |= 1u << bit;
        }
    }
    if (captured) {
        const auto mouse = state.keys.mouse;
        state.keys = {};
        state.keys.mouse = mouse;
        state.touch_buttons = 0u;
        state.touch_stick_x = 0.0f;
        state.touch_stick_y = 0.0f;
        for (std::size_t i = 0; i < sdl::kMaxPads; ++i) {
            state.pads[i] = {};
            state.motion[i] = {};
        }
    }
    const in::ControlsConfig& config = live_controls();
    in::PlayerInput snapshot{};

    // Gamepads: the bindings and tuning of each one's scheme, as on Windows.
    static std::array<std::uint32_t, sdl::kMaxPads> s_previous{};
    in::LogicalInput pads{};
    float pad_pointer_speed = 0.0f;
    float motion_x = 0.0f;
    float motion_y = 0.0f;  // down positive, as the pointer's
    bool shake_remote = false;
    bool shake_nunchuk = false;
    const in::tilt::Vec3* controller_up = nullptr;
    const std::uint64_t motion_now_us = galaxy::steady_ms() * 1000u;
    for (std::size_t i = 0; i < sdl::kMaxPads; ++i) {
        in::PadSample sample = state.pads[i];
        if (!sample.connected) {
            s_previous[i] = 0u;
            continue;
        }
        const in::SchemeTuning& tuning = config.tune(sample.scheme);
        const sdl::PadMotion& motion = state.motion[i];
        if (tuning.gyro_pointer && motion.imu_live) {
            motion_x += motion.gyro_x;
            motion_y -= tuning.invert_pointer_y ? -motion.gyro_y : motion.gyro_y;
        }
        if (tuning.touch_pointer && motion.touch_live) {
            motion_x += motion.touch_dx * 2.0f * tuning.touch_sensitivity;
            motion_y += (tuning.invert_pointer_y ? -motion.touch_dy : motion.touch_dy) * 2.0f * tuning.touch_sensitivity;
        }
        if (motion.shake_until_us != 0u && motion_now_us < motion.shake_until_us) {
            shake_remote = shake_remote || tuning.motion_shake == in::MotionShake::Remote;
            shake_nunchuk = shake_nunchuk || tuning.motion_shake == in::MotionShake::Nunchuk;
        }
        if (controller_up == nullptr && motion.gravity_valid) {
            controller_up = &motion.gravity;
        }
        const in::StickVector left = in::apply_radial_deadzone(sample.axes[0], sample.axes[1], tuning.stick_deadzone);
        const in::StickVector right =
            in::apply_radial_deadzone(sample.axes[2], sample.axes[3], tuning.pointer_deadzone);
        sample.axes[0] = left.x;
        sample.axes[1] = left.y;
        sample.axes[2] = right.x;
        sample.axes[3] = right.y;
        in::LogicalInput logic =
            in::evaluate_pad_bindings(config.table(sample.scheme), sample, kTriggerThreshold, s_previous[i], false);
        s_previous[i] = logic.pressed;
        if (tuning.invert_pointer_y) {
            logic.pointer_y = -logic.pointer_y;
        }
        if (logic.pressed != 0u || logic.stick_x != 0.0f || logic.stick_y != 0.0f || logic.pointer_x != 0.0f ||
            logic.pointer_y != 0.0f) {
            g_rumble_pad = state.pad_ids[i];
            g_rumble_strength = tuning.vibration ? static_cast<float>(tuning.rumble) / 255.0f : 0.0f;
            pad_pointer_speed = tuning.pointer_speed;
        }
        pads = in::merge_logical_input(pads, logic);
    }
    in::LogicalInput keys = in::evaluate_keyboard_bindings(config.table(in::ControlScheme::Keyboard), state.keys);
    if (config.tune(in::ControlScheme::Keyboard).invert_pointer_y) {
        keys.pointer_y = -keys.pointer_y;
    }
    const in::LogicalInput logic = in::merge_logical_input(in::merge_logical_input(pads, keys), touch_logic(state));
    const auto held = [&logic](in::GameInput target) { return logic.held(target); };

    snapshot.buttons.left = held(in::GameInput::DpadLeft);
    snapshot.buttons.right = held(in::GameInput::DpadRight);
    snapshot.buttons.down = held(in::GameInput::DpadDown);
    snapshot.buttons.up = held(in::GameInput::DpadUp);
    snapshot.buttons.plus = held(in::GameInput::Plus);
    snapshot.buttons.minus = held(in::GameInput::Minus);
    snapshot.buttons.one = held(in::GameInput::One);
    snapshot.buttons.two = held(in::GameInput::Two);
    snapshot.buttons.home = held(in::GameInput::Home);
    // A controller's B: jump while Mario is under control, back in menus.
    static in::JumpBackLatch s_jump_back{};
    const in::JumpBackLatch::Output jump_back = s_jump_back.update(held(in::GameInput::JumpBack), context);
    snapshot.buttons.a = held(in::GameInput::A) || jump_back.a;
    snapshot.buttons.b = held(in::GameInput::B) || jump_back.b;
    const in::StickValue stick = in::nunchuk_stick(logic.stick_x, logic.stick_y);
    snapshot.stick_x = stick.x;
    snapshot.stick_y = stick.y;
    snapshot.buttons.c = held(in::GameInput::C);
    snapshot.buttons.z = held(in::GameInput::Z);

    // The pointer: the finger (or mouse) on the picture where it is; a stick or keys move it at their speed.
    static float s_pointer_x = 0.0f;
    static float s_pointer_y = 0.0f;
    static std::uint64_t s_pointer_ms = 0;
    static std::uint64_t s_hold_ms = 0;
    static std::uint64_t s_last_ms = galaxy::steady_ms();
    const std::uint64_t now = galaxy::steady_ms();
    const float dt = std::min(0.1f, static_cast<float>(now - s_last_ms) / 1000.0f);
    s_last_ms = now;
    float x = 0.0f;
    float y = 0.0f;
    if (state.finger_ms != 0 && now - state.finger_ms <= kTouchPointerHoldMs &&
        project_window_point(state, state.finger_x, state.finger_y, x, y)) {
        if (state.finger_down || state.finger_ms > s_pointer_ms) {
            s_pointer_x = x;
            s_pointer_y = y;
            s_pointer_ms = state.finger_ms;
            s_hold_ms = kTouchPointerHoldMs;
        }
    }
    const float axis_x = pads.pointer_x != 0.0f || pads.pointer_y != 0.0f ? pads.pointer_x : keys.pointer_x;
    const float axis_y = pads.pointer_x != 0.0f || pads.pointer_y != 0.0f ? pads.pointer_y : keys.pointer_y;
    if (axis_x != 0.0f || axis_y != 0.0f) {
        const float speed = pad_pointer_speed > 0.0f ? pad_pointer_speed : config.tune(in::ControlScheme::Keyboard).pointer_speed;
        s_pointer_x = std::clamp(s_pointer_x + axis_x * speed * dt, -1.0f, 1.0f);
        s_pointer_y = std::clamp(s_pointer_y - axis_y * speed * dt, -1.0f, 1.0f);
        s_pointer_ms = now;
        s_hold_ms = kStickPointerHoldMs;
    }
    if (motion_x != 0.0f || motion_y != 0.0f) {
        s_pointer_x = std::clamp(s_pointer_x + motion_x, -1.0f, 1.0f);
        s_pointer_y = std::clamp(s_pointer_y + motion_y, -1.0f, 1.0f);
        s_pointer_ms = now;
        s_hold_ms = kStickPointerHoldMs;
    }
    // A tap in a menu points there and presses A.
    static std::uint64_t s_taps = state.taps;
    static bool s_tap_waiting = false;
    static std::uint64_t s_tap_due_frame = 0;
    static std::uint64_t s_tap_press_end_frame = 0;
    static bool s_pointer_shown = false;
    static std::uint64_t s_pointer_shown_frame = 0;  // since when the game has had the pointer
    if (state.taps != s_taps) {
        s_taps = state.taps;
        if (context == in::PlayContext::Menu && project_window_point(state, state.tap_x, state.tap_y, x, y)) {
            s_pointer_x = x;
            s_pointer_y = y;
            s_pointer_ms = now;
            s_hold_ms = kTouchPointerHoldMs;
            if (!s_pointer_shown) {
                s_pointer_shown_frame = frame;
            }
            s_tap_waiting = true;
            s_tap_due_frame = s_pointer_shown_frame + kTapHoverFrames;
        }
    }
    if (s_tap_waiting && frame >= s_tap_due_frame) {
        s_tap_waiting = false;
        s_tap_press_end_frame = frame + kTapPressFrames;
    }
    const bool tap_pressing = frame < s_tap_press_end_frame;
    if (tap_pressing) {
        snapshot.buttons.a = true;
    }
    // The pointer stays until the press has landed, however slowly the frames come.
    if (s_tap_waiting || tap_pressing) {
        s_pointer_ms = now;
    }
    const bool pointer_active = s_pointer_ms != 0 && now - s_pointer_ms <= s_hold_ms;
    if (pointer_active && !s_pointer_shown) {
        s_pointer_shown_frame = frame;
    }
    s_pointer_shown = pointer_active;
    in::set_pointer(snapshot, s_pointer_x, s_pointer_y, pointer_active);

    // Tilt sections steer with the stick.
    static in::tilt::Tilt s_tilt{};
    const in::tilt::Tilt::Output tilt = s_tilt.update(section, controller_up, logic.stick_x, logic.stick_y, false);
    if (tilt.active) {
        snapshot.gravity = tilt.game;
    }
    snapshot.pointer_on = snapshot.pointer_on && in::remote_faces_screen(snapshot.gravity);
    snapshot.shake = held(in::GameInput::Shake) || shake_remote;
    snapshot.nunchuk_shake = held(in::GameInput::NunchukShake) || shake_nunchuk;
    return snapshot;
}

void native_game_rumble(bool motor_on, std::int32_t) noexcept {
    if (g_rumble_pad != 0) {
        sdl::rumble(g_rumble_pad, motor_on ? g_rumble_strength : 0.0f);
    }
}

GamePaths locate_game(const std::filesystem::path& input) {
    return {input.parent_path()};
}

// The Windows host's links between the window and the live controls; the SDL host reads its own state.
void install_host_pointer_provider(HostPointerProvider) noexcept {}
void install_host_pointer_transition_provider(HostPointerTransitionProvider) noexcept {}
void install_native_keyboard_capture_provider(NativeKeyboardCaptureProvider) noexcept {}
void install_native_settings_pad_hooks(NativeSettingsVisibleProvider, NativeSettingsKeySink) noexcept {}
void set_env_flag_cache_enabled(bool) noexcept {}


}  // namespace galaxy::host
