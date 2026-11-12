// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The SDL host's platform layer (platform/sdl): SDL started before the renderer, its events pumped by the host's main
// loop, and the input it gathers (touch controls, the finger on the picture, keys, gamepads) for the virtual Wii
// Remote (native_game_host_sdl.cpp).

#include "galaxy/controller_roster.h"
#include "galaxy/input_bindings.h"

#include <array>
#include <cstdint>

namespace galaxy::sdl {

// SDL's video, audio, gamepad and sensor subsystems, and logs to the platform's log (Android: logcat, and
// <data>/logs/game.log, the previous run's kept as game.previous.log); false when SDL cannot start.
bool start() noexcept;
// A fatal error, for the player: a message box, and <data>/last_error.txt for the app's launcher.
void show_fatal(const char* message) noexcept;
// Window, touch, key and gamepad events, waiting up to `timeout_ms` for the first.
void pump_events(int timeout_ms) noexcept;
// The app is out of sight (Android: the activity stopped); the host stops the game's clocks meanwhile.
[[nodiscard]] bool in_background() noexcept;

// The display's vsync as the game's frame clock (Android: Choreographer's vsync times, display_clock.cpp): sleeps
// until the vsync one frame period after `last_ns` (CLOCK_MONOTONIC nanoseconds; updated) on the grid the latest
// vsync times predict, so every frame stays on screen for the same number of vsyncs. False, at once, when there are
// no recent vsyncs (out of sight, not yet shown), the display's rate does not divide the period, or the platform has
// no display clock: the host's own clock times the frame then.
bool wait_display_frame(std::int64_t period_ns, std::int64_t& last_ns) noexcept;
// The frame grid (CLOCK_MONOTONIC nanoseconds): the latest frame slot, the frame period, the display's vsync
// interval and now; false while frames follow the host's clock.
[[nodiscard]] bool display_grid(std::int64_t& slot_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                                std::int64_t& now_ns) noexcept;
// The latest vsync and the interval between vsyncs (the stated period when known; CLOCK_MONOTONIC nanoseconds);
// false while not known.
[[nodiscard]] bool vsync_grid(std::int64_t& vsync_ns, std::int64_t& interval_ns) noexcept;
// The display's refresh rate: the vsync period the system states, else as its vsyncs measure it (0: not known).
[[nodiscard]] double display_refresh_hz() noexcept;
// Android, when `adb shell setprop debug.nebula.profile <seconds>` asks: that many seconds on, the render, host-main and
// game threads' instruction pointers are sampled for 2 s each and the busiest places logged (thread_sampler.cpp).
void start_thread_sampler() noexcept;
// Linux and Android: a fatal signal (a bad access, an abort) is reported in the log before the system's own handler runs
// (crash_log.cpp): the signal, the thread and the stack as library + offset. `fd` is the log file (-1: stderr).
void install_crash_log(int fd) noexcept;

// The on-screen controls (Android: the activity's overlay, through JNI).
enum TouchButton : std::uint32_t {
    kTouchA = 1u << 0,
    kTouchB = 1u << 1,
    kTouchZ = 1u << 2,
    kTouchC = 1u << 3,
    kTouchShake = 1u << 4,
    kTouchMinus = 1u << 5,
    kTouchPlus = 1u << 6,
    kTouchHome = 1u << 7,
    kTouchOne = 1u << 8,
    kTouchTwo = 1u << 9,
    kTouchUp = 1u << 10,
    kTouchDown = 1u << 11,
    kTouchLeft = 1u << 12,
    kTouchRight = 1u << 13,
};

// The pads the host keeps state for: the controllers the other hosts' sources can report at once (galaxy::input::kMaxControllers).
inline constexpr std::size_t kMaxPads = galaxy::input::kMaxControllers;

// One coherent copy of everything the input poll reads.
// A pad's sensors and touch surface.
struct PadMotion {
    // What the pad has, as the settings show it: motion sensors (SDL's, or the controller's own through Android) and a
    // touch surface.
    bool has_gyro = false;
    bool has_accel = false;
    bool has_touch = false;
    bool imu_live = false;
    bool touch_live = false;
    bool gravity_valid = false;
    float gyro_x = 0.0f;
    float gyro_y = 0.0f;  // up positive; picture widths turned
    float touch_dx = 0.0f;
    float touch_dy = 0.0f;  // the surface's width and height 1 each
    std::array<float, 3> gravity{0.0f, 1.0f, 0.0f};
    std::uint64_t shake_until_us = 0u;
};

struct InputState {
    // On-screen controls.
    std::uint32_t touch_buttons = 0;
    float touch_stick_x = 0.0f;  // -1..1
    float touch_stick_y = 0.0f;  // up positive
    // The finger on the picture (the pointer): window pixels, while down.
    bool finger_down = false;
    float finger_x = 0.0f;
    float finger_y = 0.0f;
    // Completed taps (a short touch that barely moved), counted; where the last one was and when it touched down
    // (steady milliseconds).
    std::uint64_t taps = 0;
    float tap_x = 0.0f;
    float tap_y = 0.0f;
    // When the finger last touched or moved (steady milliseconds; 0: never).
    std::uint64_t finger_ms = 0;
    // Where the pointer's finger last came down, and when.
    std::uint64_t finger_land_ms = 0;
    float finger_land_x = 0.0f;
    float finger_land_y = 0.0f;
    // Since it came down, the finger has gone past the system's touch slop (a still fingertip's tremor stays inside).
    bool finger_moved = false;
    // The pointer's place (finger_x, finger_y) came from a mouse: it points wherever it is over the window, as on
    // Windows, with none of a finger's rules (shown only while it touches and a moment after; a menu's pointer only
    // from a tap). `mouse_inside`: the mouse is over the window.
    bool mouse_pointer = false;
    bool mouse_inside = false;
    int window_width = 0;
    int window_height = 0;
    bool window_focused = true;
    // Keys (Windows virtual-key numbering, virtual_keys.h) and mouse buttons.
    galaxy::input::KeyboardMouseLevels keys{};
    // Gamepads, in the unified layout (input_bindings.h).
    std::array<galaxy::input::PadSample, kMaxPads> pads{};
    std::array<std::uint32_t, kMaxPads> pad_ids{};
    // Each pad's button icon model: the DualShock 3's and the GameCube pad's own buttons, else the family's.
    std::array<galaxy::input::ButtonIconModel, kMaxPads> pad_icons{};
    // Each pad's motion and touch surface since the game last took them (take_pad_motion).
    std::array<PadMotion, kMaxPads> motion{};
    // The device's own sensors (a phone), when it has them.
    PadMotion device_motion{};
};

[[nodiscard]] InputState input_state() noexcept;
// input_state with each pad's turn and touch travel, which then start again from nothing: the game's thread, once a frame.
[[nodiscard]] InputState take_input_state() noexcept;
// The game is on a pointer screen (menus, the file select): the on-screen controls leave the picture to the pointer.
void set_menu_context(bool menu) noexcept;
// The game shows its title: a tap anywhere starts it.
void set_title_screen(bool title) noexcept;
// Nebula's menu is open: a finger is its mouse (native_game_input), so a swipe is no wheel.
void set_direct_touch(bool direct) noexcept;
// Rumble on the gamepad with this SDL id (0: none) at `strength` (0..1). Any thread: SDL's thread applies it.
void rumble(std::uint32_t pad_id, float strength) noexcept;


}  // namespace galaxy::sdl
