#pragma once

// Switch Pro Controller / Switch 2 Pro Controller acquisition over Windows
// HID. One worker thread blocks on an overlapped ReadFile per device plus a
// device-change event (CM_Register_Notification on the HID interface class),
// so it costs nothing while idle and wakes at the controller's report rate.
// The input consumer only copies the latest decoded state.

#include "galaxy/input_bindings.h"
#include "galaxy/switch_controller.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace galaxy::host {

inline constexpr std::size_t kMaxSwitchPads = 4;

struct SwitchPadState {
    bool connected = false;
    input::ControlScheme scheme = input::ControlScheme::SwitchPro;
    input::switch_hid::DecodedState decoded{};
    input::switch_hid::StickCal left{};
    input::switch_hid::StickCal right{};
    // Motion shake held until this steady-clock microsecond (0: none).
    std::uint64_t shake_until_us = 0;
    // Pointer movement from the gyro since the previous consuming read
    // (input_bindings pointer units, up positive), already scaled by the
    // scheme's pointer speed.
    float gyro_x = 0.0f;
    float gyro_y = 0.0f;
};

// Starts the worker on first use; later calls are free.
void ensure_controller_hid_started() noexcept;
void stop_controller_hid() noexcept;
// Copies every connected pad into `out`; `consume_gyro` clears the gyro
// accumulators. Returns the number written. Never blocks on device I/O.
std::size_t read_switch_pads(std::span<SwitchPadState> out, bool consume_gyro) noexcept;
// Steady-clock microseconds, the time base of shake_until_us.
[[nodiscard]] std::uint64_t controller_hid_now_us() noexcept;

struct ControllerHidStatus {
    unsigned switch_pro = 0;
    unsigned switch_pro2 = 0;
    // A Pro Controller 2 is attached by USB but its vendor interface has no
    // WinUSB driver, so it cannot be told to start sending input.
    bool pro2_needs_winusb = false;
};
[[nodiscard]] ControllerHidStatus controller_hid_status() noexcept;
// Synchronous enumeration (settings UI only): any Switch Pro / Pro 2 present.
[[nodiscard]] bool switch_controller_present_now() noexcept;

}  // namespace galaxy::host
