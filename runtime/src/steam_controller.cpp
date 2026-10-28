#include "galaxy/steam_controller.h"

#include <algorithm>
#include <optional>

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

// The Deck and the 2015 controller (controller_structs.h, controller_constants.h).
constexpr std::uint8_t kMessageClassicState = 1;
constexpr std::uint8_t kMessageWireless = 3;
constexpr std::uint8_t kMessageDeckState = 9;
constexpr std::uint8_t kFeatureClearMappings = 0x81;
constexpr std::uint8_t kFeatureHapticPulse = 0x8f;
constexpr std::uint8_t kFeatureRumble = 0xeb;
constexpr std::uint8_t kSettingLeftPadMode = 7;
constexpr std::uint8_t kSettingRightPadMode = 8;
constexpr std::uint8_t kSettingSmoothMouse = 24;
constexpr std::uint8_t kSettingWirelessPacketVersion = 49;
constexpr std::uint8_t kSettingLeftPadClickPressure = 52;
constexpr std::uint8_t kSettingRightPadClickPressure = 53;
constexpr std::uint16_t kPadModeNone = 7;
constexpr std::size_t kDeckPayloadBytes = 56;
constexpr std::size_t kClassicPayloadBytes = 44;

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

// Valve's message `type` in a report as read (its report number 0 first, or not): where its payload starts, if it
// holds `bytes` of it.
std::optional<std::size_t> valve_payload(std::span<const std::uint8_t> r, std::uint8_t type, std::size_t bytes) noexcept {
    std::size_t header = 0;
    if (r.size() >= 3u && r[0] == 0u && r[1] == 1u && r[2] == 0u) {
        header = 1;
    } else if (r.size() < 2u || r[0] != 1u || r[1] != 0u) {
        return std::nullopt;
    }
    if (r.size() < header + 4u + bytes || r[header + 2u] != type) return std::nullopt;
    return header + 4u;
}

struct ButtonMap {
    std::uint32_t source;
    unsigned bit;
};

template <std::size_t N>
std::uint32_t map_buttons(std::uint32_t word, const ButtonMap (&map)[N]) noexcept {
    std::uint32_t buttons = 0;
    for (const ButtonMap& entry : map) {
        if ((word & entry.source) != 0u) buttons |= pad::bit(entry.bit);
    }
    return buttons;
}

// The controller's axes (x, y, z) become (x, z, -y) in the common sensor frame; `gyro_y_sign` -1 as the Deck's,
// +1 as the 2015 controller's gyro (SDL's drivers).
void put_sensors(const std::uint8_t* accel, const std::uint8_t* gyro, float gyro_y_sign,
                 switch_hid::DecodedState& out) noexcept {
    const int ax = s16(accel), ay = s16(accel + 2), az = s16(accel + 4);
    const int gx = s16(gyro), gy = s16(gyro + 2), gz = s16(gyro + 4);
    if (ax == 0 && ay == 0 && az == 0 && gx == 0 && gy == 0 && gz == 0) return;
    switch_hid::ImuSample& sample = out.imu[0];
    sample.accel = {static_cast<float>(ax) * kAccelGPerCount, static_cast<float>(az) * kAccelGPerCount,
                    -static_cast<float>(ay) * kAccelGPerCount};
    sample.gyro = {static_cast<float>(gx) * kGyroRadPerCount, static_cast<float>(gz) * kGyroRadPerCount,
                   gyro_y_sign * static_cast<float>(gy) * kGyroRadPerCount};
    out.imu_count = 1;
}

// A trackpad's finger at `x`, `y` (Valve's signed coordinates, the centre 0, y up), as the decoded touch.
void put_touch(bool down, int x, int y, switch_hid::DecodedState& out) noexcept {
    out.touch_surface = true;
    out.touch_down = down;
    if (down) {
        out.touch_x = std::clamp(static_cast<float>(x) / 65536.0f + 0.5f, 0.0f, 1.0f);
        out.touch_y = std::clamp(-static_cast<float>(y) / 65536.0f + 0.5f, 0.0f, 1.0f);
    }
}

struct ValveSetting {
    std::uint8_t number;
    std::uint16_t value;
};

std::size_t valve_settings(std::span<std::uint8_t> out, std::span<const ValveSetting> settings) noexcept {
    if (out.size() < kValveFeatureBytes || settings.size() * 3u + 3u > kValveFeatureBytes) return 0;
    std::fill(out.begin(), out.begin() + kValveFeatureBytes, std::uint8_t{0});
    out[1] = kFeatureSetSettings;
    out[2] = static_cast<std::uint8_t>(3u * settings.size());
    std::size_t at = 3;
    for (const auto& [number, value] : settings) {
        out[at++] = number;
        out[at++] = static_cast<std::uint8_t>(value & 0xffu);
        out[at++] = static_cast<std::uint8_t>(value >> 8);
    }
    return kValveFeatureBytes;
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
    // The right trackpad: two bytes later where the short clock's layout has a trackpad timestamp first.
    const std::size_t pad = id == kReportStateShortClock ? 26u : 24u;
    put_touch((u32(&r[2]) & button::kRightPadTouch) != 0u, s16(&r[pad]), s16(&r[pad + 2u]), out);

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

bool listed_device(std::string_view list, std::uint16_t vendor, std::uint16_t product) noexcept {
    // One hexadecimal number (an optional 0x) from `at`, or -1.
    const auto number = [&list](std::size_t& at) -> long {
        while (at < list.size() && list[at] == ' ') ++at;
        if (at + 1u < list.size() && list[at] == '0' && (list[at + 1u] == 'x' || list[at + 1u] == 'X')) at += 2u;
        long value = 0;
        std::size_t digits = 0;
        for (; at < list.size() && digits < 4u; ++at, ++digits) {
            const char c = list[at];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) break;
            value = value * 16 + d;
        }
        return digits != 0u ? value : -1;
    };
    std::size_t at = 0;
    while (at < list.size()) {
        const long v = number(at);
        const bool slash = at < list.size() && list[at] == '/';
        if (slash) ++at;
        const long p = slash ? number(at) : -1;
        if (v == vendor && p == product) return true;
        while (at < list.size() && list[at] != ',') ++at;
        if (at < list.size()) ++at;
    }
    return false;
}

bool decode_deck_report(std::span<const std::uint8_t> r, switch_hid::DecodedState& out) noexcept {
    const std::optional<std::size_t> at = valve_payload(r, kMessageDeckState, kDeckPayloadBytes);
    if (!at) return false;
    const std::uint8_t* p = r.data() + *at;
    static constexpr ButtonMap kLow[] = {
        {0x00000004, pad::kRightShoulderBit}, {0x00000008, pad::kLeftShoulderBit}, {0x00000010, pad::kNorthBit},
        {0x00000020, pad::kEastBit},          {0x00000040, pad::kWestBit},         {0x00000080, pad::kSouthBit},
        {0x00000100, pad::kDpadUpBit},        {0x00000200, pad::kDpadRightBit},    {0x00000400, pad::kDpadLeftBit},
        {0x00000800, pad::kDpadDownBit},      {0x00001000, pad::kBackBit},         {0x00002000, pad::kHomeBit},
        {0x00004000, pad::kStartBit},         {0x00008000, pad::kGripLeft2Bit},    {0x00010000, pad::kGripRight2Bit},
        {0x00020000, pad::kLeftPadBit},       {0x00040000, pad::kRightPadBit},     {0x00400000, pad::kLeftStickBit},
        {0x04000000, pad::kRightStickBit},
    };
    static constexpr ButtonMap kHigh[] = {
        {0x00000200, pad::kGripLeftBit}, {0x00000400, pad::kGripRightBit}, {0x00040000, pad::kCaptureBit},
    };
    out = switch_hid::DecodedState{};
    out.normalized = true;
    out.buttons = map_buttons(u32(p + 4), kLow) | map_buttons(u32(p + 8), kHigh);
    out.axes[0] = unit(s16(p + 44));
    out.axes[1] = unit(s16(p + 46));
    out.axes[2] = unit(s16(p + 48));
    out.axes[3] = unit(s16(p + 50));
    out.axes[4] = std::clamp(static_cast<float>(u16(p + 40)) / 32767.0f, 0.0f, 1.0f);
    out.axes[5] = std::clamp(static_cast<float>(u16(p + 42)) / 32767.0f, 0.0f, 1.0f);
    constexpr std::uint32_t kRightPadFinger = 0x00100000;
    put_touch((u32(p + 4) & kRightPadFinger) != 0u, s16(p + 16), s16(p + 18), out);
    put_sensors(p + 20, p + 26, -1.0f, out);
    return true;
}

bool decode_classic_report(std::span<const std::uint8_t> r, ClassicState& state, switch_hid::DecodedState& out) noexcept {
    const std::optional<std::size_t> at = valve_payload(r, kMessageClassicState, kClassicPayloadBytes);
    if (!at) return false;
    const std::uint8_t* p = r.data() + *at;
    const std::uint32_t packet = u32(p);
    if (state.primed && packet == state.packet) return false;
    state.packet = packet;
    state.primed = true;
    constexpr std::uint32_t kLeftPadClick = 0x00020000;
    constexpr std::uint32_t kLeftPadFinger = 0x00080000;
    constexpr std::uint32_t kRightPadFinger = 0x00100000;
    constexpr std::uint32_t kStickClick = 0x00400000;
    constexpr std::uint32_t kPadAndStick = 0x00800000;
    // The low three bytes are buttons; the fourth and fifth, the triggers.
    std::uint32_t word = u32(p + 4) & 0x00ffffffu;
    const std::array<std::int16_t, 2> shared = {static_cast<std::int16_t>(s16(p + 12)), static_cast<std::int16_t>(s16(p + 14))};
    if ((word & kLeftPadFinger) != 0u) {
        // The axes are the pad's; the stick keeps its last values while both are in use, else it is centred.
        if ((word & kPadAndStick) == 0u) state.stick = {};
    } else {
        state.stick = shared;
        // An older firmware clicks the stick as the pad while the pad is not in use.
        if ((word & kPadAndStick) == 0u && (word & kLeftPadClick) != 0u) word = (word & ~kLeftPadClick) | kStickClick;
    }
    static constexpr ButtonMap kMap[] = {
        {0x00000004, pad::kRightShoulderBit}, {0x00000008, pad::kLeftShoulderBit}, {0x00000010, pad::kNorthBit},
        {0x00000020, pad::kEastBit},          {0x00000040, pad::kWestBit},         {0x00000080, pad::kSouthBit},
        {0x00000100, pad::kDpadUpBit},        {0x00000200, pad::kDpadRightBit},    {0x00000400, pad::kDpadLeftBit},
        {0x00000800, pad::kDpadDownBit},      {0x00001000, pad::kBackBit},         {0x00002000, pad::kHomeBit},
        {0x00004000, pad::kStartBit},         {0x00008000, pad::kGripLeftBit},     {0x00010000, pad::kGripRightBit},
        {kLeftPadClick, pad::kLeftPadBit},    {0x00040000, pad::kRightStickBit},   {kStickClick, pad::kLeftStickBit},
    };
    out = switch_hid::DecodedState{};
    out.normalized = true;
    out.buttons = map_buttons(word, kMap);
    out.axes[0] = unit(state.stick[0]);
    out.axes[1] = unit(state.stick[1]);
    if ((word & kRightPadFinger) != 0u) {
        // The right pad sits turned: 15 degrees back to level.
        constexpr float kCos = 0.96592583f;
        constexpr float kSin = 0.25881905f;
        const float x = static_cast<float>(s16(p + 16));
        const float y = static_cast<float>(s16(p + 18));
        out.axes[2] = unit(static_cast<int>(kCos * x - kSin * y));
        out.axes[3] = unit(static_cast<int>(kSin * x + kCos * y));
    }
    // A trigger's byte spread over its 0..26000 travel.
    const auto trigger = [](std::uint8_t value) {
        return std::clamp(static_cast<float>((value << 7) | value) / 26000.0f, 0.0f, 1.0f);
    };
    out.axes[4] = trigger(p[7]);
    out.axes[5] = trigger(p[8]);
    put_touch((word & kRightPadFinger) != 0u, s16(p + 16), s16(p + 18), out);
    put_sensors(p + 24, p + 30, 1.0f, out);
    return true;
}

WirelessEvent decode_classic_wireless_event(std::span<const std::uint8_t> r) noexcept {
    const std::optional<std::size_t> at = valve_payload(r, kMessageWireless, 1);
    if (!at || r[*at - 1u] < 1u) return WirelessEvent::None;
    if (r[*at] == 1u) return WirelessEvent::Disconnected;
    if (r[*at] == 2u || r[*at] == 3u) return WirelessEvent::Connected;  // connected, or newly paired
    return WirelessEvent::None;
}

std::size_t build_clear_mappings_report(std::span<std::uint8_t> out) noexcept {
    if (out.size() < kValveFeatureBytes) return 0;
    std::fill(out.begin(), out.begin() + kValveFeatureBytes, std::uint8_t{0});
    out[1] = kFeatureClearMappings;
    return kValveFeatureBytes;
}

std::size_t build_valve_settings_report(Model model, std::span<std::uint8_t> out) noexcept {
    using Setting = ValveSetting;
    static constexpr Setting kDeck[] = {
        {kSettingSmoothMouse, 0},
        {kSettingLeftPadMode, kPadModeNone},
        {kSettingRightPadMode, kPadModeNone},
        {kSettingLeftPadClickPressure, 0xffff},
        {kSettingRightPadClickPressure, 0xffff},
    };
    static constexpr Setting kClassic[] = {
        {kSettingWirelessPacketVersion, 2},
        {kSettingLeftPadMode, kPadModeNone},
        {kSettingRightPadMode, kPadModeNone},
        {kSettingSmoothMouse, 0},
        {kSettingImuMode, kImuSendRawAccel | kImuSendRawGyro},
    };
    if (model == Model::Deck) return valve_settings(out, kDeck);
    if (model == Model::Classic) return valve_settings(out, kClassic);
    return 0;
}

std::size_t build_deck_watchdog_report(std::span<std::uint8_t> out) noexcept {
    static constexpr ValveSetting kWatchdog[] = {{kSettingRightPadMode, kPadModeNone}};
    return valve_settings(out, kWatchdog);
}

std::size_t build_deck_rumble_report(std::uint16_t left, std::uint16_t right, std::span<std::uint8_t> out) noexcept {
    if (out.size() < kValveFeatureBytes) return 0;
    std::fill(out.begin(), out.begin() + kValveFeatureBytes, std::uint8_t{0});
    // Rumble type 0, the system's intensity, both speeds, the left motor's gain 2 (SDL's).
    out[1] = kFeatureRumble;
    out[6] = static_cast<std::uint8_t>(left & 0xffu);
    out[7] = static_cast<std::uint8_t>(left >> 8);
    out[8] = static_cast<std::uint8_t>(right & 0xffu);
    out[9] = static_cast<std::uint8_t>(right >> 8);
    out[10] = 2;
    return kValveFeatureBytes;
}

std::size_t build_classic_haptic_report(std::uint8_t pad, std::uint16_t on_us, std::span<std::uint8_t> out) noexcept {
    if (out.size() < kValveFeatureBytes || on_us == 0u || on_us >= kClassicPulsePeriodUs) return 0;
    std::fill(out.begin(), out.begin() + kValveFeatureBytes, std::uint8_t{0});
    const std::uint16_t off_us = static_cast<std::uint16_t>(kClassicPulsePeriodUs - on_us);
    const std::uint16_t count = static_cast<std::uint16_t>(kClassicPulseUs / kClassicPulsePeriodUs);
    // MsgFireHapticPulse: the pad, the on and off times, the count, a gain of 0 dB, priority 0.
    out[1] = kFeatureHapticPulse;
    out[2] = 10;
    out[3] = pad;
    out[4] = static_cast<std::uint8_t>(on_us & 0xffu);
    out[5] = static_cast<std::uint8_t>(on_us >> 8);
    out[6] = static_cast<std::uint8_t>(off_us & 0xffu);
    out[7] = static_cast<std::uint8_t>(off_us >> 8);
    out[8] = static_cast<std::uint8_t>(count & 0xffu);
    out[9] = static_cast<std::uint8_t>(count >> 8);
    return kValveFeatureBytes;
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
