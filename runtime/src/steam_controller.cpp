#include "galaxy/steam_controller.h"

#include <algorithm>

namespace galaxy::input::steam {

namespace {

// Settings numbers of the controller's configuration table (controller_constants.h).
constexpr std::uint8_t kSettingLizardMode = 9;
constexpr std::uint8_t kSettingImuMode = 48;
constexpr std::uint8_t kSettingSteamWatchdog = 71;
constexpr std::uint16_t kImuSendRawAccel = 0x0008;
constexpr std::uint16_t kImuSendRawGyro = 0x0010;
constexpr std::uint8_t kFeatureSetSettings = 0x87;
constexpr std::uint8_t kFeatureReportId = 1;
constexpr std::uint8_t kOutputHapticRumble = 0x80;

std::int16_t s16(const std::uint8_t* d) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(d[0] | (d[1] << 8)));
}
std::uint16_t u16(const std::uint8_t* d) noexcept {
    return static_cast<std::uint16_t>(d[0] | (d[1] << 8));
}
std::uint32_t u32(const std::uint8_t* d) noexcept {
    return static_cast<std::uint32_t>(d[0]) | (static_cast<std::uint32_t>(d[1]) << 8) |
           (static_cast<std::uint32_t>(d[2]) << 16) | (static_cast<std::uint32_t>(d[3]) << 24);
}

constexpr float kPi = 3.14159265358979323846f;
constexpr float kGyroRadPerCount = 2000.0f * kPi / 180.0f / 32768.0f;
constexpr float kAccelGPerCount = 2.0f / 32768.0f;

float unit(int value) noexcept {
    return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
}

std::uint32_t unified_buttons(std::uint32_t word) noexcept {
    struct Map {
        std::uint32_t source;
        unsigned bit;
    };
    static constexpr Map kMap[] = {
        {button::kA, pad::kSouthBit}, {button::kB, pad::kEastBit}, {button::kX, pad::kWestBit},
        {button::kY, pad::kNorthBit}, {button::kQuickAccess, pad::kCaptureBit},
        {button::kRightStick, pad::kRightStickBit}, {button::kMenu, pad::kStartBit},
        {button::kRightGrip1, pad::kGripRightBit}, {button::kRightGrip2, pad::kGripRight2Bit},
        {button::kRightBumper, pad::kRightShoulderBit}, {button::kDpadDown, pad::kDpadDownBit},
        {button::kDpadRight, pad::kDpadRightBit}, {button::kDpadLeft, pad::kDpadLeftBit},
        {button::kDpadUp, pad::kDpadUpBit}, {button::kView, pad::kBackBit},
        {button::kLeftStick, pad::kLeftStickBit}, {button::kSteam, pad::kHomeBit},
        {button::kLeftGrip1, pad::kGripLeftBit}, {button::kLeftGrip2, pad::kGripLeft2Bit},
        {button::kLeftBumper, pad::kLeftShoulderBit}, {button::kRightPadClick, pad::kRightPadBit},
        {button::kLeftPadClick, pad::kLeftPadBit},
    };
    std::uint32_t buttons = 0;
    for (const Map& entry : kMap) {
        if ((word & entry.source) != 0u) buttons |= pad::bit(entry.bit);
    }
    return buttons;
}

}  // namespace

bool decode_report(std::span<const std::uint8_t> r, SensorClock& clock, switch_hid::DecodedState& out) noexcept {
    if (r.size() < kStateReportBytes) return false;
    const std::uint8_t id = r[0];
    if (id != kReportStateUsb && id != kReportStateBle && id != kReportStateShortClock) return false;
    out = switch_hid::DecodedState{};
    out.normalized = true;
    out.buttons = unified_buttons(u32(&r[2]));
    out.axes[0] = unit(s16(&r[10]));
    out.axes[1] = unit(s16(&r[12]));
    out.axes[2] = unit(s16(&r[14]));
    out.axes[3] = unit(s16(&r[16]));
    out.axes[4] = std::max(unit(s16(&r[6])), 0.0f);
    out.axes[5] = std::max(unit(s16(&r[8])), 0.0f);

    // The sensors share offsets in all three layouts; only the clock differs.
    std::uint32_t ticks;
    std::uint32_t mask;
    float seconds_per_tick;
    if (id == kReportStateShortClock) {
        ticks = u16(&r[32]);
        mask = 0xffffu;
        seconds_per_tick = 32e-6f;
    } else {
        ticks = u32(&r[30]);
        mask = 0xffffffffu;
        seconds_per_tick = 1e-6f;
    }
    const float dt = clock.primed ? static_cast<float>((ticks - clock.last) & mask) * seconds_per_tick : 0.0f;
    clock.last = ticks;
    clock.primed = true;

    std::array<int, 6> raw{};
    bool any = false;
    for (std::size_t i = 0; i < 6; ++i) {
        raw[i] = s16(&r[34 + i * 2]);
        any |= raw[i] != 0;
    }
    if (!any) return true;
    // The controller's axes (x, y, z) become (x, z, -y) in the common sensor frame.
    switch_hid::ImuSample& sample = out.imu[0];
    sample.accel = {static_cast<float>(raw[0]) * kAccelGPerCount, static_cast<float>(raw[2]) * kAccelGPerCount,
                    -static_cast<float>(raw[1]) * kAccelGPerCount};
    sample.gyro = {static_cast<float>(raw[3]) * kGyroRadPerCount, static_cast<float>(raw[5]) * kGyroRadPerCount,
                   -static_cast<float>(raw[4]) * kGyroRadPerCount};
    out.imu_count = 1;
    out.imu_dt = dt;
    return true;
}

WirelessEvent decode_wireless_event(std::span<const std::uint8_t> r) noexcept {
    if (r.size() < 2u || (r[0] != kReportWirelessStatus && r[0] != kReportWirelessStatusX)) {
        return WirelessEvent::None;
    }
    if (r[1] == 1u) return WirelessEvent::Disconnected;
    if (r[1] == 2u) return WirelessEvent::Connected;
    return WirelessEvent::None;
}

std::size_t build_settings_report(std::span<std::uint8_t> out) noexcept {
    if (out.size() < kFeatureReportBytes) return 0;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    struct Setting {
        std::uint8_t number;
        std::uint16_t value;
    };
    static constexpr Setting kSettings[] = {
        {kSettingLizardMode, 0},
        {kSettingSteamWatchdog, 0},
        {kSettingImuMode, kImuSendRawAccel | kImuSendRawGyro},
    };
    out[0] = kFeatureReportId;
    out[1] = kFeatureSetSettings;
    out[2] = static_cast<std::uint8_t>(3u * std::size(kSettings));
    std::size_t at = 3;
    for (const Setting& setting : kSettings) {
        out[at++] = setting.number;
        out[at++] = static_cast<std::uint8_t>(setting.value & 0xffu);
        out[at++] = static_cast<std::uint8_t>(setting.value >> 8);
    }
    return kFeatureReportBytes;
}

std::size_t build_rumble_report(std::uint16_t left, std::uint16_t right, std::span<std::uint8_t> out) noexcept {
    if (out.size() < kRumbleReportBytes) return 0;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    // The effect type, intensity and both gains stay zero.
    out[0] = kOutputHapticRumble;
    out[4] = static_cast<std::uint8_t>(left & 0xffu);
    out[5] = static_cast<std::uint8_t>(left >> 8);
    out[7] = static_cast<std::uint8_t>(right & 0xffu);
    out[8] = static_cast<std::uint8_t>(right >> 8);
    return kRumbleReportBytes;
}

}  // namespace galaxy::input::steam
