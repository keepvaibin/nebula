#include "galaxy/switch_controller.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace galaxy::input::switch_hid {

namespace {

constexpr float kPi = 3.14159265358979323846f;

int u12_low(const std::uint8_t* d) noexcept {
    return static_cast<int>(d[0]) | (static_cast<int>(d[1] & 0x0fu) << 8);
}
int u12_high(const std::uint8_t* d) noexcept {
    return static_cast<int>(d[0] >> 4) | (static_cast<int>(d[1]) << 4);
}
std::int16_t s16(const std::uint8_t* d) noexcept {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(d[0] | (d[1] << 8)));
}

void fill_invalid(StickAxisCal& axis) noexcept {
    if (axis.center == 0xfff || axis.center == 0) axis.center = 2048;
    if (axis.below == 0xfff || axis.below == 0) axis.below = static_cast<int>(axis.center * 0.7f);
    if (axis.above == 0xfff || axis.above == 0) axis.above = static_cast<int>(axis.center * 0.7f);
}

// Physical reach is below the calibrated extent; full output arrives at 75%.
constexpr float kSwitchStickReach = 0.75f;

float apply_deadzone(float value, float deadzone) noexcept {
    const float magnitude = std::fabs(value);
    if (magnitude <= deadzone) return 0.0f;
    const float scaled = std::min((magnitude - deadzone) / std::max(1.0f - deadzone, 1e-3f), 1.0f);
    return value < 0.0f ? -scaled : scaled;
}

constexpr std::array<std::uint8_t, 8> kNeutralRumble{0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};

Switch2Command command(std::initializer_list<std::uint8_t> bytes) noexcept {
    Switch2Command out{};
    for (std::uint8_t byte : bytes) {
        if (out.size < out.bytes.size()) out.bytes[out.size++] = byte;
    }
    return out;
}

}  // namespace

ImuCal::ImuCal() noexcept {
    const float gyro = kPi / 180.0f / 14.2842f;
    gyro_scale = {gyro, gyro, gyro};
}

StickCal parse_left_stick_cal(std::span<const std::uint8_t, 9> d) noexcept {
    StickCal cal{};
    cal.x.above = u12_low(&d[0]);
    cal.y.above = u12_high(&d[1]);
    cal.x.center = u12_low(&d[3]);
    cal.y.center = u12_high(&d[4]);
    cal.x.below = u12_low(&d[6]);
    cal.y.below = u12_high(&d[7]);
    fill_invalid(cal.x);
    fill_invalid(cal.y);
    return cal;
}

StickCal parse_right_stick_cal(std::span<const std::uint8_t, 9> d) noexcept {
    StickCal cal{};
    cal.x.center = u12_low(&d[0]);
    cal.y.center = u12_high(&d[1]);
    cal.x.below = u12_low(&d[3]);
    cal.y.below = u12_high(&d[4]);
    cal.x.above = u12_low(&d[6]);
    cal.y.above = u12_high(&d[7]);
    fill_invalid(cal.x);
    fill_invalid(cal.y);
    return cal;
}

StickCal parse_switch2_stick_cal(std::span<const std::uint8_t, 9> d) noexcept {
    StickCal cal{};
    cal.x.center = u12_low(&d[0]);
    cal.y.center = u12_high(&d[1]);
    cal.x.above = u12_low(&d[3]);
    cal.y.above = u12_high(&d[4]);
    cal.x.below = u12_low(&d[6]);
    cal.y.below = u12_high(&d[7]);
    // Switch 2 extents are the physical reach; undo the Switch Pro reach factor.
    for (StickAxisCal* axis : {&cal.x, &cal.y}) {
        fill_invalid(*axis);
        axis->above = static_cast<int>(std::lround(axis->above / kSwitchStickReach));
        axis->below = static_cast<int>(std::lround(axis->below / kSwitchStickReach));
    }
    return cal;
}

float normalize_stick(int raw, const StickAxisCal& cal) noexcept {
    const int delta = raw - cal.center;
    const int extent = delta < 0 ? cal.below : cal.above;
    if (extent <= 0) return 0.0f;
    const float value = static_cast<float>(delta) / (static_cast<float>(extent) * kSwitchStickReach);
    return std::clamp(value, -1.0f, 1.0f);
}

ImuCal parse_imu_cal(std::span<const std::uint8_t, 24> d) noexcept {
    ImuCal cal{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const int accel_origin = s16(&d[axis * 2]);
        const int accel_coeff = s16(&d[6 + axis * 2]);
        const int gyro_origin = s16(&d[12 + axis * 2]);
        const int gyro_coeff = s16(&d[18 + axis * 2]);
        if (std::abs(accel_coeff - accel_origin) >= 1000) {
            cal.accel_scale[axis] = 4.0f / static_cast<float>(accel_coeff - accel_origin);
        }
        if (std::abs(gyro_coeff - gyro_origin) >= 1000) {
            cal.gyro_scale[axis] =
                936.0f / static_cast<float>(gyro_coeff - gyro_origin) * kPi / 180.0f;
            cal.gyro_offset[axis] = static_cast<std::int16_t>(gyro_origin);
        }
    }
    return cal;
}

bool decode_pro_report(std::span<const std::uint8_t> r, const ImuCal& cal, DecodedState& out) noexcept {
    if (r.empty()) return false;
    out = DecodedState{};
    using namespace pad;
    if (r[0] == kReportSimpleState) {
        if (r.size() < 12u) return false;
        const std::uint8_t b0 = r[1];
        const std::uint8_t b1 = r[2];
        std::uint32_t buttons = 0;
        if (b0 & 0x01u) buttons |= bit(kSouthBit);   // B
        if (b0 & 0x02u) buttons |= bit(kEastBit);    // A
        if (b0 & 0x04u) buttons |= bit(kWestBit);    // Y
        if (b0 & 0x08u) buttons |= bit(kNorthBit);   // X
        if (b0 & 0x10u) buttons |= bit(kLeftShoulderBit);
        if (b0 & 0x20u) buttons |= bit(kRightShoulderBit);
        if (b1 & 0x01u) buttons |= bit(kBackBit);
        if (b1 & 0x02u) buttons |= bit(kStartBit);
        if (b1 & 0x04u) buttons |= bit(kLeftStickBit);
        if (b1 & 0x08u) buttons |= bit(kRightStickBit);
        if (b1 & 0x10u) buttons |= bit(kHomeBit);
        if (b1 & 0x20u) buttons |= bit(kCaptureBit);
        static constexpr std::uint32_t kHat[8] = {
            bit(kDpadUpBit), bit(kDpadUpBit) | bit(kDpadRightBit), bit(kDpadRightBit),
            bit(kDpadDownBit) | bit(kDpadRightBit), bit(kDpadDownBit),
            bit(kDpadDownBit) | bit(kDpadLeftBit), bit(kDpadLeftBit),
            bit(kDpadUpBit) | bit(kDpadLeftBit)};
        if (r[3] < 8u) buttons |= kHat[r[3]];
        out.buttons = buttons;
        out.left_trigger = (b0 & 0x40u) != 0u;
        out.right_trigger = (b0 & 0x80u) != 0u;
        for (std::size_t i = 0; i < 4; ++i) {
            out.stick_raw[i] = static_cast<int>(r[4 + i * 2] | (r[5 + i * 2] << 8));
        }
        out.simple = true;
        return true;
    }
    if (r[0] != kReportFullState && r[0] != kReportSubcommandReply) return false;
    if (r.size() < 13u) return false;
    const std::uint8_t right = r[3];
    const std::uint8_t shared = r[4];
    const std::uint8_t left = r[5];
    std::uint32_t buttons = 0;
    if (right & 0x01u) buttons |= bit(kWestBit);    // Y
    if (right & 0x02u) buttons |= bit(kNorthBit);   // X
    if (right & 0x04u) buttons |= bit(kSouthBit);   // B
    if (right & 0x08u) buttons |= bit(kEastBit);    // A
    if (right & 0x40u) buttons |= bit(kRightShoulderBit);
    if (shared & 0x01u) buttons |= bit(kBackBit);
    if (shared & 0x02u) buttons |= bit(kStartBit);
    if (shared & 0x04u) buttons |= bit(kRightStickBit);
    if (shared & 0x08u) buttons |= bit(kLeftStickBit);
    if (shared & 0x10u) buttons |= bit(kHomeBit);
    if (shared & 0x20u) buttons |= bit(kCaptureBit);
    if (left & 0x01u) buttons |= bit(kDpadDownBit);
    if (left & 0x02u) buttons |= bit(kDpadUpBit);
    if (left & 0x04u) buttons |= bit(kDpadRightBit);
    if (left & 0x08u) buttons |= bit(kDpadLeftBit);
    if (left & 0x40u) buttons |= bit(kLeftShoulderBit);
    out.buttons = buttons;
    out.right_trigger = (right & 0x80u) != 0u;
    out.left_trigger = (left & 0x80u) != 0u;
    out.stick_raw[0] = u12_low(&r[6]);
    out.stick_raw[1] = u12_high(&r[7]);
    out.stick_raw[2] = u12_low(&r[9]);
    out.stick_raw[3] = u12_high(&r[10]);
    if (r[0] == kReportFullState && r.size() >= 49u) {
        for (std::size_t i = 0; i < 3; ++i) {
            const std::uint8_t* s = &r[13 + i * 12];
            const int ax = s16(s), ay = s16(s + 2), az = s16(s + 4);
            const int gx = s16(s + 6) - cal.gyro_offset[0];
            const int gy = s16(s + 8) - cal.gyro_offset[1];
            const int gz = s16(s + 10) - cal.gyro_offset[2];
            if (ax == 0 && ay == 0 && az == 0) continue;  // IMU not enabled yet
            ImuSample& sample = out.imu[out.imu_count++];
            sample.accel = {-cal.accel_scale[1] * static_cast<float>(ay),
                            cal.accel_scale[2] * static_cast<float>(az),
                            -cal.accel_scale[0] * static_cast<float>(ax)};
            sample.gyro = {-cal.gyro_scale[1] * static_cast<float>(gy),
                           cal.gyro_scale[2] * static_cast<float>(gz),
                           -cal.gyro_scale[0] * static_cast<float>(gx)};
        }
    }
    return true;
}

PadSample pad_sample_from(const DecodedState& state, ControlScheme scheme, const StickCal& left,
                          const StickCal& right, float stick_deadzone, float pointer_deadzone) noexcept {
    PadSample sample{};
    sample.connected = true;
    sample.scheme = scheme;
    sample.buttons = state.buttons;
    std::array<float, 4> sticks{};
    if (state.normalized) {
        std::copy_n(state.axes.begin(), 4, sticks.begin());
        sample.axes[4] = state.axes[4];
        sample.axes[5] = state.axes[5];
    } else if (state.simple) {
        for (std::size_t i = 0; i < 4; ++i) {
            const float value = static_cast<float>(state.stick_raw[i] - 0x8000) / 20000.0f;
            sticks[i] = std::clamp((i % 2u) == 1u ? -value : value, -1.0f, 1.0f);
        }
    } else {
        sticks[0] = normalize_stick(state.stick_raw[0], left.x);
        sticks[1] = normalize_stick(state.stick_raw[1], left.y);
        sticks[2] = normalize_stick(state.stick_raw[2], right.x);
        sticks[3] = normalize_stick(state.stick_raw[3], right.y);
    }
    sample.axes[0] = apply_deadzone(sticks[0], stick_deadzone);
    sample.axes[1] = apply_deadzone(sticks[1], stick_deadzone);
    sample.axes[2] = apply_deadzone(sticks[2], pointer_deadzone);
    sample.axes[3] = apply_deadzone(sticks[3], pointer_deadzone);
    if (!state.normalized) {
        sample.axes[4] = state.left_trigger ? 1.0f : 0.0f;
        sample.axes[5] = state.right_trigger ? 1.0f : 0.0f;
    }
    return sample;
}

std::size_t build_subcommand(std::uint8_t packet_number, std::uint8_t subcommand,
                             std::span<const std::uint8_t> data, std::span<std::uint8_t> out) noexcept {
    const std::size_t size = 11u + data.size();
    if (out.size() < size) return 0;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    out[0] = kOutputSubcommand;
    out[1] = static_cast<std::uint8_t>(packet_number & 0x0fu);
    std::copy(kNeutralRumble.begin(), kNeutralRumble.end(), out.begin() + 2);
    out[10] = subcommand;
    std::copy(data.begin(), data.end(), out.begin() + 11);
    return size;
}

std::size_t build_spi_read(std::uint8_t packet_number, std::uint32_t address, std::uint8_t length,
                           std::span<std::uint8_t> out) noexcept {
    const std::array<std::uint8_t, 5> data{
        static_cast<std::uint8_t>(address), static_cast<std::uint8_t>(address >> 8),
        static_cast<std::uint8_t>(address >> 16), static_cast<std::uint8_t>(address >> 24), length};
    return build_subcommand(packet_number, kSubcommandSpiRead, data, out);
}

std::size_t build_usb_command(std::uint8_t cmd, std::span<std::uint8_t> out) noexcept {
    if (out.size() < 2u) return 0;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    out[0] = kOutputUsbCommand;
    out[1] = cmd;
    return 2;
}

JoyConSide joycon_side_of_type(std::uint8_t type) noexcept {
    return type == 1u ? JoyConSide::Left : type == 2u ? JoyConSide::Right : JoyConSide::None;
}

JoyConSide joycon_side_of_product(std::uint16_t product) noexcept {
    return product == kJoyConLeftProductId    ? JoyConSide::Left
           : product == kJoyConRightProductId ? JoyConSide::Right
                                              : JoyConSide::None;
}

void turn_right_joycon_imu(DecodedState& state) noexcept {
    for (std::size_t i = 0; i < state.imu_count; ++i) {
        ImuSample& sample = state.imu[i];
        sample.accel[0] = -sample.accel[0];
        sample.accel[1] = -sample.accel[1];
        sample.gyro[0] = -sample.gyro[0];
        sample.gyro[1] = -sample.gyro[1];
    }
}

DecodedState combine_joycons(const DecodedState& left, const DecodedState& right) noexcept {
    using namespace pad;
    constexpr std::uint32_t kLeftButtons = bit(kDpadUpBit) | bit(kDpadDownBit) | bit(kDpadLeftBit) |
                                           bit(kDpadRightBit) | bit(kLeftShoulderBit) | bit(kBackBit) |
                                           bit(kLeftStickBit) | bit(kCaptureBit);
    constexpr std::uint32_t kRightButtons = bit(kSouthBit) | bit(kEastBit) | bit(kWestBit) | bit(kNorthBit) |
                                            bit(kRightShoulderBit) | bit(kStartBit) | bit(kRightStickBit) |
                                            bit(kHomeBit);
    DecodedState out = right;
    out.buttons = (left.buttons & kLeftButtons) | (right.buttons & kRightButtons);
    out.left_trigger = left.left_trigger;
    out.right_trigger = right.right_trigger;
    out.stick_raw[0] = left.stick_raw[0];
    out.stick_raw[1] = left.stick_raw[1];
    out.simple = false;
    return out;
}

JoyConPairing pair_joycons(std::span<JoyConHalf> halves, bool pairing_screen) noexcept {
    const auto valid = [&](int i) { return i >= 0 && static_cast<std::size_t>(i) < halves.size(); };
    JoyConPairing result{};
    bool& changed = result.changed;
    // A partner that is gone, or not the other side, or not paired back, frees its half.
    for (std::size_t i = 0; i < halves.size(); ++i) {
        JoyConHalf& h = halves[i];
        if (h.partner < 0) continue;
        const bool ok = valid(h.partner) && halves[h.partner].partner == static_cast<int>(i) &&
                        halves[h.partner].side != h.side && h.side != JoyConSide::None;
        if (!ok) {
            h.partner = -1;
            changed = true;
        }
    }
    const auto join = [&](int l, int r) {
        for (const int i : {l, r}) {
            const int old = halves[i].partner;
            if (valid(old)) halves[old].partner = -1;
        }
        halves[l].partner = r;
        halves[r].partner = l;
        changed = true;
    };
    if (pairing_screen) {
        // The held halves, unpaired ones first.
        const auto held = [&](JoyConSide side, bool unpaired) {
            for (std::size_t i = 0; i < halves.size(); ++i) {
                const JoyConHalf& h = halves[i];
                if (h.side == side && h.shoulder && (!unpaired || h.partner < 0)) return static_cast<int>(i);
            }
            return -1;
        };
        int l = held(JoyConSide::Left, true);
        int r = held(JoyConSide::Right, true);
        if (l < 0 || r < 0) {
            // A pair holding both, else any two held.
            for (std::size_t i = 0; i < halves.size() && (l < 0 || r < 0); ++i) {
                const JoyConHalf& h = halves[i];
                if (h.side == JoyConSide::Left && h.shoulder && valid(h.partner) && halves[h.partner].shoulder) {
                    l = static_cast<int>(i);
                    r = h.partner;
                }
            }
            if (l < 0 || r < 0) {
                l = l >= 0 ? l : held(JoyConSide::Left, false);
                r = r >= 0 ? r : held(JoyConSide::Right, false);
            }
        }
        if (l >= 0 && r >= 0) {
            if (halves[l].partner != r) join(l, r);
            result.made = true;
        }
        return result;
    }
    int l = -1, r = -1, lefts = 0, rights = 0;
    for (std::size_t i = 0; i < halves.size(); ++i) {
        const JoyConHalf& h = halves[i];
        if (h.partner >= 0) continue;
        if (h.side == JoyConSide::Left && lefts++ == 0) l = static_cast<int>(i);
        if (h.side == JoyConSide::Right && rights++ == 0) r = static_cast<int>(i);
    }
    if (lefts == 1 && rights == 1) join(l, r);
    return result;
}

SubcommandReply match_subcommand_reply(std::span<const std::uint8_t> r, std::uint8_t subcommand,
                                       std::uint32_t spi_address) noexcept {
    SubcommandReply reply{};
    if (r.size() < 15u || r[0] != kReportSubcommandReply || (r[13] & 0x80u) == 0u ||
        r[14] != subcommand) {
        return reply;
    }
    std::span<const std::uint8_t> data = r.subspan(15);
    if (subcommand == kSubcommandSpiRead) {
        if (data.size() < 5u) return reply;
        const std::uint32_t address = static_cast<std::uint32_t>(data[0]) |
                                      (static_cast<std::uint32_t>(data[1]) << 8) |
                                      (static_cast<std::uint32_t>(data[2]) << 16) |
                                      (static_cast<std::uint32_t>(data[3]) << 24);
        if (address != spi_address) return reply;
        const std::size_t length = std::min<std::size_t>(data[4], data.size() - 5u);
        data = data.subspan(5, length);
    }
    reply.matched = true;
    reply.data = data;
    return reply;
}

std::span<const Switch2Command> switch2_init_sequence() noexcept {
    static const std::array<Switch2Command, 10> kSequence{
        command({0x07, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}),
        // Feature output mask 0x27 (buttons, sticks, IMU).
        command({0x0c, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00}),
        command({0x11, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}),
        command({0x0a, 0x91, 0x00, 0x08, 0x00, 0x14, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff,
                 0xff, 0xff, 0xff, 0xff, 0x35, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00, 0x00}),
        command({0x0c, 0x91, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00}),
        command({0x01, 0x91, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00}),
        command({0x01, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}),
        command({0x08, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}),
        // Report format 5.
        command({0x03, 0x91, 0x00, 0x0a, 0x00, 0x04, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00}),
        // Start output (6-byte host address; any value is accepted).
        command({0x03, 0x91, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x00, 0x01, 0x00, 0xff, 0xff, 0xff,
                 0xff, 0xff, 0xff}),
    };
    return kSequence;
}

Switch2Command switch2_flash_read(std::uint32_t address) noexcept {
    Switch2Command cmd = command({0x02, 0x91, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    cmd.bytes[12] = static_cast<std::uint8_t>(address);
    cmd.bytes[13] = static_cast<std::uint8_t>(address >> 8);
    cmd.bytes[14] = static_cast<std::uint8_t>(address >> 16);
    cmd.bytes[15] = static_cast<std::uint8_t>(address >> 24);
    return cmd;
}

Switch2Command switch2_player_led(std::uint8_t pattern) noexcept {
    Switch2Command cmd = command({0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    cmd.bytes[8] = pattern;
    return cmd;
}

bool decode_switch2_report(std::span<const std::uint8_t> r, Switch2ImuState& imu, DecodedState& out) noexcept {
    if (r.size() < kSwitch2ReportBytes || r[0] != kSwitch2InputReport) return false;
    out = DecodedState{};
    using namespace pad;
    std::uint32_t buttons = 0;
    const std::uint8_t b5 = r[5], b6 = r[6], b7 = r[7], b8 = r[8];
    if (b5 & 0x01u) buttons |= bit(kWestBit);   // Y
    if (b5 & 0x02u) buttons |= bit(kNorthBit);  // X
    if (b5 & 0x04u) buttons |= bit(kSouthBit);  // B
    if (b5 & 0x08u) buttons |= bit(kEastBit);   // A
    if (b5 & 0x40u) buttons |= bit(kRightShoulderBit);
    if (b6 & 0x01u) buttons |= bit(kBackBit);
    if (b6 & 0x02u) buttons |= bit(kStartBit);
    if (b6 & 0x04u) buttons |= bit(kRightStickBit);
    if (b6 & 0x08u) buttons |= bit(kLeftStickBit);
    if (b6 & 0x10u) buttons |= bit(kHomeBit);
    if (b6 & 0x20u) buttons |= bit(kCaptureBit);
    if (b6 & 0x40u) buttons |= bit(kChatBit);
    if (b7 & 0x01u) buttons |= bit(kDpadDownBit);
    if (b7 & 0x02u) buttons |= bit(kDpadUpBit);
    if (b7 & 0x04u) buttons |= bit(kDpadRightBit);
    if (b7 & 0x08u) buttons |= bit(kDpadLeftBit);
    if (b7 & 0x40u) buttons |= bit(kLeftShoulderBit);
    if (b8 & 0x01u) buttons |= bit(kGripRightBit);
    if (b8 & 0x02u) buttons |= bit(kGripLeftBit);
    out.buttons = buttons;
    out.right_trigger = (b5 & 0x80u) != 0u;
    out.left_trigger = (b7 & 0x80u) != 0u;
    out.stick_raw[0] = u12_low(&r[11]);
    out.stick_raw[1] = u12_high(&r[12]);
    out.stick_raw[2] = u12_low(&r[14]);
    out.stick_raw[3] = u12_high(&r[15]);

    const std::uint32_t timestamp = static_cast<std::uint32_t>(r[0x2b]) |
                                    (static_cast<std::uint32_t>(r[0x2c]) << 8) |
                                    (static_cast<std::uint32_t>(r[0x2d]) << 16) |
                                    (static_cast<std::uint32_t>(r[0x2e]) << 24);
    if (timestamp != 0u && !imu.ready) {
        // Timestamps are microseconds on most units; where they are not, the gyro full
        // scale differs (per SDL_hidapi_switch2.c).
        ++imu.sample_count;
        if (imu.sample_count >= 5 && imu.first_timestamp == 0u) {
            imu.first_timestamp = timestamp;
            imu.sample_count = 0;
        } else if (imu.sample_count == 100) {
            const std::uint64_t coeff =
                1000ull * (timestamp - imu.first_timestamp) / (100ull * 4ull);
            if ((coeff + 100000ull) / 200000ull == 5ull) {
                imu.gyro_full_scale = 34.8f;
                imu.ready = true;
            } else if (coeff != 0u) {
                imu.gyro_full_scale = 40.0f;
                imu.ready = true;
            } else {
                imu.first_timestamp = 0u;
                imu.sample_count = 0;
            }
        }
    }
    if (imu.ready && timestamp != 0u) {
        constexpr float kAccelG = 8.0f / 32767.0f;
        const float gyro = imu.gyro_full_scale / 32767.0f;
        ImuSample& sample = out.imu[0];
        sample.accel = {static_cast<float>(s16(&r[0x31])) * kAccelG,
                        static_cast<float>(s16(&r[0x35])) * kAccelG,
                        -static_cast<float>(s16(&r[0x33])) * kAccelG};
        sample.gyro = {static_cast<float>(s16(&r[0x37])) * gyro - imu.gyro_bias[0],
                       static_cast<float>(s16(&r[0x3b])) * gyro - imu.gyro_bias[2],
                       -static_cast<float>(s16(&r[0x39])) * gyro + imu.gyro_bias[1]};
        out.imu_count = 1;
    }
    return true;
}

namespace {

using Vec3 = std::array<float, 3>;
float dot(const Vec3& a, const Vec3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float length(const Vec3& a) noexcept { return std::sqrt(dot(a, a)); }
bool finite(const Vec3& a) noexcept {
    return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
}

}  // namespace

float SwitchMotion::shake_accel_threshold_g(std::uint8_t sensitivity) noexcept {
    static constexpr float kThresholds[kShakeSensitivitySteps] = {1.50f, 1.20f, 0.95f, 0.75f, 0.60f};
    return kThresholds[std::clamp<int>(sensitivity, 1, kShakeSensitivitySteps) - 1];
}

float SwitchMotion::shake_gyro_threshold(std::uint8_t sensitivity) noexcept {
    // 1100 .. 450 degrees per second.
    static constexpr float kThresholds[kShakeSensitivitySteps] = {19.2f, 15.7f, 12.6f, 9.8f, 7.9f};
    return kThresholds[std::clamp<int>(sensitivity, 1, kShakeSensitivitySteps) - 1];
}

SwitchMotion::Output SwitchMotion::feed(const ImuSample& sample, float dt, std::uint64_t time_us,
                                        const MotionSettings& settings) noexcept {
    Output out{};
    if (!finite(sample.accel) || !finite(sample.gyro) || !std::isfinite(dt)) {
        return out;
    }
    dt = std::clamp(dt, 0.0f, 0.05f);
    const float accel_length = length(sample.accel);
    Vec3 rate{sample.gyro[0] - bias_[0], sample.gyro[1] - bias_[1], sample.gyro[2] - bias_[2]};

    // Gravity in the controller frame: a world-fixed vector turns against the
    // controller's rotation, then leans toward the measured direction.
    if (!gravity_ready_) {
        if (accel_length > 0.5f) {
            for (std::size_t i = 0; i < 3; ++i) gravity_[i] = sample.accel[i] / accel_length;
            gravity_ready_ = true;
        }
    } else {
        const Vec3 turn = cross(rate, gravity_);
        for (std::size_t i = 0; i < 3; ++i) gravity_[i] -= turn[i] * dt;
        if (accel_length > 0.8f && accel_length < 1.2f) {
            const float blend = dt / (0.5f + dt);
            for (std::size_t i = 0; i < 3; ++i) {
                gravity_[i] += (sample.accel[i] / accel_length - gravity_[i]) * blend;
            }
        }
        const float g = length(gravity_);
        if (g > 1e-3f) {
            for (float& c : gravity_) c /= g;
        } else {
            gravity_ = {0.0f, 1.0f, 0.0f};
        }
    }

    // Rest windows refine the bias.
    for (std::size_t i = 0; i < 3; ++i) {
        sum_[i] += sample.gyro[i];
        sum_sq_[i] += static_cast<double>(sample.gyro[i]) * sample.gyro[i];
    }
    accel_min_ = std::min(accel_min_, accel_length);
    accel_max_ = std::max(accel_max_, accel_length);
    window_time_ += dt;
    ++window_samples_;
    if (window_time_ >= kRestWindowSeconds && window_samples_ >= 8u) {
        bool rest = accel_max_ - accel_min_ < 0.05f;
        Vec3 mean{};
        for (std::size_t i = 0; i < 3 && rest; ++i) {
            const double m = sum_[i] / window_samples_;
            const double variance = std::max(0.0, sum_sq_[i] / window_samples_ - m * m);
            mean[i] = static_cast<float>(m);
            rest = std::sqrt(variance) < kRestGyroSd;
        }
        // A steady slow turn is not bias: later windows may move the estimate
        // by at most 3 degrees per second.
        for (std::size_t i = 0; i < 3 && rest && rest_windows_ != 0u; ++i) {
            rest = std::fabs(mean[i] - bias_[i]) < 0.05f;
        }
        if (rest) {
            // The first rest window replaces the stored origin's residual;
            // later ones move the estimate gradually.
            const float weight = rest_windows_ == 0u ? 1.0f : 0.3f;
            for (std::size_t i = 0; i < 3; ++i) bias_[i] += (mean[i] - bias_[i]) * weight;
            ++rest_windows_;
        }
        sum_ = {};
        sum_sq_ = {};
        accel_min_ = 1e9f;
        accel_max_ = -1e9f;
        window_time_ = 0.0f;
        window_samples_ = 0u;
    }

    // Shake: linear acceleration (gravity removed) or a fast flick.
    if (settings.shake) {
        Vec3 linear{};
        for (std::size_t i = 0; i < 3; ++i) linear[i] = sample.accel[i] - gravity_[i];
        const float linear_g = gravity_ready_ ? length(linear) : 0.0f;
        const float speed = length(rate);
        const float linear_mean = 0.5f * (linear_g + previous_linear_);
        const float rate_min = std::min(speed, previous_rate_);
        previous_linear_ = linear_g;
        previous_rate_ = speed;
        const bool triggered = linear_mean >= shake_accel_threshold_g(settings.shake_sensitivity) ||
                               rate_min >= shake_gyro_threshold(settings.shake_sensitivity);
        if (triggered && time_us >= shake_cooldown_until_us_) {
            shake_active_ = true;
            shake_hold_until_us_ = time_us + kShakeHoldUs;
            shake_cooldown_until_us_ = time_us + kShakeCooldownUs;
            out.shake_started = true;
        }
    }

    if (settings.gyro_pointer && gravity_ready_) {
        // Smooth noise gate: rates under the gate shrink toward zero.
        const float speed = length(rate);
        if (speed < kGyroNoiseGate) {
            const float scale = speed / kGyroNoiseGate;
            for (float& c : rate) c *= scale;
        }
        const Vec3& up = gravity_;
        const float yaw = dot(rate, up);
        Vec3 pitch_axis{1.0f, 0.0f, 0.0f};
        const float along = dot(pitch_axis, up);
        for (std::size_t i = 0; i < 3; ++i) pitch_axis[i] -= up[i] * along;
        const float pitch_length = length(pitch_axis);
        if (pitch_length > 0.3f) {
            for (float& c : pitch_axis) c /= pitch_length;
        } else {
            pitch_axis = {1.0f, 0.0f, 0.0f};
        }
        const float pitch = dot(rate, pitch_axis);
        const float sensitivity = std::clamp(
            std::isfinite(settings.gyro_sensitivity) ? settings.gyro_sensitivity : 1.0f, 0.1f, 10.0f);
        const float gain_x = 2.0f / (kDegreesPerPictureWidth * kPi / 180.0f) * sensitivity;
        const float aspect = std::clamp(
            std::isfinite(settings.picture_aspect) ? settings.picture_aspect : 16.0f / 9.0f, 1.0f, 4.0f);
        // Yaw to the left (positive about up) moves the pointer left; pitch up
        // (positive about the right axis) moves it up.
        out.pointer_x = -yaw * dt * gain_x;
        out.pointer_y = pitch * dt * gain_x * aspect;
    }
    return out;
}

}  // namespace galaxy::input::switch_hid
