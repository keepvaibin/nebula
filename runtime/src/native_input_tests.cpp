// The player's input (PlayerInput): buttons, the virtual remote's pointer, stick and motion; a real Wii Remote's
// reports read into it, its output reports and replies; the settings-overlay pad and the input-script integer fields.

#include "galaxy/input/wiimote_input.h"
#include "galaxy/native_input.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/wiimote_hid.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool expect_close(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAILED: " << message << " actual=" << actual
                  << " expected=" << expected << '\n';
        return false;
    }
    return true;
}

bool settings_pad_toggles_navigates_and_hides_buttons() {
    namespace pad = galaxy::input::settings_pad;
    galaxy::input::NativeSettingsPadState state;
    const auto step = [&state](std::uint16_t buttons, bool visible, std::uint64_t ms,
                               float x = 0.0f, float y = 0.0f) {
        return galaxy::input::native_settings_pad_step(state, buttons, x, y, visible, ms);
    };
    const auto only_key = [](const galaxy::input::NativeSettingsPadStep& s, std::uint8_t key) {
        return s.key_count == 1u && s.keys[0] == key;
    };
    bool ok = true;
    auto s = step(pad::kStart, false, 0u);
    ok &= expect(s.key_count == 0u && s.game_buttons == pad::kStart && !s.game_neutral,
                 "Menu alone reaches the game (pause)");
    s = step(0u, false, 10u);
    s = step(pad::kBack, false, 20u);
    ok &= expect(s.key_count == 0u && s.game_buttons == pad::kBack, "View alone reaches the game");
    s = step(pad::kChord, false, 30u);
    ok &= expect(only_key(s, pad::kKeyToggle) && s.game_buttons == 0u && s.game_neutral,
                 "View+Menu toggles the overlay and hides the chord");
    s = step(pad::kChord, false, 40u);
    ok &= expect(s.key_count == 0u && s.game_buttons == 0u,
                 "a held chord stays hidden while the toggle is in flight");
    s = step(pad::kChord, true, 50u);
    ok &= expect(s.key_count == 0u && s.game_neutral, "a held chord does not repeat");
    s = step(0u, true, 60u);
    s = step(pad::kDown, true, 100u);
    ok &= expect(only_key(s, pad::kKeyDown) && s.game_neutral, "D-pad moves inside the overlay");
    s = step(pad::kDown, true, 450u);
    ok &= expect(s.key_count == 0u, "no repeat before the delay");
    s = step(pad::kDown, true, 500u);
    ok &= expect(only_key(s, pad::kKeyDown), "held direction repeats after the delay");
    s = step(pad::kDown, true, 560u);
    ok &= expect(s.key_count == 0u, "repeat waits for its interval");
    s = step(pad::kDown, true, 610u);
    ok &= expect(only_key(s, pad::kKeyDown), "repeat continues at its interval");
    s = step(0u, true, 620u);
    s = step(0u, true, 630u, -0.9f, 0.0f);
    ok &= expect(only_key(s, pad::kKeyLeft), "left stick adjusts inside the overlay");
    s = step(0u, true, 640u);
    s = step(pad::kA, true, 650u);
    ok &= expect(only_key(s, pad::kKeyAccept), "A changes the selected setting (the overlay reads it as Right)");
    s = step(pad::kShoulderRight, true, 660u);
    ok &= expect(only_key(s, pad::kKeyNextPage), "RB selects the next page");
    s = step(pad::kShoulderLeft, true, 670u);
    ok &= expect(only_key(s, pad::kKeyPreviousPage), "LB selects the previous page");
    s = step(pad::kB, true, 680u);
    ok &= expect(only_key(s, pad::kKeyClose) && s.game_buttons == 0u, "B closes the overlay");
    s = step(pad::kB, false, 690u);
    ok &= expect(s.game_buttons == 0u && !s.game_neutral,
                 "the closing press stays hidden from the game until released");
    s = step(0u, false, 700u);
    s = step(pad::kB, false, 710u);
    ok &= expect(s.game_buttons == pad::kB, "a fresh press reaches the game again");
    s = step(0u, false, 720u);
    s = step(pad::kStart, false, 730u);
    s = step(pad::kChord, false, 740u);
    ok &= expect(only_key(s, pad::kKeyToggle) && s.game_buttons == 0u,
                 "Menu then View still toggles");
    s = step(0u, true, 750u);
    s = step(pad::kStart, true, 760u);
    ok &= expect(only_key(s, pad::kKeyClose), "Menu alone closes the overlay");
    return ok;
}

bool native_input_script_unsigned_fields_fail_closed() {
    std::uint64_t value = 0x1122334455667788ull;
    bool passed = expect(
        galaxy::input::parse_native_input_script_u64("0", value) &&
            value == 0u,
        "native input script unsigned parser accepts zero");
    passed &= expect(
        galaxy::input::parse_native_input_script_u64(
            "18446744073709551615", value) &&
            value == std::numeric_limits<std::uint64_t>::max(),
        "native input script unsigned parser accepts UINT64_MAX exactly");

    constexpr std::array<std::string_view, 8> invalid_values{
        "",
        "18446744073709551616",
        "999999999999999999999999999999999999999999",
        "-1",
        "+1",
        " 1",
        "1 ",
        "1x",
    };
    for (const std::string_view invalid : invalid_values) {
        value = 0x1122334455667788ull;
        passed &= expect(
            !galaxy::input::parse_native_input_script_u64(invalid, value) &&
                value == 0x1122334455667788ull,
            "native input script unsigned parser rejects malformed or overflowing text without changing output");
    }

    std::uint64_t start_vi = 0x8877665544332211ull;
    passed &= expect(
        galaxy::input::resolve_native_input_script_start_vi(
            std::numeric_limits<std::uint64_t>::max() - 7u,
            "7",
            start_vi) &&
            start_vi == std::numeric_limits<std::uint64_t>::max(),
        "native input script relative start accepts the exact non-wrapping limit");
    start_vi = 0x8877665544332211ull;
    passed &= expect(
        !galaxy::input::resolve_native_input_script_start_vi(
            std::numeric_limits<std::uint64_t>::max() - 7u,
            "8",
            start_vi) &&
            start_vi == 0x8877665544332211ull,
        "native input script relative start rejects anchor addition overflow without changing output");
    return passed;
}

bool normalizes_xinput_axes() {
    constexpr std::int16_t kDeadzone = 7849;
    return expect(
               galaxy::input::normalize_xinput_axis(0, kDeadzone) == 0.0f,
               "XInput neutral axis is zero") &&
           expect(
               galaxy::input::normalize_xinput_axis(kDeadzone, kDeadzone) == 0.0f,
               "XInput positive deadzone edge is zero") &&
           expect(
               galaxy::input::normalize_xinput_axis(-kDeadzone, kDeadzone) == 0.0f,
               "XInput negative deadzone edge is zero") &&
           expect_close(
               galaxy::input::normalize_xinput_axis(32767, kDeadzone),
               1.0f,
               0.0001f,
               "XInput positive full-scale normalizes to one") &&
           expect_close(
               galaxy::input::normalize_xinput_axis(-32768, kDeadzone),
               -1.0f,
               0.0001f,
               "XInput negative full-scale normalizes to minus one") &&
           expect(
               galaxy::input::normalize_xinput_axis(8000, kDeadzone) > 0.0f,
               "XInput just outside deadzone is nonzero");
}

// Each button is its own bit, the game's NEBULA_BUTTON_* order.
bool player_buttons_are_bits() {
    using namespace galaxy::input;
    bool ok = true;
    const std::pair<bool ButtonState::*, std::uint32_t> map[] = {
        {&ButtonState::a, kButtonA},         {&ButtonState::b, kButtonB},         {&ButtonState::one, kButton1},
        {&ButtonState::two, kButton2},       {&ButtonState::plus, kButtonPlus},   {&ButtonState::minus, kButtonMinus},
        {&ButtonState::home, kButtonHome},   {&ButtonState::up, kButtonUp},       {&ButtonState::down, kButtonDown},
        {&ButtonState::left, kButtonLeft},   {&ButtonState::right, kButtonRight}, {&ButtonState::c, kButtonC},
        {&ButtonState::z, kButtonZ}};
    std::uint32_t all = 0;
    for (const auto& [field, bit] : map) {
        ButtonState buttons{};
        buttons.*field = true;
        ok &= expect(button_bits(buttons) == bit, "one button, one bit");
        ok &= expect((all & bit) == 0u, "no two buttons share a bit");
        all |= bit;
    }
    ok &= expect(button_bits(ButtonState{}) == 0u && all == 0x1fffu, "thirteen buttons in bits 0-12");
    return ok;
}

// The virtual remote's pointer: on the picture (clamped), level, at its distance; off without a source; lost when the
// remote points up or down.
bool virtual_pointer_and_motion() {
    using namespace galaxy::input;
    bool ok = true;
    PlayerInput input{};
    ok &= expect(!input.pointer_on && input.gravity == Gravity{0.0f, 0.0f, 1.0f} &&
                     input.nunchuk_gravity == Gravity{0.0f, 0.0f, 1.0f},
                 "at rest: no pointer, lying still");
    set_pointer(input, -0.26f, 1.7f, true);
    ok &= expect(input.pointer_on && input.pointer_x == -0.26f && input.pointer_y == 1.0f && input.roll_x == 1.0f &&
                     input.roll_y == 0.0f,
                 "a pointer on the picture, clamped, level");
    ok &= expect_close(input.distance, 1.667255f, 1e-5f, "the virtual remote's distance");
    set_pointer(input, 0.5f, 0.5f, false);
    ok &= expect(!input.pointer_on, "no source, no pointer");
    ok &= expect(remote_faces_screen({0.0f, 0.0f, 1.0f}), "lying flat: points at the screen");
    ok &= expect(!remote_faces_screen({0.0f, -1.0f, 0.0f}), "held upright (Star Ball): no pointer");
    ok &= expect(!remote_faces_screen({0.0f, 1.0f, 0.0f}), "pointing at the floor: no pointer");
    ok &= expect(remote_faces_screen({0.0f, -0.70f, 0.714f}) && !remote_faces_screen({0.0f, -0.72f, 0.69f}),
                 "the edge at ~45 degrees");
    ok &= expect(remote_faces_screen({0.0f, 0.0f, 0.0f}), "no reading: the pointer stays");
    return ok;
}

// A stick read as the Nunchuk's: dead zone, linear, full; never longer than 1.
bool nunchuk_stick_response() {
    using namespace galaxy::input;
    bool ok = true;
    ok &= expect(nunchuk_stick(0.10f, -0.14f).x == 0.0f && nunchuk_stick(0.10f, -0.14f).y == 0.0f, "inside the dead zone");
    ok &= expect_close(nunchuk_stick(0.5f, 0.0f).x, (50.5f - 15.0f) / 56.0f, 1e-5f, "linear between");
    ok &= expect(nunchuk_stick(-0.71f, 0.0f).x == -1.0f && nunchuk_stick(0.0f, 1.0f).y == 1.0f, "full from 71/101");
    const StickValue diagonal = nunchuk_stick(1.0f, 1.0f);
    ok &= expect_close(diagonal.x, 0.70710678f, 1e-5f, "a full diagonal on the circle") &&
          expect_close(diagonal.y, 0.70710678f, 1e-5f, "a full diagonal on the circle (y)");
    ok &= expect(nunchuk_stick_counts(15.0f, -15.0f).x == 0.0f && nunchuk_stick_counts(71.0f, 0.0f).x == 1.0f &&
                     nunchuk_stick_counts(-43.0f, 0.0f).x == -0.5f,
                 "a Nunchuk's own counts: 15 nothing, 43 half, 71 full");
    return ok;
}

// A remote's calibration as its EEPROM holds it: IR (unused), then the accelerometer's 0 g at 512 and 1 g at 612 on
// every axis, twice, with checksums.
std::array<std::uint8_t, 0x2a> remote_eeprom() {
    std::array<std::uint8_t, 0x2a> eeprom{};
    const std::uint8_t accel[9] = {0x80, 0x80, 0x80, 0x00, 0x99, 0x99, 0x99, 0x00, 0x00};
    for (std::size_t block = 0; block < 2; ++block) {
        int sum = 0;
        for (std::size_t i = 0; i < 9; ++i) {
            eeprom[0x16 + block * 10 + i] = accel[i];
            sum += accel[i];
        }
        eeprom[0x16 + block * 10 + 9] = static_cast<std::uint8_t>(sum + 0x55);
    }
    return eeprom;
}

// A Nunchuk's calibration block: 0 g at 512 and 1 g at 712, stick limits; its two checksums; twice.
std::array<std::uint8_t, 32> nunchuk_calibration() {
    std::array<std::uint8_t, 32> block{};
    const std::uint8_t data[14] = {0x80, 0x80, 0x80, 0x00, 0xb2, 0xb2, 0xb2, 0x00, 0xe0, 0x15, 0x7b, 0xe6, 0x1d, 0x81};
    for (std::size_t copy = 0; copy < 2; ++copy) {
        int sum = 0;
        for (std::size_t i = 0; i < 14; ++i) {
            block[copy * 16 + i] = data[i];
            sum += data[i];
        }
        block[copy * 16 + 14] = static_cast<std::uint8_t>(sum + 0x55);
        block[copy * 16 + 15] = static_cast<std::uint8_t>(sum + 0xaa);
    }
    return block;
}

// Report 0x33: buttons, the accelerometer (10 bits: X's low two and Y's and Z's second in the button bytes) and four
// extended IR lights; `lights` raw camera positions (size 8), the rest unseen.
std::array<std::uint8_t, 18> report_0x33(std::uint8_t b1, std::uint8_t b2, int x, int y, int z,
                                         std::span<const std::array<int, 2>> lights) {
    std::array<std::uint8_t, 18> r{};
    r[0] = 0x33;
    r[1] = static_cast<std::uint8_t>(b1 | ((x & 3) << 5));
    r[2] = static_cast<std::uint8_t>(b2 | ((y & 2) << 4) | ((z & 2) << 5));
    r[3] = static_cast<std::uint8_t>(x >> 2);
    r[4] = static_cast<std::uint8_t>(y >> 2);
    r[5] = static_cast<std::uint8_t>(z >> 2);
    for (std::size_t i = 0; i < 4; ++i) {
        std::uint8_t* d = r.data() + 6 + i * 3;
        if (i >= lights.size()) {
            d[0] = d[1] = d[2] = 0xff;
            continue;
        }
        const int lx = lights[i][0];
        const int ly = lights[i][1];
        d[0] = static_cast<std::uint8_t>(lx & 0xff);
        d[1] = static_cast<std::uint8_t>(ly & 0xff);
        d[2] = static_cast<std::uint8_t>(((ly >> 8) & 3) << 6 | ((lx >> 8) & 3) << 4 | 8);
    }
    return r;
}

// A real remote's reports read as the player's input: buttons, motion through its calibration, the pointer from the
// sensor bar's two lights (level, centred, then right; smoothed as the game smooths a camera pointer), none when held
// upright; its Nunchuk's stick from where it rested, C and Z, motion.
bool real_remote_reports() {
    using namespace galaxy::input;
    bool ok = true;
    wiimote::Reader reader;
    const auto eeprom = remote_eeprom();
    const auto calibration = nunchuk_calibration();
    reader.connect(eeprom, calibration, false);
    PlayerInput in{};
    const std::array<int, 2> centred[2] = {{430, 487}, {590, 487}};
    auto r = report_0x33(0x08, 0x08, 512, 512, 612, centred);  // Up, A; lying flat
    ok &= expect(reader.read(r, in), "a 0x33 report reads");
    ok &= expect(in.buttons.up && in.buttons.a && !in.buttons.b && !in.buttons.c, "Up and A");
    ok &= expect_close(in.gravity[0], 0.0f, 1e-6f, "flat: x 0") && expect_close(in.gravity[1], 0.0f, 1e-6f, "flat: y 0") &&
          expect_close(in.gravity[2], 1.0f, 1e-6f, "flat: z 1 g");
    ok &= expect(in.pointer_on, "the bar seen: a pointer");
    ok &= expect_close(in.pointer_x, 0.0f, 0.01f, "the bar's centre: the picture's (x)") &&
          expect_close(in.pointer_y, 0.0f, 0.01f, "the bar's centre: the picture's (y)");
    ok &= expect_close(in.roll_x, 1.0f, 1e-6f, "level") && expect_close(in.distance, 1.667255f, 1e-4f, "distance 1.67");
    const float first_x = in.pointer_x;
    const std::array<int, 2> left[2] = {{330, 487}, {490, 487}};
    r = report_0x33(0, 0, 512, 512, 612, left);
    ok &= expect(reader.read(r, in) && in.pointer_on, "the bar moved left in the camera");
    const float target = first_x + 100.0f * (2.0f / 1024.0f) * (1.25f / 0.55f);
    ok &= expect_close(in.pointer_x, first_x + (target - first_x) * 0.5f, 1e-4f, "half of a large step: pointing right");
    r = report_0x33(0, 0, 512, 412, 512, left);  // y -1 g: held upright
    ok &= expect(reader.read(r, in) && !in.pointer_on, "held upright: no pointer");
    ok &= expect_close(in.gravity[1], -1.0f, 1e-6f, "upright: y -1 g");
    const std::array<int, 2> too_close[2] = {{500, 487}, {520, 487}};
    r = report_0x33(0, 0, 512, 512, 612, too_close);
    ok &= expect(reader.read(r, in) && !in.pointer_on, "two lights too close are not the bar");
    // A quarter turn: the bar's lights one above the other, gravity along x.
    const std::array<int, 2> turned[2] = {{510, 407}, {510, 567}};
    r = report_0x33(0, 0, 612, 512, 512, turned);
    ok &= expect(reader.read(r, in) && in.pointer_on && std::fabs(in.roll_y) > 0.99f, "rolled a quarter turn");

    reader.connect(eeprom, calibration, true);
    std::array<std::uint8_t, 22> n{};
    n[0] = 0x37;
    n[3] = 0x80;
    n[4] = 0x80;
    n[5] = 0x99;
    for (std::size_t i = 6; i < 16; ++i) n[i] = 0xff;
    n[16] = 0x7b;
    n[17] = 0x81;
    n[18] = 0x80;
    n[19] = 0x80;
    n[20] = 0xb2;
    n[21] = 0x03;  // C and Z up
    ok &= expect(reader.read(n, in) && in.stick_x == 0.0f && in.stick_y == 0.0f && !in.buttons.c && !in.buttons.z,
                 "the Nunchuk at rest");
    ok &= expect_close(in.nunchuk_gravity[2], 1.0f, 1e-6f, "the Nunchuk lying flat: z 1 g");
    n[16] = 0x7b + 101;
    n[17] = 0x81 - 43;
    n[21] = 0x01;  // C down
    ok &= expect(reader.read(n, in) && in.buttons.c && !in.buttons.z, "C pressed");
    ok &= expect(in.stick_y < 0.0f && in.stick_x > 0.0f, "the stick right and down from where it rested");
    ok &= expect_close(in.stick_x * in.stick_x + in.stick_y * in.stick_y, 1.0f, 1e-5f, "a full push reaches the circle");
    return ok;
}

// A real Wii Remote's output reports and replies (WiiBrew's layouts).
bool real_wiimote_protocol() {
    namespace wm = galaxy::input::wiimote;
    bool ok = true;
    std::array<std::uint8_t, wm::kOutputBytes> out{};
    out.fill(0xee);
    ok &= expect(wm::build_report_mode(0x37, true, true, out) == 3u && out[0] == 0x12 && out[1] == 0x05 &&
                     out[2] == 0x37 && out[3] == 0x00,
                 "report mode: continuous with the motor on, the rest zeroed");
    ok &= expect(wm::build_rumble(false, out) == 2u && out[0] == 0x10 && out[1] == 0x00, "motor off");
    ok &= expect(wm::build_leds(0x01, false, out) == 2u && out[0] == 0x11 && out[1] == 0x10, "LED 1");
    ok &= expect(wm::build_enable(wm::kOutIrLogic, true, false, out) == 2u && out[0] == 0x1a && out[1] == 0x04,
                 "IR logic on");
    ok &= expect(wm::build_read(wm::kRegisters, wm::kExtensionCalibration, 32, false, out) == 7u && out[0] == 0x17 &&
                     out[1] == 0x04 && out[2] == 0xa4 && out[3] == 0x00 && out[4] == 0x20 && out[5] == 0x00 &&
                     out[6] == 32,
                 "read: space, 24-bit address, 16-bit size");
    const std::uint8_t sensitivity[2] = {0x63, 0x03};
    ok &= expect(wm::build_write(wm::kRegisters, 0xb0001a, sensitivity, true, out) == 22u && out[0] == 0x16 &&
                     out[1] == 0x05 && out[2] == 0xb0 && out[3] == 0x00 && out[4] == 0x1a && out[5] == 2 &&
                     out[6] == 0x63 && out[7] == 0x03 && out[8] == 0x00,
                 "write: 16 data bytes after the size");
    std::array<std::uint8_t, 17> too_long{};
    ok &= expect(wm::build_write(wm::kRegisters, 0, too_long, false, out) == 0u, "a write holds at most 16 bytes");
    ok &= expect(wm::ir_mode_for_report(0x33) == 3u && wm::ir_mode_for_report(0x37) == 1u &&
                     wm::ir_mode_for_report(0x31) == 0u,
                 "IR mode follows the data report");
    const auto setup = wm::ir_setup(1);
    ok &= expect(setup[0].address == 0xb00030 && setup[0].data[0] == 0x08 && setup[1].size == 9u &&
                     setup[1].data[6] == 0xaa && setup[3].address == 0xb00033 && setup[3].data[0] == 1u &&
                     setup[4].address == 0xb00030,
                 "IR setup: enable, sensitivity level 3, mode, enable");

    const std::uint8_t status[7] = {0x20, 0x00, 0x00, 0x0a, 0x00, 0x00, 0xc8};
    const wm::Status parsed = wm::parse_status(status);
    ok &= expect(parsed.valid && parsed.extension && parsed.ir && parsed.battery == 0xc8, "status flags and battery");
    std::array<std::uint8_t, 22> reply{};
    reply[0] = 0x21;
    reply[3] = 0x90;  // 10 bytes, no error
    reply[4] = 0x00;
    reply[5] = 0x20;
    reply[6] = 0xab;
    const wm::ReadReply read = wm::parse_read_reply(reply);
    ok &= expect(read.valid && read.error == 0u && read.address == 0x0020 && read.data.size() == 10u &&
                     read.data[0] == 0xab,
                 "read reply: size, error, address");
    const std::uint8_t ack[5] = {0x22, 0x00, 0x00, 0x16, 0x00};
    const wm::Ack parsed_ack = wm::parse_ack(ack);
    ok &= expect(parsed_ack.valid && parsed_ack.report == 0x16 && parsed_ack.error == 0u, "write acknowledged");
    const std::uint8_t nunchuk[6] = {0x00, 0x00, 0xa4, 0x20, 0x00, 0x00};
    const std::uint8_t classic[6] = {0x00, 0x00, 0xa4, 0x20, 0x01, 0x01};
    ok &= expect(wm::is_nunchuk(nunchuk) && !wm::is_nunchuk(classic), "a Nunchuk by its id");
    ok &= expect(wm::is_data_report(0x37) && !wm::is_data_report(0x21), "data reports 0x30-0x3f");
    ok &= expect(wm::battery_level(0xc8) == 4u && wm::battery_level(0x40) == 2u && wm::battery_level(0x10) == 1u &&
                     wm::battery_level(0x02) == 0u,
                 "battery levels");
    return ok;
}

// The hand-off of a real remote's state and reports to the host's input thread.
bool real_wiimote_handoff() {
    using galaxy::host::RealWiimoteEvent;
    bool ok = true;
    galaxy::host::RealWiimoteHandoff handoff;
    std::array<std::uint8_t, 22> report{};
    std::size_t size = 0;
    galaxy::host::RealWiimoteConnection got{};
    const auto next = [&] { return handoff.wait(std::chrono::milliseconds(0), report, size, got); };
    const std::uint8_t a[3] = {0x37, 0x01, 0x02};
    const std::uint8_t b[3] = {0x37, 0x03, 0x04};
    ok &= expect(next() == RealWiimoteEvent::None && !handoff.in_use(), "nothing at first");
    handoff.report(a);
    ok &= expect(next() == RealWiimoteEvent::None, "a report before the connection is dropped");
    galaxy::host::RealWiimoteConnection connection{};
    connection.nunchuk = true;
    connection.battery = 4;
    handoff.connected(connection);
    handoff.report(a);
    ok &= expect(next() == RealWiimoteEvent::Connected && got.nunchuk && got.battery == 4u && !handoff.in_use(),
                 "the connection first, not in use until the host plays with it");
    handoff.set_active(true);
    ok &= expect(handoff.in_use(), "in use once active");
    ok &= expect(next() == RealWiimoteEvent::Report && size == 3u && report[1] == 0x01, "then its report");
    ok &= expect(next() == RealWiimoteEvent::None, "then nothing");
    handoff.report(a);
    handoff.disconnected();
    handoff.connected(connection);
    handoff.report(b);
    ok &= expect(next() == RealWiimoteEvent::Connected, "a drop and return the host missed reads as connected");
    ok &= expect(next() == RealWiimoteEvent::Report && report[1] == 0x03 && next() == RealWiimoteEvent::None,
                 "only the new connection's report");
    handoff.connected(connection);
    handoff.disconnected();
    ok &= expect(next() == RealWiimoteEvent::Disconnected && !handoff.in_use() && next() == RealWiimoteEvent::None,
                 "a connection gone before the host read it reads as gone, once");
    handoff.report(a);
    ok &= expect(next() == RealWiimoteEvent::None, "no reports while disconnected");
    handoff.connected(connection);
    ok &= expect(next() == RealWiimoteEvent::Connected, "connected again");
    handoff.battery(1);
    ok &= expect(next() == RealWiimoteEvent::Battery && got.battery == 1u, "a battery change");
    for (std::uint8_t i = 0; i < 40; ++i) {
        const std::uint8_t r[2] = {0x30, i};
        handoff.report(r);
    }
    bool ordered = true;
    for (std::uint8_t i = 8; i < 40; ++i) ordered = ordered && next() == RealWiimoteEvent::Report && report[1] == i;
    ok &= expect(ordered && next() == RealWiimoteEvent::None, "a full ring keeps the newest 32, oldest first");
    const std::uint8_t idle[3] = {0x37, 0x60, 0x60};  // accelerometer bits only
    const std::uint8_t a_held[3] = {0x37, 0x00, 0x08};
    ok &= expect(!galaxy::host::real_wiimote_buttons_held(idle) && galaxy::host::real_wiimote_buttons_held(a_held),
                 "a button press, not the accelerometer's low bits");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= settings_pad_toggles_navigates_and_hides_buttons();
    ok &= native_input_script_unsigned_fields_fail_closed();
    ok &= normalizes_xinput_axes();
    ok &= player_buttons_are_bits();
    ok &= virtual_pointer_and_motion();
    ok &= nunchuk_stick_response();
    ok &= real_remote_reports();
    ok &= real_wiimote_protocol();
    ok &= real_wiimote_handoff();
    return ok ? 0 : 1;
}
