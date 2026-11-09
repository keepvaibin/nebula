// SPDX-License-Identifier: GPL-3.0-only
// The native game's host services on SDL3 platforms (native_host.h, native_audio.h): the game's sound through SDL's
// audio stream, player 1's input from the on-screen controls, the finger on the picture, keys and gamepads (through the
// shared live controls, as on Windows), vibration, and the device data the game reads.

#include "galaxy/render_api.h"
#include "sdl_host.h"

#include "galaxy/content_viewport.h"
#include "galaxy/input/host_input.h"
#include "galaxy/input/live_controls_engine.h"
#include "galaxy/input_bindings.h"
#include "galaxy/audio_mixer.h"
#include "galaxy/native_audio.h"
#include "galaxy/game_paths.h"
#include "galaxy/native_input.h"
#include "galaxy/platform_time.h"
#include "galaxy/pointer_control.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/tilt.h"

#if !defined(NEBULA_HOST_NATIVE_APPLE)
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>
#include <vector>

namespace galaxy::host {
namespace {

namespace in = galaxy::input;

// The finger's pointer stays where it was lifted this long, so the cursor does not blink between touches.
constexpr std::uint64_t kTouchPointerHoldMs = 1500;
// A tap in a menu is A, counted in the game's frames so it lands at any frame rate: the game takes A on what the pointer
// is over, which it finds some frames after the pointer arrives (InputPointer wants five valid samples, then the menu
// its hover), and reads the remote once a frame.
constexpr std::uint64_t kTapHoverFrames = 8;
constexpr std::uint64_t kTapPressFrames = 3;
// An on-screen button's press reaches the game for at least this many of its frames, however quick the finger.
constexpr std::uint64_t kTouchPressFrames = 3;
// The game's star pointer mode while the pointer is on something A grabs (StarPointerMode_BlueStarReady).
constexpr std::int32_t kStarPointerModeBlueStarReady = 21;
// Targets the game stopped reporting are gone after this long; input comes more often than the game's frames.
constexpr std::uint64_t kTouchTargetHoldMs = 100;
// After a finger lands on a target the game has this long to say that the pointer is on it (InputPointer's five, a nerve or two).
constexpr std::uint64_t kGrabArmFrames = 30;
// Around a target a finger still lands on it, in density-independent units: a fingertip hides more than the target.
constexpr float kTouchTargetPadDp = 24.0f;
// The game's star pointer mode on the title (StarPointerMode_Title): its prompt asks for A and B, so the controls stay.
constexpr std::int32_t kStarPointerModeTitle = 4;

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

// The shared live controls' window services: the finger (or the mouse) is the absolute pointer, as the mouse is on
// Windows; nothing is injected.
class SdlControlsHost final : public controls::LiveControlsHost {
public:
    SdlControlsHost(const controls::PointerSample& finger, std::uint64_t now_ms, std::uint64_t now_us)
        : finger_(finger), now_ms_(now_ms), now_us_(now_us) {}
    std::uint64_t now_ms() override { return now_ms_; }
    std::uint64_t now_us() override { return now_us_; }
    bool injected(bool) override { return false; }
    controls::PointerSample pointer(bool, bool) override { return finger_; }
    in::NativeMousePointerProjection cursor() override { return {}; }

private:
    controls::PointerSample finger_;
    std::uint64_t now_ms_;
    std::uint64_t now_us_;
};

// A point in window pixels as the pointer (-1..1 across the picture, y down), through the rectangle the picture was last drawn in.
// A circle on the picture: x, y as the pointer's (-1..1, down positive); the radius in the picture's heights.
struct TouchTarget {
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
};

struct TouchTargets {
    std::array<TouchTarget, 32> list{};
    std::uint32_t count = 0;
};

// Filled by the game during its frame; the next input after it hands it to `g_touch_targets`, for the UI thread too.
TouchTargets g_touch_targets_coming;
TouchTargets g_touch_targets;
std::uint64_t g_touch_targets_ms = 0;
std::mutex g_touch_targets_mutex;
std::atomic<unsigned> g_touch_pointer_state{0u};

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

// The target a finger at window pixels x, y lands on, the closest of them.
bool touch_target_under(const sdl::InputState& state, float x, float y, TouchTarget& out) {
    float point_x = 0.0f;
    float point_y = 0.0f;
    const auto content = galaxy::g_presentation_content.read();
    if (!content || !project_window_point(state, x, y, point_x, point_y)) {
        return false;
    }
    const float half_width = static_cast<float>(content->viewport.width) * 0.5f;
    const float half_height = static_cast<float>(content->viewport.height) * 0.5f;
    const unsigned width_dp = window_width_dp();
    const float pad = width_dp != 0u ? kTouchTargetPadDp * static_cast<float>(state.window_width) / static_cast<float>(width_dp)
                                     : half_height * 0.1f;
    float best = -1.0f;
    std::lock_guard<std::mutex> lock(g_touch_targets_mutex);
    for (std::uint32_t i = 0; i < g_touch_targets.count; ++i) {
        const TouchTarget& target = g_touch_targets.list[i];
        const float distance = std::hypot((point_x - target.x) * half_width, (point_y - target.y) * half_height);
        const float reach = target.radius * half_height * 2.0f + pad;
        if (distance <= reach && (best < 0.0f || distance < best)) {
            best = distance;
            out = target;
        }
    }
    return best >= 0.0f;
}

}  // namespace

unsigned touch_pointer_state() noexcept {
    return g_touch_pointer_state.load(std::memory_order_relaxed);
}

void note_touch_target(std::uint32_t circle) noexcept {
    std::lock_guard<std::mutex> lock(g_touch_targets_mutex);
    if (g_touch_targets_coming.count < g_touch_targets_coming.list.size()) {
        TouchTarget& target = g_touch_targets_coming.list[g_touch_targets_coming.count++];
        target.x = static_cast<float>(circle & 0xfffu) / 4095.0f * 2.0f - 1.0f;
        target.y = static_cast<float>((circle >> 12u) & 0xfffu) / 4095.0f * 2.0f - 1.0f;
        target.radius = static_cast<float>(circle >> 24u) / 255.0f;
    }
}

bool touch_target_at(float x, float y) noexcept {
    TouchTarget target;
    return touch_target_under(sdl::input_state(), x, y, target);
}

in::PlayerInput native_game_input(std::uint64_t frame, std::int32_t star_pointer_mode, std::uint8_t tilt_section) {
    // A menu that takes the screen takes the keys and pads on the window thread. The game draws it under its own
    // pointer, so the finger and the mouse still reach the game, as the mouse does on Windows.
    const bool captured = galaxy::render::native_window_input_captured();
    const in::PlayContext context = captured ? in::PlayContext::Menu
        : star_pointer_mode >= 0 && static_cast<std::uint32_t>(star_pointer_mode) < in::kStarPointerModeCount
        ? in::play_context_from_star_pointer_mode(static_cast<std::uint32_t>(star_pointer_mode))
        : in::PlayContext::Play;
    sdl::set_menu_context(context == in::PlayContext::Menu && (captured || star_pointer_mode != kStarPointerModeTitle));
    sdl::set_title_screen(star_pointer_mode == kStarPointerModeTitle);
    sdl::set_direct_touch(captured);
    const in::tilt::Section section = tilt_section == 1u ? in::tilt::Section::StarBall
        : tilt_section == 2u ? in::tilt::Section::Ray
        : in::tilt::Section::None;
    sdl::InputState state = sdl::take_input_state();
    {
        std::lock_guard<std::mutex> lock(g_touch_targets_mutex);
        const std::uint64_t now_ms = galaxy::steady_ms();
        if (g_touch_targets_coming.count != 0u) {
            g_touch_targets = g_touch_targets_coming;
            g_touch_targets_coming.count = 0u;
            g_touch_targets_ms = now_ms;
        } else if (now_ms - g_touch_targets_ms > kTouchTargetHoldMs) {
            g_touch_targets.count = 0u;
        }
    }
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
    // A finger landing on a target points at it, and once the game says the pointer is there it is A, for as long as the
    // finger stays. A finger only passing over a target grabs nothing.
    static std::uint64_t s_land_ms = 0;
    static std::uint64_t s_grab_armed_end = 0;
    static std::uint64_t s_grab_end = 0;
    static bool s_grabbing = false;
    static TouchTarget s_grab_target{};
    if (state.finger_land_ms != s_land_ms) {
        s_land_ms = state.finger_land_ms;
        s_grabbing = false;
        s_grab_armed_end = !captured && touch_target_under(state, state.finger_land_x, state.finger_land_y, s_grab_target)
            ? frame + kGrabArmFrames : 0u;
    }
    if (frame < s_grab_armed_end && star_pointer_mode == kStarPointerModeBlueStarReady) {
        s_grab_armed_end = 0u;
        s_grabbing = true;
        s_grab_end = frame + kTouchPressFrames;
    }
    if (s_grabbing) {
        if (state.finger_down && s_grab_end <= frame) {
            s_grab_end = frame + 1u;
        }
        s_grabbing = !captured && frame < s_grab_end;
        if (s_grabbing) {
            state.touch_buttons |= sdl::kTouchA;
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
    // The input mode's own devices. Auto takes them all.
    const galaxy::RuntimeInputMode input_mode = galaxy::get_runtime_input_mode_state().mode;
    const bool use_keyboard =
        input_mode == galaxy::RuntimeInputMode::Auto || input_mode == galaxy::RuntimeInputMode::KeyboardMouse;
    if (!use_keyboard) {
        const auto mouse = state.keys.mouse;
        state.keys = {};
        state.keys.mouse = mouse;
    }
    if (input_mode != galaxy::RuntimeInputMode::Auto && input_mode != galaxy::RuntimeInputMode::Phone) {
        state.touch_buttons = 0u;
        state.touch_stick_x = 0.0f;
        state.touch_stick_y = 0.0f;
    }
    if (input_mode != galaxy::RuntimeInputMode::Auto && input_mode != galaxy::RuntimeInputMode::Controller) {
        for (std::size_t i = 0; i < sdl::kMaxPads; ++i) {
            state.pads[i] = {};
            state.motion[i] = {};
        }
    }
    const in::ControlsConfig& config = live_controls();
    const std::uint64_t now = galaxy::steady_ms();
    const std::uint64_t now_us = galaxy::steady_us();

    // The finger (or the mouse) on the picture: where it is, while it is down and kTouchPointerHoldMs after. The shared
    // controls take it as the mouse, so it and a controller's gyro, stick and recenter share one pointer.
    static float s_finger_x = 0.0f;
    static float s_finger_y = 0.0f;
    static std::uint64_t s_finger_ms = 0;  // when the finger last put the pointer somewhere or kept it there
    bool finger_moved = false;
    const auto finger_to = [&finger_moved](float to_x, float to_y, std::uint64_t at_ms) {
        s_finger_x = to_x;
        s_finger_y = to_y;
        s_finger_ms = at_ms;
        finger_moved = true;
    };
    float x = 0.0f;
    float y = 0.0f;
    if (context != in::PlayContext::Menu && state.finger_ms != 0 && now - state.finger_ms <= kTouchPointerHoldMs &&
        project_window_point(state, state.finger_x, state.finger_y, x, y)) {
        if (state.finger_down || state.finger_ms > s_finger_ms) {
            finger_to(x, y, state.finger_ms);
        }
    }
    // On the target itself, however far off it the fingertip landed: the game's own test decides, with a smaller circle.
    if (frame < s_grab_armed_end || s_grabbing) {
        finger_to(s_grab_target.x, s_grab_target.y, s_finger_ms);
    }
    // Nebula's menu takes the finger as its mouse: it points where the finger is and, once the pointer has been there
    // kTapHoverFrames, holds A while the finger stays (the menu presses, drags its sliders, lists and mods, and clicks
    // where it is let go). A touch too short for that is a tap, below.
    static std::uint64_t s_direct_land_ms = 0;
    static std::uint64_t s_direct_land_frame = 0;
    static std::uint64_t s_direct_held_land_ms = 0;  // the touch that held A
    bool direct_finger = false;
    bool direct_press = false;
    // Until A is down and the finger has moved past the touch slop it points where it landed: the press lands on what the
    // finger touched however fast it then moves (a mod pressed and dragged in one motion), and a press or a slider's grab
    // holds still under a fingertip's tremor on any screen.
    if (captured && state.finger_down && state.finger_land_ms != s_direct_land_ms) {
        s_direct_land_ms = state.finger_land_ms;
        s_direct_land_frame = frame;
    }
    const bool at_land = !state.finger_moved || frame <= s_direct_land_frame + kTapHoverFrames;
    if (captured && state.finger_down &&
        project_window_point(state, at_land ? state.finger_land_x : state.finger_x,
                             at_land ? state.finger_land_y : state.finger_y, x, y)) {
        finger_to(x, y, now);
        direct_finger = true;
        if (frame >= s_direct_land_frame + kTapHoverFrames) {
            direct_press = true;
            s_direct_held_land_ms = state.finger_land_ms;
        }
    }
    // The frame after the finger leaves still has its pointer where it left, however slowly the frames come: the menu
    // clicks on that release.
    static bool s_direct_finger_before = false;
    if (!direct_finger && s_direct_finger_before) {
        s_finger_ms = now;
    }
    s_direct_finger_before = direct_finger;
    // A tap in a menu points there and presses A.
    static std::uint64_t s_taps = state.taps;
    static bool s_tap_waiting = false;
    static std::uint64_t s_tap_due_frame = 0;
    static std::uint64_t s_tap_press_end_frame = 0;
    static bool s_pointer_shown = false;
    static std::uint64_t s_pointer_shown_frame = 0;  // since when the game has had the pointer
    if (state.taps != s_taps) {
        s_taps = state.taps;
        // In Nebula's menu a touch that held A has pressed already.
        const bool pressed = captured && s_direct_held_land_ms == state.finger_land_ms;
        if (!pressed && context == in::PlayContext::Menu && project_window_point(state, state.tap_x, state.tap_y, x, y)) {
            finger_to(x, y, now);
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
    // The pointer stays until the press has landed and been let go (Nebula's menu clicks on the release), however
    // slowly the frames come.
    if (s_tap_waiting || frame <= s_tap_press_end_frame) {
        s_finger_ms = now;
    }
    // A game menu's touch becomes a pointer only after it resolves to a tap: a swipe never hovers buttons along its path.
    // Nebula's menu has the finger's pointer while it is down and for kTouchPointerHoldMs after, so it sees the release
    // where the finger left.
    bool finger_shown = s_finger_ms != 0 && now - s_finger_ms <= kTouchPointerHoldMs &&
        (direct_finger || captured || context != in::PlayContext::Menu ||
         (!state.finger_down && (s_tap_waiting || tap_pressing)));
    // A mouse points wherever it is over the picture, in a menu as in play, for as long as it is there: the pointer of
    // the Windows host, not a finger's.
    if (state.mouse_pointer) {
        finger_shown = state.mouse_inside && state.finger_ms != 0 &&
                       project_window_point(state, state.finger_x, state.finger_y, x, y);
        if (finger_shown) {
            finger_moved = finger_moved || x != s_finger_x || y != s_finger_y;
            s_finger_x = x;
            s_finger_y = y;
            s_finger_ms = now;
        }
    }
    controls::PointerSample finger{};
    finger.polled = true;
    finger.window_focused = state.window_focused;
    finger.absolute_valid = true;
    finger.inside_client = finger_shown;
    finger.raw_motion = finger_moved;
    finger.projected = {s_finger_x, s_finger_y, finger_shown};

    // Gamepads as on Windows: each one's scheme, dead zones, gyro, touch surface and shake.
    std::array<controls::LivePad, sdl::kMaxPads> pads{};
    std::size_t pad_count = 0;
    for (std::size_t i = 0; i < sdl::kMaxPads; ++i) {
        const in::PadSample& sample = state.pads[i];
        if (!sample.connected) {
            continue;
        }
        const in::SchemeTuning& tuning = config.tune(sample.scheme);
        const sdl::PadMotion& motion = state.motion[i];
        controls::LivePad& live = pads[pad_count++];
        live.sample = sample;
        const in::StickVector left = in::apply_radial_deadzone(sample.axes[0], sample.axes[1], tuning.stick_deadzone);
        const in::StickVector right = in::apply_radial_deadzone(sample.axes[2], sample.axes[3], tuning.pointer_deadzone);
        live.sample.axes[0] = left.x;
        live.sample.axes[1] = left.y;
        live.sample.axes[2] = right.x;
        live.sample.axes[3] = right.y;
        live.device_slot = static_cast<std::uint8_t>(i);
        live.imu_live = motion.imu_live;
        live.gyro_x = motion.gyro_x;
        live.gyro_y = motion.gyro_y;
        live.touch_live = motion.touch_live;
        live.touch_dx = motion.touch_dx;
        live.touch_dy = motion.touch_dy;
        live.gravity_valid = motion.imu_live && motion.gravity_valid;
        live.gravity = motion.gravity;
        const bool shaking = motion.shake_until_us != 0u && now_us < motion.shake_until_us;
        live.shake_remote = shaking && tuning.motion_shake == in::MotionShake::Remote;
        live.shake_nunchuk = shaking && tuning.motion_shake == in::MotionShake::Nunchuk;
    }

    controls::LiveControlsFrame controls_frame{config};
    controls_frame.pads = std::span<const controls::LivePad>(pads.data(), pad_count);
    controls_frame.keyboard_levels = state.keys;
    controls_frame.script = controls::extension_controls(frame);
    controls_frame.script_pointer_grace_ms = 250u;
    controls_frame.game_frame = frame;
    controls_frame.window_focused = state.window_focused;
    controls_frame.use_keyboard = use_keyboard;
    controls_frame.use_pads = pad_count != 0u;
    controls_frame.allow_background_device_input = true;
    controls_frame.allow_absolute_pointer = true;
    controls_frame.context = context;
    controls_frame.tilt_section = section;
    controls_frame.extra = touch_logic(state);
    // No controller in hand: the device's own sensors are the remote's.
    if (pad_count == 0u) {
        if (state.device_motion.gravity_valid) {
            controls_frame.device_up = &state.device_motion.gravity;
        }
        controls_frame.device_shake =
            state.device_motion.shake_until_us != 0u && now_us < state.device_motion.shake_until_us;
    }
    static controls::LiveControlsEngine s_engine;
    SdlControlsHost host(finger, now, now_us);
    const controls::LiveControlsResult result = s_engine.sample(controls_frame, host);
    in::PlayerInput snapshot = result.input;
    if (direct_press || tap_pressing) {
        snapshot.buttons.a = true;
    }
    if (snapshot.pointer_on && !s_pointer_shown) {
        s_pointer_shown_frame = frame;
    }
    s_pointer_shown = snapshot.pointer_on;
    if (result.rumble.device.has_value() && result.rumble.device->slot < sdl::kMaxPads) {
        g_rumble_pad = state.pad_ids[result.rumble.device->slot];
        g_rumble_strength = static_cast<float>(result.rumble.strength) / 255.0f;
    }
#if defined(__ANDROID__)
    const bool by_finger = result.pointer_owner == in::PointerSource::Mouse;
    g_touch_pointer_state.store(!by_finger ? 1u : context == in::PlayContext::Menu ? 3u : 2u, std::memory_order_relaxed);
#endif
    return snapshot;
}

void native_game_rumble(std::uint8_t strength, std::int32_t) noexcept {
    if (g_rumble_pad != 0) {
        sdl::rumble(g_rumble_pad, (static_cast<float>(strength) / 255.0f) * g_rumble_strength);
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


}  // namespace galaxy::host
