// SPDX-License-Identifier: GPL-3.0-only
// Key numbers from the game window's key messages.
#pragma once

#include "galaxy/native_input.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace galaxy::input {

// Some injected Windows keyboard events carry a virtual key but no scan code.
// Map generic Shift/Control to the left/right bindings.
[[nodiscard]] constexpr std::uint32_t normalize_native_window_key(
    std::uint32_t key, std::uint32_t mapped_shift, bool right_control) noexcept {
    if (key == 0x10u) { // VK_SHIFT
        return mapped_shift == 0xa0u || mapped_shift == 0xa1u
            ? mapped_shift : 0xa0u;
    }
    if (key == 0x11u) return right_control ? 0xa3u : 0xa2u; // VK_CONTROL
    return key;
}

} // namespace galaxy::input
