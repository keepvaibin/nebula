// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The SDL host's platform layer (platform/sdl): SDL started before the renderer, its events pumped by the host's main
// loop, and the input it gathers (touch controls, the finger on the picture, keys, gamepads) for the virtual Wii
// Remote (native_game_host_sdl.cpp).

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

// The display's vsync as the game's retrace clock (Android: Choreographer's vsync times, display_clock.cpp): sleeps
// until the vsync one retrace period after `last_ns` (CLOCK_MONOTONIC nanoseconds; updated) on the grid the latest
// vsync times predict, so every frame stays on screen for the same number of vsyncs. False, at once, when there are
// no recent vsyncs (out of sight, not yet shown), the display's rate does not divide the period, or the platform has
// no display clock: the host's own clock runs the retrace then.
bool wait_display_retrace(std::int64_t period_ns, std::int64_t& last_ns) noexcept;
// The retrace grid (CLOCK_MONOTONIC nanoseconds): the latest retrace, the retrace period, the display's vsync
// interval and now; false while the retrace follows the host's clock.
[[nodiscard]] bool display_grid(std::int64_t& retrace_ns, std::int64_t& period_ns, std::int64_t& vsync_ns,
                                std::int64_t& now_ns) noexcept;
// The latest vsync and the interval between vsyncs (the stated period when known; CLOCK_MONOTONIC nanoseconds);
// false while not known.
[[nodiscard]] bool vsync_grid(std::int64_t& vsync_ns, std::int64_t& interval_ns) noexcept;
// The display's refresh rate: the vsync period the system states, else as its vsyncs measure it (0: not known).
[[nodiscard]] double display_refresh_hz() noexcept;
// Android, when `adb shell setprop debug.nebula.profile <seconds>` asks: that many seconds on, the GX, host-main and
// game threads' instruction pointers are sampled for 2 s each and the busiest places logged (thread_sampler.cpp).
void start_thread_sampler() noexcept;

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

inline constexpr std::size_t kMaxPads = 4;

// One coherent copy of everything the input poll reads.
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
    std::uint64_t tap_down_ms = 0;
    // When the finger last touched or moved (steady milliseconds; 0: never).
    std::uint64_t finger_ms = 0;
    int window_width = 0;
    int window_height = 0;
    bool window_focused = true;
    // Keys (Windows virtual-key numbering, virtual_keys.h) and mouse buttons.
    galaxy::input::KeyboardMouseLevels keys{};
    // Gamepads, in the unified layout (input_bindings.h).
    std::array<galaxy::input::PadSample, kMaxPads> pads{};
    std::array<std::uint32_t, kMaxPads> pad_ids{};
};

[[nodiscard]] InputState input_state() noexcept;
// The game is on a pointer screen (menus, the file select): the on-screen controls leave the picture to the pointer.
void set_menu_context(bool menu) noexcept;
// Rumble on the gamepad with this SDL id (0: none) at `strength` (0..1). Any thread: SDL's thread applies it.
void rumble(std::uint32_t pad_id, float strength) noexcept;

// The HOME Menu (Android: the activity's dialog): -1 until the player chooses (0 return, 1 quit, 2 reset). Any
// thread: SDL's thread shows and hides it.
void home_menu_show() noexcept;
void home_menu_hide() noexcept;
[[nodiscard]] int home_menu_choice() noexcept;

}  // namespace galaxy::sdl
