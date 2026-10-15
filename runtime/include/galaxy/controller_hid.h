#pragma once

// Switch Pro Controller, Switch 2 Pro Controller, PlayStation (DualShock 4,
// DualSense, DualSense Edge), Steam Controller and generic game controller
// (generic_hid.h) acquisition over Windows HID. One worker thread blocks on an overlapped ReadFile per device plus a
// device-change event (CM_Register_Notification on the HID interface class),
// so it costs nothing while idle and wakes at the controller's report rate.
// The input consumer only copies the latest decoded state.

#include "galaxy/controller_roster.h"
#include "galaxy/input_bindings.h"
#include "galaxy/switch_controller.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace galaxy::host {

inline constexpr std::size_t kMaxHidPads = 8;

struct HidPadState {
    bool connected = false;
    input::ControlScheme scheme = input::ControlScheme::SwitchPro;
    input::switch_hid::DecodedState decoded{};
    // Switch sticks are raw and calibrated here; the other families arrive
    // normalised.
    input::switch_hid::StickCal left{};
    input::switch_hid::StickCal right{};
    // Motion shake held until this steady-clock microsecond (0: none).
    std::uint64_t shake_until_us = 0;
    // Pointer movement from the gyro since the previous consuming read
    // (pointer units, 2 = picture width; up positive), integrated per IMU
    // sample in world yaw/pitch (switch_controller.h SwitchMotion).
    float gyro_x = 0.0f;
    float gyro_y = 0.0f;
    // The last report carried IMU samples.
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
// input report; the controller worker drives the motor (XInput, HID output
// reports) and turns it off when publications stop for rumble::kStallUs, the
// window loses focus, a pause or HOME menu opens, the controller disconnects
// or the process exits. Test sessions (hidden window, GALAXY_RUMBLE_TRACE)
// never drive a motor.
struct RumbleTarget {
    enum class Kind : std::uint8_t { None, XInput, Hid };
    Kind kind = Kind::None;
    std::uint8_t slot = 0;  // XInput user index or HID worker slot
    input::ControlScheme scheme = input::ControlScheme::Xbox;  // Hid: the controller family
    friend bool operator==(RumbleTarget, RumbleTarget) = default;
};
struct RumbleRequest {
    bool motor_on = false;   // the game's motor bit
    std::uint8_t level = 0;  // motor level, 0..rumble::kLevelSteps (rumble::MotorEnvelope)
    std::uint8_t strength = 0;  // percent; 0 = vibration off
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
std::size_t read_hid_pads(std::span<HidPadState> out, bool consume_gyro) noexcept;
// Steady-clock microseconds, the time base of shake_until_us.
[[nodiscard]] std::uint64_t controller_hid_now_us() noexcept;

// The HID controllers attached now, by model name (settings UI). With the
// worker running these are its devices and slots; without it the devices are
// enumerated synchronously and numbered in order.
std::size_t hid_controllers(std::span<input::ControllerInfo> out) noexcept;
[[nodiscard]] bool hid_controller_present_now() noexcept;

}  // namespace galaxy::host
