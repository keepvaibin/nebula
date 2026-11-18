// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/input/raw_mouse.h"
#include <cstddef>
#include <optional>
#include <span>

namespace galaxy::host {
// Only the window thread registers its own visible foreground-input window. Legacy mouse messages stay enabled.
bool install_native_raw_mouse(std::uintptr_t window) noexcept;
void remove_native_raw_mouse(std::uintptr_t window) noexcept;
void publish_native_raw_mouse(std::uintptr_t window, std::uintptr_t input_handle) noexcept;
void clear_native_raw_mouse() noexcept;
galaxy::input::RawMouseMotion sample_native_raw_mouse() noexcept;

// A mod's first-person look's mouse hold (galaxy/input/mouse_look_hold.h). The game's thread says at each controller read
// whether the look wants the mouse; a change is posted to the window as kNativeMouseLookMessage, and the window's thread
// holds or gives it back there and at its own focus, size and move messages. Raw input is registered only while held.
inline constexpr unsigned kNativeMouseLookMessage = 0x0400u + 0x48u;  // WM_USER + 0x48 (host_window_win32.h has 0x45 to 0x47)
void want_native_mouse_look(bool wanted) noexcept;
[[nodiscard]] bool native_mouse_look_wanted() noexcept;
// The window the change is posted to (the window's thread, from WM_CREATE; 0 at WM_DESTROY).
void set_native_mouse_look_window(std::uintptr_t window) noexcept;
// Decode a copied native packet without accessing a device or a window.
std::optional<galaxy::input::RawMouseMotion> decode_native_raw_mouse(std::span<const std::byte> packet) noexcept;
}  // namespace galaxy::host
