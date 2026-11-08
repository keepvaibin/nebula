// SPDX-License-Identifier: GPL-3.0-only
// The MotionPlus's samples, the Nunchuk passed through it, and the motion pointer (docs/WII_REMOTE.md).
#include "galaxy/input/wiimote_input.h"
#include "galaxy/input/wiimote_motion_pointer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

namespace wm = galaxy::input::wiimote;

int g_failed = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++g_failed;
    }
}

bool near(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// A MotionPlus block: 14-bit yaw, roll, pitch; the slow flags; a Nunchuk in its port.
std::array<std::uint8_t, 6> mp_block(unsigned yaw, unsigned roll, unsigned pitch, bool yaw_slow, bool roll_slow,
                                     bool pitch_slow, bool port) {
    std::array<std::uint8_t, 6> b{};
    b[0] = static_cast<std::uint8_t>(yaw & 0xffu);
    b[1] = static_cast<std::uint8_t>(roll & 0xffu);
    b[2] = static_cast<std::uint8_t>(pitch & 0xffu);
    b[3] = static_cast<std::uint8_t>(((yaw >> 8u) << 2u) | (yaw_slow ? 2u : 0u) | (pitch_slow ? 1u : 0u));
    b[4] = static_cast<std::uint8_t>(((roll >> 8u) << 2u) | (roll_slow ? 2u : 0u) | (port ? 1u : 0u));
    b[5] = static_cast<std::uint8_t>(((pitch >> 8u) << 2u) | 2u);
    return b;
}

void test_rates() {
    const wm::MotionPlusCalibration c = wm::parse_motion_plus_calibration({});  // a typical unit
    // Zero is 32768 in 16 bits: 8192 in 14. 17408 / 4 = 4352 counts is 1200 deg/s fast.
    wm::MotionRates rates;
    bool port = true;
    std::array<std::uint8_t, 6> b = mp_block(8192u, 8192u, 8192u, false, false, false, false);
    expect(wm::motion_plus_rates(b, c, rates, port) && rates.fresh, "a MotionPlus block reads as rates");
    expect(!port, "no Nunchuk in its port");
    expect(near(rates.rate[0], 0.0f, 1e-4f) && near(rates.rate[1], 0.0f, 1e-4f) && near(rates.rate[2], 0.0f, 1e-4f),
           "zero counts are no turn");
    // Yaw +4352 fast: +1200 deg/s raw, the remote's z by the right-hand rule is the other way.
    b = mp_block(8192u + 4352u, 8192u, 8192u, false, false, false, true);
    (void)wm::motion_plus_rates(b, c, rates, port);
    const float full = 1200.0f * 3.14159265f / 180.0f;
    expect(near(rates.rate[2], -full, 0.01f), "yaw counts turn about -z");
    expect(port, "a Nunchuk in its port");
    // Roll slow: 270 deg/s for the same counts, about +y.
    b = mp_block(8192u, 8192u + 4352u, 8192u, false, true, false, false);
    (void)wm::motion_plus_rates(b, c, rates, port);
    expect(near(rates.rate[1], 270.0f * 3.14159265f / 180.0f, 0.01f), "slow roll counts turn about +y");
    // Pitch: about -x.
    b = mp_block(8192u, 8192u, 8192u - 4352u, false, false, false, false);
    (void)wm::motion_plus_rates(b, c, rates, port);
    expect(near(rates.rate[0], full, 0.01f), "pitch counts turn about -x");
    // The Nunchuk's block (bit 1 of byte 5 clear) is not a sample.
    b[5] = 0;
    expect(!wm::motion_plus_rates(b, c, rates, port), "a passed-on Nunchuk block is not a sample");
}

void test_calibration() {
    std::array<std::uint8_t, 32> block{};
    const auto put = [&](std::size_t at, unsigned v) {
        block[at] = static_cast<std::uint8_t>(v >> 8u);
        block[at + 1] = static_cast<std::uint8_t>(v & 0xffu);
    };
    // Fast: zeros 0x7e00, 0x8100, 0x7f00; scales 0x4300 above; 200 x 6 deg/s. Slow: a broken block.
    put(0, 0x7e00u);
    put(2, 0x8100u);
    put(4, 0x7f00u);
    put(6, 0x7e00u + 0x4300u);
    put(8, 0x8100u + 0x4300u);
    put(10, 0x7f00u + 0x4300u);
    block[12] = 200;
    const wm::MotionPlusCalibration c = wm::parse_motion_plus_calibration(block);
    expect(c.fast_zero[0] == 32256.0f && c.fast_zero[1] == 33024.0f && c.fast_span[2] == 17152.0f &&
               c.fast_degrees[0] == 1200.0f,
           "a fast block is read");
    expect(c.slow_zero[0] == 32768.0f && c.slow_degrees[1] == 270.0f, "a broken slow block takes a typical unit's");
}

// Dolphin's MotionPlus::ApplyPassthroughModifications for the Nunchuk, then the MotionPlus's own bits.
std::array<std::uint8_t, 6> pass_through(std::array<std::uint8_t, 6> d) {
    const auto bit = [](std::uint8_t v, int n) { return (v >> n) & 1u; };
    const auto set = [](std::uint8_t& v, int n, unsigned on) {
        v = static_cast<std::uint8_t>(on != 0u ? (v | (1u << n)) : (v & ~(1u << n)));
    };
    set(d[5], 6, bit(d[5], 7));
    set(d[5], 7, bit(d[4], 0));
    set(d[5], 4, bit(d[5], 3));
    set(d[5], 3, bit(d[5], 1));
    set(d[5], 2, bit(d[5], 0));
    set(d[4], 0, 1u);  // a Nunchuk in the port
    set(d[5], 0, 0u);
    set(d[5], 1, 0u);  // not a MotionPlus sample
    return d;
}

void test_passthrough() {
    // Stick 0x9a, 0x61; accelerometer high bytes; C held (bit 1 clear), Z up (bit 0 set); low bits 1,0,1 / 0,1 / 1,0.
    const std::array<std::uint8_t, 6> nunchuk{0x9a, 0x61, 0x84, 0x7c, 0xb3, 0b10'01'10'01};
    const std::array<std::uint8_t, 6> back = wm::undo_nunchuk_passthrough(pass_through(nunchuk));
    expect(back[0] == nunchuk[0] && back[1] == nunchuk[1] && back[2] == nunchuk[2] && back[3] == nunchuk[3],
           "the stick and the accelerometer's high bytes pass unchanged");
    expect((back[4] & 0xfeu) == (nunchuk[4] & 0xfeu), "z's high byte but its lowest bit");
    expect((back[5] & 0x03u) == (nunchuk[5] & 0x03u), "C and Z come back");
    // Each accelerometer value's second-lowest bit is kept; its lowest is lost (it takes the second's).
    expect(((back[5] >> 3) & 1u) == ((nunchuk[5] >> 3) & 1u) && ((back[5] >> 5) & 1u) == ((nunchuk[5] >> 5) & 1u) &&
               ((back[5] >> 7) & 1u) == ((nunchuk[5] >> 7) & 1u),
           "x, y and z keep their second-lowest bit");
}

wm::MotionRates turn(float x, float y, float z) {
    wm::MotionRates r;
    r.fresh = true;
    r.rate = {x, y, z};
    return r;
}

galaxy::input::PlayerInput level() {
    galaxy::input::PlayerInput in{};
    in.gravity = {0.0f, 0.0f, 1.0f};  // face up, pointing at the screen
    return in;
}

void test_pointer() {
    wm::MotionPointer pointer;
    std::uint64_t t = 1'000'000;
    galaxy::input::PlayerInput in = level();
    // Settle gravity and the first rest window (half a second), not started: no pointer until 2.
    for (int i = 0; i < 60; ++i) {
        in = level();
        pointer.apply(in, turn(0, 0, 0), t += 10'000);
    }
    expect(!in.pointer_on, "no pointer before the first 2");
    in = level();
    in.buttons.two = true;
    pointer.apply(in, turn(0, 0, 0), t += 10'000);
    expect(!in.buttons.two, "2 never reaches the game");
    expect(in.pointer_on && near(in.pointer_x, 0.0f, 1e-4f) && near(in.pointer_y, 0.0f, 1e-4f), "2 shows it in the middle");
    // Released; the nose turns left 10 degrees (positive yaw about the remote's z, up when level).
    const float rate = 10.0f * 3.14159265f / 180.0f / 0.5f;
    for (int i = 0; i < 50; ++i) {
        in = level();
        pointer.apply(in, turn(0, 0, rate), t += 10'000);
    }
    // 40 degrees per picture width (2 units): 10 degrees is half a unit to the left.
    expect(in.pointer_on && near(in.pointer_x, -0.5f, 0.05f) && near(in.pointer_y, 0.0f, 0.02f),
           "turning the nose left moves it left");
    // The nose tips up 9 degrees (negative pitch about x, the remote's left side): up the picture.
    const float up = 9.0f * 3.14159265f / 180.0f / 0.5f;
    for (int i = 0; i < 50; ++i) {
        in = level();
        pointer.apply(in, turn(-up, 0, 0), t += 10'000);
    }
    expect(in.pointer_y < -0.3f, "tipping the nose up moves it up (y down positive)");
    // Five seconds still: hidden, and still hidden after a move, until 2.
    for (int i = 0; i < 520; ++i) {
        in = level();
        pointer.apply(in, turn(0, 0, 0), t += 10'000);
    }
    expect(!in.pointer_on, "it hides after five seconds at rest");
    for (int i = 0; i < 20; ++i) {
        in = level();
        pointer.apply(in, turn(0, 0, rate), t += 10'000);
    }
    expect(!in.pointer_on, "a hidden pointer waits for 2");
    in = level();
    in.buttons.two = true;
    pointer.apply(in, turn(0, 0, 0), t += 10'000);
    expect(in.pointer_on && near(in.pointer_x, 0.0f, 1e-4f), "2 brings it back in the middle");
    // Upright (the Star Ball): no pointer.
    in = galaxy::input::PlayerInput{};
    in.gravity = {0.0f, -1.0f, 0.0f};
    pointer.apply(in, turn(0, 0, 0), t += 10'000);
    expect(!in.pointer_on, "held upright it has no pointer");
}

}  // namespace

int main() {
    test_rates();
    test_calibration();
    test_passthrough();
    test_pointer();
    if (g_failed == 0) std::printf("wiimote motion pointer tests passed\n");
    return g_failed == 0 ? 0 : 1;
}
