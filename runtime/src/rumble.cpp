#include "galaxy/rumble.h"

#include <algorithm>
#include <cmath>

namespace galaxy::input::rumble {

namespace {

int encoded_frequency(float hz) noexcept {
    const float clamped = std::clamp(std::isfinite(hz) ? hz : 160.0f, 40.875f, 1252.0f);
    return static_cast<int>(std::lround(std::log2(clamped / 10.0f) * 32.0f));
}

// 0..100 (0 = silent); amplitudes at or under 0.12 have no documented code
// and are sent as silent.
int encoded_amplitude(float amplitude) noexcept {
    const float a = std::clamp(std::isfinite(amplitude) ? amplitude : 0.0f, 0.0f, 1.0f);
    if (a > 0.23f) return std::clamp(static_cast<int>(std::lround(std::log2(a * 8.7f) * 32.0f)), 0, 100);
    if (a > 0.12f) return std::clamp(static_cast<int>(std::lround(std::log2(a * 17.0f) * 16.0f)), 0, 100);
    return 0;
}

float strength_fraction(std::uint8_t strength) noexcept {
    const float s = std::clamp<float>(strength, kMinStrength, kMaxStrength);
    return (s - kMinStrength) / static_cast<float>(kMaxStrength - kMinStrength);
}

float level_fraction(std::uint8_t level) noexcept {
    return static_cast<float>(std::min(level, kLevelSteps)) / static_cast<float>(kLevelSteps);
}

}  // namespace

float MotorEnvelope::update(bool on, std::uint64_t now_us) noexcept {
    const float dt = last_us == 0u || now_us < last_us
        ? 0.0f
        : std::min(static_cast<float>(now_us - last_us) * 1e-6f, 1.0f);
    last_us = now_us;
    const float target = on ? 1.0f : 0.0f;
    const float seconds = on ? kMotorRiseSeconds : kMotorFallSeconds;
    level += (target - level) * (1.0f - std::exp(-dt / seconds));
    if (!on && level < kMotorStopLevel) level = 0.0f;
    return level;
}

std::uint8_t quantize_level(float level) noexcept {
    if (!std::isfinite(level) || level <= 0.0f) return 0;
    return static_cast<std::uint8_t>(std::clamp<long>(std::lround(level * kLevelSteps), 1, kLevelSteps));
}

float switch_low_amplitude(std::uint8_t level, std::uint8_t strength) noexcept {
    if (level == 0u || strength == 0u) return 0.0f;
    return level_fraction(level) *
           (kSwitchLowAmplitudeMin + (kSwitchLowAmplitudeMax - kSwitchLowAmplitudeMin) * strength_fraction(strength));
}

std::array<std::uint8_t, 4> encode_switch_hd_rumble(float high_hz, float high_amplitude, float low_hz,
                                                    float low_amplitude) noexcept {
    const int high = std::clamp((encoded_frequency(std::max(high_hz, 81.75f)) - 0x60) * 4, 0, 0x1FC);
    const int low = std::clamp(encoded_frequency(std::min(low_hz, 626.0f)) - 0x40, 0, 0x7F);
    const int high_amp = encoded_amplitude(high_amplitude) * 2;
    const int low_code = encoded_amplitude(low_amplitude);
    const int low_amp = ((low_code & 1) != 0 ? 0x8000 : 0) | (low_code / 2 + 64);
    return {static_cast<std::uint8_t>(high & 0xFF),
            static_cast<std::uint8_t>(high_amp + ((high >> 8) & 0xFF)),
            static_cast<std::uint8_t>(low + ((low_amp >> 8) & 0xFF)),
            static_cast<std::uint8_t>(low_amp & 0xFF)};
}

std::array<std::uint8_t, 8> switch_pro_motor_rumble(std::uint8_t level, std::uint8_t strength) noexcept {
    std::array<std::uint8_t, 8> out{};
    const float low = switch_low_amplitude(level, strength);
    const std::array<std::uint8_t, 4> side = low > 0.0f
        ? encode_switch_hd_rumble(kSwitchMotorHighHz, low * kSwitchHighShare, kSwitchMotorLowHz, low)
        : kSwitchNeutralRumble;
    std::copy(side.begin(), side.end(), out.begin());
    std::copy(side.begin(), side.end(), out.begin() + 4);
    return out;
}

std::size_t build_switch_pro_rumble_report(std::uint8_t packet_number, const std::array<std::uint8_t, 8>& rumble,
                                           std::span<std::uint8_t> out) noexcept {
    if (out.size() < 10u) return 0;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    out[0] = 0x10;
    out[1] = static_cast<std::uint8_t>(packet_number & 0x0Fu);
    std::copy(rumble.begin(), rumble.end(), out.begin() + 2);
    return 10;
}

std::array<std::uint8_t, 5> encode_switch2_hd_rumble(std::uint16_t high_frequency, std::uint16_t high_amplitude,
                                                     std::uint16_t low_frequency, std::uint16_t low_amplitude) noexcept {
    return {static_cast<std::uint8_t>(high_frequency & 0xFFu),
            static_cast<std::uint8_t>(((high_amplitude >> 4) & 0xFCu) | ((high_frequency >> 8) & 0x03u)),
            static_cast<std::uint8_t>((high_amplitude >> 12) | (low_frequency << 4)),
            static_cast<std::uint8_t>((low_amplitude & 0xC0u) | ((low_frequency >> 4) & 0x3Fu)),
            static_cast<std::uint8_t>(low_amplitude >> 8)};
}

std::size_t build_switch2_rumble_report(std::uint8_t sequence, std::uint8_t level, std::uint8_t strength,
                                        std::span<std::uint8_t> out) noexcept {
    if (out.size() < kSwitch2RumbleReportBytes) return 0;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    const float low_fraction = switch_low_amplitude(level, strength);
    const auto low = static_cast<std::uint16_t>(std::lround(kSwitch2MaxAmplitude * low_fraction));
    const auto high = static_cast<std::uint16_t>(std::lround(kSwitch2MaxAmplitude * low_fraction * kSwitchHighShare));
    out[0] = kSwitch2RumbleReport;
    out[1] = static_cast<std::uint8_t>(0x50u | (sequence & 0x0Fu));
    const auto motor = encode_switch2_hd_rumble(kSwitch2HighFrequency, high, kSwitch2LowFrequency, low);
    std::copy(motor.begin(), motor.end(), out.begin() + 2);
    std::copy(out.begin() + 1, out.begin() + 7, out.begin() + 0x11);
    return kSwitch2RumbleReportBytes;
}

XInputMotors xinput_motor_speeds(std::uint8_t level, std::uint8_t strength) noexcept {
    if (level == 0u || strength == 0u) return {};
    const float speed = level_fraction(level) * (kXInputSpeedMin + (1.0f - kXInputSpeedMin) * strength_fraction(strength));
    return {0u, static_cast<std::uint16_t>(std::lround(65535.0f * std::clamp(speed, 0.0f, 1.0f)))};
}

bool motor_should_run(const GateInputs& in) noexcept {
    return in.game_motor_on && in.strength != 0u && in.focused && !in.paused &&
           in.now_us >= in.published_us && in.now_us - in.published_us <= kStallUs;
}

}  // namespace galaxy::input::rumble
