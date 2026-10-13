#pragma once

// Controller vibration that mirrors the virtual Wii Remote's motor. The game
// drives the motor itself (WPadRumble::updateRumble 0x803AC3E8 ->
// WPADControlMotor 0x804D9000); its on/off bit reaches the emulated Wii Remote
// in every output report. The bit is copied, as it is, to the controller that
// drives the remote: no host-made patterns.
//
// Encodings: Switch Pro Controller HD rumble per dekuNukem
// Nintendo_Switch_Reverse_Engineering (rumble_data_table.md); Pro Controller 2
// packet layout per SDL's zlib-licensed SDL_hidapi_switch2.c. Independent
// implementations.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace galaxy::input::rumble {

// tune.<scheme>.rumble: vibration strength in percent.
inline constexpr std::array<std::uint8_t, 5> kStrengthSteps{0, 25, 50, 75, 100};
inline constexpr std::uint8_t kDefaultStrength = 100;
// The nearest step (ties go down); not a number: the default.
[[nodiscard]] inline std::uint8_t snap_strength(float percent) noexcept {
    if (!std::isfinite(percent)) return kDefaultStrength;
    std::uint8_t best = kStrengthSteps[0];
    for (const std::uint8_t step : kStrengthSteps) {
        if (std::fabs(percent - step) < std::fabs(percent - best)) best = step;
    }
    return best;
}

// ---- Switch Pro Controller ---------------------------------------------------
// One actuator's 4 bytes: high band (frequency + amplitude) and low band.
// Frequency in Hz (40.875-1252), amplitude 0..1 (values above 1 clamp to 1).
[[nodiscard]] std::array<std::uint8_t, 4> encode_switch_hd_rumble(
    float high_hz, float high_amplitude, float low_hz, float low_amplitude) noexcept;
// 00 01 40 40: 320 Hz / 160 Hz at zero amplitude.
inline constexpr std::array<std::uint8_t, 4> kSwitchNeutralRumble{0x00, 0x01, 0x40, 0x40};
// The Wii Remote motor on both actuators: a 160 Hz low band (0.8 at full
// strength) under a 320 Hz high band (0.6), scaled by `strength` percent;
// off is the neutral frame.
inline constexpr float kSwitchMotorLowHz = 160.0f;
inline constexpr float kSwitchMotorHighHz = 320.0f;
inline constexpr float kSwitchMotorLowAmplitude = 0.8f;
inline constexpr float kSwitchMotorHighAmplitude = 0.6f;
[[nodiscard]] std::array<std::uint8_t, 8> switch_pro_motor_rumble(bool on, std::uint8_t strength) noexcept;
// Output report 0x10 (rumble only): id, packet number, left 4, right 4.
// Returns the meaningful length (pad the rest of `out` with zeros).
std::size_t build_switch_pro_rumble_report(std::uint8_t packet_number,
                                           const std::array<std::uint8_t, 8>& rumble,
                                           std::span<std::uint8_t> out) noexcept;

// ---- Switch 2 Pro Controller (USB HID output report 0x02, 64 bytes) --------
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
// 0x12..0x16. Low band 0.8, high band 0.6 of the limit at full strength.
std::size_t build_switch2_rumble_report(std::uint8_t sequence, bool on, std::uint8_t strength,
                                        std::span<std::uint8_t> out) noexcept;

// ---- Xbox (XInput) -------------------------------------------------------------
struct XInputMotors {
    std::uint16_t left = 0;   // low-frequency (heavy) motor
    std::uint16_t right = 0;  // high-frequency (light) motor
};
// The Wii Remote's small motor: the light motor at full strength, the heavy
// one at half.
[[nodiscard]] XInputMotors xinput_motor_speeds(bool on, std::uint8_t strength) noexcept;

// ---- Safety gate ----------------------------------------------------------------
// The physical motor runs only while the game's motor bit is on, the game has
// published its state within kStallUs (a stalled game never leaves a motor
// running), the game window has focus, no pause or HOME menu is open and the
// strength is not zero.
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
