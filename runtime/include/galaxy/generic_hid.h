#pragma once

// Generic HID game controllers: devices with a Joystick or Game Pad top-level
// collection (Generic Desktop page, usages 0x04 / 0x05) that no dedicated
// driver claims. Buttons are read by number, the hat switch as the d-pad, X/Y
// as the left stick and Z/Rz (or Rx/Ry) as the right one, as the HID Usage
// Tables define those usages. The report itself is read through the Windows
// HID parser (controller_hid.cpp); this maps the values onto the unified pad.

#include "galaxy/switch_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace galaxy::input::generic_hid {

inline constexpr std::uint16_t kUsagePageGenericDesktop = 0x01;
inline constexpr std::uint16_t kUsagePageSimulation = 0x02;
inline constexpr std::uint16_t kUsagePageButton = 0x09;
inline constexpr std::uint16_t kUsageJoystick = 0x04;
inline constexpr std::uint16_t kUsageGamePad = 0x05;
inline constexpr std::uint16_t kUsageX = 0x30;
inline constexpr std::uint16_t kUsageY = 0x31;
inline constexpr std::uint16_t kUsageZ = 0x32;
inline constexpr std::uint16_t kUsageRx = 0x33;
inline constexpr std::uint16_t kUsageRy = 0x34;
inline constexpr std::uint16_t kUsageRz = 0x35;
inline constexpr std::uint16_t kUsageHatSwitch = 0x39;
inline constexpr std::uint16_t kUsageAccelerator = 0xc4;
inline constexpr std::uint16_t kUsageBrake = 0xc5;

// Buttons 1..kMaxButtons are read; the first kMappedButtons have a unified bit.
inline constexpr std::size_t kMaxButtons = 32;
inline constexpr std::size_t kMappedButtons = 19;

[[nodiscard]] constexpr bool is_game_controller(std::uint16_t usage_page, std::uint16_t usage) noexcept {
    return usage_page == kUsagePageGenericDesktop && (usage == kUsageJoystick || usage == kUsageGamePad);
}

// Vendors whose controllers are read elsewhere: Microsoft (XInput), Nintendo,
// Sony and Valve (their own drivers in controller_hid.cpp; Steam's virtual
// pads are XInput devices). Paths with "IG_" are XInput interfaces.
[[nodiscard]] bool claimed_elsewhere(std::uint16_t vendor, const wchar_t* device_path) noexcept;

enum class Axis : std::uint8_t { X, Y, Z, Rx, Ry, Rz, Accelerator, Brake };
inline constexpr std::size_t kAxisCount = 8;

// One value usage as the device describes it.
struct ValueRange {
    bool present = false;
    std::int32_t min = 0;
    std::int32_t max = 0;
    std::uint16_t bits = 0;  // report field size, for sign extension
};

struct Layout {
    std::array<ValueRange, kAxisCount> axes{};
    ValueRange hat{};
    std::uint16_t buttons = 0;  // highest button number the device has
};

// The values of one report as the HID parser returns them: raw (unsigned)
// fields, and the numbers of the buttons held.
struct Raw {
    std::array<std::uint32_t, kAxisCount> axes{};
    std::uint32_t hat = 0;
    std::uint32_t buttons = 0;  // bit n-1: button n held
};

// The unified bit of 1-based button `number`, or -1 when it has none:
// 1..4 South, East, West, North; 5/6 LB/RB; 7/8 the grips (often L2/R2 as
// buttons); 9 Back; 10 Start; 11/12 the stick clicks; 13 Home; 14 Capture;
// 15..19 the remaining extra bits.
[[nodiscard]] int button_bit(unsigned number) noexcept;

// A field as a signed logical value (sign-extended when the range is signed).
[[nodiscard]] std::int32_t logical_value(const ValueRange& range, std::uint32_t raw) noexcept;

// Unified state of one report: `normalized` axes (sticks -1..1, up positive;
// triggers 0..1), buttons, the hat as the d-pad. The right stick is Z/Rz when
// the device has both, else Rx/Ry; triggers come from whichever of Rx/Ry the
// right stick leaves free, then Accelerator/Brake.
void map_report(const Layout& layout, const Raw& raw, switch_hid::DecodedState& out) noexcept;

}  // namespace galaxy::input::generic_hid
