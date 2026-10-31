#pragma once

// A real Wii Remote's data reports as the player's input (PlayerInput): its buttons, its accelerometer and its
// Nunchuk's through their own calibration, and its IR camera's view of the sensor bar as the pointer. Report layouts
// per WiiBrew's Wiimote pages; the pointer is this file's own two-light tracker.

#include "galaxy/native_input.h"

#include <array>
#include <cstdint>
#include <span>

namespace galaxy::input::wiimote {

// The IR camera's picture: 1024 x 768, a light (x, y) with y down as the camera sees it.
struct IrLight {
    float x = 0.0f;
    float y = 0.0f;
};

class Reader {
public:
    // A connection: the remote's calibration as read from its EEPROM (0x2A bytes from 0), the Nunchuk's calibration
    // block (32 bytes from 0xA40020) when one is attached.
    void connect(std::span<const std::uint8_t> eeprom, std::span<const std::uint8_t> nunchuk_calibration, bool nunchuk);
    // One data report from its id (0x30-0x37) as one moment of input; false for a report this reader does not take.
    bool read(std::span<const std::uint8_t> report, PlayerInput& out);

private:
    void point(std::span<const IrLight> lights, PlayerInput& out);

    std::array<float, 3> zero_{512.0f, 512.0f, 512.0f};
    std::array<float, 3> scale_{0.01f, 0.01f, 0.01f};  // g per count
    std::array<float, 3> nunchuk_zero_{512.0f, 512.0f, 512.0f};
    std::array<float, 3> nunchuk_scale_{1.0f / 204.0f, 1.0f / 204.0f, 1.0f / 204.0f};
    bool nunchuk_ = false;
    bool stick_origin_set_ = false;
    int stick_origin_x_ = 0;
    int stick_origin_y_ = 0;
    // The pointer as last seen (for its smoothing) and whether it was.
    bool pointing_ = false;
    float pointer_x_ = 0.0f;
    float pointer_y_ = 0.0f;
    Gravity gravity_{0.0f, 0.0f, 1.0f};
};

// The IR lights of a report's camera bytes: basic (10 bytes, 4 lights in 2 pairs) or extended (12, 4 lights of 3).
// Lights the camera does not see are left out; returns how many it sees.
std::size_t ir_lights_basic(std::span<const std::uint8_t> bytes, std::array<IrLight, 4>& out) noexcept;
std::size_t ir_lights_extended(std::span<const std::uint8_t> bytes, std::array<IrLight, 4>& out) noexcept;

// The pointer from the sensor bar's two lights `a` and `b` (camera picture) and the remote's gravity: the picture's
// position the remote points at (-1..1 across, y down), the remote's roll as the screen sees it, the distance. False
// when no pair of the lights reads as the bar.
struct BarView {
    float x = 0.0f;
    float y = 0.0f;
    float roll_x = 1.0f;
    float roll_y = 0.0f;
    float distance = 0.0f;
};
[[nodiscard]] bool sensor_bar_view(std::span<const IrLight> lights, const Gravity& gravity, BarView& out) noexcept;

}  // namespace galaxy::input::wiimote
