// SPDX-License-Identifier: CC0-1.0
// The game window's key presses and levels for the input report. The press latch follows the per-frame press state
// of TwilitRealm/dusklight, src/dusk/action_bindings.cpp::{updateActionBindings,getActionBindTrig}, revision
// 40457c6adb381928e4b5fef6ed459ed291edd5e2 (CC0), with separate producer and consumer clocks.
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

// Keeps a non-repeat key-down from the window thread until the input report consumes it, even when the key-up
// arrives first. It never makes up repeats.
class NativeWindowKeyPressLatch {
public:
    void publish(std::uint32_t key) noexcept {
        if (key < pending_.size()) pending_[key].store(true, std::memory_order_release);
    }
    [[nodiscard]] bool consume(std::uint32_t key) noexcept {
        return key < pending_.size() &&
            pending_[key].exchange(false, std::memory_order_acq_rel);
    }
    void clear() noexcept {
        for (auto& key : pending_) key.store(false, std::memory_order_release);
    }
private:
    std::array<std::atomic_bool, 256> pending_{};
};

// Keys the game window currently holds down, kept from its WM_KEYDOWN /
// WM_KEYUP messages and cleared when it loses focus, so the input poll reads
// memory instead of issuing one GetAsyncKeyState system call per key.
class NativeWindowKeyHeldState {
public:
    void set(std::uint32_t key, bool down) noexcept {
        if (key < held_.size()) held_[key].store(down, std::memory_order_release);
    }
    [[nodiscard]] bool held(std::uint32_t key) const noexcept {
        return key < held_.size() && held_[key].load(std::memory_order_acquire);
    }
    void clear() noexcept {
        for (auto& key : held_) key.store(false, std::memory_order_release);
    }
private:
    std::array<std::atomic_bool, 256> held_{};
};

} // namespace galaxy::input
