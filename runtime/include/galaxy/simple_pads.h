// Controllers with one fixed input report and no handshake to speak of: the input-only Switch pads, Stadia, Luna,
// Hori's Steam pad and the DualShock 3.
#pragma once

#include "galaxy/input_bindings.h"
#include "galaxy/switch_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace galaxy::input::simple_pads {

enum class Layout : std::uint8_t { None, SwitchInputOnly, Stadia, LunaUsb, LunaBluetooth, HoriSteam, Sixaxis, WiiUPro };

struct Product {
    Layout layout = Layout::None;
    ControlScheme scheme = ControlScheme::Generic;
    const char* name = "";
};
[[nodiscard]] std::optional<Product> find_product(std::uint16_t vendor, std::uint16_t product) noexcept;

// What outlives a report: Luna's Home over Bluetooth, which arrives in one of its own; a Wii U Pro's sticks' centres,
// taken from its first report.
struct ReportState {
    bool home = false;
    bool centred = false;
    std::array<std::uint16_t, 4> centre{};
};

// Byte 0 is the report id (Windows' 0x00 for a device without ids). False for other reports.
[[nodiscard]] bool decode_report(Layout layout, std::span<const std::uint8_t> report, ReportState& state,
                                 switch_hid::DecodedState& out) noexcept;

// A DualShock 3 has an accelerometer and no gyro this code reads.
[[nodiscard]] constexpr bool has_gyro(Layout layout) noexcept { return layout != Layout::Sixaxis; }

// The motor report for a speed of 0..65535 (0 stops it): its size, 0 where the layout has no motor.
[[nodiscard]] std::size_t build_motor_report(Layout layout, std::uint16_t speed, std::span<std::uint8_t> out) noexcept;

inline constexpr std::uint8_t kSixaxisFeatureInit = 0xf2;
inline constexpr std::uint8_t kSixaxisFeatureAddress = 0xf5;
inline constexpr std::size_t kSixaxisReportBytes = 49;
inline constexpr std::uint8_t kWiiUProReport = 0x3d;

}  // namespace galaxy::input::simple_pads
