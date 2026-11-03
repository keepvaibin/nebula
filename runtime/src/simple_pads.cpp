#include "galaxy/simple_pads.h"

#include "galaxy/wiimote_hid.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace galaxy::input::simple_pads {

namespace sw = switch_hid;

namespace {

struct Row {
    std::uint16_t vendor;
    std::uint16_t product;
    Layout layout;
    ControlScheme scheme;
    const char* name;
};

constexpr Row kProducts[] = {
    {0x0e6f, 0x0180, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PDP Faceoff Wired"},
    {0x0e6f, 0x0181, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PDP Faceoff Deluxe"},
    {0x0e6f, 0x0184, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PDP Faceoff Wired"},
    {0x0e6f, 0x0185, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PDP Wired Fight Pad"},
    {0x0e6f, 0x0187, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PDP Rock Candy"},
    {0x0e6f, 0x0188, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PDP Afterglow Wired"},
    {0x0f0d, 0x0092, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "HORI Pokken Pad"},
    {0x0f0d, 0x00aa, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "HORI Real Arcade Pro V"},
    {0x0f0d, 0x00c1, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "HORIPAD"},
    {0x0f0d, 0x00f0, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "HORI Taiko Drum"},
    {0x20d6, 0xa711, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PowerA Wired GameCube Style"},
    {0x20d6, 0xa712, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PowerA Fusion Fight Pad"},
    {0x20d6, 0xa713, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PowerA Super Mario Wired"},
    {0x20d6, 0xa714, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PowerA Spectra"},
    {0x20d6, 0xa715, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PowerA Fusion Wireless Arcade"},
    {0x20d6, 0xa716, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PowerA Fusion Pro"},
    {0x20d6, 0xa718, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "PowerA Nano Wired"},
    {0x33dd, 0x0001, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "ZUIKI MasterController"},
    {0x33dd, 0x0002, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "ZUIKI MasterController"},
    {0x33dd, 0x0003, Layout::SwitchInputOnly, ControlScheme::SwitchPro, "ZUIKI MasterController"},
    {0x18d1, 0x9400, Layout::Stadia, ControlScheme::Generic, "Stadia Controller"},
    {0x1949, 0x0419, Layout::LunaUsb, ControlScheme::Generic, "Luna Controller"},
    {0x0171, 0x0419, Layout::LunaBluetooth, ControlScheme::Generic, "Luna Controller"},
    {0x0f0d, 0x01ab, Layout::HoriSteam, ControlScheme::Steam, "HORIPAD for Steam"},
    {0x0f0d, 0x0196, Layout::HoriSteam, ControlScheme::Steam, "HORIPAD for Steam"},
    {0x054c, 0x0268, Layout::Sixaxis, ControlScheme::PlayStation, "DualShock 3"},
};

using namespace pad;

constexpr std::uint32_t kHat[8] = {
    bit(kDpadUpBit),
    bit(kDpadUpBit) | bit(kDpadRightBit),
    bit(kDpadRightBit),
    bit(kDpadDownBit) | bit(kDpadRightBit),
    bit(kDpadDownBit),
    bit(kDpadDownBit) | bit(kDpadLeftBit),
    bit(kDpadLeftBit),
    bit(kDpadUpBit) | bit(kDpadLeftBit),
};

// 0 north, clockwise; anything past north-west is centred.
std::uint32_t hat_buttons(unsigned value) noexcept { return value < 8u ? kHat[value] : 0u; }

std::uint32_t bits_of(std::uint8_t byte, std::span<const std::uint8_t> indices) noexcept {
    std::uint32_t buttons = 0;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if ((byte & (1u << i)) != 0u && indices[i] != 0xffu) buttons |= bit(indices[i]);
    }
    return buttons;
}

float stick(std::uint8_t raw, std::uint8_t centre, bool up_positive) noexcept {
    const float offset = static_cast<float>(raw) - static_cast<float>(centre);
    const float value = offset >= 0.0f ? offset / static_cast<float>(255u - centre) : offset / static_cast<float>(centre);
    return up_positive ? -value : value;
}

void sticks(switch_hid::DecodedState& out, std::uint8_t lx, std::uint8_t ly, std::uint8_t rx, std::uint8_t ry,
            std::uint8_t centre) noexcept {
    out.normalized = true;
    out.axes[0] = stick(lx, centre, false);
    out.axes[1] = stick(ly, centre, true);
    out.axes[2] = stick(rx, centre, false);
    out.axes[3] = stick(ry, centre, true);
}

std::int16_t s16(const std::uint8_t* d) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(d[0] | (d[1] << 8)));
}

constexpr std::uint8_t kNo = 0xff;

bool decode_input_only(std::span<const std::uint8_t> r, sw::DecodedState& out) noexcept {
    if (r.size() < 8u || r[0] != 0x00u) return false;
    static constexpr std::array<std::uint8_t, 6> kFirst{kWestBit, kSouthBit, kEastBit, kNorthBit, kLeftShoulderBit,
                                                        kRightShoulderBit};
    static constexpr std::array<std::uint8_t, 6> kSecond{kBackBit, kStartBit, kLeftStickBit, kRightStickBit, kHomeBit,
                                                         kCaptureBit};
    out.buttons = bits_of(r[1], kFirst) | bits_of(r[2], kSecond) | hat_buttons(r[3]);
    sticks(out, r[4], r[5], r[6], r[7], 128u);
    out.axes[4] = (r[1] & 0x40u) != 0u ? 1.0f : 0.0f;
    out.axes[5] = (r[1] & 0x80u) != 0u ? 1.0f : 0.0f;
    return true;
}

bool decode_stadia(std::span<const std::uint8_t> r, sw::DecodedState& out) noexcept {
    if (r.size() < 10u || r[0] != 0x03u) return false;
    static constexpr std::array<std::uint8_t, 8> kSecond{kCaptureBit, kChatBit, kNo, kNo, kHomeBit, kStartBit, kBackBit,
                                                         kRightStickBit};
    static constexpr std::array<std::uint8_t, 7> kThird{kLeftStickBit, kRightShoulderBit, kLeftShoulderBit, kNorthBit,
                                                        kWestBit, kEastBit, kSouthBit};
    out.buttons = hat_buttons(r[1]) | bits_of(r[2], kSecond) | bits_of(r[3], kThird);
    sticks(out, r[4], r[5], r[6], r[7], 128u);
    out.axes[4] = static_cast<float>(r[8]) / 255.0f;
    out.axes[5] = static_cast<float>(r[9]) / 255.0f;
    return true;
}

bool decode_luna_usb(std::span<const std::uint8_t> r, sw::DecodedState& out) noexcept {
    if (r.size() < 10u) return false;
    static constexpr std::array<std::uint8_t, 8> kFirst{kSouthBit, kEastBit, kWestBit, kNorthBit, kLeftShoulderBit,
                                                        kRightShoulderBit, kBackBit, kStartBit};
    static constexpr std::array<std::uint8_t, 4> kSecond{kHomeBit, kChatBit, kLeftStickBit, kRightStickBit};
    out.buttons = bits_of(r[1], kFirst) | bits_of(r[2], kSecond) | hat_buttons(r[3] & 0x0fu);
    sticks(out, r[4], r[5], r[6], r[7], 0x7fu);
    out.axes[4] = static_cast<float>(r[8]) / 255.0f;
    out.axes[5] = static_cast<float>(r[9]) / 255.0f;
    return true;
}

bool decode_luna_bluetooth(std::span<const std::uint8_t> r, ReportState& state, sw::DecodedState& out) noexcept {
    if (r.empty()) return false;
    if (r[0] == 0x02u && r.size() >= 2u) {
        state.home = (r[1] & 0x01u) != 0u;
        return false;
    }
    if (r[0] != 0x01u || r.size() < 17u) return false;
    static constexpr std::array<std::uint8_t, 8> kFirst{kSouthBit, kEastBit, kNo, kWestBit, kNorthBit, kNo,
                                                        kLeftShoulderBit, kRightShoulderBit};
    static constexpr std::array<std::uint8_t, 7> kSecond{kNo, kNo, kNo, kStartBit, kNo, kLeftStickBit, kRightStickBit};
    static constexpr std::array<std::uint8_t, 2> kThird{kBackBit, kChatBit};
    const unsigned hat = r[13] & 0x0fu;
    out.buttons = bits_of(r[14], kFirst) | bits_of(r[15], kSecond) | bits_of(r[16], kThird) |
                  (hat != 0u ? hat_buttons(hat - 1u) : 0u) | (state.home ? bit(kHomeBit) : 0u);
    sticks(out, r[2], r[4], r[6], r[8], 0x7fu);
    const auto trigger = [&](std::size_t at) {
        const int raw = (r[at] | (r[at + 1] << 8)) & 0x3ff;
        return static_cast<float>(raw) / 1023.0f;
    };
    out.axes[4] = trigger(9);
    out.axes[5] = trigger(11);
    return true;
}

bool decode_hori_steam(std::span<const std::uint8_t> r, sw::DecodedState& out) noexcept {
    if (r.size() < 10u || (r[0] != 0x07u && r[0] != 0x00u)) return false;
    static constexpr std::array<std::uint8_t, 8> kSixth{kNorthBit, kGripLeftBit, kLeftShoulderBit, kRightShoulderBit,
                                                        kNo, kNo, kBackBit, kStartBit};
    static constexpr std::array<std::uint8_t, 8> kSeventh{kHomeBit, kLeftStickBit, kRightStickBit, kGripRightBit,
                                                          kNo, kNo, kGripRight2Bit, kGripLeft2Bit};
    out.buttons = hat_buttons(r[5] & 0x0fu) | ((r[5] & 0x10u) != 0u ? bit(kSouthBit) : 0u) |
                  ((r[5] & 0x20u) != 0u ? bit(kEastBit) : 0u) | ((r[5] & 0x40u) != 0u ? bit(kCaptureBit) : 0u) |
                  ((r[5] & 0x80u) != 0u ? bit(kWestBit) : 0u) | bits_of(r[6], kSixth) | bits_of(r[7], kSeventh);
    sticks(out, r[1], r[2], r[3], r[4], 128u);
    out.axes[4] = static_cast<float>(r[9]) / 255.0f;
    out.axes[5] = static_cast<float>(r[8]) / 255.0f;
    if (r.size() >= 24u) {
        bool any = false;
        for (std::size_t i = 12; i < 24u && !any; ++i) any = r[i] != 0u;
        if (any) {
            constexpr float kRadians = 3.14159265358979323846f / 180.0f;
            constexpr float kGyro = 2048.0f / 32768.0f * kRadians;
            constexpr float kAccel = 8.0f / 32768.0f;
            sw::ImuSample& sample = out.imu[0];
            sample.gyro = {-static_cast<float>(s16(&r[16])) * kGyro, -static_cast<float>(s16(&r[12])) * kGyro,
                           -static_cast<float>(s16(&r[14])) * kGyro};
            sample.accel = {static_cast<float>(s16(&r[22])) * kAccel, -static_cast<float>(s16(&r[20])) * kAccel,
                            static_cast<float>(s16(&r[18])) * kAccel};
            out.imu_count = 1;
        }
    }
    return true;
}

bool decode_sixaxis(std::span<const std::uint8_t> r, sw::DecodedState& out) noexcept {
    if (r.size() < kSixaxisReportBytes || r[0] != 0x01u || r[1] == 0xffu) return false;
    static constexpr std::array<std::uint8_t, 8> kSecond{kBackBit, kLeftStickBit, kRightStickBit, kStartBit,
                                                         kDpadUpBit, kDpadRightBit, kDpadDownBit, kDpadLeftBit};
    static constexpr std::array<std::uint8_t, 8> kThird{kNo, kNo, kLeftShoulderBit, kRightShoulderBit,
                                                        kNorthBit, kEastBit, kSouthBit, kWestBit};
    out.buttons = bits_of(r[2], kSecond) | bits_of(r[3], kThird) | ((r[4] & 0x01u) != 0u ? bit(kHomeBit) : 0u);
    sticks(out, r[6], r[7], r[8], r[9], 128u);
    out.axes[4] = static_cast<float>(r[18]) / 255.0f;
    out.axes[5] = static_cast<float>(r[19]) / 255.0f;
    const auto accel = [&](std::size_t at) {
        return static_cast<float>(((r[at] << 8) | r[at + 1]) - 511) / 113.0f;
    };
    sw::ImuSample& sample = out.imu[0];
    sample.accel = {accel(41), -accel(45), -accel(43)};
    out.imu_count = 1;
    return true;
}

bool decode_wii_u_pro(std::span<const std::uint8_t> r, ReportState& state, sw::DecodedState& out) noexcept {
    if (r.size() < 12u || r[0] != kWiiUProReport) return false;
    const std::uint8_t* e = r.data() + 1;
    std::array<std::uint16_t, 4> raw{};
    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<std::uint16_t>(e[i * 2] | (e[i * 2 + 1] << 8));
    if (!state.centred) {
        state.centre = raw;
        state.centred = true;
    }
    // Held bits read 0.
    static constexpr std::array<std::uint8_t, 8> kFirst{kNo, kRightShoulderBit, kStartBit, kHomeBit, kBackBit,
                                                        kLeftShoulderBit, kDpadDownBit, kDpadRightBit};
    static constexpr std::array<std::uint8_t, 8> kSecond{kDpadUpBit, kDpadLeftBit, kNo, kNorthBit, kEastBit, kWestBit,
                                                         kSouthBit, kNo};
    static constexpr std::array<std::uint8_t, 2> kThird{kRightStickBit, kLeftStickBit};
    out.buttons = bits_of(static_cast<std::uint8_t>(~e[8]), kFirst) | bits_of(static_cast<std::uint8_t>(~e[9]), kSecond) |
                  bits_of(static_cast<std::uint8_t>(~e[10]), kThird);
    out.normalized = true;
    constexpr float kTravel = 1000.0f;
    const auto axis = [&](std::size_t i) {
        return std::clamp((static_cast<float>(raw[i]) - static_cast<float>(state.centre[i])) / kTravel, -1.0f, 1.0f);
    };
    out.axes[0] = axis(0);
    out.axes[2] = axis(1);
    out.axes[1] = axis(2);
    out.axes[3] = axis(3);
    out.axes[4] = (e[9] & 0x80u) == 0u ? 1.0f : 0.0f;
    out.axes[5] = (e[9] & 0x04u) == 0u ? 1.0f : 0.0f;
    return true;
}

}  // namespace

std::optional<Product> find_product(std::uint16_t vendor, std::uint16_t product) noexcept {
    for (const Row& row : kProducts) {
        if (row.vendor == vendor && row.product == product) return Product{row.layout, row.scheme, row.name};
    }
    return std::nullopt;
}

bool decode_report(Layout layout, std::span<const std::uint8_t> report, ReportState& state,
                   sw::DecodedState& out) noexcept {
    out = sw::DecodedState{};
    switch (layout) {
    case Layout::SwitchInputOnly: return decode_input_only(report, out);
    case Layout::Stadia: return decode_stadia(report, out);
    case Layout::LunaUsb: return decode_luna_usb(report, out);
    case Layout::LunaBluetooth: return decode_luna_bluetooth(report, state, out);
    case Layout::HoriSteam: return decode_hori_steam(report, out);
    case Layout::Sixaxis: return decode_sixaxis(report, out);
    case Layout::WiiUPro: return decode_wii_u_pro(report, state, out);
    case Layout::None: break;
    }
    return false;
}

std::size_t build_motor_report(Layout layout, std::uint16_t speed, std::span<std::uint8_t> out) noexcept {
    switch (layout) {
    case Layout::Stadia: {
        if (out.size() < 5u) return 0;
        std::fill(out.begin(), out.end(), std::uint8_t{0});
        out[0] = 0x05;
        out[3] = static_cast<std::uint8_t>(speed);
        out[4] = static_cast<std::uint8_t>(speed >> 8);
        return 5;
    }
    case Layout::LunaBluetooth: {
        if (out.size() < 9u) return 0;
        static constexpr std::array<std::uint8_t, 9> kFrame{0x03, 0x0f, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xeb};
        std::fill(out.begin(), out.end(), std::uint8_t{0});
        std::copy(kFrame.begin(), kFrame.end(), out.begin());
        out[5] = static_cast<std::uint8_t>(static_cast<unsigned>(speed) * 100u / 65535u);
        return 9;
    }
    case Layout::Sixaxis: {
        if (out.size() < kSixaxisReportBytes) return 0;
        static constexpr std::array<std::uint8_t, 35> kEffects{
            0x01, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x27, 0x10, 0x00, 0x32, 0xff, 0x27, 0x10,
            0x00, 0x32, 0xff, 0x27, 0x10, 0x00, 0x32, 0xff, 0x27, 0x10, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00};
        std::fill(out.begin(), out.end(), std::uint8_t{0});
        out[0] = 0x01;
        std::copy(kEffects.begin(), kEffects.end(), out.begin() + 1);
        out[5] = static_cast<std::uint8_t>(speed >> 8);
        return kSixaxisReportBytes;
    }
    case Layout::WiiUPro: return wiimote::build_rumble(speed != 0u, out);
    default: return 0;
    }
}

}  // namespace galaxy::input::simple_pads
