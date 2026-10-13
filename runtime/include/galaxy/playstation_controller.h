#pragma once

// Sony DualShock 4, DualSense and DualSense Edge over HID (USB and Bluetooth):
// input report decoding, IMU calibration and the output (effects) reports.
// Report layouts and feature report numbers follow SDL's zlib-licensed
// SDL_hidapi_ps4.c and SDL_hidapi_ps5.c (see docs/KEYBOARD_CONTROLS.md); this
// file is an independent implementation.

#include "galaxy/switch_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace galaxy::input::playstation {

inline constexpr std::uint16_t kSonyVendorId = 0x054c;
inline constexpr std::uint16_t kDualShock4ProductId = 0x05c4;
inline constexpr std::uint16_t kDualShock4SlimProductId = 0x09cc;
inline constexpr std::uint16_t kDualSenseProductId = 0x0ce6;
inline constexpr std::uint16_t kDualSenseEdgeProductId = 0x0df2;

enum class Model : std::uint8_t { DualShock4, DualSense, DualSenseEdge };
enum class Transport : std::uint8_t { Usb, Bluetooth };

[[nodiscard]] std::optional<Model> model_for_product(std::uint16_t vendor, std::uint16_t product) noexcept;
[[nodiscard]] const char* model_name(Model model) noexcept;

// Input report ids. Over Bluetooth a controller sends the short report 0x01
// until a calibration feature report is read, then the full one.
inline constexpr std::uint8_t kReportState = 0x01;
inline constexpr std::uint8_t kDualShock4BluetoothFirst = 0x11;  // 0x11..0x19
inline constexpr std::uint8_t kDualShock4BluetoothLast = 0x19;
inline constexpr std::uint8_t kDualSenseBluetooth = 0x31;
// Bluetooth reports carry a CRC-32 in the last four bytes of their first 78.
inline constexpr std::size_t kBluetoothReportBytes = 78;

// Feature reports. Reading the Bluetooth calibration report switches a
// DualShock 4 to full reports; a DualSense switches on any of these.
inline constexpr std::uint8_t kFeatureDualShock4CalibrationUsb = 0x02;
inline constexpr std::uint8_t kFeatureDualShock4CalibrationBluetooth = 0x05;
inline constexpr std::uint8_t kFeatureDualSenseCalibration = 0x05;
inline constexpr std::uint8_t kFeatureDualSenseFirmware = 0x20;

// Sensor calibration: bias and scale per axis (gyro x, y, z then accel x, y, z)
// turning raw counts into rad/s and g. The defaults are the nominal sensor
// ranges (2000 deg/s over +-32768 at 1/16 deg/s per count, 8192 counts per g).
struct ImuCal {
    std::array<std::int16_t, 6> bias{};
    std::array<float, 6> scale{};
    bool hardware = false;
    ImuCal() noexcept;
};

// Calibration feature report (byte 0 = report id, at least 35 bytes). The
// Bluetooth DualShock 4 report orders the gyro limits plus-first; the others
// group them by axis. A calibration with a bias over 1024 counts or a scale
// off the nominal by half is bad and leaves the defaults.
[[nodiscard]] ImuCal parse_calibration(std::span<const std::uint8_t> feature, bool plus_first_order) noexcept;

// Device clock of the sensors: dt from consecutive reports (DualShock 4: 16-bit
// ticks of 16/3 us; DualSense: 32-bit ticks of 1/3 us).
struct SensorClock {
    std::uint32_t last = 0;
    bool primed = false;
};

// One input report as read from the handle (byte 0 = report id; trailing zero
// padding up to the handle's report length is ignored). `out.normalized` is
// set; buttons use the unified bits (Cross = South, Share/Create = Back,
// Options = Start, PS = Home, touchpad click = Capture, mute = Chat, Edge
// paddles = GL/GR). Over Bluetooth a full report with a bad CRC is rejected.
[[nodiscard]] bool decode_report(Model model, Transport transport, std::span<const std::uint8_t> report,
                                 const ImuCal& cal, SensorClock& clock, switch_hid::DecodedState& out) noexcept;

// CRC-32 (reflected 0xEDB88320) of `seed` followed by `data`: the Bluetooth
// report check. Input reports use seed 0xA1, output reports 0xA2.
[[nodiscard]] std::uint32_t report_crc(std::uint8_t seed, std::span<const std::uint8_t> data) noexcept;
[[nodiscard]] bool bluetooth_crc_valid(std::span<const std::uint8_t> report) noexcept;

// Output state. Motors are 0..255 (left: low-frequency, right: high-frequency).
// A flag selects what the controller applies from the report; everything else
// in the controller is left as it is.
struct Effects {
    std::uint8_t rumble_left = 0;
    std::uint8_t rumble_right = 0;
    std::array<std::uint8_t, 3> led{};
    bool apply_rumble = false;
    bool apply_led = false;
    // DualSense: hands the light bar back to the controller first (the pairing
    // animation otherwise swallows the colour).
    bool release_led = false;
    // DualSense firmware 2.24 and later and the Edge take full-range motors;
    // older firmware takes half range.
    bool full_range_rumble = true;
};

// Report length the builders return, by model and transport.
[[nodiscard]] std::size_t output_report_bytes(Model model, Transport transport) noexcept;
// Zeroes `out` and writes the report; returns its meaningful length (0 when
// `out` is too small). The caller writes the handle's full output length.
std::size_t build_output_report(Model model, Transport transport, const Effects& effects,
                                std::span<std::uint8_t> out) noexcept;

// Firmware version of a DualSense from feature report 0x20 (0 if unknown).
[[nodiscard]] std::uint16_t dualsense_firmware(std::span<const std::uint8_t> feature) noexcept;
[[nodiscard]] bool dualsense_full_range_rumble(Model model, std::uint16_t firmware) noexcept;

}  // namespace galaxy::input::playstation
