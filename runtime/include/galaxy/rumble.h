#pragma once

// Controller vibration that mirrors the virtual Wii Remote's motor. The game
// drives the motor itself (WPadRumble::updateRumble -> WPADControlMotor); its
// on/off bit goes through the motor model below to the controller in use,
// with no patterns of the host's own.
//
// Encodings: Switch Pro Controller HD rumble per dekuNukem
// Nintendo_Switch_Reverse_Engineering (rumble_data_table.md); Pro Controller 2
// packet layout per SDL's zlib-licensed SDL_hidapi_switch2.c. Independent
// implementations.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace galaxy::input::rumble {

// tune.<scheme>.vibration turns the motor on or off; tune.<scheme>.rumble is
// its strength in percent, 10..100 in steps of 10.
inline constexpr std::uint8_t kMinStrength = 10;
inline constexpr std::uint8_t kMaxStrength = 100;
inline constexpr std::uint8_t kStrengthStep = 10;
inline constexpr std::uint8_t kDefaultStrength = 50;
// The nearest step (ties go down) within 10..100; not a number: the default.
[[nodiscard]] inline std::uint8_t snap_strength(float percent) noexcept {
    if (!std::isfinite(percent)) return kDefaultStrength;
    const float steps = (std::clamp<float>(percent, kMinStrength, kMaxStrength) - kMinStrength) / kStrengthStep;
    int whole = static_cast<int>(std::floor(steps));
    if (steps - static_cast<float>(whole) > 0.5f) ++whole;
    return static_cast<std::uint8_t>(kMinStrength + kStrengthStep * whole);
}

// The Wii Remote's motor takes tens of milliseconds to spin up and down, so
// the game's short pulses (a Star Bit shot is four within about 170 ms) feel
// like one soft buzz. Controllers with instant actuators get the same curve:
// the level rises toward 1 while the game's bit is on and falls toward 0
// while it is off. Published levels are quantized to kLevelSteps.
inline constexpr float kMotorRiseSeconds = 0.03f;
inline constexpr float kMotorFallSeconds = 0.06f;
inline constexpr float kMotorStopLevel = 0.05f;
inline constexpr std::uint8_t kLevelSteps = 32;
struct MotorEnvelope {
    float level = 0.0f;
    std::uint64_t last_us = 0;
    float update(bool on, std::uint64_t now_us) noexcept;
};
[[nodiscard]] std::uint8_t quantize_level(float level) noexcept;

// Output strength for a quantized level (0..kLevelSteps) at `strength`
// percent: Switch HD rumble amplitude of the 160 Hz low band (the 320 Hz high
// band, the audible part, runs at kSwitchHighShare of it) and the XInput
// light motor (the heavy motor stays off). The lowest strength still reaches
// the actuators' threshold.
inline constexpr float kSwitchLowAmplitudeMin = 0.18f;
inline constexpr float kSwitchLowAmplitudeMax = 0.70f;
inline constexpr float kSwitchHighShare = 0.35f;
inline constexpr float kXInputSpeedMin = 0.15f;
[[nodiscard]] float switch_low_amplitude(std::uint8_t level, std::uint8_t strength) noexcept;

// Switch Pro Controller: one actuator's 4 bytes, high band (frequency +
// amplitude) then low band. Frequency in Hz (40.875-1252), amplitude 0..1
// (values above 1 clamp to 1).
[[nodiscard]] std::array<std::uint8_t, 4> encode_switch_hd_rumble(
    float high_hz, float high_amplitude, float low_hz, float low_amplitude) noexcept;
// 00 01 40 40: 320 Hz / 160 Hz at zero amplitude.
inline constexpr std::array<std::uint8_t, 4> kSwitchNeutralRumble{0x00, 0x01, 0x40, 0x40};
inline constexpr float kSwitchMotorLowHz = 160.0f;
inline constexpr float kSwitchMotorHighHz = 320.0f;
// Both actuators; level 0 or strength 0 is the neutral frame.
[[nodiscard]] std::array<std::uint8_t, 8> switch_pro_motor_rumble(std::uint8_t level, std::uint8_t strength) noexcept;
// Output report 0x10 (rumble only): id, packet number, left 4, right 4.
// Returns the meaningful length (pad the rest of `out` with zeros).
std::size_t build_switch_pro_rumble_report(std::uint8_t packet_number,
                                           const std::array<std::uint8_t, 8>& rumble,
                                           std::span<std::uint8_t> out) noexcept;

// Switch 2 Pro Controller: USB HID output report 0x02, 64 bytes.
inline constexpr std::uint8_t kSwitch2RumbleReport = 0x02;
inline constexpr std::size_t kSwitch2RumbleReportBytes = 64;
inline constexpr std::uint16_t kSwitch2HighFrequency = 0x187;
inline constexpr std::uint16_t kSwitch2LowFrequency = 0x112;
// Amplitudes above this are not sent (SDL's limit for the actuators).
inline constexpr std::uint16_t kSwitch2MaxAmplitude = 29000;
// 5 bytes: 10-bit frequencies and amplitudes packed as SDL_hidapi_switch2.c.
[[nodiscard]] std::array<std::uint8_t, 5> encode_switch2_hd_rumble(
    std::uint16_t high_frequency, std::uint16_t high_amplitude,
    std::uint16_t low_frequency, std::uint16_t low_amplitude) noexcept;
// Report 0x02: sequence nibble 0x5n at 1 and 0x11, the motor at 2..6 and
// 0x12..0x16, amplitudes as switch_pro_motor_rumble scaled to the limit.
std::size_t build_switch2_rumble_report(std::uint8_t sequence, std::uint8_t level, std::uint8_t strength,
                                        std::span<std::uint8_t> out) noexcept;

struct XInputMotors {
    std::uint16_t left = 0;   // low-frequency (heavy) motor
    std::uint16_t right = 0;  // high-frequency (light) motor
};
[[nodiscard]] XInputMotors xinput_motor_speeds(std::uint8_t level, std::uint8_t strength) noexcept;

// The physical motor runs only while the motor level is above zero, the game has published
// its state within kStallUs (a stalled game never leaves a motor running), the game window
// has focus, no pause or HOME menu is open and vibration is on (strength not zero). A closed
// gate stops the motor at once.
inline constexpr std::uint64_t kStallUs = 100'000;
struct GateInputs {
    bool game_motor_on = false;
    std::uint64_t now_us = 0;
    std::uint64_t published_us = 0;  // last publication of the game's state
    bool focused = false;
    bool paused = false;
    std::uint8_t strength = 0;
};
[[nodiscard]] bool motor_should_run(const GateInputs& inputs) noexcept;

}  // namespace galaxy::input::rumble
