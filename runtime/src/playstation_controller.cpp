#include "galaxy/playstation_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace galaxy::input::playstation {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDefaultGyroDegreesPerCount = 1.0f / 16.0f;
constexpr float kDefaultAccelCountsPerG = 8192.0f;

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

constexpr std::array<std::uint32_t, 256> make_crc_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) value = (value & 1u) != 0u ? (value >> 1) ^ 0xedb88320u : value >> 1;
        table[i] = value;
    }
    return table;
}
constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

// 0..255 around a centre of 128: both ends reach exactly +-1.
float stick_axis(std::uint8_t raw, bool up_positive) noexcept {
    const float offset = static_cast<float>(raw) - 128.0f;
    const float value = offset >= 0.0f ? offset / 127.0f : offset / 128.0f;
    return up_positive ? -value : value;
}

constexpr std::uint32_t kHat[8] = {
    pad::bit(pad::kDpadUpBit),
    pad::bit(pad::kDpadUpBit) | pad::bit(pad::kDpadRightBit),
    pad::bit(pad::kDpadRightBit),
    pad::bit(pad::kDpadDownBit) | pad::bit(pad::kDpadRightBit),
    pad::bit(pad::kDpadDownBit),
    pad::bit(pad::kDpadDownBit) | pad::bit(pad::kDpadLeftBit),
    pad::bit(pad::kDpadLeftBit),
    pad::bit(pad::kDpadUpBit) | pad::bit(pad::kDpadLeftBit),
};

struct Layout {
    std::size_t base = 0;  // index of the left stick X byte
    bool full = false;     // sensors and extended buttons present
};

// Face buttons, shoulders and the three button bytes shared by every layout.
void decode_buttons(const std::uint8_t* b, Model model, bool full, switch_hid::DecodedState& out) noexcept {
    std::uint32_t buttons = 0;
    if ((b[0] & 0x0fu) < 8u) buttons |= kHat[b[0] & 0x0fu];
    if (b[0] & 0x10u) buttons |= pad::bit(pad::kWestBit);    // Square
    if (b[0] & 0x20u) buttons |= pad::bit(pad::kSouthBit);   // Cross
    if (b[0] & 0x40u) buttons |= pad::bit(pad::kEastBit);    // Circle
    if (b[0] & 0x80u) buttons |= pad::bit(pad::kNorthBit);   // Triangle
    if (b[1] & 0x01u) buttons |= pad::bit(pad::kLeftShoulderBit);
    if (b[1] & 0x02u) buttons |= pad::bit(pad::kRightShoulderBit);
    if (b[1] & 0x10u) buttons |= pad::bit(pad::kBackBit);    // Share, Create
    if (b[1] & 0x20u) buttons |= pad::bit(pad::kStartBit);   // Options
    if (b[1] & 0x40u) buttons |= pad::bit(pad::kLeftStickBit);
    if (b[1] & 0x80u) buttons |= pad::bit(pad::kRightStickBit);
    if (b[2] & 0x01u) buttons |= pad::bit(pad::kHomeBit);
    if (b[2] & 0x02u) buttons |= pad::bit(pad::kCaptureBit);
    if (full && model != Model::DualShock4) {
        // The DualShock 4 keeps a report counter in the upper six bits.
        if (b[2] & 0x04u) buttons |= pad::bit(pad::kChatBit);
        if (model == Model::DualSenseEdge) {
            if (b[2] & 0x40u) buttons |= pad::bit(pad::kGripLeftBit);
            if (b[2] & 0x80u) buttons |= pad::bit(pad::kGripRightBit);
        }
    }
    out.buttons = buttons;
}

float trigger_value(std::uint8_t analog, bool digital) noexcept {
    if (analog == 0u && digital) return 1.0f;
    return static_cast<float>(analog) / 255.0f;
}

// Elapsed seconds on the controller's own clock since the previous report.
float clock_delta(SensorClock& clock, std::uint32_t ticks, std::uint32_t mask, float seconds_per_tick) noexcept {
    ticks &= mask;
    const float dt = clock.primed ? static_cast<float>((ticks - clock.last) & mask) * seconds_per_tick : 0.0f;
    clock.last = ticks;
    clock.primed = true;
    return dt;
}

}  // namespace

std::optional<Model> model_for_product(std::uint16_t vendor, std::uint16_t product) noexcept {
    if (vendor != kSonyVendorId) return std::nullopt;
    switch (product) {
    case kDualShock4ProductId:
    case kDualShock4SlimProductId: return Model::DualShock4;
    case kDualSenseProductId: return Model::DualSense;
    case kDualSenseEdgeProductId: return Model::DualSenseEdge;
    default: return std::nullopt;
    }
}

const char* model_name(Model model) noexcept {
    switch (model) {
    case Model::DualShock4: return "DualShock 4";
    case Model::DualSense: return "DualSense";
    case Model::DualSenseEdge: return "DualSense Edge";
    }
    return "PlayStation controller";
}

ImuCal::ImuCal() noexcept {
    for (std::size_t i = 0; i < 3; ++i) {
        scale[i] = kDefaultGyroDegreesPerCount * kPi / 180.0f;
        scale[3 + i] = 1.0f / kDefaultAccelCountsPerG;
    }
}

ImuCal parse_calibration(std::span<const std::uint8_t> f, bool plus_first_order) noexcept {
    ImuCal cal{};
    if (f.size() < 35u) return cal;
    const std::uint8_t* d = f.data();
    const int gyro_bias[3] = {s16(d + 1), s16(d + 3), s16(d + 5)};
    int gyro_plus[3];
    int gyro_minus[3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (plus_first_order) {
            gyro_plus[axis] = s16(d + 7 + axis * 2);
            gyro_minus[axis] = s16(d + 13 + axis * 2);
        } else {
            gyro_plus[axis] = s16(d + 7 + axis * 4);
            gyro_minus[axis] = s16(d + 9 + axis * 4);
        }
    }
    const int speed = s16(d + 19) + s16(d + 21);
    ImuCal calibrated{};
    bool valid = true;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const int span = std::abs(gyro_plus[axis] - gyro_bias[axis]) + std::abs(gyro_minus[axis] - gyro_bias[axis]);
        if (span == 0) continue;
        const float degrees_per_count = static_cast<float>(speed) / static_cast<float>(span);
        calibrated.bias[axis] = static_cast<std::int16_t>(gyro_bias[axis]);
        calibrated.scale[axis] = degrees_per_count * kPi / 180.0f;
        valid = valid && std::fabs(1.0f - degrees_per_count / kDefaultGyroDegreesPerCount) <= 0.5f;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const int plus = s16(d + 23 + axis * 4);
        const int minus = s16(d + 25 + axis * 4);
        const int range = static_cast<std::int16_t>(plus - minus);
        if (range == 0) continue;
        calibrated.bias[3 + axis] = static_cast<std::int16_t>(plus - range / 2);
        const float g_per_count = 2.0f / static_cast<float>(range);
        calibrated.scale[3 + axis] = g_per_count;
        valid = valid && std::fabs(1.0f - g_per_count * kDefaultAccelCountsPerG) <= 0.5f;
    }
    for (std::int16_t bias : calibrated.bias) valid = valid && std::abs(static_cast<int>(bias)) <= 1024;
    if (!valid) return cal;
    calibrated.hardware = true;
    return calibrated;
}

std::uint32_t report_crc(std::uint8_t seed, std::span<const std::uint8_t> data) noexcept {
    std::uint32_t crc = 0xffffffffu;
    crc = kCrcTable[(crc ^ seed) & 0xffu] ^ (crc >> 8);
    for (std::uint8_t byte : data) crc = kCrcTable[(crc ^ byte) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

bool bluetooth_crc_valid(std::span<const std::uint8_t> report) noexcept {
    if (report.size() < kBluetoothReportBytes) return false;
    return report_crc(0xa1, report.first(kBluetoothReportBytes - 4)) == u32(&report[kBluetoothReportBytes - 4]);
}

bool decode_report(Model model, Transport transport, std::span<const std::uint8_t> r, const ImuCal& cal,
                   SensorClock& clock, switch_hid::DecodedState& out) noexcept {
    if (r.empty()) return false;
    const bool dualsense = model != Model::DualShock4;
    Layout layout{};
    if (transport == Transport::Usb) {
        if (r[0] != kReportState || r.size() < (dualsense ? 32u : 25u)) return false;
        layout.base = 1;
        layout.full = true;
    } else if (r[0] == kReportState) {
        if (r.size() < 10u) return false;
        layout.base = 1;
    } else {
        const bool state_id = dualsense ? r[0] == kDualSenseBluetooth
                                        : r[0] >= kDualShock4BluetoothFirst && r[0] <= kDualShock4BluetoothLast;
        if (!state_id || r.size() < kBluetoothReportBytes || !bluetooth_crc_valid(r)) return false;
        if (!dualsense && (r[1] & 0x80u) == 0u) return false;
        layout.base = dualsense ? 2u : 3u;
        layout.full = true;
    }
    const std::uint8_t* p = r.data() + layout.base;
    out = switch_hid::DecodedState{};
    out.normalized = true;
    out.axes[0] = stick_axis(p[0], false);
    out.axes[1] = stick_axis(p[1], true);
    out.axes[2] = stick_axis(p[2], false);
    out.axes[3] = stick_axis(p[3], true);
    if (layout.full && dualsense) {
        decode_buttons(p + 7, model, true, out);
        out.axes[4] = trigger_value(p[4], (p[8] & 0x04u) != 0u);
        out.axes[5] = trigger_value(p[5], (p[8] & 0x08u) != 0u);
    } else {
        decode_buttons(p + 4, model, layout.full, out);
        out.axes[4] = trigger_value(p[7], (p[5] & 0x04u) != 0u);
        out.axes[5] = trigger_value(p[8], (p[5] & 0x08u) != 0u);
    }
    if (!layout.full) return true;

    const std::uint8_t* gyro = p + (dualsense ? 15 : 12);
    const std::uint8_t* accel = p + (dualsense ? 21 : 18);
    std::array<std::int16_t, 6> raw{};
    bool any = false;
    for (std::size_t i = 0; i < 3; ++i) {
        raw[i] = s16(gyro + i * 2);
        raw[3 + i] = s16(accel + i * 2);
    }
    for (std::int16_t value : raw) any |= value != 0;
    const float dt = dualsense ? clock_delta(clock, u32(p + 27), 0xffffffffu, 1.0f / 3.0e6f)
                               : clock_delta(clock, u16(p + 9), 0xffffu, 16.0f / 3.0f * 1e-6f);
    if (!any) return true;
    switch_hid::ImuSample& sample = out.imu[0];
    for (std::size_t i = 0; i < 3; ++i) {
        sample.gyro[i] = static_cast<float>(raw[i] - cal.bias[i]) * cal.scale[i];
        sample.accel[i] = static_cast<float>(raw[3 + i] - cal.bias[3 + i]) * cal.scale[3 + i];
    }
    out.imu_count = 1;
    out.imu_dt = dt;
    return true;
}

std::size_t output_report_bytes(Model model, Transport transport) noexcept {
    if (transport == Transport::Bluetooth) return kBluetoothReportBytes;
    return model == Model::DualShock4 ? 32u : 48u;
}

std::size_t build_output_report(Model model, Transport transport, const Effects& e,
                                std::span<std::uint8_t> out) noexcept {
    const std::size_t size = output_report_bytes(model, transport);
    if (out.size() < size) return 0;
    std::fill(out.begin(), out.end(), std::uint8_t{0});
    const bool bluetooth = transport == Transport::Bluetooth;
    if (model == Model::DualShock4) {
        // DS4 effect bytes: right motor, left motor, red, green, blue.
        std::uint8_t* effects = &out[bluetooth ? 6 : 4];
        std::uint8_t flags = 0;
        if (e.apply_rumble) flags |= 0x01;
        if (e.apply_led) flags |= 0x02;
        if (bluetooth) {
            out[0] = 0x11;
            out[1] = 0xc4;  // HID data with CRC, 4 ms report interval
            out[3] = flags;
        } else {
            out[0] = 0x05;
            out[1] = flags;
        }
        effects[0] = e.rumble_right;
        effects[1] = e.rumble_left;
        effects[2] = e.led[0];
        effects[3] = e.led[1];
        effects[4] = e.led[2];
    } else {
        std::uint8_t* effects = &out[bluetooth ? 3 : 1];
        if (bluetooth) {
            out[0] = 0x31;
            out[2] = 0x10;
        } else {
            out[0] = 0x02;
        }
        if (e.apply_rumble) {
            effects[0] |= 0x02;  // classic rumble in place of audio haptics
            if (e.full_range_rumble) {
                effects[38] |= 0x04;
                effects[2] = e.rumble_right;
                effects[3] = e.rumble_left;
            } else {
                effects[0] |= 0x01;
                effects[2] = static_cast<std::uint8_t>(e.rumble_right >> 1);
                effects[3] = static_cast<std::uint8_t>(e.rumble_left >> 1);
            }
        }
        if (e.release_led) effects[1] |= 0x08;
        if (e.apply_led) {
            effects[1] |= 0x04;
            effects[44] = e.led[0];
            effects[45] = e.led[1];
            effects[46] = e.led[2];
        }
    }
    if (bluetooth) {
        const std::uint32_t crc = report_crc(0xa2, std::span<const std::uint8_t>(out.data(), size - 4));
        for (std::size_t i = 0; i < 4; ++i) out[size - 4 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
    }
    return size;
}

std::uint16_t dualsense_firmware(std::span<const std::uint8_t> f) noexcept {
    return f.size() >= 46u ? u16(&f[44]) : std::uint16_t{0};
}

bool dualsense_full_range_rumble(Model model, std::uint16_t firmware) noexcept {
    return model == Model::DualSenseEdge || firmware == 0u || firmware >= 0x0224u;
}

}  // namespace galaxy::input::playstation
