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

// One MotionPlus sample: rad/s about the remote's own axes (x out of its left side, y toward its expansion port, z out
// of its A face), right-handed.
struct MotionRates {
    bool fresh = false;  // this report carried a MotionPlus sample
    std::array<float, 3> rate{};
};

// A MotionPlus's calibration (32 bytes from 0xA40020 once it is on): per axis (yaw, roll, pitch) the zero and the
// counts from it to `degrees` deg/s, at its fast and its slow range. Values out of reason take a typical unit's.
struct MotionPlusCalibration {
    std::array<float, 3> fast_zero{};
    std::array<float, 3> fast_span{};
    std::array<float, 3> fast_degrees{};
    std::array<float, 3> slow_zero{};
    std::array<float, 3> slow_span{};
    std::array<float, 3> slow_degrees{};
};
[[nodiscard]] MotionPlusCalibration parse_motion_plus_calibration(std::span<const std::uint8_t> block) noexcept;
// A MotionPlus data block (6 extension bytes, byte 5 bit 1 set) as rates; false for the Nunchuk's block it passes on.
// `passthrough_port`: a Nunchuk is plugged into the MotionPlus.
bool motion_plus_rates(std::span<const std::uint8_t> block, const MotionPlusCalibration& calibration, MotionRates& out,
                       bool& passthrough_port) noexcept;
// A Nunchuk's block as the MotionPlus passes it on (Nunchuk passthrough), back in the Nunchuk's own layout.
[[nodiscard]] std::array<std::uint8_t, 6> undo_nunchuk_passthrough(std::span<const std::uint8_t> block) noexcept;

class Reader {
public:
    // A connection: the remote's calibration as read from its EEPROM (0x2A bytes from 0), the Nunchuk's calibration
    // block (32 bytes from 0xA40020, or 16 from the MotionPlus's copy) when one is attached, the MotionPlus's (32
    // bytes) when it is on.
    void connect(std::span<const std::uint8_t> eeprom, std::span<const std::uint8_t> nunchuk_calibration, bool nunchuk,
                 std::span<const std::uint8_t> motion_plus_calibration = {});
    // One data report from its id (0x30-0x37) as one moment of input; false for a report this reader does not take.
    // With the MotionPlus on, a report carries either its sample (in `rates`) or the Nunchuk's; the Nunchuk's last
    // state stays in every report.
    bool read(std::span<const std::uint8_t> report, PlayerInput& out, MotionRates* rates = nullptr);

private:
    void point(std::span<const IrLight> lights, PlayerInput& out);
    void read_nunchuk(const std::uint8_t* e);

    bool motion_plus_ = false;
    MotionPlusCalibration motion_plus_calibration_{};
    // The Nunchuk as last read.
    bool nunchuk_c_ = false;
    bool nunchuk_z_ = false;
    float nunchuk_stick_x_ = 0.0f;
    float nunchuk_stick_y_ = 0.0f;
    Gravity nunchuk_gravity_{0.0f, 0.0f, 1.0f};

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
