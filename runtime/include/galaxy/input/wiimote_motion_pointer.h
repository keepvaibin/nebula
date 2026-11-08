// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A real Wii Remote's pointer from its Wii MotionPlus (docs/WII_REMOTE.md, Motion pointer): the remote's
// accelerometer and rates go through the pad gyro pointer (switch_controller.h SwitchMotion), and the pointer behaves
// as the gyro pointer of the game's Switch release (pointer_control.h): 2 puts it in the middle and shows it, it hides
// after a rest, it leaves the picture as a remote pointed past the screen does.

#include "galaxy/input/wiimote_input.h"
#include "galaxy/native_input.h"
#include "galaxy/pointer_control.h"
#include "galaxy/switch_controller.h"

#include <array>
#include <cstdint>

namespace galaxy::input::wiimote {

class MotionPointer {
public:
    void reset() noexcept;
    // One report at `now_us`, read: its buttons, its acceleration (input.gravity) and, when `rates.fresh`, a MotionPlus
    // sample. Writes the pointer into `input` and takes 2 from it (2 is the pointer's in this mode).
    void apply(PlayerInput& input, const MotionRates& rates, std::uint64_t now_us) noexcept;

    // The remote's frame to SwitchMotion's (x right, y up, z toward the player, the remote held level at the screen).
    [[nodiscard]] static std::array<float, 3> to_pad_frame(const std::array<float, 3>& remote) noexcept {
        return {-remote[0], remote[2], remote[1]};
    }

    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] float x() const noexcept { return x_; }
    [[nodiscard]] float y() const noexcept { return y_; }  // up positive

private:
    switch_hid::SwitchMotion motion_{};
    RecenterHold recenter_{};
    PointerRestHide hide_{};
    float x_ = 0.0f;
    float y_ = 0.0f;
    bool started_ = false;  // 2 pressed since the connection
    std::uint64_t last_rates_us_ = 0;
};

}  // namespace galaxy::input::wiimote
