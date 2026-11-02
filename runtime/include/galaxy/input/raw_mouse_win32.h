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
// Decode a copied native packet without accessing a device or a window.
std::optional<galaxy::input::RawMouseMotion> decode_native_raw_mouse(std::span<const std::byte> packet) noexcept;
}  // namespace galaxy::host
