// SPDX-License-Identifier: CC0-1.0
// Frame-owned current/previous press state adapted from TwilitRealm/dusklight,
// src/dusk/action_bindings.cpp::{updateActionBindings,getActionBindTrig},
// revision 40457c6adb381928e4b5fef6ed459ed291edd5e2 (CC0).
// Galaxy changes: packed Wii masks, separate producer/consumer clocks and a
// rising-edge latch. Acquisition helpers below are independently written.
#pragma once

#include "galaxy/native_input.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace galaxy::input {

[[nodiscard]] inline std::uint32_t synthetic_kpad_button_mask(
    const WiimoteInputSnapshot& snapshot) noexcept {
    const auto& b = snapshot.buttons;
    return (b.left ? 0x0001u : 0u) | (b.right ? 0x0002u : 0u) |
        (b.down ? 0x0004u : 0u) | (b.up ? 0x0008u : 0u) |
        (b.plus ? 0x0010u : 0u) | (b.two ? 0x0100u : 0u) |
        (b.one ? 0x0200u : 0u) | (b.b ? 0x0400u : 0u) |
        (b.a ? 0x0800u : 0u) | (b.minus ? 0x1000u : 0u) |
        (snapshot.nunchuk.z ? 0x2000u : 0u) |
        (snapshot.nunchuk.c ? 0x4000u : 0u) | (b.home ? 0x8000u : 0u);
}

struct SyntheticKpadButtons {
    std::uint32_t hold{}, trigger{}, release{}, retained_press{};
};

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

// Owned by the guest execution thread. Several 100 Hz device acquisitions can
// occur before one game-frame read. A press observed between those reads must
// survive replacement of the latest device sample. At most one edge per bit
// is represented in a game frame; repeated taps inside that frame coalesce.
class SyntheticKpadButtonState {
public:
    void capture(std::uint32_t hold) noexcept {
        pending_press_ |= hold & ~latest_hold_;
        latest_hold_ = hold;
    }
    [[nodiscard]] SyntheticKpadButtons consume(bool retain_edges = true) noexcept {
        const auto previous = current_frame_;
        const auto retained = retain_edges ? pending_press_ & ~latest_hold_ : 0u;
        current_frame_ = latest_hold_ | retained;
        pending_press_ = 0u;
        return {current_frame_, current_frame_ & ~previous,
                previous & ~current_frame_, retained};
    }
    void reset() noexcept { *this = {}; }
private:
    std::uint32_t latest_hold_{}, pending_press_{}, current_frame_{};
};

// Window messages are a separate producer from the host HID acquisition.
// Remember a non-repeat key-down even if key-up arrives before the next poll.
// Held-key levels still come from the OS. This latch never fabricates repeats.
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

} // namespace galaxy::input
