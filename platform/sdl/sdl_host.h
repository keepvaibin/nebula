// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The SDL host's platform layer (platform/sdl): SDL started before the renderer, its events pumped by the host's main
// loop, and the input it gathers (touch controls, the finger on the picture, keys, gamepads) for the virtual Wii
// Remote (native_game_host_sdl.cpp).

#include "galaxy/input_bindings.h"

#include <array>
#include <cstdint>

namespace galaxy::sdl {

// SDL's video, audio, gamepad and sensor subsystems, and logs to the platform's log (Android: logcat); false when SDL
// cannot start.
bool start() noexcept;
// Window, touch, key and gamepad events, waiting up to `timeout_ms` for the first.
void pump_events(int timeout_ms) noexcept;

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
    // Completed taps (a short touch that barely moved), counted; where the last one was.
    std::uint64_t taps = 0;
    float tap_x = 0.0f;
    float tap_y = 0.0f;
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
// Rumble on the gamepad with this SDL id (0: none) at `strength` (0..1).
void rumble(std::uint32_t pad_id, float strength) noexcept;

// The HOME Menu (Android: the activity's dialog): -1 until the player chooses (0 return, 1 quit, 2 reset).
void home_menu_show() noexcept;
void home_menu_hide() noexcept;
[[nodiscard]] int home_menu_choice() noexcept;

}  // namespace galaxy::sdl
