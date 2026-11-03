// PlayStation, Steam and generic HID controllers: input report decoding,
// calibration, output reports, and the device list of the CONTROLS page.
// Reports are built byte by byte from the documented layouts (SDL's hidapi
// PS4/PS5/Steam drivers); generic values as the HID parser returns them.

#include "galaxy/controller_roster.h"
#include "galaxy/generic_hid.h"
#include "galaxy/input_bindings.h"
#include "galaxy/playstation_controller.h"
#include "galaxy/rumble.h"
#include "galaxy/simple_pads.h"
#include "galaxy/steam_controller.h"
#include "galaxy/switch2_ble.h"
#include "galaxy/switch_controller.h"
#include "galaxy/wiimote_hid.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace galaxy::input;
namespace ps = galaxy::input::playstation;
namespace sv = galaxy::input::steam;
namespace gh = galaxy::input::generic_hid;
using Bytes = std::vector<std::uint8_t>;

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++g_failures;
    }
}

bool close_to(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) <= tolerance;
}

constexpr float kPi = 3.14159265358979323846f;

void put16(Bytes& r, std::size_t at, int value) {
    r[at] = static_cast<std::uint8_t>(value & 0xff);
    r[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}
void put32(Bytes& r, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) r[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::uint32_t get32(const Bytes& r, std::size_t at) {
    return static_cast<std::uint32_t>(r[at]) | (static_cast<std::uint32_t>(r[at + 1]) << 8) |
           (static_cast<std::uint32_t>(r[at + 2]) << 16) | (static_cast<std::uint32_t>(r[at + 3]) << 24);
}
std::uint32_t bit(unsigned index) { return 1u << index; }

// Stamps a Bluetooth CRC over the first 74 bytes with the input seed.
void seal_input(Bytes& r) {
    put32(r, 74, ps::report_crc(0xa1, std::span<const std::uint8_t>(r.data(), 74)));
}

// DualShock 4 USB report 0x01. Offsets from the report start (id at 0).
Bytes ds4_usb() {
    Bytes r(64, 0);
    r[0] = 0x01;
    for (std::size_t i = 1; i <= 4; ++i) r[i] = 128;
    r[5] = 0x08;  // hat released
    return r;
}

// DualShock 4 Bluetooth report 0x11: the same payload two bytes later.
Bytes ds4_bluetooth() {
    Bytes r(78, 0);
    r[0] = 0x11;
    r[1] = 0xc4;  // HID data present (0x80)
    for (std::size_t i = 3; i <= 6; ++i) r[i] = 128;
    r[7] = 0x08;
    seal_input(r);
    return r;
}

// DualSense USB report 0x01.
Bytes ds5_usb() {
    Bytes r(64, 0);
    r[0] = 0x01;
    for (std::size_t i = 1; i <= 4; ++i) r[i] = 128;
    r[8] = 0x08;
    return r;
}

Bytes ds5_bluetooth() {
    Bytes r(78, 0);
    r[0] = 0x31;
    r[1] = 0x01;
    for (std::size_t i = 2; i <= 5; ++i) r[i] = 128;
    r[9] = 0x08;
    seal_input(r);
    return r;
}

struct Decoded {
    bool ok = false;
    switch_hid::DecodedState state{};
};

Decoded decode(ps::Model model, ps::Transport transport, const Bytes& report, const ps::ImuCal& cal,
               ps::SensorClock& clock) {
    Decoded result;
    result.ok = ps::decode_report(model, transport, report, cal, clock, result.state);
    return result;
}

Decoded decode(ps::Model model, ps::Transport transport, const Bytes& report) {
    ps::SensorClock clock;
    return decode(model, transport, report, ps::ImuCal{}, clock);
}

void test_crc() {
    // CRC-32 check value of "123456789", split into the seed byte and the rest.
    const std::string rest = "23456789";
    const std::uint32_t crc = ps::report_crc(
        '1', std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(rest.data()), rest.size()));
    expect(crc == 0xcbf43926u, "report CRC is CRC-32");
    Bytes r = ds4_bluetooth();
    expect(ps::bluetooth_crc_valid(r), "sealed Bluetooth report passes the CRC");
    r[10] ^= 0x01;
    expect(!ps::bluetooth_crc_valid(r), "a flipped bit fails the CRC");
    expect(!ps::bluetooth_crc_valid(Bytes(40, 0)), "a short report has no CRC");
}

void test_dualshock4_usb() {
    Bytes r = ds4_usb();
    Decoded neutral = decode(ps::Model::DualShock4, ps::Transport::Usb, r);
    expect(neutral.ok && neutral.state.normalized, "DualShock 4 USB report decodes");
    expect(neutral.state.buttons == 0u, "neutral DualShock 4 has no buttons");
    for (std::size_t i = 0; i < 4; ++i) expect(close_to(neutral.state.axes[i], 0.0f, 0.01f), "sticks centre at 128");
    expect(neutral.state.axes[4] == 0.0f && neutral.state.axes[5] == 0.0f, "triggers rest at 0");
    expect(neutral.state.imu_count == 0, "no sensor data, no IMU sample");

    r[1] = 255; r[2] = 0; r[3] = 0; r[4] = 255;
    Decoded sticks = decode(ps::Model::DualShock4, ps::Transport::Usb, r);
    expect(close_to(sticks.state.axes[0], 1.0f), "left stick right is +1");
    expect(close_to(sticks.state.axes[1], 1.0f), "left stick up (raw 0) is +1");
    expect(close_to(sticks.state.axes[2], -1.0f), "right stick left is -1");
    expect(close_to(sticks.state.axes[3], -1.0f), "right stick down (raw 255) is -1");

    r = ds4_usb();
    r[5] = 0x01 | 0x20 | 0x80;  // hat NE, Cross, Triangle
    r[6] = 0x01 | 0x02 | 0x10 | 0x20 | 0x40 | 0x80;
    r[7] = 0xff;  // PS and touchpad plus the report counter bits
    const Decoded buttons = decode(ps::Model::DualShock4, ps::Transport::Usb, r);
    const std::uint32_t expected = bit(pad::kDpadUpBit) | bit(pad::kDpadRightBit) | bit(pad::kSouthBit) |
                                   bit(pad::kNorthBit) | bit(pad::kLeftShoulderBit) | bit(pad::kRightShoulderBit) |
                                   bit(pad::kBackBit) | bit(pad::kStartBit) | bit(pad::kLeftStickBit) |
                                   bit(pad::kRightStickBit) | bit(pad::kHomeBit) | bit(pad::kCaptureBit);
    expect(buttons.state.buttons == expected, "DualShock 4 buttons map to the unified bits");

    r = ds4_usb();
    r[5] = 0x10 | 0x40 | 0x04;  // Square, Circle, hat down
    const Decoded face = decode(ps::Model::DualShock4, ps::Transport::Usb, r);
    expect(face.state.buttons == (bit(pad::kWestBit) | bit(pad::kEastBit) | bit(pad::kDpadDownBit)),
           "Square is West, Circle is East");

    r = ds4_usb();
    r[8] = 255; r[9] = 128;
    const Decoded triggers = decode(ps::Model::DualShock4, ps::Transport::Usb, r);
    expect(close_to(triggers.state.axes[4], 1.0f) && close_to(triggers.state.axes[5], 128.0f / 255.0f), "analog triggers");
    r = ds4_usb();
    r[6] = 0x04;
    expect(close_to(decode(ps::Model::DualShock4, ps::Transport::Usb, r).state.axes[4], 1.0f),
           "a digital-only L2 reads as fully pressed");

    r = ds4_usb();
    r[0] = 0x02;
    expect(!decode(ps::Model::DualShock4, ps::Transport::Usb, r).ok, "an unknown report id is rejected");
    expect(!decode(ps::Model::DualShock4, ps::Transport::Usb, Bytes(12, 0)).ok, "a short USB report is rejected");
}

void test_dualshock4_motion() {
    Bytes r = ds4_usb();
    put16(r, 15, 1600);   // gyro Y: 100 deg/s at 1/16 deg/s per count
    put16(r, 23, 8192);   // accel Z: 1 g
    put16(r, 19, -4096);  // accel X: -0.5 g
    ps::SensorClock clock;
    put16(r, 10, 1000);
    Decoded first = decode(ps::Model::DualShock4, ps::Transport::Usb, r, ps::ImuCal{}, clock);
    expect(first.ok && first.state.imu_count == 1, "sensor data gives one IMU sample");
    expect(close_to(first.state.imu[0].gyro[1], 100.0f * kPi / 180.0f, 1e-3f), "gyro counts to rad/s");
    expect(close_to(first.state.imu[0].accel[2], 1.0f) && close_to(first.state.imu[0].accel[0], -0.5f), "accel counts to g");
    expect(first.state.imu_dt == 0.0f, "the first report has no dt");
    put16(r, 10, 1750);  // 750 ticks of 16/3 us = 4 ms
    Decoded second = decode(ps::Model::DualShock4, ps::Transport::Usb, r, ps::ImuCal{}, clock);
    expect(close_to(second.state.imu_dt, 0.004f, 1e-5f), "dt from the 16-bit sensor clock");
    put16(r, 10, 0xffff);
    (void)decode(ps::Model::DualShock4, ps::Transport::Usb, r, ps::ImuCal{}, clock);
    put16(r, 10, 749);  // wraps: 750 ticks later
    Decoded wrapped = decode(ps::Model::DualShock4, ps::Transport::Usb, r, ps::ImuCal{}, clock);
    expect(close_to(wrapped.state.imu_dt, 0.004f, 1e-5f), "the sensor clock wraps");

    ps::ImuCal cal;
    cal.bias[1] = 100;
    cal.scale[1] = 0.5f;
    put16(r, 15, 300);
    ps::SensorClock clean;
    expect(close_to(decode(ps::Model::DualShock4, ps::Transport::Usb, r, cal, clean).state.imu[0].gyro[1], 100.0f),
           "calibration subtracts the bias and applies the scale");
}

void test_dualshock4_bluetooth() {
    Bytes r = ds4_bluetooth();
    expect(decode(ps::Model::DualShock4, ps::Transport::Bluetooth, r).ok, "DualShock 4 full Bluetooth report decodes");
    r[3] = 255;
    r[7] = 0x28;  // Cross, hat released
    seal_input(r);
    const Decoded moved = decode(ps::Model::DualShock4, ps::Transport::Bluetooth, r);
    expect(moved.ok && close_to(moved.state.axes[0], 1.0f), "Bluetooth payload starts at byte 3");
    expect((moved.state.buttons & bit(pad::kSouthBit)) != 0u, "Bluetooth buttons follow the sticks");

    Bytes bad = r;
    bad[20] ^= 0x10;
    expect(!decode(ps::Model::DualShock4, ps::Transport::Bluetooth, bad).ok, "a corrupt Bluetooth report is dropped");
    Bytes no_hid = ds4_bluetooth();
    no_hid[1] = 0x40;
    seal_input(no_hid);
    expect(!decode(ps::Model::DualShock4, ps::Transport::Bluetooth, no_hid).ok, "a report without HID data is dropped");
    for (std::uint8_t id = 0x12; id <= 0x19; ++id) {
        Bytes other = ds4_bluetooth();
        other[0] = id;
        seal_input(other);
        expect(decode(ps::Model::DualShock4, ps::Transport::Bluetooth, other).ok, "report ids 0x11..0x19 decode");
    }

    // The short report before the mode switch: padded to the handle's length.
    Bytes simple(547, 0);
    simple[0] = 0x01;
    for (std::size_t i = 1; i <= 4; ++i) simple[i] = 128;
    simple[1] = 0;
    simple[5] = 0x21;  // Cross, hat NE
    simple[6] = 0x10;
    simple[8] = 200;
    const Decoded shortened = decode(ps::Model::DualShock4, ps::Transport::Bluetooth, simple);
    expect(shortened.ok && close_to(shortened.state.axes[0], -1.0f, 0.01f), "short Bluetooth report decodes sticks");
    expect(shortened.state.buttons ==
               (bit(pad::kSouthBit) | bit(pad::kDpadUpBit) | bit(pad::kDpadRightBit) | bit(pad::kBackBit)),
           "short Bluetooth report decodes buttons");
    expect(close_to(shortened.state.axes[4], 200.0f / 255.0f), "short Bluetooth report decodes L2");
    expect(shortened.state.imu_count == 0, "short Bluetooth report has no sensors");
}

void test_dualsense() {
    Bytes r = ds5_usb();
    expect(decode(ps::Model::DualSense, ps::Transport::Usb, r).ok, "DualSense USB report decodes");
    r[5] = 255; r[6] = 40;       // L2 full, R2 analog
    r[8] = 0x80 | 0x02;           // Triangle, hat E
    r[9] = 0x04 | 0x08 | 0x10;    // digital L2/R2, Create
    r[10] = 0x01 | 0x02 | 0x04 | 0x40 | 0x80;
    const Decoded dual = decode(ps::Model::DualSense, ps::Transport::Usb, r);
    expect(dual.state.buttons == (bit(pad::kNorthBit) | bit(pad::kDpadRightBit) | bit(pad::kBackBit) |
                                  bit(pad::kHomeBit) | bit(pad::kCaptureBit) | bit(pad::kChatBit)),
           "DualSense buttons: mute is Chat, paddles are an Edge feature");
    expect(close_to(dual.state.axes[4], 1.0f) && close_to(dual.state.axes[5], 40.0f / 255.0f), "DualSense triggers");
    const Decoded edge = decode(ps::Model::DualSenseEdge, ps::Transport::Usb, r);
    expect((edge.state.buttons & (bit(pad::kGripLeftBit) | bit(pad::kGripRightBit))) ==
               (bit(pad::kGripLeftBit) | bit(pad::kGripRightBit)),
           "DualSense Edge paddles are GL and GR");

    r = ds5_usb();
    r[1] = 0; r[2] = 255; r[3] = 255; r[4] = 0;
    const Decoded sticks = decode(ps::Model::DualSense, ps::Transport::Usb, r);
    expect(close_to(sticks.state.axes[0], -1.0f, 0.01f) && close_to(sticks.state.axes[1], -1.0f, 0.01f) &&
               close_to(sticks.state.axes[2], 1.0f) && close_to(sticks.state.axes[3], 1.0f),
           "DualSense sticks, Y up positive");

    r = ds5_usb();
    put16(r, 16, -800);  // gyro X
    put16(r, 26, 8192);  // accel Z
    put32(r, 28, 3000000u);
    ps::SensorClock clock;
    (void)decode(ps::Model::DualSense, ps::Transport::Usb, r, ps::ImuCal{}, clock);
    put32(r, 28, 3000000u + 12000u);  // 12000 ticks of 1/3 us = 4 ms
    const Decoded motion = decode(ps::Model::DualSense, ps::Transport::Usb, r, ps::ImuCal{}, clock);
    expect(motion.state.imu_count == 1 && close_to(motion.state.imu[0].gyro[0], -50.0f * kPi / 180.0f, 1e-3f),
           "DualSense gyro counts to rad/s");
    expect(close_to(motion.state.imu[0].accel[2], 1.0f), "DualSense accel counts to g");
    expect(close_to(motion.state.imu_dt, 0.004f, 1e-5f), "dt from the 32-bit sensor clock");

    Bytes bt = ds5_bluetooth();
    expect(decode(ps::Model::DualSense, ps::Transport::Bluetooth, bt).ok, "DualSense Bluetooth report decodes");
    bt[2] = 255; bt[9] = 0x20;
    seal_input(bt);
    const Decoded bluetooth = decode(ps::Model::DualSense, ps::Transport::Bluetooth, bt);
    expect(bluetooth.ok && close_to(bluetooth.state.axes[0], 1.0f) && (bluetooth.state.buttons & bit(pad::kSouthBit)) != 0u,
           "DualSense Bluetooth payload starts at byte 2");
    bt[30] ^= 1;
    expect(!decode(ps::Model::DualSense, ps::Transport::Bluetooth, bt).ok, "a corrupt DualSense report is dropped");

    Bytes simple(78, 0);
    simple[0] = 0x01;
    simple[1] = 255;
    simple[5] = 0x08;
    simple[6] = 0x20;
    const Decoded shortened = decode(ps::Model::DualSense, ps::Transport::Bluetooth, simple);
    expect(shortened.ok && close_to(shortened.state.axes[0], 1.0f) && (shortened.state.buttons & bit(pad::kStartBit)) != 0u,
           "DualSense short Bluetooth report");
}

Bytes calibration_feature(bool plus_first) {
    Bytes f(41, 0);
    f[0] = 0x05;
    put16(f, 1, 10);
    put16(f, 3, -20);
    put16(f, 5, 5);
    // Gyro limits at nominal sensitivity: plus - bias = 8640, bias - minus = 8640.
    const int plus[3] = {8650, 8620, 8645};
    const int minus[3] = {-8630, -8660, -8635};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (plus_first) {
            put16(f, 7 + axis * 2, plus[axis]);
            put16(f, 13 + axis * 2, minus[axis]);
        } else {
            put16(f, 7 + axis * 4, plus[axis]);
            put16(f, 9 + axis * 4, minus[axis]);
        }
    }
    put16(f, 19, 540);
    put16(f, 21, 540);
    const int accel_plus[3] = {8200, 8190, 8210};
    const int accel_minus[3] = {-8184, -8194, -8174};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        put16(f, 23 + axis * 4, accel_plus[axis]);
        put16(f, 25 + axis * 4, accel_minus[axis]);
    }
    return f;
}

void test_calibration() {
    for (const bool plus_first : {false, true}) {
        const ps::ImuCal cal = ps::parse_calibration(calibration_feature(plus_first), plus_first);
        expect(cal.hardware, "a sane calibration is used");
        expect(cal.bias[0] == 10 && cal.bias[1] == -20 && cal.bias[2] == 5, "gyro bias");
        expect(close_to(cal.scale[0], 1080.0f / 17280.0f * kPi / 180.0f, 1e-6f), "gyro scale from the speed limits");
        expect(cal.bias[3] == 8 && cal.bias[4] == -2 && cal.bias[5] == 18, "accel bias is the midpoint");
        expect(close_to(cal.scale[3], 2.0f / 16384.0f, 1e-8f), "accel scale from the 2 g range");
    }
    Bytes bad = calibration_feature(false);
    put16(bad, 1, 3000);
    const ps::ImuCal fallback = ps::parse_calibration(bad, false);
    expect(!fallback.hardware && fallback.bias[0] == 0 && fallback.scale == ps::ImuCal{}.scale,
           "a calibration with a large bias is ignored");
    Bytes skewed = calibration_feature(false);
    put16(skewed, 19, 2000);
    put16(skewed, 21, 2000);
    expect(!ps::parse_calibration(skewed, false).hardware, "a gyro scale off nominal by half is ignored");
    expect(!ps::parse_calibration(Bytes(20, 0), false).hardware, "a short calibration report is ignored");
    const ps::ImuCal nominal;
    expect(close_to(nominal.scale[0], kPi / 180.0f / 16.0f, 1e-7f) && close_to(nominal.scale[3], 1.0f / 8192.0f, 1e-9f),
           "default sensor scale");
}

bool output_crc_ok(const Bytes& r) {
    return get32(r, 74) == ps::report_crc(0xa2, std::span<const std::uint8_t>(r.data(), 74));
}

void test_output_reports() {
    Bytes out(128, 0xee);
    ps::Effects e;
    e.rumble_left = 0x80;
    e.rumble_right = 0x40;
    e.led = {1, 2, 3};
    e.apply_rumble = true;
    e.apply_led = true;
    expect(ps::build_output_report(ps::Model::DualShock4, ps::Transport::Usb, e, out) == 32, "DualShock 4 USB length");
    expect(out[0] == 0x05 && out[1] == 0x03 && out[4] == 0x40 && out[5] == 0x80 && out[6] == 1 && out[7] == 2 &&
               out[8] == 3,
           "DualShock 4 USB effects");
    expect(out[2] == 0 && out[3] == 0 && out[9] == 0 && out[31] == 0 && out[100] == 0, "the rest of the report is zero");
    e.apply_led = false;
    (void)ps::build_output_report(ps::Model::DualShock4, ps::Transport::Usb, e, out);
    expect(out[1] == 0x01, "rumble only leaves the light bar alone");
    e.apply_rumble = false;
    e.apply_led = true;
    (void)ps::build_output_report(ps::Model::DualShock4, ps::Transport::Usb, e, out);
    expect(out[1] == 0x02, "light bar only");

    e = {};
    e.rumble_left = 10;
    e.rumble_right = 20;
    e.led = {9, 8, 7};
    e.apply_rumble = e.apply_led = true;
    expect(ps::build_output_report(ps::Model::DualShock4, ps::Transport::Bluetooth, e, out) == 78,
           "DualShock 4 Bluetooth length");
    expect(out[0] == 0x11 && out[1] == 0xc4 && out[3] == 0x03 && out[6] == 20 && out[7] == 10 && out[8] == 9 &&
               out[9] == 8 && out[10] == 7,
           "DualShock 4 Bluetooth effects");
    expect(output_crc_ok(out), "DualShock 4 Bluetooth output CRC");

    e = {};
    e.rumble_left = 0xf0;
    e.rumble_right = 0x20;
    e.apply_rumble = true;
    expect(ps::build_output_report(ps::Model::DualSense, ps::Transport::Usb, e, out) == 48, "DualSense USB length");
    expect(out[0] == 0x02 && out[1] == 0x02 && out[2] == 0 && out[3] == 0x20 && out[4] == 0xf0 && out[39] == 0x04,
           "DualSense full-range rumble");
    e.full_range_rumble = false;
    (void)ps::build_output_report(ps::Model::DualSense, ps::Transport::Usb, e, out);
    expect(out[1] == 0x03 && out[3] == 0x10 && out[4] == 0x78 && out[39] == 0, "older DualSense firmware: half range");
    e = {};
    e.apply_rumble = true;
    (void)ps::build_output_report(ps::Model::DualSense, ps::Transport::Usb, e, out);
    expect(out[1] == 0x03 && out[3] == 0 && out[4] == 0 && out[39] == 0x04, "stopping is an explicit zero rumble");

    e = {};
    e.release_led = true;
    (void)ps::build_output_report(ps::Model::DualSense, ps::Transport::Usb, e, out);
    expect(out[2] == 0x08 && out[1] == 0, "release the light bar");
    e = {};
    e.led = {0x20, 0x21, 0x22};
    e.apply_led = true;
    (void)ps::build_output_report(ps::Model::DualSense, ps::Transport::Usb, e, out);
    expect(out[2] == 0x04 && out[45] == 0x20 && out[46] == 0x21 && out[47] == 0x22, "DualSense light bar colour");

    e = {};
    e.rumble_left = 5;
    e.rumble_right = 6;
    e.led = {1, 2, 3};
    e.apply_rumble = e.apply_led = true;
    expect(ps::build_output_report(ps::Model::DualSense, ps::Transport::Bluetooth, e, out) == 78,
           "DualSense Bluetooth length");
    expect(out[0] == 0x31 && out[1] == 0 && out[2] == 0x10 && out[3] == 0x02 && out[4] == 0x04 && out[5] == 6 &&
               out[6] == 5 && out[41] == 0x04 && out[47] == 1 && out[48] == 2 && out[49] == 3,
           "DualSense Bluetooth effects");
    expect(output_crc_ok(out), "DualSense Bluetooth output CRC");
    e.sequence = 0x1b;
    (void)ps::build_output_report(ps::Model::DualSense, ps::Transport::Bluetooth, e, out);
    expect(out[1] == 0xb0 && out[2] == 0x10 && output_crc_ok(out), "DualSense Bluetooth sequence in the high nibble");

    Bytes small(16, 0);
    expect(ps::build_output_report(ps::Model::DualSense, ps::Transport::Usb, e, small) == 0, "a short buffer is refused");

    Bytes firmware(64, 0);
    expect(ps::dualsense_firmware(firmware) == 0, "unknown firmware");
    put16(firmware, 44, 0x0224);
    expect(ps::dualsense_firmware(firmware) == 0x0224, "firmware version");
    expect(ps::dualsense_full_range_rumble(ps::Model::DualSense, 0x0224) &&
               !ps::dualsense_full_range_rumble(ps::Model::DualSense, 0x0223) &&
               ps::dualsense_full_range_rumble(ps::Model::DualSenseEdge, 0x0100) &&
               ps::dualsense_full_range_rumble(ps::Model::DualSense, 0),
           "full-range rumble needs firmware 2.24, the Edge, or an unknown version");
}

void test_products() {
    expect(ps::model_for_product(0x054c, 0x05c4) == ps::Model::DualShock4, "DualShock 4");
    expect(ps::model_for_product(0x054c, 0x09cc) == ps::Model::DualShock4, "DualShock 4 (2016)");
    expect(ps::model_for_product(0x054c, 0x0ce6) == ps::Model::DualSense, "DualSense");
    expect(ps::model_for_product(0x054c, 0x0df2) == ps::Model::DualSenseEdge, "DualSense Edge");
    expect(!ps::model_for_product(0x057e, 0x0ce6).has_value() && !ps::model_for_product(0x054c, 0x0268).has_value(),
           "other vendors and the DualShock 3 are not claimed");
    expect(std::strcmp(ps::model_name(ps::Model::DualSenseEdge), "DualSense Edge") == 0, "model names");
}

// Steam Controller state report: 0x42 and 0x45 share the pad block, 0x47 has a
// 16-bit trackpad and sensor clock. Offsets from the report start (id at 0).
Bytes steam_report(std::uint8_t id) {
    Bytes r(id == 0x42 ? 54 : 46, 0);
    r[0] = id;
    return r;
}

void test_steam() {
    Bytes r = steam_report(0x45);
    r[1] = 7;
    const std::uint32_t word = sv::button::kA | sv::button::kMenu | sv::button::kView | sv::button::kLeftBumper |
                               sv::button::kDpadUp | sv::button::kSteam | sv::button::kQuickAccess |
                               sv::button::kLeftGrip1 | sv::button::kRightGrip2 | sv::button::kRightPadClick;
    put32(r, 2, word);
    sv::SensorClock clock;
    switch_hid::DecodedState state;
    expect(sv::decode_report(r, clock, state) && state.normalized, "Steam Controller state report decodes");
    expect(state.buttons == (bit(pad::kSouthBit) | bit(pad::kStartBit) | bit(pad::kBackBit) |
                             bit(pad::kLeftShoulderBit) | bit(pad::kDpadUpBit) | bit(pad::kHomeBit) |
                             bit(pad::kCaptureBit) | bit(pad::kGripLeftBit) | bit(pad::kGripRight2Bit) |
                             bit(pad::kRightPadBit)),
           "Steam buttons map to the unified bits");
    put32(r, 2, sv::button::kB | sv::button::kX | sv::button::kY | sv::button::kRightBumper | sv::button::kLeftStick |
                    sv::button::kRightStick | sv::button::kDpadDown | sv::button::kDpadLeft | sv::button::kDpadRight |
                    sv::button::kRightGrip1 | sv::button::kLeftGrip2 | sv::button::kLeftPadClick);
    expect(sv::decode_report(r, clock, state), "second state report");
    expect(state.buttons == (bit(pad::kEastBit) | bit(pad::kWestBit) | bit(pad::kNorthBit) |
                             bit(pad::kRightShoulderBit) | bit(pad::kLeftStickBit) | bit(pad::kRightStickBit) |
                             bit(pad::kDpadDownBit) | bit(pad::kDpadLeftBit) | bit(pad::kDpadRightBit) |
                             bit(pad::kGripRightBit) | bit(pad::kGripLeft2Bit) | bit(pad::kLeftPadBit)),
           "the other Steam buttons");

    put32(r, 2, 0);
    put16(r, 6, 16384);    // left trigger half
    put16(r, 8, -300);     // a negative trigger reading is released
    put16(r, 10, 32767);   // left stick right
    put16(r, 12, -32768);  // left stick down
    put16(r, 14, -16384);  // right stick left half
    put16(r, 16, 16384);   // right stick up half
    expect(sv::decode_report(r, clock, state), "axes report");
    expect(close_to(state.axes[4], 0.5f, 1e-3f) && state.axes[5] == 0.0f, "Steam triggers");
    expect(close_to(state.axes[0], 1.0f) && close_to(state.axes[1], -1.0f) && close_to(state.axes[2], -0.5f, 1e-3f) &&
               close_to(state.axes[3], 0.5f, 1e-3f),
           "Steam sticks, Y up positive");
    expect(state.imu_count == 0, "no sensor data before the IMU is enabled");

    // Sensors: 0x45 and 0x42 carry a 32-bit microsecond clock.
    put32(r, 30, 1000);
    put16(r, 34, 16384);  // accel X: 1 g
    put16(r, 36, 16384);  // accel Y: maps to -Z
    put16(r, 38, 16384);  // accel Z: maps to Y
    put16(r, 40, 16384);  // gyro X: 1000 deg/s
    put16(r, 42, 8192);   // gyro Y: maps to -Z
    put16(r, 44, -8192);  // gyro Z: maps to Y
    sv::SensorClock fresh;
    expect(sv::decode_report(r, fresh, state) && state.imu_count == 1, "sensor sample");
    expect(close_to(state.imu[0].accel[0], 1.0f) && close_to(state.imu[0].accel[1], 1.0f) && close_to(state.imu[0].accel[2], -1.0f),
           "Steam accel axes");
    const float rate = 1000.0f * kPi / 180.0f;
    expect(close_to(state.imu[0].gyro[0], rate, 1e-2f) && close_to(state.imu[0].gyro[1], -0.5f * rate, 1e-2f) &&
               close_to(state.imu[0].gyro[2], -0.5f * rate, 1e-2f),
           "Steam gyro axes");
    expect(state.imu_dt == 0.0f, "no dt on the first report");
    put32(r, 30, 5000);
    expect(sv::decode_report(r, fresh, state) && close_to(state.imu_dt, 0.004f, 1e-6f), "dt from the microsecond clock");

    Bytes usb = steam_report(0x42);
    put16(usb, 10, 20000);
    put32(usb, 30, 0xfffffff0u);
    put16(usb, 34, 100);
    sv::SensorClock usb_clock;
    expect(sv::decode_report(usb, usb_clock, state) && close_to(state.axes[0], 20000.0f / 32767.0f), "0x42 report decodes");
    put32(usb, 30, 0x00000f90u);
    expect(sv::decode_report(usb, usb_clock, state) && close_to(state.imu_dt, 0.004f, 1e-6f), "the 32-bit clock wraps");

    Bytes shortclock = steam_report(0x47);
    put16(shortclock, 32, 100);
    put16(shortclock, 34, 50);
    sv::SensorClock short_state;
    expect(sv::decode_report(shortclock, short_state, state), "0x47 report decodes");
    put16(shortclock, 32, 225);  // 125 ticks of 32 us
    expect(sv::decode_report(shortclock, short_state, state) && close_to(state.imu_dt, 0.004f, 1e-6f),
           "dt from the 16-bit clock");

    Bytes battery(15, 0);
    battery[0] = 0x43;
    expect(!sv::decode_report(battery, clock, state), "other reports are not state");
    expect(!sv::decode_report(Bytes(20, 0), clock, state), "a short report is not state");
    Bytes wrong = r;
    wrong[0] = 0x44;
    expect(!sv::decode_report(wrong, clock, state), "an unknown id is not state");

    Bytes wireless{0x79, 2};
    expect(sv::decode_wireless_event(wireless) == sv::WirelessEvent::Connected, "puck connect");
    wireless[1] = 1;
    expect(sv::decode_wireless_event(wireless) == sv::WirelessEvent::Disconnected, "puck disconnect");
    wireless[1] = 3;
    expect(sv::decode_wireless_event(wireless) == sv::WirelessEvent::None, "pairing is no connection change");
    expect(sv::decode_wireless_event(Bytes{0x46, 2}) == sv::WirelessEvent::Connected, "puck status 0x46");
    expect(sv::decode_wireless_event(Bytes{0x42, 2}) == sv::WirelessEvent::None, "not a wireless report");

    Bytes feature(64, 0xee);
    expect(sv::build_settings_report(feature) == 64, "settings report length");
    const Bytes expected_feature = [] {
        Bytes f(64, 0);
        const std::uint8_t head[] = {1, 0x87, 9, 9, 0, 0, 71, 0, 0, 48, 0x18, 0x00};
        std::memcpy(f.data(), head, sizeof(head));
        return f;
    }();
    expect(feature == expected_feature, "settings: lizard mode off, watchdog off, raw accel and gyro");
    expect(sv::build_settings_report(std::span<std::uint8_t>(feature.data(), 10)) == 0, "short settings buffer");

    Bytes rumble(16, 0xee);
    expect(sv::build_rumble_report(0x1234, 0xabcd, rumble) == 10, "rumble report length");
    const std::uint8_t expected_rumble[10] = {0x80, 0, 0, 0, 0x34, 0x12, 0, 0xcd, 0xab, 0};
    expect(std::memcmp(rumble.data(), expected_rumble, 10) == 0, "rumble report bytes");
    expect(rumble[10] == 0, "the rest of the buffer is cleared");
}

// Valve's own reports (the Deck, the 2015 controller) as read: report number 0, the version-1 header (version, message,
// length), then the payload from byte 5.
Bytes valve_report(std::uint8_t message, std::uint8_t length) {
    Bytes r(65, 0);
    r[1] = 1;
    r[3] = message;
    r[4] = length;
    return r;
}

void test_steam_deck() {
    expect(sv::model_of_product(0x1205) == sv::Model::Deck && sv::model_of_product(0x1102) == sv::Model::Classic &&
               sv::model_of_product(0x1142) == sv::Model::Classic && sv::model_of_product(0x1302) == sv::Model::Controller &&
               sv::model_of_product(0x1304) == sv::Model::Controller && !sv::model_of_product(0x1234).has_value(),
           "Valve products by model");
    expect(sv::is_receiver_product(0x1142) && sv::is_receiver_product(0x1305) && !sv::is_receiver_product(0x1102) &&
               !sv::is_receiver_product(0x1205),
           "receivers: the pucks and the 2015 receiver");
    expect(sv::is_controller_interface(0x1205, 2) && !sv::is_controller_interface(0x1205, 0) &&
               !sv::is_controller_interface(0x1205, 1) && sv::is_controller_interface(0x1102, 2) &&
               !sv::is_controller_interface(0x1102, 1) && sv::is_controller_interface(0x1142, 1) &&
               sv::is_controller_interface(0x1142, 4) && !sv::is_controller_interface(0x1142, 0) &&
               !sv::is_controller_interface(0x1142, 5) && sv::is_controller_interface(0x1304, 2) &&
               !sv::is_controller_interface(0x1304, 1),
           "only the controller interfaces (not the keyboard and mouse ones)");

    Bytes r = valve_report(9, 64);
    put32(r, 9, 0x00000080u | 0x00000004u | 0x00001000u | 0x00004000u | 0x00002000u | 0x00000100u | 0x00008000u |
                    0x00020000u | 0x04000000u);  // A, R, View, Menu, Steam, Up, L5, left pad click, R3
    put32(r, 13, 0x00000200u | 0x00040000u);     // L4, quick access
    put16(r, 49, 32767);    // left stick right
    put16(r, 51, 16384);    // left stick up half
    put16(r, 53, -32768);   // right stick left
    put16(r, 55, -16384);   // right stick down half
    put16(r, 45, 16384);    // left trigger half
    put16(r, 47, 32767);    // right trigger full
    put16(r, 25, 16384);    // accel X: 1 g
    put16(r, 27, 16384);    // accel Y: maps to -Z
    put16(r, 29, -16384);   // accel Z: maps to Y
    put16(r, 31, 16384);    // gyro X: 1000 deg/s
    put16(r, 33, 8192);     // gyro Y: maps to -Z
    put16(r, 35, -8192);    // gyro Z: maps to Y
    switch_hid::DecodedState state;
    expect(sv::decode_deck_report(r, state) && state.normalized, "Deck state decodes");
    expect(state.buttons == (bit(pad::kSouthBit) | bit(pad::kRightShoulderBit) | bit(pad::kBackBit) |
                             bit(pad::kStartBit) | bit(pad::kHomeBit) | bit(pad::kDpadUpBit) |
                             bit(pad::kGripLeft2Bit) | bit(pad::kLeftPadBit) | bit(pad::kRightStickBit) |
                             bit(pad::kGripLeftBit) | bit(pad::kCaptureBit)),
           "Deck buttons: as the Steam family's (L4 GL, L5 GL2, quick access Capture)");
    expect(close_to(state.axes[0], 1.0f) && close_to(state.axes[1], 0.5f, 1e-3f) && close_to(state.axes[2], -1.0f) &&
               close_to(state.axes[3], -0.5f, 1e-3f),
           "Deck sticks, Y up positive");
    expect(close_to(state.axes[4], 0.5f, 1e-3f) && close_to(state.axes[5], 1.0f), "Deck triggers");
    const float rate = 1000.0f * kPi / 180.0f;
    expect(state.imu_count == 1 && close_to(state.imu[0].accel[0], 1.0f) && close_to(state.imu[0].accel[1], -1.0f) &&
               close_to(state.imu[0].accel[2], -1.0f) && close_to(state.imu[0].gyro[0], rate, 1e-2f) &&
               close_to(state.imu[0].gyro[1], -0.5f * rate, 1e-2f) && close_to(state.imu[0].gyro[2], -0.5f * rate, 1e-2f),
           "Deck sensors: (x, z, -y)");
    const Bytes bare(r.begin() + 1, r.end());
    expect(sv::decode_deck_report(bare, state) && close_to(state.axes[0], 1.0f), "a report without its number");
    Bytes other = r;
    other[3] = 1;
    expect(!sv::decode_deck_report(other, state), "another message is not the Deck's state");
    expect(!sv::decode_deck_report(Bytes(40, 0), state), "a short report is not state");

    Bytes feature(65, 0xee);
    expect(sv::build_clear_mappings_report(feature) == 65 && feature[0] == 0 && feature[1] == 0x81 && feature[2] == 0 &&
               feature[64] == 0,
           "keyboard and mouse buttons off");
    expect(sv::build_valve_settings_report(sv::Model::Deck, feature) == 65, "Deck settings length");
    const std::uint8_t deck_settings[] = {0, 0x87, 15, 24, 0, 0, 7, 7, 0, 8, 7, 0, 52, 0xff, 0xff, 53, 0xff, 0xff, 0};
    expect(std::memcmp(feature.data(), deck_settings, sizeof(deck_settings)) == 0,
           "Deck: smooth mouse off, both pads no mouse, no pressure clicks");
    expect(sv::build_deck_watchdog_report(feature) == 65 && feature[1] == 0x87 && feature[2] == 3 && feature[3] == 8 &&
               feature[4] == 7 && feature[5] == 0 && feature[6] == 0,
           "Deck watchdog: the right pad no mouse");
    expect(sv::build_deck_rumble_report(0x1234, 0xabcd, feature) == 65, "Deck rumble length");
    const std::uint8_t deck_rumble[] = {0, 0xeb, 0, 0, 0, 0, 0x34, 0x12, 0xcd, 0xab, 2, 0, 0};
    expect(std::memcmp(feature.data(), deck_rumble, sizeof(deck_rumble)) == 0, "Deck rumble bytes");
    expect(sv::build_valve_settings_report(sv::Model::Controller, feature) == 0, "the 2026 controller has its own");
    expect(sv::build_clear_mappings_report(std::span<std::uint8_t>(feature.data(), 64)) == 0, "a short feature buffer");
}

void test_steam_input_list() {
    const char* list = "0x28de/0x1205,0x045E/0x028E, 0x057e/0x2009";
    expect(sv::listed_device(list, 0x28de, 0x1205) && sv::listed_device(list, 0x045e, 0x028e) &&
               sv::listed_device(list, 0x057e, 0x2009),
           "Steam Input's devices, any case, spaces after commas");
    expect(!sv::listed_device(list, 0x28de, 0x1102) && !sv::listed_device(list, 0x1205, 0x28de) &&
               !sv::listed_device("", 0x28de, 0x1205) && !sv::listed_device("0x28de", 0x28de, 0x1205) &&
               !sv::listed_device("garbage,,/", 0x28de, 0x1205),
           "others, an empty list, a vendor alone, garbage");
}

void test_steam_classic() {
    Bytes r = valve_report(1, 60);
    put32(r, 5, 1);                   // report number
    put32(r, 9, 0x00000080u | 0x00000008u | 0x00008000u | 0x00001000u | 0x00004000u);  // A, LB, left grip, Back, Start
    r[12] = 100;                      // left trigger byte
    r[13] = 255;                      // right trigger byte
    put16(r, 17, 20000);              // the shared axes: the stick (no finger on the pad)
    put16(r, 19, -10000);
    sv::ClassicState classic;
    switch_hid::DecodedState state;
    expect(sv::decode_classic_report(r, classic, state) && state.normalized, "2015 controller state decodes");
    expect(state.buttons == (bit(pad::kSouthBit) | bit(pad::kLeftShoulderBit) | bit(pad::kGripLeftBit) |
                             bit(pad::kBackBit) | bit(pad::kStartBit)),
           "2015 buttons");
    expect(close_to(state.axes[0], 20000.0f / 32767.0f) && close_to(state.axes[1], -10000.0f / 32767.0f),
           "the stick, Y up positive");
    expect(close_to(state.axes[4], ((100 << 7) | 100) / 26000.0f) && close_to(state.axes[5], 1.0f),
           "triggers over their travel");
    expect(!sv::decode_classic_report(r, classic, state), "a repeated report is no new state");

    // A finger on the left pad: the shared axes are the pad's; with the stick in use too, the stick keeps its last.
    put32(r, 5, 2);
    put32(r, 9, 0x00080000u | 0x00800000u);
    put16(r, 17, 5000);
    put16(r, 19, 5000);
    expect(sv::decode_classic_report(r, classic, state) && close_to(state.axes[0], 20000.0f / 32767.0f) &&
               close_to(state.axes[1], -10000.0f / 32767.0f),
           "pad and stick both in use: the stick keeps its values");
    put32(r, 5, 3);
    put32(r, 9, 0x00080000u);
    expect(sv::decode_classic_report(r, classic, state) && state.axes[0] == 0.0f && state.axes[1] == 0.0f,
           "the pad alone: the stick is centred");

    // An older firmware's stick click comes as the pad's while the pad is not in use.
    put32(r, 5, 4);
    put32(r, 9, 0x00020000u);
    expect(sv::decode_classic_report(r, classic, state) && state.buttons == bit(pad::kLeftStickBit),
           "the stick's click from the pad's");
    put32(r, 5, 5);
    put32(r, 9, 0x00020000u | 0x00080000u);
    expect(sv::decode_classic_report(r, classic, state) && state.buttons == bit(pad::kLeftPadBit),
           "the pad's click with a finger on it");

    // The right pad as the right stick, turned 15 degrees back; its click as the right stick's.
    put32(r, 5, 6);
    put32(r, 9, 0x00100000u | 0x00040000u);
    put16(r, 21, 10000);
    put16(r, 23, 0);
    expect(sv::decode_classic_report(r, classic, state) && state.buttons == bit(pad::kRightStickBit) &&
               close_to(state.axes[2], 0.96592583f * 10000.0f / 32767.0f, 2e-4f) &&
               close_to(state.axes[3], 0.25881905f * 10000.0f / 32767.0f, 2e-4f),
           "the right pad, turned level");
    put32(r, 5, 7);
    put32(r, 9, 0);
    expect(sv::decode_classic_report(r, classic, state) && state.axes[2] == 0.0f && state.axes[3] == 0.0f,
           "no finger on the right pad: centred");

    // Sensors once on: accel (x, z, -y), gyro (x, z, y) as SDL reads this controller.
    put32(r, 5, 8);
    put16(r, 29, 16384);
    put16(r, 31, 16384);
    put16(r, 33, -16384);
    put16(r, 35, 16384);
    put16(r, 37, 8192);
    put16(r, 39, -8192);
    const float rate = 1000.0f * kPi / 180.0f;
    expect(sv::decode_classic_report(r, classic, state) && state.imu_count == 1 &&
               close_to(state.imu[0].accel[0], 1.0f) && close_to(state.imu[0].accel[1], -1.0f) &&
               close_to(state.imu[0].accel[2], -1.0f) && close_to(state.imu[0].gyro[0], rate, 1e-2f) &&
               close_to(state.imu[0].gyro[1], -0.5f * rate, 1e-2f) && close_to(state.imu[0].gyro[2], 0.5f * rate, 1e-2f),
           "2015 sensors");

    Bytes event = valve_report(3, 1);
    event[5] = 2;
    expect(sv::decode_classic_wireless_event(event) == sv::WirelessEvent::Connected, "receiver: connected");
    event[5] = 3;
    expect(sv::decode_classic_wireless_event(event) == sv::WirelessEvent::Connected, "receiver: newly paired");
    event[5] = 1;
    expect(sv::decode_classic_wireless_event(event) == sv::WirelessEvent::Disconnected, "receiver: gone");
    const Bytes bare(event.begin() + 1, event.end());
    expect(sv::decode_classic_wireless_event(bare) == sv::WirelessEvent::Disconnected, "an event without its number");
    expect(sv::decode_classic_wireless_event(r) == sv::WirelessEvent::None, "state is no wireless event");

    Bytes feature(65, 0xee);
    expect(sv::build_valve_settings_report(sv::Model::Classic, feature) == 65, "2015 settings length");
    const std::uint8_t classic_settings[] = {0, 0x87, 15, 49, 2, 0, 7, 7, 0, 8, 7, 0, 24, 0, 0, 48, 0x18, 0, 0};
    expect(std::memcmp(feature.data(), classic_settings, sizeof(classic_settings)) == 0,
           "2015: packet version 2, both pads no mouse, smooth mouse off, raw accel and gyro");
    expect(sv::build_classic_haptic_report(1, 2000, feature) == 65, "haptic pulse length");
    const std::uint8_t pulse[] = {0, 0x8f, 10, 1, 0xd0, 0x07, 0xb8, 0x0b, 12, 0, 0, 0, 0, 0};
    expect(std::memcmp(feature.data(), pulse, sizeof(pulse)) == 0, "haptic pulse: pad 1, 2 ms on, 3 ms off, 12 times");
    expect(sv::build_classic_haptic_report(0, 0, feature) == 0 && sv::build_classic_haptic_report(0, 5000, feature) == 0,
           "a pulse is on for part of its period");
}

void test_pad_sample() {
    Bytes r = ds4_usb();
    r[1] = 255;
    r[2] = 128;
    r[8] = 64;
    const Decoded decoded = decode(ps::Model::DualShock4, ps::Transport::Usb, r);
    const PadSample sample = switch_hid::pad_sample_from(decoded.state, ControlScheme::PlayStation, {}, {}, 0.1f, 0.2f);
    expect(sample.connected && sample.scheme == ControlScheme::PlayStation, "pad sample scheme");
    expect(close_to(sample.axes[0], 1.0f) && sample.axes[1] == 0.0f, "dead zone applies to the normalised sticks");
    expect(close_to(sample.axes[4], 64.0f / 255.0f) && sample.axes[5] == 0.0f, "triggers pass through");
    r[3] = 128 + 15;  // 11.8 percent: inside the 0.2 pointer dead zone
    const Decoded small = decode(ps::Model::DualShock4, ps::Transport::Usb, r);
    expect(switch_hid::pad_sample_from(small.state, ControlScheme::PlayStation, {}, {}, 0.1f, 0.2f).axes[2] == 0.0f,
           "the pointer stick has its own dead zone");
}

ControllerInfo controller(ControlScheme scheme, std::uint8_t slot, ControllerLink link, const char* model) {
    ControllerInfo info;
    info.scheme = scheme;
    info.slot = slot;
    info.link = link;
    info.model = model;
    return info;
}

std::string label_of(const DeviceList& list, std::size_t index) {
    return list.entries[index].label.data();
}

void test_device_list() {
    DeviceList list;
    build_device_list({}, std::nullopt, list);
    expect(list.count == 1 && label_of(list, 0) == "Keyboard and mouse", "no controllers: keyboard and mouse only");
    expect(list.entries[0].key.scheme == ControlScheme::Keyboard && !list.entries[0].active, "keyboard entry");

    const std::array<ControllerInfo, 4> mixed{
        controller(ControlScheme::PlayStation, 1, ControllerLink::Usb, "DualSense"),
        controller(ControlScheme::Xbox, 2, ControllerLink::Unknown, "Xbox controller"),
        controller(ControlScheme::PlayStation, 0, ControllerLink::Bluetooth, "DualSense"),
        controller(ControlScheme::Steam, 0, ControllerLink::Wireless, "Steam Controller"),
    };
    build_device_list(mixed, DeviceKey{ControlScheme::PlayStation, 1}, list);
    expect(list.count == 5, "keyboard plus four controllers");
    expect(label_of(list, 1) == "Xbox controller", "Xbox first, no link named");
    expect(label_of(list, 2) == "DualSense 1 (Bluetooth)" && label_of(list, 3) == "DualSense 2 (USB)",
           "two controllers of one model are numbered by slot");
    expect(label_of(list, 4) == "Steam Controller (wireless)", "wireless link");
    expect(!list.entries[0].active && !list.entries[1].active && !list.entries[2].active && list.entries[3].active &&
               !list.entries[4].active,
           "the active controller is marked");
    expect(list.entries[3].key == DeviceKey{ControlScheme::PlayStation, 1}, "entries carry scheme and slot");

    build_device_list(mixed, DeviceKey{ControlScheme::Keyboard, kKeyboardSlot}, list);
    expect(list.entries[0].active && !list.entries[1].active, "the keyboard can be the active device");

    // Hot plug.
    build_device_list(mixed, std::nullopt, list);
    const DeviceKey editing{ControlScheme::PlayStation, 1};
    expect(resolve_device(list, editing) == 3, "the edited controller keeps its entry");
    const std::array<ControllerInfo, 1> remaining{controller(ControlScheme::PlayStation, 0, ControllerLink::Usb, "DualShock 4")};
    build_device_list(remaining, std::nullopt, list);
    expect(resolve_device(list, editing) == 1, "another controller of the family takes over the edit");
    build_device_list({}, std::nullopt, list);
    expect(resolve_device(list, editing) == 0, "no controller left: back to the keyboard");
    expect(resolve_device(list, DeviceKey{}) == 0, "the keyboard resolves to itself");

    // Different models of one family are not numbered.
    const std::array<ControllerInfo, 2> family{
        controller(ControlScheme::PlayStation, 0, ControllerLink::Usb, "DualShock 4"),
        controller(ControlScheme::PlayStation, 1, ControllerLink::Usb, "DualSense Edge"),
    };
    build_device_list(family, std::nullopt, list);
    expect(label_of(list, 1) == "DualShock 4 (USB)" && label_of(list, 2) == "DualSense Edge (USB)",
           "models of one family keep their names");

    std::array<ControllerInfo, 16> many{};
    for (std::size_t i = 0; i < many.size(); ++i) {
        many[i] = controller(ControlScheme::SwitchPro, static_cast<std::uint8_t>(i), ControllerLink::Usb, "Switch Pro Controller");
    }
    build_device_list(many, std::nullopt, list);
    expect(list.count == kMaxControllers + 1, "a long list is cut at its capacity");
    expect(label_of(list, kMaxControllers) == "Switch Pro Controller 12 (USB)", "numbering stays within the list");

    ControllerInfo undriven = controller(ControlScheme::SwitchPro2, 0, ControllerLink::Usb, "Switch 2 Pro Controller");
    undriven.note = "needs WinUSB";
    build_device_list(std::span<const ControllerInfo>(&undriven, 1), std::nullopt, list);
    expect(label_of(list, 1) == "Switch 2 Pro Controller (USB) - needs WinUSB", "a note follows the name");
}

}  // namespace

// A two-stick pad as cheap USB controllers describe it: X/Y/Z/Rz 0..255,
// hat 0..7 (8 = centred), 12 buttons.
gh::Layout usb_pad_layout() {
    gh::Layout layout{};
    for (const gh::Axis axis : {gh::Axis::X, gh::Axis::Y, gh::Axis::Z, gh::Axis::Rz}) {
        layout.axes[static_cast<std::size_t>(axis)] = {true, 0, 255, 8};
    }
    layout.hat = {true, 0, 7, 4};
    layout.buttons = 12;
    return layout;
}

void test_generic() {
    expect(gh::is_game_controller(0x01, 0x05) && gh::is_game_controller(0x01, 0x04), "game pad and joystick collections");
    expect(!gh::is_game_controller(0x01, 0x02) && !gh::is_game_controller(0x0c, 0x01), "mice and consumer controls are not");
    expect(gh::claimed_elsewhere(0x045e, L"hid#vid_045e&pid_02ff#7"), "Microsoft pads are XInput's");
    expect(gh::claimed_elsewhere(0x0e6f, L"hid#vid_0e6f&pid_02a1&IG_00#8"), "XInput interfaces are skipped");
    expect(gh::claimed_elsewhere(0x054c, L"x") && gh::claimed_elsewhere(0x28de, L"x"), "Sony and Valve have their own paths");
    expect(!gh::claimed_elsewhere(0x0079, L"hid#vid_0079&pid_0006#7"), "an unknown pad is generic");

    expect(gh::button_bit(1) == static_cast<int>(pad::kSouthBit) && gh::button_bit(2) == static_cast<int>(pad::kEastBit),
           "buttons 1 and 2 are South and East");
    expect(gh::button_bit(9) == static_cast<int>(pad::kBackBit) && gh::button_bit(10) == static_cast<int>(pad::kStartBit),
           "buttons 9 and 10 are Back and Start");
    expect(gh::button_bit(0) == -1 && gh::button_bit(20) == -1, "button 0 and buttons past 19 have no bit");
    std::uint32_t seen = 0;
    for (unsigned n = 1; n <= gh::kMappedButtons; ++n) seen |= 1u << static_cast<unsigned>(gh::button_bit(n));
    expect(seen == (((1u << pad::kButtonBitCount) - 1u) & ~0xfu), "buttons 1..19 cover every unified bit but the d-pad");

    const gh::Layout layout = usb_pad_layout();
    gh::Raw raw{};
    raw.axes = {128, 128, 128, 0, 0, 128, 0, 0};
    raw.hat = 8;
    switch_hid::DecodedState state{};
    gh::map_report(layout, raw, state);
    expect(state.normalized && state.buttons == 0u, "neutral generic pad");
    for (std::size_t i = 0; i < 4; ++i) expect(close_to(state.axes[i], 0.0f, 0.01f), "sticks centre at the middle of the range");
    expect(state.axes[4] == 0.0f && state.axes[5] == 0.0f, "no trigger axes: triggers rest");

    raw.axes[0] = 255;  // X right
    raw.axes[1] = 0;    // Y up
    raw.axes[2] = 0;    // Z: right stick left
    raw.axes[5] = 255;  // Rz: right stick down
    raw.hat = 1;        // north-east
    raw.buttons = (1u << 0) | (1u << 6) | (1u << 11);  // 1, 7, 12
    gh::map_report(layout, raw, state);
    expect(close_to(state.axes[0], 1.0f) && close_to(state.axes[1], 1.0f), "X right is +1, Y up is +1");
    expect(close_to(state.axes[2], -1.0f) && close_to(state.axes[3], -1.0f), "Z/Rz drive the right stick");
    expect(state.buttons == (bit(pad::kDpadUpBit) | bit(pad::kDpadRightBit) | bit(pad::kSouthBit) |
                             bit(pad::kGripLeftBit) | bit(pad::kRightStickBit)),
           "hat diagonal and buttons 1, 7, 12");

    gh::Layout xbox_like{};
    for (const gh::Axis axis : {gh::Axis::X, gh::Axis::Y, gh::Axis::Rx, gh::Axis::Ry}) {
        xbox_like.axes[static_cast<std::size_t>(axis)] = {true, -32768, 32767, 16};
    }
    xbox_like.axes[static_cast<std::size_t>(gh::Axis::Z)] = {true, 0, 1023, 10};
    gh::Raw signed_raw{};
    signed_raw.axes[3] = 0x8000u;  // Rx -32768 as the 16-bit field
    signed_raw.axes[4] = 0x7fffu;  // Ry +32767
    signed_raw.axes[2] = 1023u;    // Z pulled
    gh::map_report(xbox_like, signed_raw, state);
    expect(close_to(state.axes[2], -1.0f) && close_to(state.axes[3], -1.0f), "signed Rx/Ry drive the right stick");
    expect(close_to(state.axes[4], 1.0f) && state.axes[5] == 0.0f, "Z is the left trigger when Rx/Ry are the stick");
    expect(gh::logical_value({true, -128, 127, 8}, 0xffu) == -1, "8-bit field 0xff is -1 in a signed range");
    expect(gh::logical_value({true, 0, 255, 8}, 0xffu) == 255, "unsigned ranges stay unsigned");

    gh::Layout four_way{};
    four_way.hat = {true, 1, 4, 4};
    gh::Raw hat_raw{};
    hat_raw.hat = 3;  // third of four: south
    gh::map_report(four_way, hat_raw, state);
    expect(state.buttons == bit(pad::kDpadDownBit), "a four-position hat from 1: 3 is down");
    hat_raw.hat = 0;
    gh::map_report(four_way, hat_raw, state);
    expect(state.buttons == 0u, "a hat value below its range is centred");

    const PadSample sample = switch_hid::pad_sample_from(state, ControlScheme::Generic, {}, {}, 0.15f, 0.15f);
    expect(sample.scheme == ControlScheme::Generic && sample.connected, "generic pads become pad samples");

    // Bindings: the scheme, its names and defaults.
    expect(std::string(control_scheme_id(ControlScheme::Generic)) == "generic", "scheme id");
    expect(parse_control_scheme("generic") == ControlScheme::Generic, "scheme id parses");
    expect(format_input_source(ControlScheme::Generic, {SourceKind::Button, static_cast<std::uint8_t>(pad::kSouthBit)}) ==
               "Button1",
           "South is Button1 on a generic pad");
    const auto parsed = parse_input_source(ControlScheme::Generic, "Button8");
    expect(parsed && parsed->kind == SourceKind::Button && parsed->code == pad::kGripRightBit, "Button8 parses");
    const BindingTable defaults = default_binding_table(ControlScheme::Generic);
    expect(defaults[GameInput::A][0] == InputSource{SourceKind::Button, static_cast<std::uint8_t>(pad::kSouthBit)},
           "button 1 jumps");
    expect(defaults[GameInput::Z][1] == InputSource{SourceKind::Button, static_cast<std::uint8_t>(pad::kGripLeftBit)},
           "button 7 crouches");
    expect(legacy_binding_table(ControlScheme::Generic) == defaults &&
               version3_binding_table(ControlScheme::Generic) == defaults,
           "migration never changes generic bindings");
    for (std::size_t t = 0; t < kGameInputCount; ++t) {
        for (const InputSource source : defaults.targets[t]) {
            if (!source.empty()) {
                expect(binding_source_allowed(ControlScheme::Generic, static_cast<GameInput>(t), source),
                       "every generic default is allowed");
            }
        }
    }
    expect(!scheme_has_motion(ControlScheme::Generic) && scheme_uses_hid(ControlScheme::Generic),
           "generic pads: HID, no motion");
}

// The trackpads: where a finger is (0 to 1 across, 0 to 1 down), and when it is lifted.
void test_switch2_family() {
    namespace sw = galaxy::input::switch_hid;
    namespace rb = galaxy::input::rumble;
    expect(sw::joycon_side_of_product(sw::kJoyCon2LeftProductId) == sw::JoyConSide::Left &&
               sw::joycon_side_of_product(sw::kJoyCon2RightProductId) == sw::JoyConSide::Right &&
               sw::joycon_side_of_product(sw::kGameCubeProductId) == sw::JoyConSide::None,
           "Joy-Con 2 ids name their sides");

    Bytes r(64, 0);
    r[0] = 0x05;
    r[6] = 0x40;  // C
    sw::Switch2ImuState imu;
    sw::DecodedState state;
    expect(sw::decode_switch2_report(r, imu, state) && (state.buttons & bit(pad::kChatBit)) != 0u, "report 0x05: the C button");
    sw::DecodedState half;
    half.buttons = bit(pad::kChatBit);
    expect((sw::combine_joycons({}, half).buttons & bit(pad::kChatBit)) != 0u, "a Joy-Con 2 pair keeps the right half's C");

    r[61] = 20;  // left at rest above its zero of 16
    r[62] = 200;
    sw::apply_gamecube_triggers(r, {16u, 16u}, state);
    expect(!state.left_trigger && state.right_trigger, "GameCube analog triggers past half way, from their own rest");
    sw::DecodedState clicked;
    clicked.left_trigger = true;
    r[61] = 0;
    sw::apply_gamecube_triggers(r, {16u, 16u}, clicked);
    expect(clicked.left_trigger, "the digital bit stays");

    Bytes joycon(64, 0xcc);
    expect(rb::build_joycon2_rumble_report(3, rb::kLevelSteps, 100, joycon) == 64u && joycon[0] == 0x01u && joycon[1] == 0x53u &&
               joycon[2] != 0u && joycon[0x11] == 0u && joycon[0x13] == 0u,
           "Joy-Con 2 vibration: report 0x01, one motor");
    Bytes pro(64, 0);
    expect(rb::build_switch2_rumble_report(3, rb::kLevelSteps, 100, pro) == 64u && pro[0] == 0x02u && pro[0x12] == pro[2],
           "Pro 2 vibration: report 0x02, two motors");
    Bytes cube(64, 0xcc);
    std::uint16_t error = 0;
    int pulses = 0;
    for (int i = 0; i < 32; ++i) {
        expect(rb::build_gamecube_rumble_report(static_cast<std::uint8_t>(i), rb::kLevelSteps, 50, error, cube) == 64u && cube[0] == 0x03u,
               "GameCube vibration: report 0x03");
        pulses += cube[2];
    }
    expect(pulses > 0 && pulses < 32, "a GameCube motor at half strength pulses");
    expect(rb::build_gamecube_rumble_report(0, 0, 0, error, cube) == 64u && cube[2] == 2u && error == 0u, "a GameCube motor stops with 2");
}

namespace simple = galaxy::input::simple_pads;

simple::Layout layout_of(std::uint16_t vendor, std::uint16_t product) {
    const auto found = simple::find_product(vendor, product);
    return found ? found->layout : simple::Layout::None;
}

void test_input_only_switch() {
    namespace sw = galaxy::input::switch_hid;
    expect(layout_of(0x20d6, 0xa711) == simple::Layout::SwitchInputOnly && layout_of(0x0f0d, 0x00c1) == simple::Layout::SwitchInputOnly &&
               layout_of(0x33dd, 0x0002) == simple::Layout::SwitchInputOnly && layout_of(0x0e6f, 0x0188) == simple::Layout::SwitchInputOnly,
           "input-only Switch pads are known");
    expect(simple::find_product(0x20d6, 0xa717) == std::nullopt, "an unlisted id stays with the generic path");
    Bytes r(9, 0);
    r[1] = 0x01 | 0x10 | 0x40;  // Y, L, ZL
    r[2] = 0x01 | 0x20;         // Minus, Capture
    r[3] = 0x02;                // east
    r[4] = 0xff;
    r[5] = 0x00;  // left stick right and up
    r[6] = 0x80;
    r[7] = 0x80;
    simple::ReportState report_state;
    sw::DecodedState state;
    expect(simple::decode_report(simple::Layout::SwitchInputOnly, r, report_state, state), "input-only report decodes");
    expect(state.buttons == (bit(pad::kWestBit) | bit(pad::kLeftShoulderBit) | bit(pad::kBackBit) | bit(pad::kCaptureBit) | bit(pad::kDpadRightBit)),
           "input-only buttons by their documented bits");
    expect(state.normalized && close_to(state.axes[0], 1.0f) && close_to(state.axes[1], 1.0f) && close_to(state.axes[2], 0.0f),
           "input-only sticks, up positive");
    expect(close_to(state.axes[4], 1.0f) && close_to(state.axes[5], 0.0f), "input-only ZL is a full trigger");
    r[3] = 8;
    expect(simple::decode_report(simple::Layout::SwitchInputOnly, r, report_state, state) &&
               (state.buttons & bit(pad::kDpadRightBit)) == 0u,
           "hat 8 is centred");
    r[0] = 0x05;
    expect(!simple::decode_report(simple::Layout::SwitchInputOnly, r, report_state, state), "a report with an id is not this layout");
}

void test_stadia() {
    namespace sw = galaxy::input::switch_hid;
    Bytes r(11, 0);
    r[0] = 0x03;
    r[1] = 4;                // south
    r[2] = 0x01 | 0x20;      // Capture, Menu
    r[3] = 0x01 | 0x40;      // L3, A
    r[4] = 0x80;
    r[5] = 0xff;             // stick down
    r[6] = r[7] = 0x80;
    r[8] = 255;
    r[9] = 0;
    simple::ReportState report_state;
    sw::DecodedState state;
    expect(simple::decode_report(simple::Layout::Stadia, r, report_state, state), "Stadia report 0x03");
    expect(state.buttons == (bit(pad::kDpadDownBit) | bit(pad::kCaptureBit) | bit(pad::kStartBit) | bit(pad::kLeftStickBit) | bit(pad::kSouthBit)),
           "Stadia buttons");
    expect(close_to(state.axes[1], -1.0f) && close_to(state.axes[4], 1.0f) && close_to(state.axes[5], 0.0f), "Stadia axes");
    Bytes out(16, 0xcc);
    expect(simple::build_motor_report(simple::Layout::Stadia, 0x1234, out) == 5u && out[0] == 0x05u && out[1] == 0u && out[2] == 0u &&
               out[3] == 0x34u && out[4] == 0x12u,
           "Stadia motor report 0x05");
}

void test_luna() {
    namespace sw = galaxy::input::switch_hid;
    Bytes usb(10, 0);
    usb[1] = 0x01 | 0x40;  // A, View
    usb[2] = 0x01;         // Home
    usb[3] = 0x06;         // west
    usb[4] = usb[5] = usb[6] = usb[7] = 0x7f;
    usb[8] = 255;
    simple::ReportState report_state;
    sw::DecodedState state;
    expect(simple::decode_report(simple::Layout::LunaUsb, usb, report_state, state) &&
               state.buttons == (bit(pad::kSouthBit) | bit(pad::kBackBit) | bit(pad::kHomeBit) | bit(pad::kDpadLeftBit)),
           "Luna USB buttons");
    expect(close_to(state.axes[0], 0.0f) && close_to(state.axes[4], 1.0f), "Luna USB centre 0x7f and trigger");

    Bytes bt(17, 0);
    bt[0] = 0x01;
    bt[2] = bt[4] = bt[6] = bt[8] = 0x7f;
    bt[9] = 0xff;
    bt[10] = 0x03;  // left trigger full, 10 bits
    bt[13] = 1;     // north
    bt[14] = 0x08;  // X
    bt[15] = 0x08;  // Menu
    bt[16] = 0x01;  // View
    expect(simple::decode_report(simple::Layout::LunaBluetooth, bt, report_state, state) &&
               state.buttons == (bit(pad::kDpadUpBit) | bit(pad::kWestBit) | bit(pad::kStartBit) | bit(pad::kBackBit)),
           "Luna Bluetooth buttons");
    expect(close_to(state.axes[4], 1.0f) && close_to(state.axes[5], 0.0f), "Luna Bluetooth 10-bit trigger");
    Bytes guide{0x02, 0x01};
    expect(!simple::decode_report(simple::Layout::LunaBluetooth, guide, report_state, state) && report_state.home, "Luna Home has its own report");
    expect(simple::decode_report(simple::Layout::LunaBluetooth, bt, report_state, state) && (state.buttons & bit(pad::kHomeBit)) != 0u,
           "and stays held in the state");
    Bytes out(16, 0xcc);
    expect(simple::build_motor_report(simple::Layout::LunaBluetooth, 65535, out) == 9u && out[0] == 0x03u && out[1] == 0x0fu && out[5] == 100u &&
               out[8] == 0xebu,
           "Luna Bluetooth motor report");
    expect(simple::build_motor_report(simple::Layout::LunaUsb, 65535, out) == 0u, "no motor over USB");
}

void test_hori_steam() {
    namespace sw = galaxy::input::switch_hid;
    Bytes r(44, 0);
    r[0] = 0x07;
    r[1] = r[2] = r[3] = r[4] = 0x80;
    r[5] = 0x08 | 0x10 | 0x40;  // centred, South, QAM
    r[6] = 0x02 | 0x80;         // M1, Start
    r[7] = 0x08 | 0x40;         // M2, FR
    r[8] = 255;                 // right trigger first
    r[9] = 0;
    put16(r, 12, 16);  // 1 degree per second, negated, on axis 1
    put16(r, 18, 4096);  // 1 g on axis 2
    simple::ReportState report_state;
    sw::DecodedState state;
    expect(simple::decode_report(simple::Layout::HoriSteam, r, report_state, state), "Hori Steam report");
    expect(state.buttons == (bit(pad::kSouthBit) | bit(pad::kCaptureBit) | bit(pad::kGripLeftBit) | bit(pad::kStartBit) | bit(pad::kGripRightBit) | bit(pad::kGripRight2Bit)),
           "Hori Steam buttons");
    expect(close_to(state.axes[4], 0.0f) && close_to(state.axes[5], 1.0f), "Hori Steam right trigger comes first");
    expect(state.imu_count == 1u && close_to(state.imu[0].gyro[1], -1.0f * kPi / 180.0f) && close_to(state.imu[0].accel[2], 1.0f),
           "Hori Steam motion");
    r[0] = 0x01;
    expect(!simple::decode_report(simple::Layout::HoriSteam, r, report_state, state), "other reports are its firmware's");
    expect(simple::find_product(0x0f0d, 0x0196)->scheme == ControlScheme::Steam, "its motion reaches the pointer row");
}

void test_sixaxis() {
    namespace sw = galaxy::input::switch_hid;
    Bytes r(49, 0);
    r[0] = 0x01;
    r[2] = 0x01 | 0x10;  // Select, up
    r[3] = 0x40;         // Cross
    r[4] = 0x01;         // PS
    r[6] = r[7] = r[8] = r[9] = 0x80;
    r[18] = 255;
    const auto be = [&](std::size_t at, int v) {
        r[at] = static_cast<std::uint8_t>(v >> 8);
        r[at + 1] = static_cast<std::uint8_t>(v);
    };
    be(41, 511);
    be(43, 511 + 113);
    be(45, 511);
    simple::ReportState report_state;
    sw::DecodedState state;
    expect(simple::decode_report(simple::Layout::Sixaxis, r, report_state, state), "DualShock 3 report");
    expect(state.buttons == (bit(pad::kBackBit) | bit(pad::kDpadUpBit) | bit(pad::kSouthBit) | bit(pad::kHomeBit)), "DualShock 3 buttons");
    expect(close_to(state.axes[4], 1.0f), "DualShock 3 analog L2");
    expect(state.imu_count == 1u && close_to(state.imu[0].accel[2], -1.0f) && close_to(state.imu[0].accel[0], 0.0f), "DualShock 3 accelerometer");
    expect(!simple::has_gyro(simple::Layout::Sixaxis) && simple::has_gyro(simple::Layout::HoriSteam), "no gyro read from it");
    r[1] = 0xff;
    expect(!simple::decode_report(simple::Layout::Sixaxis, r, report_state, state), "byte 1 0xff is no report");
    Bytes out(49, 0xcc);
    expect(simple::build_motor_report(simple::Layout::Sixaxis, 0xff00, out) == 49u && out[0] == 0x01u && out[2] == 0xffu && out[5] == 0xffu && out[10] == 0x02u,
           "DualShock 3 motor and player light");
}

void test_wii_u_pro() {
    namespace sw = galaxy::input::switch_hid;
    namespace wm = galaxy::input::wiimote;
    expect(wm::is_wii_u_pro_name(L"Nintendo RVL-CNT-01-UC") && !wm::is_wii_u_pro_name(L"Nintendo RVL-CNT-01-TR") &&
               !wm::is_wii_u_pro_name(L"Nintendo RVL-CNT-01"),
           "a Wii U Pro by its name, not a remote");
    Bytes r(22, 0xff);
    r[0] = simple::kWiiUProReport;
    const auto put_axes = [&](int lx, int rx, int ly, int ry) {
        put16(r, 1, lx);
        put16(r, 3, rx);
        put16(r, 5, ly);
        put16(r, 7, ry);
    };
    put_axes(2000, 2000, 2000, 2000);
    simple::ReportState report_state;
    sw::DecodedState state;
    expect(simple::decode_report(simple::Layout::WiiUPro, r, report_state, state) && state.buttons == 0u && state.axes[0] == 0.0f,
           "Wii U Pro at rest: held bits read 0, the first report is the centre");
    put_axes(3000, 2000, 1000, 2500);
    r[9] = static_cast<std::uint8_t>(~0x02u);   // R
    r[10] = static_cast<std::uint8_t>(~(0x40u | 0x80u | 0x04u));  // B, ZL, ZR
    r[11] = static_cast<std::uint8_t>(~0x02u);  // L3
    expect(simple::decode_report(simple::Layout::WiiUPro, r, report_state, state), "Wii U Pro report");
    expect(state.buttons == (bit(pad::kRightShoulderBit) | bit(pad::kSouthBit) | bit(pad::kLeftStickBit)), "Wii U Pro buttons");
    expect(close_to(state.axes[0], 1.0f) && close_to(state.axes[1], -1.0f) && close_to(state.axes[2], 0.0f) && close_to(state.axes[3], 0.5f),
           "Wii U Pro sticks: x, x, y, y; up positive");
    expect(close_to(state.axes[4], 1.0f) && close_to(state.axes[5], 1.0f), "Wii U Pro's digital triggers");
    Bytes out(22, 0xcc);
    expect(simple::build_motor_report(simple::Layout::WiiUPro, 1000, out) != 0u && out[0] == wm::kOutRumble && (out[1] & 1u) == 1u, "its motor");
    expect(simple::build_motor_report(simple::Layout::WiiUPro, 0, out) != 0u && (out[1] & 1u) == 0u, "its motor off");
}

void test_steam_classic_ble() {
    namespace sw = galaxy::input::switch_hid;
    expect(sv::model_of_product(0x1105) == sv::Model::Classic && sv::model_of_product(0x1106) == sv::Model::Classic, "Bluetooth ids of the 2015 controller");
    // A state message over two segments: buttons, triggers, stick, right pad, accel, gyro.
    Bytes message;
    const unsigned mask = 0x10u | 0x20u | 0x80u | 0x200u | 0x400u | 0x800u;
    message.push_back(static_cast<std::uint8_t>(4u | (mask & 0xf0u)));
    message.push_back(static_cast<std::uint8_t>(mask >> 8));
    message.insert(message.end(), {0x80, 0x00, 0x10});  // A, right pad finger
    message.insert(message.end(), {0xcc, 0x00});        // left trigger, right trigger
    const auto add16 = [&](int v) {
        message.push_back(static_cast<std::uint8_t>(v & 0xff));
        message.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    };
    add16(20000);
    add16(0);  // stick
    add16(0);
    add16(0);  // right pad centred
    add16(100);
    add16(200);
    add16(300);  // accel
    add16(400);
    add16(500);
    add16(600);  // gyro
    message.resize(36, 0);
    const auto segment = [&](unsigned index, bool last) {
        Bytes r(20, 0);
        r[0] = 0x03;
        r[1] = static_cast<std::uint8_t>(0x80u | (last ? 0x40u : 0u) | index);
        std::copy_n(message.begin() + index * 18, 18, r.begin() + 2);
        return r;
    };
    sv::ClassicState classic;
    sw::DecodedState state;
    expect(!sv::decode_classic_ble_report(segment(0, false), classic, state), "the first segment alone is no state");
    expect(sv::decode_classic_ble_report(segment(1, true), classic, state), "the last completes it");
    expect(state.buttons == bit(pad::kSouthBit) && state.touch_surface && state.touch_down, "its buttons and finger");
    expect(close_to(state.axes[0], 20000.0f / 32767.0f) && state.axes[4] > 0.9f && state.axes[5] == 0.0f, "its axes");
    expect(state.imu_count == 1u, "its motion");

    Bytes only{};
    only.resize(36, 0);
    only[0] = static_cast<std::uint8_t>(4u | 0x20u);
    only[2] = 0x00;
    only[3] = 0xcc;
    Bytes triggers(20, 0);
    triggers[0] = 0x03;
    triggers[1] = 0x80u | 0x40u;
    std::copy_n(only.begin(), 18, triggers.begin() + 2);
    expect(sv::decode_classic_ble_report(triggers, classic, state) && state.buttons == bit(pad::kSouthBit) && state.axes[5] > 0.9f && state.axes[4] == 0.0f,
           "a chunk carries what changed; the rest stays");

    Bytes bad = segment(1, true);
    sv::ClassicState fresh;
    expect(!sv::decode_classic_ble_report(bad, fresh, state), "a stray later segment is dropped");
    Bytes empty(20, 0);
    empty[0] = 0x03;
    expect(!sv::decode_classic_ble_report(empty, fresh, state), "an empty segment is ignored");

    Bytes feature(sv::kValveFeatureBytes, 0);
    feature[1] = 0x87;
    feature[2] = 30;
    feature[3] = 0xaa;
    feature[32] = 0xbb;
    Bytes out(20, 0xcc);
    expect(sv::build_classic_ble_segment(feature, 0, out) == 20u && out[0] == 0x03u && out[1] == 0x80u && out[2] == 0x87u && out[3] == 30u && out[4] == 0xaau,
           "a feature's first segment");
    expect(sv::build_classic_ble_segment(feature, 1, out) == 20u && out[1] == (0x80u | 0x40u | 1u) && out[2 + 13] == 0xbbu, "and its last");
    expect(sv::build_classic_ble_segment(feature, 2, out) == 0u, "and no more");
}

void test_switch2_ble() {
    namespace ble = galaxy::input::switch2_ble;
    namespace sw = galaxy::input::switch_hid;
    Bytes advert{0x01, 0x00, 0x03, 0x7e, 0x05, 0x67, 0x20};
    auto seen = ble::parse_advert(advert, 0x112233445566ull);
    expect(seen && seen->product == sw::kJoyCon2LeftProductId && seen->pairing, "a Joy-Con 2 (L) advertising");
    advert[5] = 0x66;
    expect(ble::parse_advert(advert, 0)->product == sw::kJoyCon2RightProductId, "a Joy-Con 2 (R) advertising");
    advert[3] = 0x00;
    expect(!ble::parse_advert(advert, 0), "another vendor's");
    Bytes paired{0x01, 0x00, 0x03, 0x7e, 0x05, 0x67, 0x20, 0, 0, 0, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
    seen = ble::parse_advert(paired, 0x112233445566ull);
    expect(seen && !seen->pairing, "one paired with this host");
    expect(!ble::parse_advert(paired, 0x665544332211ull), "one paired with another");
    paired[10] = paired[11] = paired[12] = paired[13] = paired[14] = paired[15] = 0;
    expect(ble::parse_advert(paired, 0x112233445566ull)->pairing, "one in pairing mode");

    const std::array<std::uint8_t, 4> payload{0x27, 0, 0, 0};
    const auto frame = ble::command(0x0c, 0x02, payload);
    expect(frame == std::vector<std::uint8_t>{0x0c, 0x91, 0x01, 0x02, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00}, "a command's frame");
    const auto read = ble::flash_read(0x1fc040);
    expect(read == std::vector<std::uint8_t>{0x02, 0x91, 0x01, 0x04, 0x00, 0x08, 0x00, 0x00, 0x40, 0x7e, 0x00, 0x00, 0x40, 0xc0, 0x1f, 0x00}, "a flash read's frame");
    Bytes reply(0x50, 0);
    reply[0x10] = 0xb2;
    const auto block = ble::flash_block(reply);
    expect(block.size() == 0x40u && block[0] == 0xb2u, "the block in a reply");
    expect(ble::flash_block(Bytes(8, 0)).empty(), "a short reply has none");
    const auto commands = ble::set_up_commands();
    expect(!commands.empty() && commands[0][0] == 0x03 && commands[0][3] == 0x0d && commands[0][2] == 0x01, "set-up starts output, over BLE");

    Bytes value(ble::kCommonReportBytes, 0);
    value[5] = 0x08;  // byte 6 of the USB report: A at byte 5 there is byte 4 here
    value[4] = 0x08;
    const auto report = ble::usb_report(value);
    sw::Switch2ImuState imu;
    sw::DecodedState state;
    expect(report && (*report)[0] == sw::kSwitch2InputReport && sw::decode_switch2_report(*report, imu, state) && (state.buttons & bit(pad::kEastBit)) != 0u,
           "the common report decodes as report 0x05");
    expect(!ble::usb_report(Bytes(10, 0)), "a short value is no report");

    const auto motor = ble::motor_value(3, galaxy::input::rumble::kLevelSteps, 100);
    expect(motor[0] == 0x00u && motor[1] == 0x53u && motor[2] != 0u && motor[2] == motor[7] && motor[3] == motor[8] && motor[6] == motor[16],
           "the vibration value: its sequence and the motor three times");
}

void test_licensed_playstation() {
    expect(ps::model_for_product(0x054c, 0x0ba0) == ps::Model::DualShock4, "the DS4 wireless adapter reads as a DualShock 4");
    expect(ps::model_for_product(0x0f0d, 0x0066) == ps::Model::DualShock4, "a licensed Hori pad reads as a DualShock 4");
    expect(ps::model_for_product(0x358a, 0x0304) == ps::Model::DualSense, "Backbone reads as a DualSense");
    expect(!ps::model_for_product(0x0f0d, 0x0001).has_value(), "an unknown Hori id is not claimed");

    // A licensed pad has nothing until it says so.
    ps::Capabilities caps = ps::capabilities_of_product(0x0f0d, 0x0066);
    expect(!caps.official && !caps.sensors && !caps.touchpad, "a licensed pad starts with no sensors or touchpad");
    ps::ImuCal cal;
    Bytes report(ps::kCapabilityReportBytes, 0);
    report[0] = ps::kFeatureCapabilities;
    report[2] = 0x27;
    report[4] = 0x02 | 0x08;  // sensors and vibration, no touchpad or light bar
    report[10] = 1;           // gyro: 1/8 degree per second a count
    report[12] = 8;
    ps::apply_capability_report(ps::Model::DualShock4, 0x0f0d, 0x0066, report, caps, cal);
    expect(caps.sensors && caps.vibration && !caps.touchpad && !caps.lightbar, "feature 0x03 sets what the pad has");
    expect(close_to(cal.scale[0], 0.125f * 3.14159265f / 180.0f), "feature 0x03 sets the gyro's scale");

    ps::Capabilities unanswered = ps::capabilities_of_product(0x0f0d, 0x0066);
    ps::apply_capability_report(ps::Model::DualShock4, 0x0f0d, 0x0066, {}, unanswered, cal);
    expect(!unanswered.sensors && !unanswered.vibration, "no answer: nothing assumed");
    ps::Capabilities razer = ps::capabilities_of_product(0x1532, 0x1000);
    ps::apply_capability_report(ps::Model::DualShock4, 0x1532, 0x1000, {}, razer, cal);
    expect(razer.touchpad && razer.vibration && !razer.sensors, "a Razer DS4 pad: touchpad and vibration without asking");
    expect(!ps::capabilities_of_product(0x0e6f, 0x0203).effects, "Victrix 0203 is sent no effects");

    // No touchpad: the same bytes report none.
    Bytes r = ds4_usb();
    r.resize(std::max<std::size_t>(r.size(), 64u));
    r[35] = 0x01;
    ps::SensorClock clock;
    switch_hid::DecodedState state;
    ps::Capabilities none = caps;
    expect(ps::decode_report(ps::Model::DualShock4, ps::Transport::Usb, r, ps::ImuCal{}, clock, state, none) && !state.touch_surface,
           "a pad without a touchpad reports no surface");

    // A DS4 finger at its documented offset (USB: report byte 35).
    Bytes usb = ds4_usb();
    usb.resize(std::max<std::size_t>(usb.size(), 64u));
    usb[35] = 0x01;
    usb[36] = 0x80;
    usb[37] = 0xc7;
    usb[38] = 0x1c;
    const Decoded wired = decode(ps::Model::DualShock4, ps::Transport::Usb, usb);
    expect(wired.ok && wired.state.touch_down && close_to(wired.state.touch_x, 1.0f) && close_to(wired.state.touch_y, 0.5f),
           "DualShock 4 touchpad over USB: finger 1 at byte 35");

    // The USB adapter with no pad connected: neutral, whatever the bytes.
    ps::Capabilities dongle = ps::capabilities_of_product(0x054c, 0x0ba0);
    expect(dongle.dongle && dongle.official, "the adapter is official");
    Bytes absent = ds4_usb();
    absent.resize(std::max<std::size_t>(absent.size(), 64u));
    absent[1] = 0xff;
    absent[31] = 0x04;
    ps::SensorClock clock2;
    expect(ps::decode_report(ps::Model::DualShock4, ps::Transport::Usb, absent, ps::ImuCal{}, clock2, state, dongle) &&
               state.axes[0] == 0.0f && state.buttons == 0u,
           "the adapter with no pad holds nothing");

    // A licensed DualSense: touch one byte earlier than Sony's (report byte 32).
    ps::Capabilities alternate;
    alternate.official = false;
    alternate.alternate_layout = true;
    Bytes sense(64, 0);
    sense[0] = ps::kReportState;
    sense[1] = sense[2] = sense[3] = sense[4] = 0x80;
    sense[32] = 0x01;
    sense[33] = 0x80;
    sense[34] = 0xc7;
    sense[35] = 0x1c;
    ps::SensorClock clock3;
    expect(ps::decode_report(ps::Model::DualSense, ps::Transport::Usb, sense, ps::ImuCal{}, clock3, state, alternate) &&
               state.touch_down && close_to(state.touch_x, 1.0f),
           "a licensed DualSense's finger at its own offset");
}

void test_touch() {
    // DualSense USB: the first finger at packet byte 32 (report byte 33): x 960 of 1920, y 535 of 1070.
    Bytes r = ds5_usb();
    r[33] = 0x05;
    r[34] = 0xc0;
    r[35] = 0x73;
    r[36] = 0x21;
    Decoded d = decode(ps::Model::DualSense, ps::Transport::Usb, r);
    expect(d.ok && d.state.touch_surface && d.state.touch_down && close_to(d.state.touch_x, 0.5f) &&
               close_to(d.state.touch_y, 0.5f),
           "DualSense touchpad: a finger in the middle");
    r[33] = 0x85;
    d = decode(ps::Model::DualSense, ps::Transport::Usb, r);
    expect(d.ok && d.state.touch_surface && !d.state.touch_down, "DualSense touchpad: the finger lifted (bit 7)");
    // DualShock 4 over Bluetooth: two bytes later; y over 920.
    Bytes bt = ds4_bluetooth();
    bt[37] = 0x01;
    bt[38] = 0x80;
    bt[39] = 0xc7;
    bt[40] = 0x1c;  // x 1920, y 460
    seal_input(bt);
    d = decode(ps::Model::DualShock4, ps::Transport::Bluetooth, bt);
    expect(d.ok && d.state.touch_down && close_to(d.state.touch_x, 1.0f) && close_to(d.state.touch_y, 0.5f),
           "DualShock 4 touchpad over Bluetooth: the right edge, half way down");
    // The DualShock 4's short Bluetooth report (no touchpad in it).
    Bytes simple(10, 0);
    simple[0] = 0x01;
    d = decode(ps::Model::DualShock4, ps::Transport::Bluetooth, simple);
    expect(d.ok && !d.state.touch_surface, "the short report has no touchpad");

    // The 2026 Steam Controller: the right pad's finger bit, its signed coordinates (centre 0, y up).
    switch_hid::DecodedState state;
    sv::SensorClock clock;
    Bytes steam = steam_report(0x45);
    put32(steam, 2, sv::button::kRightPadTouch);
    put16(steam, 24, 16384);   // a quarter right of the centre
    put16(steam, 26, -16384);  // a quarter below
    expect(sv::decode_report(steam, clock, state) && state.touch_surface && state.touch_down &&
               close_to(state.touch_x, 0.75f) && close_to(state.touch_y, 0.75f),
           "Steam Controller right pad: x right, y down");
    put32(steam, 2, 0);
    expect(sv::decode_report(steam, clock, state) && state.touch_surface && !state.touch_down,
           "Steam Controller right pad: no finger");
    Bytes shortclock = steam_report(0x47);
    put32(shortclock, 2, sv::button::kRightPadTouch);
    put16(shortclock, 26, -32768);  // the left edge
    put16(shortclock, 28, 32767);   // the top
    sv::SensorClock short_clock;
    expect(sv::decode_report(shortclock, short_clock, state) && state.touch_down && close_to(state.touch_x, 0.0f) &&
               close_to(state.touch_y, 0.0f, 1e-4f),
           "0x47: the pad two bytes later");

    // The Steam Deck: bit 0x00100000 of the low button word, the right pad at state bytes 16 and 18 (report 21, 23).
    Bytes deck = valve_report(9, 64);
    put32(deck, 9, 0x00100000u);
    put16(deck, 21, 0);
    put16(deck, 23, 0);
    expect(sv::decode_deck_report(deck, state) && state.touch_down && close_to(state.touch_x, 0.5f) &&
               close_to(state.touch_y, 0.5f),
           "Steam Deck right pad: the centre");
    put32(deck, 9, 0);
    expect(sv::decode_deck_report(deck, state) && state.touch_surface && !state.touch_down, "Steam Deck: no finger");

    // The 2015 Steam Controller: the same bit and place.
    Bytes classic_report = valve_report(1, 60);
    put32(classic_report, 5, 1);
    put32(classic_report, 9, 0x00100000u);
    put16(classic_report, 21, 32767);
    put16(classic_report, 23, 0);
    sv::ClassicState classic;
    expect(sv::decode_classic_report(classic_report, classic, state) && state.touch_down &&
               close_to(state.touch_x, 1.0f, 1e-4f) && close_to(state.touch_y, 0.5f),
           "2015 Steam Controller right pad: the right edge");
}

void test_report_mode_watch() {
    namespace sw = galaxy::input::switch_hid;
    using Watch = sw::ReportModeWatch;
    const std::uint64_t ms = 1000u;
    {
        // Steam's start: simple reports, then full reports without motion; asked again each second until motion returns.
        Watch w;
        expect(!w.update(sw::kReportFullState, 3u, true, 1000 * ms), "motion arriving: nothing to ask");
        expect(!w.update(sw::kReportSimpleState, 0u, true, 1100 * ms), "lost just now: not yet");
        expect(!w.update(sw::kReportSubcommandReply, 0u, true, 1400 * ms), "a subcommand reply says nothing");
        expect(w.update(sw::kReportSimpleState, 0u, true, 1400 * ms), "lost past the limit: ask");
        expect(!w.update(sw::kReportFullState, 0u, true, 1500 * ms), "asked a moment ago");
        expect(w.update(sw::kReportFullState, 0u, true, 2400 * ms), "full reports without motion: ask again");
        expect(!w.update(sw::kReportFullState, 3u, true, 2500 * ms), "motion back");
        expect(w.lost_since_us == 0u, "the loss is over");
        expect(!w.update(sw::kReportFullState, 0u, true, 2600 * ms), "a new loss waits again");
        expect(w.update(sw::kReportFullState, 0u, true, 3500 * ms), "a later loss is asked about too");
    }
    {
        // A pad with no sensors, known in advance: full reports are all that is wanted.
        Watch w;
        for (std::uint64_t t = 1; t < 100; ++t) {
            expect(!w.update(sw::kReportFullState, 0u, false, t * 1000 * ms), "no sensors wanted: never asked");
        }
        expect(w.update(sw::kReportSimpleState, 0u, false, 100'000 * ms) == false, "lost just now");
        expect(w.update(sw::kReportSimpleState, 0u, false, 100'300 * ms), "its mode lost: asked");
    }
    {
        // A pad that never answers: asked a few times, then left alone.
        Watch w;
        std::size_t asks = 0u;
        for (std::uint64_t t = 1; t < 200; ++t) {
            asks += w.update(sw::kReportSimpleState, 0u, true, t * 500 * ms) ? 1u : 0u;
        }
        expect(asks == Watch::kMostUnanswered, "a pad that never answers is asked a few times");
    }
    {
        // One that has answered is asked for as long as it takes.
        Watch w;
        (void)w.update(sw::kReportFullState, 3u, true, 1000 * ms);
        std::size_t asks = 0u;
        for (std::uint64_t t = 1; t < 100; ++t) {
            asks += w.update(sw::kReportFullState, 0u, true, 1000 * ms + t * 1000 * ms) ? 1u : 0u;
        }
        expect(asks >= 90u, "one that answered before is asked every second");
    }
}

int main() {
    test_report_mode_watch();
    test_crc();
    test_dualshock4_usb();
    test_dualshock4_motion();
    test_dualshock4_bluetooth();
    test_dualsense();
    test_calibration();
    test_output_reports();
    test_products();
    test_steam();
    test_steam_deck();
    test_steam_classic();
    test_steam_input_list();
    test_pad_sample();
    test_device_list();
    test_generic();
    test_touch();
    test_licensed_playstation();
    test_switch2_family();
    test_input_only_switch();
    test_stadia();
    test_luna();
    test_hori_steam();
    test_sixaxis();
    test_wii_u_pro();
    test_steam_classic_ble();
    test_switch2_ble();
    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "pad controller tests passed\n";
    return 0;
}
