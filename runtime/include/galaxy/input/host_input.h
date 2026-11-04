// The host's controls as the game's input (runtime/src/input/host_input.cpp): pointer sources, window keys,
// binding capture, controllers attached, button icons, vibration.
#pragma once

#include "galaxy/controller_roster.h"
#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/host_pointer_events.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/input/window_keys.h"
#include "galaxy/wiimote_speaker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace galaxy::host {

struct HostPointerState {
    bool window_focused = false;
    bool inside_client = false;
    bool absolute_valid = false;
    bool left_button = false;
    bool right_button = false;
    int client_x = 0;
    int client_y = 0;
    int client_width = 0;
    int client_height = 0;
    std::uint64_t absolute_sequence = 0;
    // The game window (HWND), to tell whether a click lands on it.
    std::uintptr_t debug_window = 0;
};

using HostPointerProvider = HostPointerState (*)() noexcept;
using HostPointerTransitionProvider = HostPointerTransitionPoll (*)(
    std::uint64_t after_sequence) noexcept;
using NativeKeyboardCaptureProvider = bool (*)() noexcept;
using NativeSettingsVisibleProvider = bool (*)() noexcept;
using NativeSettingsKeySink = void (*)(std::uintptr_t virtual_key) noexcept;

void install_host_pointer_provider(HostPointerProvider provider) noexcept;
void install_host_pointer_transition_provider(
    HostPointerTransitionProvider provider) noexcept;
// The settings overlay owns window-key navigation; while it is visible,
// polled keyboard state is not forwarded to the game. Null disables capture.
void install_native_keyboard_capture_provider(
    NativeKeyboardCaptureProvider provider) noexcept;
[[nodiscard]] bool native_keyboard_capture_active() noexcept;
// Controller access to the settings overlay (galaxy/native_settings_pad.h):
// the overlay's visibility and a sink that delivers its keys to the window
// thread. Null disables the controller chord.
void install_native_settings_pad_hooks(
    NativeSettingsVisibleProvider visible,
    NativeSettingsKeySink sink) noexcept;
void publish_native_window_key_press(std::uint32_t key) noexcept;
void clear_native_window_key_presses() noexcept;
// Window-thread key level (down on WM_KEYDOWN, up on WM_KEYUP); cleared with
// the press latch.
void publish_native_window_key_state(std::uint32_t key, bool down) noexcept;
// One window key event, including its native scan identity. Call before discarding the message payload.
void publish_native_window_key_event(std::uint32_t key, std::uintptr_t message_bits,
                                    bool down, bool repeat, bool injected);
// Whether the window message for `key` / the latest mouse move or button came
// from SendInput (a controller mapper such as Steam's desktop layout) instead
// of a device. In auto input mode with a controller attached such input is
// ignored so the controller is not read twice.
void publish_native_window_input_origin(std::uint32_t key, bool injected) noexcept;
void publish_native_pointer_input_origin(bool button, bool injected) noexcept;
[[nodiscard]] bool native_window_key_injected(std::uint32_t key) noexcept;
[[nodiscard]] bool native_pointer_input_injected(bool button) noexcept;

// Binding capture for the overlay's CONTROLS page. While armed, the controller
// of `scheme` (and `slot`, 0xff: any of the family) is read in every input
// mode, never reaches the overlay's pad navigation, and the first new press
// (button, stick direction past 0.6, trigger past 0.5) is stored; the overlay
// is told through its key sink with kBindingCaptureKey and collects it with
// take_controller_binding_capture.
inline constexpr std::uintptr_t kBindingCaptureKey = 0xE8u;
void arm_controller_binding_capture(input::ControlScheme scheme, std::uint8_t slot = input::kKeyboardSlot) noexcept;
void cancel_controller_binding_capture() noexcept;
[[nodiscard]] std::optional<input::InputSource> take_controller_binding_capture() noexcept;

// The controllers attached now by model name: XInput pads and HID controllers
// (Switch, PlayStation, Steam). The input polls keep it current while
// controllers are polled; otherwise XInput is probed at most once a second.
std::size_t connected_controllers(std::span<input::ControllerInfo> out) noexcept;
// The device that last gave input, a controller or the keyboard and mouse.
[[nodiscard]] std::optional<input::DeviceKey> last_input_device() noexcept;
// The game's button icons now (ButtonIcons setting, the controller last used and its bindings); `menu`: a menu shows.
[[nodiscard]] input::ButtonIconSpec button_icons(bool menu) noexcept;
// The style the Auto setting shows now (the controller in use; the game's before it first asks).
[[nodiscard]] input::ButtonIconStyle button_icons_auto_style() noexcept;
// A person typed or clicked (overlay keys included).
void note_keyboard_input() noexcept;
#if defined(__ANDROID__)
// A keyboard or mouse plugged in, not the touch screen that stands in for them.
[[nodiscard]] bool physical_keyboard_attached() noexcept;
#endif
// The system lists a keyboard or a mouse.
[[nodiscard]] bool keyboard_or_mouse_attached() noexcept;
// The phone's on-screen controls: the editor where they are arranged, and what the input mode says of them.
void arrange_touch_controls() noexcept;
void input_mode_changed() noexcept;
// The on-screen controls' settings for the screen as it is now (each size and turn of a screen has its own size). size: in
// percent, 0: what the game gives this screen.
struct TouchControlsSettings {
    unsigned size = 0;
    unsigned opacity = 60;
    bool vibration = true;
};
[[nodiscard]] bool touch_controls_present() noexcept;
[[nodiscard]] TouchControlsSettings touch_controls_settings() noexcept;
void set_touch_controls_settings(const TouchControlsSettings& settings) noexcept;
void reset_touch_controls_places() noexcept;
// The window's width in density-independent units (96 to the inch on a desktop, 160 on a phone), for what must stay
// readable by its real size.
[[nodiscard]] unsigned window_width_dp() noexcept;
// NEBULA_OPTION_TOUCH_POINTER and NEBULA_CONTEXT_TOUCH_TARGET.
[[nodiscard]] unsigned touch_pointer_state() noexcept;
void note_touch_target(std::uint32_t circle) noexcept;
// A finger landing at x, y (window pixels) lands on something the pointer grabs.
[[nodiscard]] bool touch_target_at(float x, float y) noexcept;
// The chosen input mode has nothing to play with (input_missing): the game shows its message, and Enter opens the
// settings menu as F1 does.
void set_input_missing_shown(bool shown) noexcept;
[[nodiscard]] bool input_missing_shown() noexcept;
// Synchronous check (settings UI only): a controller is attached now.
[[nodiscard]] bool controller_present_now() noexcept;

// The live controls (bindings, keyboard and mouse, controllers, the scripted and replayed test input) as player 1's
// input at retrace `current_vi`. `star_pointer_mode` (-1 none) and `tilt_section` (0 none, 1 Star Ball, 2 Ray) come
// from the game: the control mapping's menu or play context and the tilt steering.
[[nodiscard]] galaxy::input::PlayerInput native_game_input(
    std::uint64_t current_vi,
    std::int32_t star_pointer_mode,
    std::uint8_t tilt_section);
// Player 2 (co-star mode), as of the last native_game_input: false while nobody plays it.
[[nodiscard]] bool native_game_co_star(galaxy::input::PlayerInput* input) noexcept;
// The game's motor bit to the controller in use, held off in menus (the HOME Menu, pause, error). Renew it with every
// input sample: a motor whose state goes stale stops.
void native_game_rumble(bool motor_on, std::int32_t star_pointer_mode) noexcept;

}  // namespace galaxy::host
