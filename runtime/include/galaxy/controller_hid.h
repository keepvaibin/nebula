#pragma once

// Switch Pro Controller / Switch 2 Pro Controller acquisition over Windows
// HID. One worker thread blocks on an overlapped ReadFile per device plus a
// device-change event (CM_Register_Notification on the HID interface class),
// so it costs nothing while idle and wakes at the controller's report rate.
// The input consumer only copies the latest decoded state.

#include "galaxy/input_bindings.h"
#include "galaxy/switch_controller.h"

#include <array>
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
    // (pointer units, 2 = picture width; up positive), integrated per IMU
    // sample in world yaw/pitch (switch_controller.h SwitchMotion).
    float gyro_x = 0.0f;
    float gyro_y = 0.0f;
    // The last report carried IMU samples (0x30 with the IMU on).
    bool imu_live = false;
    unsigned gyro_rest_windows = 0;
    // Controller orientation: the gravity reaction (up) in the SDL frame,
    // low-passed (SwitchMotion), valid once the IMU has streamed.
    std::array<float, 3> gravity{0.0f, 1.0f, 0.0f};
    bool gravity_valid = false;
    // Worker slot of this controller.
    std::uint8_t slot = 0;
};

// ---- Vibration ----------------------------------------------------------------
// The virtual Wii Remote's motor bit (rumble.h) mirrored to the controller in
// use. The emulation thread publishes the game's state with every virtual
// input report; the controller worker drives the motor (XInput, Switch output
// reports) and turns it off when publications stop for rumble::kStallUs, the
// window loses focus, a pause or HOME menu opens, the controller disconnects
// or the process exits. Test sessions (hidden window, GALAXY_RUMBLE_TRACE)
// never drive a motor.
struct RumbleTarget {
    enum class Kind : std::uint8_t { None, XInput, Switch };
    Kind kind = Kind::None;
    std::uint8_t slot = 0;  // XInput user index or Switch worker slot
    friend bool operator==(RumbleTarget, RumbleTarget) = default;
};
struct RumbleRequest {
    bool motor_on = false;  // the game's motor bit
    std::uint8_t strength = 0;
    RumbleTarget target{};
    bool focused = false;
    bool paused = false;
};
void publish_rumble_request(const RumbleRequest& request) noexcept;
// Every motor off (exit, crash handler); idempotent, bounded waits.
void stop_all_rumble() noexcept;
[[nodiscard]] bool rumble_output_disabled() noexcept;

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
