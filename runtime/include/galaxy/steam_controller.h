#pragma once

// Valve's 2026 Steam Controller (USB 28de:1302, Bluetooth LE 28de:1303, and the
// wireless pucks 28de:1304 / 28de:1305): input report decoding and the output
// and feature reports that switch off its keyboard/mouse emulation, start the
// sensors and drive the haptic motors. Layouts follow SDL's zlib-licensed
// SDL_hidapi_steam_triton.c with Valve's controller_structs.h, cross-checked
// against the Linux hid-steam driver's tables (see docs/KEYBOARD_CONTROLS.md);
// this file is an independent implementation.

#include "galaxy/switch_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace galaxy::input::steam {

inline constexpr std::uint16_t kValveVendorId = 0x28de;
inline constexpr std::uint16_t kControllerUsbProductId = 0x1302;
inline constexpr std::uint16_t kControllerBleProductId = 0x1303;
inline constexpr std::uint16_t kProteusPuckProductId = 0x1304;
inline constexpr std::uint16_t kNereidPuckProductId = 0x1305;

// A puck exposes up to four controllers on USB interfaces 2..5.
inline constexpr unsigned kPuckFirstInterface = 2;
inline constexpr unsigned kPuckLastInterface = 5;

[[nodiscard]] constexpr bool is_controller_product(std::uint16_t product) noexcept {
    return product == kControllerUsbProductId || product == kControllerBleProductId;
}
[[nodiscard]] constexpr bool is_puck_product(std::uint16_t product) noexcept {
    return product == kProteusPuckProductId || product == kNereidPuckProductId;
}

// Input report ids (byte 0). 0x42 (USB) carries a quaternion after the sensors;
// 0x45 (Bluetooth) and 0x47 (16-bit sensor clock) do not.
inline constexpr std::uint8_t kReportStateUsb = 0x42;
inline constexpr std::uint8_t kReportBattery = 0x43;
inline constexpr std::uint8_t kReportStateBle = 0x45;
inline constexpr std::uint8_t kReportWirelessStatusX = 0x46;
inline constexpr std::uint8_t kReportStateShortClock = 0x47;
inline constexpr std::uint8_t kReportWirelessStatus = 0x79;
inline constexpr std::size_t kStateReportBytes = 46;

// Button word (report bytes 2..5, little endian).
namespace button {
inline constexpr std::uint32_t kA = 0x00000001;
inline constexpr std::uint32_t kB = 0x00000002;
inline constexpr std::uint32_t kX = 0x00000004;
inline constexpr std::uint32_t kY = 0x00000008;
inline constexpr std::uint32_t kQuickAccess = 0x00000010;
inline constexpr std::uint32_t kRightStick = 0x00000020;
inline constexpr std::uint32_t kMenu = 0x00000040;      // right small button: Start
inline constexpr std::uint32_t kRightGrip1 = 0x00000080;  // R4
inline constexpr std::uint32_t kRightGrip2 = 0x00000100;  // R5
inline constexpr std::uint32_t kRightBumper = 0x00000200;
inline constexpr std::uint32_t kDpadDown = 0x00000400;
inline constexpr std::uint32_t kDpadRight = 0x00000800;
inline constexpr std::uint32_t kDpadLeft = 0x00001000;
inline constexpr std::uint32_t kDpadUp = 0x00002000;
inline constexpr std::uint32_t kView = 0x00004000;      // left small button: Back
inline constexpr std::uint32_t kLeftStick = 0x00008000;
inline constexpr std::uint32_t kSteam = 0x00010000;
inline constexpr std::uint32_t kLeftGrip1 = 0x00020000;   // L4
inline constexpr std::uint32_t kLeftGrip2 = 0x00040000;   // L5
inline constexpr std::uint32_t kLeftBumper = 0x00080000;
inline constexpr std::uint32_t kRightPadClick = 0x00400000;
inline constexpr std::uint32_t kLeftPadClick = 0x04000000;
}  // namespace button

// Device clock of the sensors, for dt between reports.
struct SensorClock {
    std::uint32_t last = 0;
    bool primed = false;
};

// A state report (0x42, 0x45 or 0x47, at least kStateReportBytes). `out` takes
// the unified buttons (A = South, Menu = Start, View = Back, Steam = Home, quick
// access = Capture, L4/R4 = GL/GR, L5/R5 = GL2/GR2, pad clicks = LPad/RPad), the
// normalised sticks and analog triggers, and one IMU sample once the sensors
// are on. Other ids return false.
[[nodiscard]] bool decode_report(std::span<const std::uint8_t> report, SensorClock& clock,
                                 switch_hid::DecodedState& out) noexcept;

enum class WirelessEvent : std::uint8_t { None, Disconnected, Connected };
// Puck reports 0x46 and 0x79: a controller connecting to or leaving its slot.
[[nodiscard]] WirelessEvent decode_wireless_event(std::span<const std::uint8_t> report) noexcept;

// Feature report 1 (64 bytes including the id): settings the controller applies
// while the host keeps sending them. The lizard-mode emulation comes back after
// kSettingsRefreshMs without a refresh.
inline constexpr std::size_t kFeatureReportBytes = 64;
inline constexpr unsigned kSettingsRefreshMs = 3000;
std::size_t build_settings_report(std::span<std::uint8_t> out) noexcept;

// Output report 0x80: both haptic motors, speeds 0..65535. The controller stops
// them about 50 ms after the last report, so a held rumble repeats every
// kRumbleRepeatMs.
inline constexpr std::size_t kRumbleReportBytes = 10;
inline constexpr unsigned kRumbleRepeatMs = 40;
std::size_t build_rumble_report(std::uint16_t left, std::uint16_t right, std::span<std::uint8_t> out) noexcept;

}  // namespace galaxy::input::steam
