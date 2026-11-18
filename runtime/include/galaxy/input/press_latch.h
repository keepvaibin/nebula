// SPDX-License-Identifier: GPL-3.0-only
// Presses that come and go between two reads of the game (P-075). Buttons travel as levels, so a press that starts and
// ends between two reads would never reach the game. Every host notes each press as it sees one (an event, a report, a
// sample on a thread of its own), and the game's read takes the levels together with every press since its last read:
// a press is down for one step at least.
#pragma once

#include <cstdint>

namespace galaxy::input {

class PressLatch {
public:
    // Buttons seen down now (under the lock of whoever keeps the latch).
    void press(std::uint32_t buttons) noexcept { pressed_ |= buttons; }
    // The game's read: the levels and every press since the last read.
    [[nodiscard]] std::uint32_t take(std::uint32_t levels) noexcept {
        const std::uint32_t out = levels | pressed_;
        pressed_ = 0u;
        return out;
    }

private:
    std::uint32_t pressed_ = 0u;
};

}  // namespace galaxy::input
