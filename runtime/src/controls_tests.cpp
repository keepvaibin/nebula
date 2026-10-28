// Remappable controls: binding model, ini round trip, evaluation, chord
// mapping, Switch report decoding, the motion integrator, pointer ownership,
// the stick pointer, the jump/back context rule, tilt sections, vibration and
// settings migration.

#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/pointer_control.h"
#include "galaxy/rumble.h"
#include "galaxy/switch_controller.h"
#include "galaxy/tilt.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace galaxy::input;

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++g_failures;
    }
}

bool approx(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

void test_names_round_trip() {
    for (std::size_t s = 0; s < kControlSchemeCount; ++s) {
        const auto scheme = static_cast<ControlScheme>(s);
        expect(parse_control_scheme(control_scheme_id(scheme)) == scheme, "scheme id round trip");
        const BindingTable table = default_binding_table(scheme);
        for (std::size_t t = 0; t < kWiiTargetCount; ++t) {
            const auto target = static_cast<WiiTarget>(t);
            expect(parse_wii_target(wii_target_id(target)) == target, "target id round trip");
            const std::string text = format_target_bindings(scheme, table[target]);
            TargetBindings parsed{};
            expect(parse_target_bindings(scheme, text, parsed), "default bindings parse");
            expect(parsed == table[target], "default bindings round trip");
            for (const InputSource& source : table[target]) {
                if (source.empty()) continue;
                expect(binding_source_allowed(scheme, target, source), "defaults are allowed sources");
            }
        }
    }
    // Every virtual key formats to a name that parses back.
    for (unsigned vk = 1; vk < 256; ++vk) {
        const InputSource source{SourceKind::Key, static_cast<std::uint8_t>(vk)};
        const std::string name = format_input_source(ControlScheme::Keyboard, source);
        const auto parsed = parse_input_source(ControlScheme::Keyboard, name);
        expect(parsed.has_value() && *parsed == source, "virtual key name round trip");
    }
    // Pad names per scheme.
    for (ControlScheme scheme : {ControlScheme::Xbox, ControlScheme::SwitchPro, ControlScheme::SwitchPro2,
                                 ControlScheme::PlayStation, ControlScheme::Steam, ControlScheme::Generic}) {
        for (unsigned bit = 0; bit < pad::kButtonBitCount; ++bit) {
            const InputSource source{SourceKind::Button, static_cast<std::uint8_t>(bit)};
            const auto parsed = parse_input_source(scheme, format_input_source(scheme, source));
            expect(parsed.has_value() && *parsed == source, "pad button name round trip");
        }
        for (unsigned axis = 0; axis < kPadAxisCount; ++axis) {
            const InputSource pos{SourceKind::AxisPositive, static_cast<std::uint8_t>(axis)};
            expect(parse_input_source(scheme, format_input_source(scheme, pos)) == pos, "axis+ round trip");
            if (axis < 4u) {
                const InputSource neg{SourceKind::AxisNegative, static_cast<std::uint8_t>(axis)};
                expect(parse_input_source(scheme, format_input_source(scheme, neg)) == neg, "axis- round trip");
            }
        }
    }
    expect(format_input_source(ControlScheme::Xbox, {SourceKind::Button, pad::kSouthBit}) == "A",
           "Xbox south is A");
    expect(format_input_source(ControlScheme::SwitchPro, {SourceKind::Button, pad::kSouthBit}) == "B",
           "Nintendo south is B");
    expect(format_input_source(ControlScheme::SwitchPro, {SourceKind::AxisPositive, 5}) == "ZR",
           "Nintendo right trigger is ZR");
    TargetBindings bad{};
    bad[0] = {SourceKind::Key, 'X'};
    expect(!parse_target_bindings(ControlScheme::Keyboard, "Space, NoSuchKey", bad) &&
               bad[0] == InputSource{SourceKind::Key, 'X'},
           "an unknown name rejects the line and keeps the bindings");
    expect(!binding_source_allowed(ControlScheme::Keyboard, WiiTarget::StickUp,
                                   {SourceKind::Mouse, 0}),
           "mouse buttons cannot drive the stick");
    expect(!binding_source_allowed(ControlScheme::Xbox, WiiTarget::Home,
                                   {SourceKind::Button, pad::kHomeBit}),
           "XInput cannot read Guide");
    expect(binding_source_allowed(ControlScheme::SwitchPro2, WiiTarget::C,
                                  {SourceKind::Button, pad::kGripLeftBit}) &&
               !binding_source_allowed(ControlScheme::SwitchPro, WiiTarget::C,
                                       {SourceKind::Button, pad::kGripLeftBit}),
           "GL exists only on the Pro Controller 2");
}

void test_ini_round_trip() {
    ControlsConfig config = default_controls_config();
    config.table(ControlScheme::Xbox)[WiiTarget::A] = {};
    add_binding(config.table(ControlScheme::Xbox)[WiiTarget::A], {SourceKind::Button, pad::kNorthBit});
    add_binding(config.table(ControlScheme::Keyboard)[WiiTarget::NunchukShake], {SourceKind::Key, 'F'});
    config.tune(ControlScheme::SwitchPro).gyro_pointer = false;
    config.tune(ControlScheme::SwitchPro2).gyro_sensitivity = 1.7f;
    config.tune(ControlScheme::SwitchPro).shake_sensitivity = 5;
    config.tune(ControlScheme::SwitchPro2).motion_shake = MotionShake::Nunchuk;
    config.tune(ControlScheme::Xbox).pointer_speed = 3.7f;
    config.tune(ControlScheme::Keyboard).invert_pointer_y = true;
    config.tune(ControlScheme::Xbox).vibration = false;
    config.tune(ControlScheme::SwitchPro).rumble = 70;
    const std::string text = format_controls_settings(config);
    ControlsConfig parsed = default_controls_config();
    std::size_t lines = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string line = text.substr(start, end - start);
        start = end + 1u;
        const std::size_t eq = line.find('=');
        expect(eq != std::string::npos, "every line is key=value");
        expect(apply_controls_setting(parsed, line.substr(0, eq), line.substr(eq + 1u)), "line is owned");
        ++lines;
    }
    expect(lines >= kControlSchemeCount * kWiiTargetCount, "one line per binding");
    expect(parsed == config, "controls survive the ini round trip exactly");
    expect(text.find("bind.keyboard.a=Space, Enter, MouseLeft\n") != std::string::npos,
           "keyboard A line is stable text");
    expect(text.find("bind.xbox.b=LB\n") != std::string::npos, "xbox B line is stable text");
    expect(text.find("bind.xbox.jump_back=B\n") != std::string::npos, "xbox jump/back line is stable text");
    expect(text.find("bind.switch_pro.a=A\n") != std::string::npos, "switch A line is stable text");
    expect(text.find("tune.xbox.vibration=0\n") != std::string::npos &&
               text.find("tune.switch_pro.rumble=70\n") != std::string::npos &&
               text.find("tune.switch_pro2.vibration=1\n") != std::string::npos &&
               text.find("tune.switch_pro2.rumble=50\n") != std::string::npos &&
               text.find("tune.keyboard.rumble") == std::string::npos &&
               text.find("tune.keyboard.vibration") == std::string::npos,
           "vibration lines (controllers only, default on at 50%)");
    ControlsConfig snapped = default_controls_config();
    expect(apply_controls_setting(snapped, "tune.switch_pro2.rumble", "65") &&
               snapped.tune(ControlScheme::SwitchPro2).rumble == 60u,
           "vibration strength snaps to the nearest 10%, ties down");
    expect(apply_controls_setting(snapped, "tune.xbox.rumble", "loud") &&
               snapped.tune(ControlScheme::Xbox).rumble == 50u,
           "malformed vibration strength keeps the previous value");
    expect(apply_controls_setting(snapped, "tune.xbox.rumble", "0") &&
               !snapped.tune(ControlScheme::Xbox).vibration && snapped.tune(ControlScheme::Xbox).rumble == 50u,
           "a version-4 strength of 0 turns vibration off");
    expect(!apply_controls_setting(parsed, "audio_master_gain", "1"), "foreign keys are not owned");
    ControlsConfig malformed = default_controls_config();
    expect(apply_controls_setting(malformed, "tune.xbox.pointer_speed", "fast"), "malformed tune is owned");
    expect(malformed == default_controls_config(), "malformed value keeps the previous value");
    // Deadzone default reproduces the XInput constant exactly.
    const float dz = default_scheme_tuning(ControlScheme::Xbox).stick_deadzone;
    expect(std::lround(dz * 32767.0f) == XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, "xbox left deadzone default");
}

void test_evaluation() {
    const ControlsConfig config = default_controls_config();
    KeyboardMouseLevels levels{};
    levels.set_key('W', true);
    levels.set_key('D', true);
    levels.set_key(VK_RETURN, true);
    levels.set_key('L', true);
    LogicalInput kb = evaluate_keyboard_bindings(config.table(ControlScheme::Keyboard), levels);
    expect(kb.held(WiiTarget::A) && !kb.held(WiiTarget::B), "Enter presses A");
    expect(approx(kb.stick_x, 1.0f) && approx(kb.stick_y, 1.0f), "WD gives up-right stick");
    expect(approx(kb.pointer_x, 1.0f) && approx(kb.pointer_y, 0.0f), "L moves the pointer right");
    levels.set_key('A', true);
    kb = evaluate_keyboard_bindings(config.table(ControlScheme::Keyboard), levels);
    expect(approx(kb.stick_x, 0.0f), "A and D cancel like the fixed map");
    levels = {};
    levels.mouse = 1u << static_cast<unsigned>(MouseButton::Right);
    expect(mouse_bound_targets(config.table(ControlScheme::Keyboard), levels.mouse) ==
               wii_target_bit(WiiTarget::B),
           "right mouse button drives B");

    PadSample sample{};
    sample.connected = true;
    sample.scheme = ControlScheme::Xbox;
    sample.buttons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_LEFT_SHOULDER;
    sample.axes[static_cast<std::size_t>(PadAxis::LeftX)] = -0.4f;
    sample.axes[static_cast<std::size_t>(PadAxis::RightY)] = 0.8f;
    sample.axes[static_cast<std::size_t>(PadAxis::RightTrigger)] = 96.0f / 255.0f;
    const float threshold = 96.0f / 255.0f;
    LogicalInput pad = evaluate_pad_bindings(config.table(ControlScheme::Xbox), sample, threshold);
    expect(pad.held(WiiTarget::A) && pad.held(WiiTarget::B), "XInput A and LB map to A and B");
    expect(pad.held(WiiTarget::Shake) && !pad.held(WiiTarget::Z), "right trigger at the threshold spins");
    expect(approx(pad.stick_x, -0.4f) && !pad.held(WiiTarget::StickLeft), "analog stick passes through");
    expect(approx(pad.pointer_y, 0.8f), "right stick drives the pointer");
    sample.axes[static_cast<std::size_t>(PadAxis::RightTrigger)] = 95.0f / 255.0f;
    pad = evaluate_pad_bindings(config.table(ControlScheme::Xbox), sample, threshold);
    expect(!pad.held(WiiTarget::Z), "below the threshold the trigger is released");

    // Axis direction bound to a digital target.
    BindingTable custom = config.table(ControlScheme::Xbox);
    custom[WiiTarget::One] = {};
    add_binding(custom[WiiTarget::One], {SourceKind::AxisNegative, static_cast<std::uint8_t>(PadAxis::LeftY)});
    sample = {};
    sample.connected = true;
    sample.axes[static_cast<std::size_t>(PadAxis::LeftY)] = -0.49f;
    expect(!evaluate_pad_bindings(custom, sample, threshold).held(WiiTarget::One), "half axis below 0.5");
    sample.axes[static_cast<std::size_t>(PadAxis::LeftY)] = -0.5f;
    expect(evaluate_pad_bindings(custom, sample, threshold).held(WiiTarget::One), "half axis at 0.5");
    sample.connected = false;
    expect(evaluate_pad_bindings(custom, sample, threshold).pressed == 0u, "disconnected pad is neutral");

    LogicalInput a{};
    a.pressed = wii_target_bit(WiiTarget::A);
    a.stick_x = 0.3f;
    LogicalInput b{};
    b.pressed = wii_target_bit(WiiTarget::B);
    b.stick_x = -0.6f;
    const LogicalInput merged = merge_logical_input(a, b);
    expect(merged.held(WiiTarget::A) && merged.held(WiiTarget::B) && approx(merged.stick_x, -0.6f),
           "merge ORs buttons and keeps the larger axis");

    const KeyList keys = referenced_keys(config.table(ControlScheme::Keyboard));
    expect(keys.count == 32u, "default keyboard map reads the 32 keys of the fixed map");
}

void test_chord_and_capture() {
    using namespace settings_pad;
    // Switch Minus+Plus is the overlay chord.
    const std::uint32_t switch_chord = pad::bit(pad::kBackBit) | pad::bit(pad::kStartBit);
    expect((settings_pad_buttons(ControlScheme::SwitchPro, switch_chord) & kChord) == kChord,
           "Minus+Plus maps to the overlay chord");
    // Nintendo A (east) acts as the overlay's A; Nintendo B closes.
    expect(settings_pad_buttons(ControlScheme::SwitchPro2, pad::bit(pad::kEastBit)) == kA,
           "Nintendo A is the overlay A");
    expect(settings_pad_buttons(ControlScheme::SwitchPro, pad::bit(pad::kSouthBit)) == kB,
           "Nintendo B is the overlay B");
    expect(settings_pad_buttons(ControlScheme::Xbox, XINPUT_GAMEPAD_A) == kA, "Xbox A unchanged");
    const std::uint32_t buttons = pad::bit(pad::kEastBit) | pad::bit(pad::kWestBit) | pad::bit(pad::kGripLeftBit);
    expect(unified_from_settings_pad(ControlScheme::SwitchPro2,
                                     settings_pad_buttons(ControlScheme::SwitchPro2, buttons), buttons) == buttons,
           "the swap is its own inverse and keeps extra bits");
    NativeSettingsPadState state{};
    auto step = native_settings_pad_step(state, settings_pad_buttons(ControlScheme::SwitchPro, switch_chord),
                                         0.0f, 0.0f, false, 0u);
    expect(step.key_count == 1u && step.keys[0] == kKeyToggle && step.game_neutral,
           "Switch chord toggles the overlay and hides itself from the game");

    PadSample base{};
    base.connected = true;
    base.buttons = pad::bit(pad::kSouthBit);
    PadSample now = base;
    expect(!detect_pad_capture(base, now).has_value(), "a held button is not captured");
    now.buttons |= pad::bit(pad::kGripRightBit);
    expect(detect_pad_capture(base, now) == InputSource{SourceKind::Button, pad::kGripRightBit},
           "a new press is captured");
    now = base;
    now.axes[static_cast<std::size_t>(PadAxis::RightX)] = -0.7f;
    expect(detect_pad_capture(base, now) ==
               InputSource{SourceKind::AxisNegative, static_cast<std::uint8_t>(PadAxis::RightX)},
           "a stick direction is captured");
    now = base;
    now.axes[static_cast<std::size_t>(PadAxis::LeftTrigger)] = 0.9f;
    expect(detect_pad_capture(base, now) ==
               InputSource{SourceKind::AxisPositive, static_cast<std::uint8_t>(PadAxis::LeftTrigger)},
           "a trigger is captured");
}

std::vector<std::uint8_t> full_report() {
    std::vector<std::uint8_t> r(49, 0);
    r[0] = switch_hid::kReportFullState;
    return r;
}

void put_u12_pair(std::uint8_t* d, int x, int y) {
    d[0] = static_cast<std::uint8_t>(x & 0xff);
    d[1] = static_cast<std::uint8_t>(((x >> 8) & 0x0f) | ((y & 0x0f) << 4));
    d[2] = static_cast<std::uint8_t>(y >> 4);
}

void put_s16(std::uint8_t* d, int v) {
    d[0] = static_cast<std::uint8_t>(v & 0xff);
    d[1] = static_cast<std::uint8_t>((v >> 8) & 0xff);
}

void test_switch_pro_decoding() {
    using namespace switch_hid;
    // Factory stick calibration (left: max, center, min; right: center, min, max).
    std::array<std::uint8_t, 9> left_cal{};
    put_u12_pair(&left_cal[0], 1500, 1400);   // above
    put_u12_pair(&left_cal[3], 2000, 2100);   // center
    put_u12_pair(&left_cal[6], 1300, 1200);   // below
    const StickCal left = parse_left_stick_cal(left_cal);
    expect(left.x.center == 2000 && left.y.center == 2100 && left.x.above == 1500 &&
               left.y.above == 1400 && left.x.below == 1300 && left.y.below == 1200,
           "left stick calibration field order");
    std::array<std::uint8_t, 9> right_cal{};
    put_u12_pair(&right_cal[0], 2048, 2040);
    put_u12_pair(&right_cal[3], 1100, 1110);
    put_u12_pair(&right_cal[6], 1200, 1210);
    const StickCal right = parse_right_stick_cal(right_cal);
    expect(right.x.center == 2048 && right.y.center == 2040 && right.x.below == 1100 &&
               right.x.above == 1200 && right.y.above == 1210,
           "right stick calibration field order");
    std::array<std::uint8_t, 9> blank{};
    blank.fill(0xff);
    const StickCal fallback = parse_left_stick_cal(blank);
    expect(fallback.x.center == 2048 && fallback.x.above > 1000, "unprogrammed calibration falls back");
    expect(approx(normalize_stick(2000, left.x), 0.0f) &&
               approx(normalize_stick(2000 + 1125, left.x), 1.0f) &&
               approx(normalize_stick(2000 - 975, left.x), -1.0f) &&
               approx(normalize_stick(4095, left.x), 1.0f),
           "stick normalisation reaches full scale at 75% of the calibrated extent");

    auto r = full_report();
    r[3] = 0x08 | 0x80;  // A, ZR
    r[4] = 0x02 | 0x10;  // Plus, Home
    r[5] = 0x02 | 0x40;  // Up, L
    put_u12_pair(&r[6], 2000 + 1125, 2100);
    put_u12_pair(&r[9], 2048, 2040 + 700);
    put_s16(&r[13], 0);      // accel x
    put_s16(&r[15], 0);      // accel y
    put_s16(&r[17], 4096);   // accel z = 1 g flat
    put_s16(&r[19], 0);
    put_s16(&r[21], 0);
    put_s16(&r[23], 143);    // gyro z ~ 10 dps
    const ImuCal imu_cal{};
    DecodedState decoded{};
    expect(decode_pro_report(r, imu_cal, decoded), "0x30 decodes");
    expect(decoded.buttons == (pad::bit(pad::kEastBit) | pad::bit(pad::kStartBit) |
                               pad::bit(pad::kHomeBit) | pad::bit(pad::kDpadUpBit) |
                               pad::bit(pad::kLeftShoulderBit)),
           "0x30 button bits");
    expect(decoded.right_trigger && !decoded.left_trigger, "ZR is a trigger");
    expect(decoded.imu_count == 1u, "zero IMU samples are skipped");
    expect(approx(decoded.imu[0].accel[1], 1.0f) && approx(decoded.imu[0].accel[0], 0.0f),
           "flat controller reads +1 g up");
    expect(approx(decoded.imu[0].gyro[1], 143.0f / 14.2842f * 3.14159265f / 180.0f, 1e-3f),
           "gyro z becomes yaw");
    const PadSample sample = pad_sample_from(decoded, ControlScheme::SwitchPro, left, right, 0.12f, 0.12f);
    expect(approx(sample.axes[0], 1.0f) && approx(sample.axes[1], 0.0f), "left stick full right");
    expect(sample.axes[3] > 0.5f && approx(sample.axes[5], 1.0f), "right stick up, ZR full");
    const LogicalInput logic = evaluate_pad_bindings(
        default_binding_table(ControlScheme::SwitchPro), sample, 96.0f / 255.0f);
    expect(logic.held(WiiTarget::A) && logic.held(WiiTarget::B) && logic.held(WiiTarget::Plus) &&
               logic.held(WiiTarget::Home) && logic.held(WiiTarget::DpadUp) && !logic.held(WiiTarget::Z) && logic.held(WiiTarget::Shake),
           "Switch defaults: A->A, L->B, ZR->spin, Plus, Home, D-pad");

    // Simple 0x3F report (Bluetooth default mode).
    std::vector<std::uint8_t> simple(12, 0);
    simple[0] = kReportSimpleState;
    simple[1] = 0x01;  // B
    simple[2] = 0x01;  // Minus
    simple[3] = 2;     // hat right
    for (int i = 0; i < 4; ++i) put_s16(&simple[4 + i * 2], 0x8000);
    expect(decode_pro_report(simple, imu_cal, decoded) && decoded.simple, "0x3F decodes");
    expect(decoded.buttons == (pad::bit(pad::kSouthBit) | pad::bit(pad::kBackBit) |
                               pad::bit(pad::kDpadRightBit)),
           "0x3F buttons and hat");

    // Subcommand framing and replies.
    std::array<std::uint8_t, 64> out{};
    const std::size_t n = build_spi_read(3, kSpiFactoryStickCal, 18, out);
    expect(n == 16u && out[0] == 0x01 && out[1] == 3 && out[2] == 0x00 && out[3] == 0x01 &&
               out[10] == kSubcommandSpiRead && out[11] == 0x3d && out[12] == 0x60 && out[15] == 18,
           "SPI read subcommand layout");
    std::vector<std::uint8_t> reply(64, 0);
    reply[0] = kReportSubcommandReply;
    reply[13] = 0x90;
    reply[14] = kSubcommandSpiRead;
    reply[15] = 0x3d;
    reply[16] = 0x60;
    reply[19] = 18;
    reply[20] = 0xab;
    const auto matched = match_subcommand_reply(reply, kSubcommandSpiRead, kSpiFactoryStickCal);
    expect(matched.matched && matched.data.size() == 18u && matched.data[0] == 0xab, "SPI reply matched");
    expect(!match_subcommand_reply(reply, kSubcommandSpiRead, kSpiFactoryImuCal).matched,
           "a reply for another address is ignored");
    expect(build_usb_command(kUsbHandshake, out) == 2u && out[0] == 0x80 && out[1] == 0x02,
           "USB handshake command");

    // IMU factory calibration: accel sensitivity 16384 over origin 0 = 1/4096 g.
    std::array<std::uint8_t, 24> imu{};
    for (int axis = 0; axis < 3; ++axis) {
        put_s16(&imu[6 + axis * 2], 16384);
        put_s16(&imu[12 + axis * 2], 10);
        put_s16(&imu[18 + axis * 2], 13371 + 10);
    }
    const ImuCal parsed_cal = parse_imu_cal(imu);
    expect(approx(parsed_cal.accel_scale[0], 1.0f / 4096.0f, 1e-8f) && parsed_cal.gyro_offset[1] == 10,
           "IMU calibration parse");
}

// What the controller worker does with each Joy-Con report, timed (a pair: both halves' reports decoded, the right
// half's sensor turned, the halves combined, each half's three motion samples fed, the pairs checked): the worker's
// own share of a press's way to the game, printed for the record.
void time_joycon_reports() {
    using namespace switch_hid;
    const ImuCal imu_cal{};
    auto l = full_report();
    auto r = full_report();
    l[5] = 0x40;
    r[3] = 0x40;
    put_s16(&l[17], 4096);
    put_s16(&r[17], 4096);
    SwitchMotion left_motion{};
    SwitchMotion right_motion{};
    MotionSettings settings{};
    settings.shake = true;
    settings.gyro_pointer = true;
    std::vector<JoyConHalf> halves(2);
    halves[0].side = JoyConSide::Left;
    halves[1].side = JoyConSide::Right;
    constexpr int kReports = 200000;
    std::uint32_t sink = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kReports; ++i) {
        l[1] = static_cast<std::uint8_t>(i);
        r[1] = static_cast<std::uint8_t>(i);
        DecodedState left{};
        DecodedState right{};
        sink += decode_pro_report(l, imu_cal, left) ? 1u : 0u;
        sink += decode_pro_report(r, imu_cal, right) ? 1u : 0u;
        turn_right_joycon_imu(right);
        const DecodedState pair = combine_joycons(left, right);
        sink += pair.buttons;
        const std::uint64_t t = static_cast<std::uint64_t>(i) * 15000u;
        for (std::size_t k = 0; k < left.imu_count; ++k) sink += left_motion.feed(left.imu[k], 0.005f, t, settings).shake_started;
        for (std::size_t k = 0; k < right.imu_count; ++k) sink += right_motion.feed(right.imu[k], 0.005f, t, settings).shake_started;
        sink += pair_joycons(halves, false).changed ? 1u : 0u;
    }
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
    const double each = ns / kReports / 2.0;
    std::printf("[timing] a Joy-Con report in the worker (decode, turn, combine, 3 motion samples, pairs): %.0f ns\n", each);
    expect(sink != 0u && each < 50000.0, "a Joy-Con report takes the worker well under a millisecond");
}

void test_joycons() {
    using namespace switch_hid;
    expect(joycon_side_of_type(1) == JoyConSide::Left && joycon_side_of_type(2) == JoyConSide::Right &&
               joycon_side_of_type(3) == JoyConSide::None,
           "device info types: 1 left, 2 right, 3 a Pro Controller");
    expect(joycon_side_of_product(kJoyConLeftProductId) == JoyConSide::Left &&
               joycon_side_of_product(kJoyConRightProductId) == JoyConSide::Right &&
               joycon_side_of_product(kChargingGripProductId) == JoyConSide::None &&
               joycon_side_of_product(kProControllerProductId) == JoyConSide::None,
           "Joy-Con products; a charging grip's side comes from the controller");

    const ImuCal imu_cal{};
    auto l = full_report();
    l[3] = 0x08;                 // a right-side bit a left half never sends: masked out
    l[4] = 0x01 | 0x08 | 0x20;   // Minus, left stick, Capture
    l[5] = 0x02 | 0x40 | 0x80;   // Up, L, ZL
    put_u12_pair(&l[6], 3000, 1000);
    put_s16(&l[17], 4096);
    DecodedState left{};
    expect(decode_pro_report(l, imu_cal, left), "a left half's 0x30 decodes");
    auto r = full_report();
    r[3] = 0x04 | 0x40 | 0x80;   // B, R, ZR
    r[4] = 0x02 | 0x04 | 0x10;   // Plus, right stick, Home
    r[5] = 0x01;                 // a left-side bit a right half never sends: masked out
    put_u12_pair(&r[9], 1500, 2500);
    put_s16(&r[13], 100);        // accel x
    put_s16(&r[15], 200);        // accel y
    put_s16(&r[17], 4096);       // accel z
    put_s16(&r[19], 143);        // gyro x
    put_s16(&r[21], 286);        // gyro y
    DecodedState right{};
    expect(decode_pro_report(r, imu_cal, right), "a right half's 0x30 decodes");
    const ImuSample before = right.imu[0];
    turn_right_joycon_imu(right);
    expect(right.imu[0].accel[0] == -before.accel[0] && right.imu[0].accel[1] == -before.accel[1] &&
               right.imu[0].accel[2] == before.accel[2] && right.imu[0].gyro[0] == -before.gyro[0] &&
               right.imu[0].gyro[1] == -before.gyro[1] && right.imu[0].gyro[2] == before.gyro[2],
           "a right half's sensor reads its first two axes negated");

    const DecodedState pair = combine_joycons(left, right);
    expect(pair.buttons == (pad::bit(pad::kBackBit) | pad::bit(pad::kLeftStickBit) | pad::bit(pad::kCaptureBit) |
                            pad::bit(pad::kDpadUpBit) | pad::bit(pad::kLeftShoulderBit) | pad::bit(pad::kSouthBit) |
                            pad::bit(pad::kRightShoulderBit) | pad::bit(pad::kStartBit) |
                            pad::bit(pad::kRightStickBit) | pad::bit(pad::kHomeBit)),
           "a pair takes each half's own buttons");
    expect(pair.left_trigger && pair.right_trigger, "ZL from the left half, ZR from the right");
    expect(pair.stick_raw[0] == 3000 && pair.stick_raw[1] == 1000 && pair.stick_raw[2] == 1500 &&
               pair.stick_raw[3] == 2500,
           "each stick from its half");
    expect(pair.imu_count == right.imu_count && pair.imu[0].gyro[0] == right.imu[0].gyro[0],
           "the pair's motion is the right half's");
    const PadSample sample = pad_sample_from(pair, ControlScheme::SwitchPro, StickCal{}, StickCal{}, 0.12f, 0.12f);
    const LogicalInput logic = evaluate_pad_bindings(default_binding_table(ControlScheme::SwitchPro), sample,
                                                     96.0f / 255.0f);
    expect(logic.held(WiiTarget::Plus) && logic.held(WiiTarget::Home) && logic.held(WiiTarget::DpadUp),
           "a pair drives the Switch bindings");

    // Pairing.
    std::array<JoyConHalf, 2> two{{{JoyConSide::Left, -1, false}, {JoyConSide::Right, -1, false}}};
    JoyConPairing pairing = pair_joycons(two, false);
    expect(pairing.changed && !pairing.made && two[0].partner == 1 && two[1].partner == 0,
           "the only left and right pair by themselves");
    expect(!pair_joycons(two, false).changed, "a pair stays as it is");
    two[0].shoulder = two[1].shoulder = true;
    pairing = pair_joycons(two, true);
    expect(!pairing.changed && pairing.made, "on the screen, a pair holding both is made again (the screen confirms it)");
    std::array<JoyConHalf, 3> extra{{{JoyConSide::Left, -1, false}, {JoyConSide::Left, -1, false},
                                     {JoyConSide::Right, -1, false}}};
    expect(!pair_joycons(extra, false).changed && extra[2].partner == -1, "two lefts for one right wait for the screen");
    extra[1].shoulder = true;
    pairing = pair_joycons(extra, true);
    expect(!pairing.changed && !pairing.made, "the screen waits for both shoulders");
    extra[2].shoulder = true;
    pairing = pair_joycons(extra, true);
    expect(pairing.changed && pairing.made && extra[1].partner == 2 && extra[2].partner == 1 && extra[0].partner == -1,
           "on the screen, L and R held together pair those two");
    // Another pair's L held while two new halves are held: the new ones pair, the other pair stays.
    std::array<JoyConHalf, 4> others{{{JoyConSide::Left, 1, true}, {JoyConSide::Right, 0, false},
                                      {JoyConSide::Left, -1, true}, {JoyConSide::Right, -1, true}}};
    pairing = pair_joycons(others, true);
    expect(pairing.made && others[2].partner == 3 && others[3].partner == 2 && others[0].partner == 1 &&
               others[1].partner == 0,
           "a held shoulder of another pair never splits it");
    std::array<JoyConHalf, 4> swap{{{JoyConSide::Left, 1, false}, {JoyConSide::Right, 0, true},
                                    {JoyConSide::Left, 3, true}, {JoyConSide::Right, 2, false}}};
    pairing = pair_joycons(swap, true);
    expect(pairing.changed && pairing.made && swap[2].partner == 1 && swap[1].partner == 2 && swap[0].partner == -1 &&
               swap[3].partner == -1,
           "pairing two halves of two pairs on the screen leaves the partners they had");
    std::array<JoyConHalf, 2> stale{{{JoyConSide::Left, 1, false}, {JoyConSide::Right, -1, false}}};
    expect(pair_joycons(stale, true).changed && stale[0].partner == -1, "a partner that does not pair back is let go");
    std::array<JoyConHalf, 2> same{{{JoyConSide::Left, 1, true}, {JoyConSide::Left, 0, true}}};
    expect(pair_joycons(same, false).changed && same[0].partner == -1 && same[1].partner == -1,
           "two halves of one side never pair");
}

void test_switch2_decoding() {
    using namespace switch_hid;
    std::vector<std::uint8_t> r(64, 0);
    r[0] = kSwitch2InputReport;
    r[5] = 0x08 | 0x40;  // A, R
    r[6] = 0x01 | 0x02 | 0x40;  // Minus, Plus, C
    r[7] = 0x80;         // ZL
    r[8] = 0x02;         // GL
    put_u12_pair(&r[11], 2048, 2048);
    put_u12_pair(&r[14], 2048, 2048);
    Switch2ImuState imu{};
    DecodedState decoded{};
    expect(decode_switch2_report(r, imu, decoded), "Pro 2 report decodes");
    expect(decoded.buttons == (pad::bit(pad::kEastBit) | pad::bit(pad::kRightShoulderBit) |
                               pad::bit(pad::kBackBit) | pad::bit(pad::kStartBit) |
                               pad::bit(pad::kChatBit) | pad::bit(pad::kGripLeftBit)),
           "Pro 2 button bits");
    expect(decoded.left_trigger && decoded.imu_count == 0u, "ZL; IMU waits for the rate check");
    expect((settings_pad_buttons(ControlScheme::SwitchPro2, decoded.buttons) & settings_pad::kChord) ==
               settings_pad::kChord,
           "Pro 2 Minus+Plus is the overlay chord");
    // Feed microsecond timestamps at 4 ms per report.
    std::uint32_t t = 1000;
    for (int i = 0; i < 120 && !imu.ready; ++i) {
        t += 4000;
        r[0x2b] = static_cast<std::uint8_t>(t);
        r[0x2c] = static_cast<std::uint8_t>(t >> 8);
        r[0x2d] = static_cast<std::uint8_t>(t >> 16);
        r[0x2e] = static_cast<std::uint8_t>(t >> 24);
        (void)decode_switch2_report(r, imu, decoded);
    }
    expect(imu.ready && approx(imu.gyro_full_scale, 34.8f), "microsecond timestamps keep 34.8 rad/s");
    put_s16(&r[0x35], 4096);   // accel -> +Y
    put_s16(&r[0x3b], 3277);   // gyro -> yaw
    expect(decode_switch2_report(r, imu, decoded) && decoded.imu_count == 1u, "Pro 2 IMU sample");
    expect(approx(decoded.imu[0].accel[1], 4096.0f * 8.0f / 32767.0f, 1e-4f), "Pro 2 accel scale");
    expect(approx(decoded.imu[0].gyro[1], 3277.0f * 34.8f / 32767.0f, 1e-3f), "Pro 2 gyro scale");
    std::array<std::uint8_t, 9> cal{};
    put_u12_pair(&cal[0], 2000, 2010);
    put_u12_pair(&cal[3], 1500, 1510);
    put_u12_pair(&cal[6], 1400, 1410);
    const StickCal stick = parse_switch2_stick_cal(cal);
    expect(approx(normalize_stick(2000 + 1500, stick.x), 1.0f, 2e-3f) &&
               approx(normalize_stick(2010 - 1410, stick.y), -1.0f, 2e-3f),
           "Pro 2 calibration reaches full scale at its stored extents");
    const auto sequence = switch2_init_sequence();
    bool sizes_ok = !sequence.empty();
    for (const Switch2Command& cmd : sequence) {
        sizes_ok = sizes_ok && cmd.size == static_cast<std::size_t>(cmd.bytes[5]) + 8u && cmd.bytes[1] == 0x91;
    }
    expect(sizes_ok, "Pro 2 commands are header + declared length");
    const Switch2Command flash = switch2_flash_read(0x13080);
    expect(flash.size == 16u && flash.bytes[12] == 0x80 && flash.bytes[13] == 0x30 && flash.bytes[14] == 0x01,
           "Pro 2 flash read address");
}

// Rotates `v` about unit axis `k` by `angle` radians (Rodrigues).
std::array<float, 3> rotate(const std::array<float, 3>& v, const std::array<float, 3>& k, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    const float d = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
    const std::array<float, 3> x{k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
    return {v[0] * c + x[0] * s + k[0] * d * (1 - c), v[1] * c + x[1] * s + k[1] * d * (1 - c),
            v[2] * c + x[2] * s + k[2] * d * (1 - c)};
}

void test_motion() {
    using namespace switch_hid;
    constexpr float kPi = 3.14159265f;
    constexpr float kDeg = kPi / 180.0f;
    MotionSettings settings{};
    settings.picture_aspect = 16.0f / 9.0f;

    // At rest (held flat: gravity reaction along +Y) nothing shakes and the
    // pointer does not move, even with a constant 1.5 deg/s gyro bias, once a
    // rest window has measured it.
    {
        SwitchMotion motion{};
        ImuSample rest{};
        rest.accel = {0.0f, 1.0f, 0.0f};
        rest.gyro = {1.5f * kDeg, -1.0f * kDeg, 0.7f * kDeg};
        float x = 0.0f, y = 0.0f;
        int starts = 0;
        std::uint64_t t = 0;
        for (int i = 0; i < 200 * 60; ++i, t += 5000) {  // 60 s
            const auto out = motion.feed(rest, 0.005f, t, settings);
            if (i >= 200) {  // after the first second
                x += out.pointer_x;
                y += out.pointer_y;
            }
            starts += out.shake_started ? 1 : 0;
        }
        expect(starts == 0, "resting controller never shakes");
        expect(motion.rest_windows() > 100u, "rest windows refine the bias");
        expect(std::fabs(x) < 0.01f && std::fabs(y) < 0.01f, "no drift over 60 s at rest with a biased gyro");
    }

    // Yaw: 40 degrees about the world vertical is one picture width (2
    // units), left turn moves left; held flat and held upright alike.
    for (const bool upright : {false, true}) {
        SwitchMotion motion{};
        // Flat: up is +Y. Upright (face toward the player): up is -Z.
        const std::array<float, 3> up = upright ? std::array<float, 3>{0.0f, 0.0f, -1.0f}
                                                : std::array<float, 3>{0.0f, 1.0f, 0.0f};
        ImuSample s{};
        s.accel = up;
        std::uint64_t t = 0;
        for (int i = 0; i < 40; ++i, t += 5000) (void)motion.feed(s, 0.005f, t, settings);
        // Turn left at 80 deg/s for 0.5 s: 40 degrees about `up`.
        float x = 0.0f, y = 0.0f;
        std::array<float, 3> accel = up;
        for (int i = 0; i < 100; ++i, t += 5000) {
            s.gyro = {up[0] * 80.0f * kDeg, up[1] * 80.0f * kDeg, up[2] * 80.0f * kDeg};
            s.accel = accel;  // turning about gravity leaves the accelerometer unchanged
            const auto out = motion.feed(s, 0.005f, t, settings);
            x += out.pointer_x;
            y += out.pointer_y;
        }
        expect(approx(x, -2.0f, 0.03f) && std::fabs(y) < 0.03f,
               upright ? "upright: 40 deg of yaw is one picture width to the left"
                       : "flat: 40 deg of yaw is one picture width to the left");
    }

    // Pitch up about the right axis moves the pointer up; vertical gain keeps
    // pixels per degree (aspect 16:9: 22.5 degrees cover the height).
    {
        SwitchMotion motion{};
        ImuSample s{};
        s.accel = {0.0f, 1.0f, 0.0f};
        std::uint64_t t = 0;
        for (int i = 0; i < 40; ++i, t += 5000) (void)motion.feed(s, 0.005f, t, settings);
        float x = 0.0f, y = 0.0f;
        std::array<float, 3> g{0.0f, 1.0f, 0.0f};
        const float rate = 45.0f * kDeg;
        for (int i = 0; i < 100; ++i, t += 5000) {
            s.gyro = {rate, 0.0f, 0.0f};
            g = rotate(g, {1.0f, 0.0f, 0.0f}, -rate * 0.005f);  // world vector turns against the body
            s.accel = g;
            const auto out = motion.feed(s, 0.005f, t, settings);
            x += out.pointer_x;
            y += out.pointer_y;
        }
        expect(approx(y, 2.0f, 0.05f) && std::fabs(x) < 0.03f, "22.5 deg of pitch up is one picture height up");
    }

    // Gyro off: no pointer output, gravity and bias still tracked.
    {
        SwitchMotion motion{};
        MotionSettings off = settings;
        off.gyro_pointer = false;
        ImuSample s{};
        s.accel = {0.0f, 1.0f, 0.0f};
        s.gyro = {0.0f, 1.0f, 0.0f};
        const auto out = motion.feed(s, 0.005f, 0, off);
        expect(out.pointer_x == 0.0f && out.pointer_y == 0.0f, "gyro pointer off emits nothing");
    }

    // Shake: a sideways 1.2 g jolt (|a| only 1.56 g, 0.56 over rest) counts at
    // the default sensitivity, a 0.4 g handling bump does not, and the
    // cooldown suppresses repeats.
    {
        SwitchMotion motion{};
        ImuSample s{};
        s.accel = {0.0f, 1.0f, 0.0f};
        std::uint64_t t = 0;
        for (int i = 0; i < 100; ++i, t += 5000) (void)motion.feed(s, 0.005f, t, settings);
        int bumps = 0;
        for (int i = 0; i < 4; ++i, t += 5000) {
            s.accel = {(i % 2 == 0) ? 0.4f : -0.4f, 1.0f, 0.0f};
            bumps += motion.feed(s, 0.005f, t, settings).shake_started ? 1 : 0;
        }
        expect(bumps == 0, "a 0.4 g handling bump is not a shake");
        bool started = false;
        std::uint64_t start_time = 0;
        int retriggers = 0;
        for (int i = 0; i < 40; ++i, t += 5000) {
            s.accel = {(i % 4 < 2) ? 1.2f : -1.2f, 1.0f, 0.0f};
            if (motion.feed(s, 0.005f, t, settings).shake_started) {
                if (!started) {
                    started = true;
                    start_time = t;
                } else if (t < start_time + SwitchMotion::kShakeCooldownUs) {
                    ++retriggers;
                }
            }
        }
        expect(started, "a sideways 1.2 g shake starts a shake (|a|-1g alone would read 0.56 g)");
        expect(motion.shake_pressed(start_time + 1000) &&
                   !motion.shake_pressed(start_time + SwitchMotion::kShakeHoldUs),
               "a shake is held for the hold time");
        expect(retriggers == 0, "cooldown suppresses repeats");
    }
    // A fast wrist flick (900 deg/s) with little linear acceleration counts;
    // a 300 deg/s aiming turn does not.
    {
        SwitchMotion flick{};
        SwitchMotion aim{};
        ImuSample s{};
        s.accel = {0.0f, 1.0f, 0.0f};
        std::uint64_t t = 0;
        for (int i = 0; i < 100; ++i, t += 5000) {
            (void)flick.feed(s, 0.005f, t, settings);
            (void)aim.feed(s, 0.005f, t, settings);
        }
        bool flicked = false, aimed = false;
        std::array<float, 3> g1{0.0f, 1.0f, 0.0f}, g2 = g1;
        for (int i = 0; i < 10; ++i, t += 5000) {
            ImuSample a = s, b = s;
            a.gyro = {900.0f * kDeg, 0.0f, 0.0f};
            b.gyro = {300.0f * kDeg, 0.0f, 0.0f};
            g1 = rotate(g1, {1.0f, 0.0f, 0.0f}, -900.0f * kDeg * 0.005f);
            g2 = rotate(g2, {1.0f, 0.0f, 0.0f}, -300.0f * kDeg * 0.005f);
            a.accel = g1;
            b.accel = g2;
            flicked |= flick.feed(a, 0.005f, t, settings).shake_started;
            aimed |= aim.feed(b, 0.005f, t, settings).shake_started;
        }
        expect(flicked, "a 900 deg/s flick is a shake");
        expect(!aimed, "a 300 deg/s turn (fast aiming, tilting) is not a shake");
    }
    expect(SwitchMotion::shake_accel_threshold_g(1) > SwitchMotion::shake_accel_threshold_g(3) &&
               SwitchMotion::shake_accel_threshold_g(3) > SwitchMotion::shake_accel_threshold_g(5) &&
               SwitchMotion::shake_gyro_threshold(1) > SwitchMotion::shake_gyro_threshold(5),
           "higher sensitivity needs less motion");
}

void test_pointer_ownership() {
    PointerOwner owner{};
    std::uint64_t t = 1'000'000;
    PointerActivity none{};
    PointerActivity mouse{};
    mouse.mouse = true;
    PointerActivity stick{};
    stick.stick = true;
    PointerActivity both{};
    both.mouse = true;
    both.stick = true;
    expect(owner.update(none, t) == PointerSource::None, "nothing moved: no owner");
    expect(owner.update(mouse, t) == PointerSource::Mouse, "the mouse moves first and owns");
    t += 10'000;
    expect(owner.update(stick, t) == PointerSource::Mouse, "a stick nudge right after the mouse does not steal");
    t += 100'000;
    expect(owner.update(mouse, t) == PointerSource::Mouse, "a moving mouse keeps the pointer");
    t += kPointerHandoffUs - 1u;
    expect(owner.update(stick, t) == PointerSource::Mouse, "handoff waits the full hysteresis");
    t += 1u;
    expect(owner.update(stick, t) == PointerSource::Stick, "the stick takes over once the mouse rested");
    t += 10'000;
    expect(owner.update(both, t) == PointerSource::Stick,
           "a mouse moved by a controller mapper while the stick moves does not steal");
    t += 10'000;
    expect(owner.update(none, t) == PointerSource::Stick, "a resting stick keeps the pointer");
    t += kPointerHandoffUs;
    expect(owner.update(both, t) == PointerSource::Stick, "both start together: the controller wins");
    t += kPointerHandoffUs;
    PointerActivity gyro{};
    gyro.gyro = true;
    expect(owner.update(gyro, t) == PointerSource::Gyro, "the gyro takes over after the handoff");
    expect(std::string(pointer_source_name(PointerSource::Gyro)) == "gyro", "source names");
}

void test_stick_pointer() {
    // Radial dead zone: inside is zero in every direction, outside rescales.
    const float dz = 8689.0f / 32767.0f;
    expect(apply_radial_deadzone(0.2f, 0.15f, dz).x == 0.0f, "inside the radial dead zone is zero");
    const StickVector diag = apply_radial_deadzone(0.25f, 0.25f, dz);  // |v| = 0.354 > 0.265
    expect(diag.x > 0.0f && approx(diag.x, diag.y), "a diagonal outside the radius keeps its direction");
    const StickVector full = apply_radial_deadzone(1.0f, 0.0f, dz);
    expect(approx(full.x, 1.0f) && full.y == 0.0f, "full deflection stays full");
    const StickVector edge = apply_radial_deadzone(dz + 0.001f, 0.0f, dz);
    expect(edge.x > 0.0f && edge.x < 0.01f, "output starts at zero past the dead zone");

    // Response: slow near the centre, the configured speed at full deflection.
    const StickVector v_full = stick_pointer_velocity({1.0f, 0.0f}, 2.4f);
    const StickVector v_half = stick_pointer_velocity({0.5f, 0.0f}, 2.4f);
    const StickVector v_small = stick_pointer_velocity({0.1f, 0.0f}, 2.4f);
    expect(approx(v_full.x, 2.4f), "full deflection moves at the pointer speed");
    expect(v_half.x < 0.3f * 2.4f && v_half.x > 0.15f * 2.4f, "half deflection is under a third of the speed");
    expect(v_small.x < 0.04f * 2.4f, "a small deflection places the pointer finely");
    expect(stick_pointer_velocity({0.0f, 0.0f}, 2.4f).x == 0.0f, "a resting stick does not move the pointer");

    // Integration with the real report interval: the same stick held for
    // 0.5 s moves the same distance at 66 Hz and 125 Hz reports, and the
    // pointer stops at the picture edge.
    for (const float hz : {66.7f, 125.0f}) {
        float x = 0.0f;
        const float dt = 1.0f / hz;
        for (float t = 0.0f; t < 0.5f - 1e-4f; t += dt) {
            x = std::clamp(x + v_full.x * dt, -kStickPointerLimit, kStickPointerLimit);
        }
        expect(approx(x, 1.0f, 0.04f), "0.5 s at full deflection crosses half the picture at any report rate");
    }
}

// Seconds until PointerRestHide hides a pointer that follows `at(t)` (pointer
// units), sampled every `report_s`; -1 if it never hides within `seconds`.
template <typename Path>
double hide_time(Path&& at, double seconds, double report_s = 0.015) {
    PointerRestHide hide{};
    const float r = kDefaultPointerHideRadius;
    const float s = kDefaultPointerHideSeconds;
    for (double t = 0.0; t <= seconds; t += report_s) {
        const auto p = at(t);
        if (hide.update(p[0], p[1], static_cast<std::uint64_t>(t * 1e6) + 1'000'000u, true, false, false, s, r)) {
            return t;
        }
    }
    return -1.0;
}

void test_rest_hide() {
    using P = std::array<float, 2>;
    // A still pointer hides after the default 4 s.
    const double still = hide_time([](double) { return P{0.3f, -0.2f}; }, 6.0);
    expect(still >= 3.99 && still <= 4.03, "a still pointer hides after 4 s");
    // Tremor (+-0.025 units at 9 Hz) plus small jerks (0.08 units for 60 ms,
    // alternating sides, every 0.5 s) never restart the timer.
    const double shaky = hide_time([](double t) {
        const double tremor = 0.025 * std::sin(2.0 * 3.14159265 * 9.0 * t);
        const double phase = std::fmod(t + 0.2, 0.5);
        const double jerk = phase < 0.06 ? (std::fmod(t + 0.2, 1.0) < 0.5 ? 0.08 : -0.08) : 0.0;
        return P{static_cast<float>(0.3 + tremor + jerk), static_cast<float>(-0.2 + 0.5 * tremor)};
    }, 6.0);
    expect(shaky >= 3.99 && shaky <= 4.05, "tremor and small jerks inside the radius hide at 4 s");
    // A single-report spike far outside the radius is filtered out.
    const double spike = hide_time([](double t) {
        return P{t > 1.0 && t < 1.015 ? 0.8f : 0.3f, -0.2f};
    }, 6.0);
    expect(spike >= 3.99 && spike <= 4.03, "a one-report spike does not restart the timer");
    // A slow deliberate pan (0.15 units/s, 3 degrees per second at sensitivity
    // 1) never hides while it lasts; it hides 4 s after it stops.
    const double pan = hide_time([](double t) {
        return P{static_cast<float>(-0.9 + 0.15 * std::min(t, 10.0)), 0.0f};
    }, 9.9);
    expect(pan < 0.0, "a slow pan never hides");
    const double after_pan = hide_time([](double t) {
        return P{static_cast<float>(-0.9 + 0.15 * std::min(t, 10.0)), 0.0f};
    }, 16.0);
    // The timer runs from the last re-anchor, at most radius/speed (0.8 s)
    // before the pan stopped.
    expect(after_pan > 13.1 && after_pan < 14.1, "the pointer hides within 4 s after the pan stops");

    // Stick pointer (not latched): tremor keeps it hidden, leaving the radius
    // shows it, recenter shows it.
    PointerRestHide hide{};
    std::uint64_t t = 1'000'000;
    for (int i = 0; i < 410; ++i, t += 10'000) (void)hide.update(0.2f, 0.1f, t, true, false, false, 4.0f, 0.12f);
    expect(hide.hidden, "hidden after 4.1 s");
    bool shown = false;
    for (int i = 0; i < 50; ++i, t += 10'000) {
        shown |= !hide.update(0.2f + ((i & 1) ? 0.04f : -0.04f), 0.1f, t, true, false, false, 4.0f, 0.12f);
    }
    expect(!shown, "tremor inside the radius keeps a hidden pointer hidden");
    for (int i = 0; i < 30; ++i, t += 10'000) (void)hide.update(0.5f, 0.1f, t, true, false, false, 4.0f, 0.12f);
    expect(!hide.hidden, "a stick pointer moved out of the radius shows again");
    for (int i = 0; i < 410; ++i, t += 10'000) (void)hide.update(0.5f, 0.1f, t, true, false, false, 4.0f, 0.12f);
    expect(hide.hidden, "the moved pointer hides again 4 s after it settles");
    expect(!hide.update(0.0f, 0.0f, t, true, true, false, 4.0f, 0.12f), "recenter shows it");

    // Gyro pointer (latched): once hidden, only recenter brings it back.
    PointerRestHide gyro{};
    t = 1'000'000;
    for (int i = 0; i < 410; ++i, t += 10'000) (void)gyro.update(0.2f, 0.1f, t, true, false, true, 4.0f, 0.12f);
    expect(gyro.hidden, "gyro pointer hidden after 4.1 s");
    bool gyro_shown = false;
    for (int i = 0; i < 200; ++i, t += 10'000) {
        gyro_shown |= !gyro.update(0.2f + 0.01f * static_cast<float>(i), -0.3f, t, true, false, true, 4.0f, 0.12f);
    }
    expect(!gyro_shown, "a hidden gyro pointer stays hidden while the controller turns");
    expect(!gyro.update(0.0f, 0.0f, t, true, true, true, 4.0f, 0.12f), "recenter shows the gyro pointer");
    t += 10'000;
    expect(!gyro.update(0.05f, 0.0f, t, true, false, true, 4.0f, 0.12f) && !gyro.hidden,
           "after recenter the gyro pointer moves and the timer starts again");

    PointerRestHide mouse{};
    bool ever = false;
    for (int i = 0; i < 1000; ++i) ever |= mouse.update(0.0f, 0.0f, i * 10'000ull, false, false, false, 4.0f, 0.12f);
    expect(!ever, "a mouse-owned pointer never rest-hides");
    PointerRestHide never{};
    ever = false;
    for (int i = 0; i < 1000; ++i) ever |= never.update(0.0f, 0.0f, i * 10'000ull, true, false, true, 0.0f, 0.12f);
    expect(!ever, "0 seconds never hides");
}

void test_mouse_rest_hide() {
    MouseRestHide hide{};
    std::uint64_t t = 1'000'000;
    bool hidden = false;
    for (int i = 0; i < 440; ++i, t += 10'000) hidden |= hide.update(true, i == 0, t);
    expect(!hidden, "a still mouse pointer shows for 4.4 s");
    for (int i = 0; i < 20; ++i, t += 10'000) hidden |= hide.update(true, false, t);
    expect(hidden, "a still mouse pointer hides by 4.6 s");
    expect(!hide.update(true, true, t), "a mouse move shows it");
    t += 10'000;
    expect(!hide.update(true, false, t), "the timer starts again after the move");
    MouseRestHide click{};
    t = 1'000'000;
    for (int i = 0; i < 600; ++i, t += 10'000) (void)click.update(true, false, t);
    expect(!click.update(true, true, t), "a held mouse button shows it");
    MouseRestHide other{};
    hidden = false;
    t = 1'000'000;
    for (int i = 0; i < 1000; ++i, t += 10'000) hidden |= other.update(false, false, t);
    expect(!hidden, "a pointer the mouse does not own never mouse-hides");
    expect(!other.update(true, false, t), "the mouse taking the pointer starts the timer");
}

// The gyro pointer of a hand-held Pro Controller: SwitchMotion integrates a
// synthetic hand (tremor, small jerks, a slow pan) and the rest-hide rule
// runs on the integrated pointer at the report rate.
double gyro_hide_time(double seconds, double pan_dps, double pan_until_s) {
    using namespace switch_hid;
    constexpr double kDeg = 3.14159265358979 / 180.0;
    SwitchMotion motion{};
    MotionSettings settings{};
    PointerRestHide hide{};
    float x = 0.0f, y = 0.0f;
    std::uint64_t t_us = 1'000'000;
    double hidden_at = -1.0;
    for (int report = 0; report * 0.015 < seconds; ++report) {
        for (int k = 0; k < 3; ++k, t_us += 5000) {
            const double t = (t_us - 1'000'000) * 1e-6;
            // Yaw and pitch rates (deg/s): 9 Hz tremor of +-0.5 degrees, a
            // 1.5 degree jerk every 0.5 s (60 ms, alternating), the pan.
            const double tremor = 0.5 * 2.0 * 3.14159265 * 9.0 * std::cos(2.0 * 3.14159265 * 9.0 * t);
            const double phase = std::fmod(t, 0.5);
            const double jerk = phase < 0.06 ? (std::fmod(t, 1.0) < 0.5 ? 25.0 : -25.0) : 0.0;
            const double pan = t < pan_until_s ? pan_dps : 0.0;
            ImuSample s{};
            s.accel = {0.0f, 1.0f, 0.0f};  // held flat: gravity reaction along +Y (up)
            s.gyro = {static_cast<float>((0.6 * tremor) * kDeg), static_cast<float>((tremor + jerk + pan) * kDeg), 0.0f};
            const auto out = motion.feed(s, 0.005f, t_us, settings);
            x += out.pointer_x;
            y -= out.pointer_y;
        }
        if (hide.update(x, y, t_us, true, false, true, kDefaultPointerHideSeconds, kDefaultPointerHideRadius) &&
            hidden_at < 0.0) {
            hidden_at = (t_us - 1'000'000) * 1e-6;
        }
        if (!hide.hidden) hidden_at = -1.0;
    }
    return hidden_at;
}

void test_rest_hide_gyro() {
    const double held = gyro_hide_time(6.0, 0.0, 0.0);
    expect(held >= 3.95 && held <= 4.3, "hand-held gyro (tremor + small jerks) hides at about 4 s");
    expect(gyro_hide_time(8.0, 3.0, 8.0) < 0.0, "a slow 3 deg/s pan never hides while it lasts");
}

std::uint8_t rb_default_strength() { return galaxy::input::rumble::kDefaultStrength; }

void test_rumble_encoding() {
    namespace rb = galaxy::input::rumble;
    using B4 = std::array<std::uint8_t, 4>;
    // Neutral: 320 Hz high band, 160 Hz low band, zero amplitude.
    expect(rb::encode_switch_hd_rumble(320.0f, 0.0f, 160.0f, 0.0f) == rb::kSwitchNeutralRumble,
           "zero amplitude encodes the neutral frame 00 01 40 40");
    // dekuNukem rumble_data_table.md rows: amplitude 1.003 -> HA C8, LA 0072;
    // 0.501 -> 88, 0062; 0.206 -> 3A, 804E (half step in byte 2 bit 7).
    expect(rb::encode_switch_hd_rumble(320.0f, 1.0f, 160.0f, 1.0f) == B4{0x00, 0xC9, 0x40, 0x72},
           "full amplitude: HA C8 + high bit, LA 72");
    expect(rb::encode_switch_hd_rumble(320.0f, 0.501f, 160.0f, 0.501f) == B4{0x00, 0x89, 0x40, 0x62},
           "amplitude 0.501: HA 88, LA 62");
    expect(rb::encode_switch_hd_rumble(320.0f, 0.206f, 160.0f, 0.206f) == B4{0x00, 0x3B, 0xC0, 0x4E},
           "amplitude 0.206: HA 3A, LA 80 4E");
    // Frequencies: 160 Hz high band is 0x0080, 320 Hz low band is 0x60.
    expect(rb::encode_switch_hd_rumble(160.0f, 0.0f, 320.0f, 0.0f) == B4{0x80, 0x00, 0x60, 0x40},
           "160 Hz high band, 320 Hz low band");
    const auto off = rb::switch_pro_motor_rumble(0, 100);
    expect(std::equal(off.begin(), off.begin() + 4, rb::kSwitchNeutralRumble.begin()) &&
               std::equal(off.begin() + 4, off.end(), rb::kSwitchNeutralRumble.begin()),
           "motor stopped: neutral on both actuators");
    const auto on = rb::switch_pro_motor_rumble(rb::kLevelSteps, 100);
    const auto expected = rb::encode_switch_hd_rumble(rb::kSwitchMotorHighHz,
                                                      rb::kSwitchLowAmplitudeMax * rb::kSwitchHighShare,
                                                      rb::kSwitchMotorLowHz, rb::kSwitchLowAmplitudeMax);
    expect(std::equal(expected.begin(), expected.end(), on.begin()) &&
               std::equal(expected.begin(), expected.end(), on.begin() + 4) && on[1] != 0x01u,
           "full level at 100%: 160 Hz at 0.70 with 320 Hz at 0.35 of it, both actuators");
    const auto weakest = rb::switch_pro_motor_rumble(rb::kLevelSteps, rb::kMinStrength);
    expect(weakest[3] != 0x40u && weakest[3] < on[3], "10% is weaker and not silent (low band)");
    const auto half_level = rb::switch_pro_motor_rumble(rb::kLevelSteps / 2u, 100);
    expect(half_level[3] < on[3], "a lower motor level is weaker");
    const auto none = rb::switch_pro_motor_rumble(rb::kLevelSteps, 0);
    expect(std::equal(none.begin(), none.begin() + 4, rb::kSwitchNeutralRumble.begin()), "vibration off is silent");
    std::array<std::uint8_t, 64> report{};
    report.fill(0xAA);
    expect(rb::build_switch_pro_rumble_report(0x13, on, report) == 10u && report[0] == 0x10u && report[1] == 0x03u &&
               std::equal(on.begin(), on.end(), report.begin() + 2) && report[10] == 0u && report[63] == 0u,
           "report 0x10: id, packet nibble, 8 rumble bytes, zero padding");
    // Pro Controller 2 (SDL_hidapi_switch2.c layout).
    std::array<std::uint8_t, 64> pro2{};
    expect(rb::build_switch2_rumble_report(0x12, 0, 100, pro2) == 64u && pro2[0] == 0x02u && pro2[1] == 0x52u &&
               pro2[2] == 0x87u && pro2[3] == 0x01u && pro2[4] == 0x20u && pro2[5] == 0x11u && pro2[6] == 0x00u &&
               std::equal(pro2.begin() + 1, pro2.begin() + 7, pro2.begin() + 0x11) && pro2[7] == 0u,
           "Pro Controller 2 report: 0x02, 0x5n, frequencies with zero amplitude, copy at 0x11");
    (void)rb::build_switch2_rumble_report(0, rb::kLevelSteps, 100, pro2);
    const auto motor2 = rb::encode_switch2_hd_rumble(rb::kSwitch2HighFrequency, 7105, rb::kSwitch2LowFrequency, 20300);
    expect(std::equal(motor2.begin(), motor2.end(), pro2.begin() + 2),
           "Pro Controller 2 amplitudes 0.70 and 0.245 of 29000 at full level");
    // Xbox: the light motor only.
    const auto x_off = rb::xinput_motor_speeds(0, 100);
    const auto x_full = rb::xinput_motor_speeds(rb::kLevelSteps, 100);
    const auto x_half = rb::xinput_motor_speeds(rb::kLevelSteps / 2u, 100);
    const auto x_min = rb::xinput_motor_speeds(rb::kLevelSteps, rb::kMinStrength);
    expect(x_off.left == 0u && x_off.right == 0u, "xbox off");
    expect(x_full.left == 0u && x_full.right == 65535u, "xbox full: light motor full, heavy motor off");
    expect(x_half.left == 0u && x_half.right == 32768u, "xbox half level");
    expect(x_min.right == 9830u, "xbox 10%: light motor at 15%");
    expect(rb::snap_strength(30.0f) == 30u && rb::snap_strength(63.0f) == 60u && rb::snap_strength(65.0f) == 60u &&
               rb::snap_strength(66.0f) == 70u && rb::snap_strength(110.0f) == 100u &&
               rb::snap_strength(-5.0f) == 10u && rb::snap_strength(std::nanf("")) == rb::kDefaultStrength,
           "strength snaps to 10..100 in steps of 10");
    // Motor model: rises over ~30 ms, falls over ~60 ms.
    rb::MotorEnvelope motor{};
    expect(motor.update(true, 1'000'000) == 0.0f, "no time has passed: still stopped");
    const float risen = motor.update(true, 1'030'000);
    expect(risen > 0.62f && risen < 0.64f, "one rise time: 63%");
    const float fallen = motor.update(false, 1'090'000);
    expect(fallen > 0.22f && fallen < 0.24f, "one fall time: 37% of that");
    for (std::uint64_t t = 1'100'000; t < 1'400'000; t += 10'000) (void)motor.update(false, t);
    expect(motor.level == 0.0f, "the motor stops below 5%");
    expect(rb::quantize_level(0.0f) == 0u && rb::quantize_level(0.001f) == 1u &&
               rb::quantize_level(1.0f) == rb::kLevelSteps && rb::quantize_level(0.5f) == rb::kLevelSteps / 2u,
           "levels quantize to 0..32, any motion to at least 1");
    // A Star Bit shot: four 30 ms pulses 30 ms apart, sampled every 10 ms,
    // feel like one buzz: the motor never stops between pulses and never
    // reaches full speed.
    rb::MotorEnvelope shot{};
    float low = 1.0f, high = 0.0f;
    std::uint64_t now = 2'000'000;
    (void)shot.update(false, now);
    for (int pulse = 0; pulse < 4; ++pulse) {
        for (int i = 0; i < 6; ++i) {
            now += 10'000;
            const float level = shot.update(i < 3, now);
            if (pulse > 0 || i >= 3) low = std::min(low, level);
            high = std::max(high, level);
        }
    }
    expect(low > 0.2f && high < 0.9f, "a Star Bit shot is one continuous, soft buzz");
}

void test_rumble_gate() {
    namespace rb = galaxy::input::rumble;
    rb::GateInputs in{};
    in.game_motor_on = true;
    in.now_us = 10'000'000;
    in.published_us = in.now_us - 20'000;
    in.focused = true;
    in.strength = 100;
    expect(rb::motor_should_run(in), "motor bit on, fresh, focused: runs");
    auto stalled = in;
    stalled.published_us = in.now_us - rb::kStallUs - 1;
    expect(!rb::motor_should_run(stalled), "no publication for over 100 ms: off");
    auto unfocused = in;
    unfocused.focused = false;
    expect(!rb::motor_should_run(unfocused), "window without focus: off");
    auto paused = in;
    paused.paused = true;
    expect(!rb::motor_should_run(paused), "pause or HOME menu: off");
    auto silent = in;
    silent.strength = 0;
    expect(!rb::motor_should_run(silent), "strength 0: off");
    auto motor_off = in;
    motor_off.game_motor_on = false;
    expect(!rb::motor_should_run(motor_off), "game motor off: off");
    auto clock_back = in;
    clock_back.published_us = in.now_us + 1;
    expect(!rb::motor_should_run(clock_back), "a publication from the future is not trusted");
}

// SphereAccelSensorController::clacXY for the Wii Remote: X from (a.x, |a.y|),
// Y the signed angle of (-a.y, a.z) from (cos 10, sin 10); 5-degree dead zone,
// full at 25 degrees.
std::array<float, 2> star_ball_clac_xy(const tilt::Vec3& a) {
    constexpr float kDeg = 3.14159265f / 180.0f;
    const float dead = 5.0f * kDeg, full = 25.0f * kDeg, lean = 10.0f * kDeg;
    float x = 0.0f, y = 0.0f;
    if (a[0] * a[0] + a[1] * a[1] > 3.8147e-6f) x = std::asin(a[0] / std::sqrt(a[0] * a[0] + a[1] * a[1]));
    const float vx = -a[1], vy = a[2];
    const float l = std::sqrt(vx * vx + vy * vy);
    if (l * l > 3.8147e-6f) {
        const float nx = vx / l, ny = vy / l;
        y = std::acos(std::clamp(nx * std::cos(lean) + ny * std::sin(lean), -1.0f, 1.0f));
        if (ny * std::cos(lean) - nx * std::sin(lean) < 0.0f) y = -y;
    }
    const auto shape = [&](float v) {
        if (std::fabs(v) < dead) return 0.0f;
        return (v > 0.0f ? v - dead : v + dead) / (full - dead);
    };
    return {shape(x), shape(y)};
}

// SurfRay::updateRotate's turn input: turning when a.z > -0.7 or
// |a.x| > 0.15; amount a.x pushed away from zero by clamp(-a.z, 0, 1),
// clamped to 1.5.
float ray_turn(const tilt::Vec3& a) {
    if (!(a[2] > -0.7f || std::fabs(a[0]) > 0.15f)) return 0.0f;
    float f = a[0];
    const float extra = std::clamp(-a[2], 0.0f, 1.0f);
    if (f > 0.0f) f += extra; else if (f < 0.0f) f -= extra;
    return std::clamp(f, -1.5f, 1.5f);
}

void test_tilt() {
    using namespace tilt;
    const auto close_to = [](float a, float b, float eps) { return std::fabs(a - b) <= eps; };
    // Star Ball: the controller's neutral hold is clacXY's zero.
    const auto neutral = star_ball_clac_xy(remote_gravity(Section::StarBall, {0.0f, 1.0f, 0.0f}));
    expect(close_to(neutral[0], 0.0f, 1e-4f) && close_to(neutral[1], 0.0f, 1e-4f), "star ball: the neutral hold rolls nowhere");
    // Tilt right / forward 15 degrees: half of the range, positive.
    const auto right = star_ball_clac_xy(remote_gravity(Section::StarBall, up_from_angles(15.0f, 0.0f)));
    const auto forward = star_ball_clac_xy(remote_gravity(Section::StarBall, up_from_angles(0.0f, 15.0f)));
    const auto left_back = star_ball_clac_xy(remote_gravity(Section::StarBall, up_from_angles(-15.0f, -15.0f)));
    expect(close_to(right[0], 0.5f, 0.02f) && close_to(right[1], 0.0f, 0.02f), "star ball: 15 degrees right is X +0.5");
    expect(close_to(forward[1], 0.5f, 0.02f) && close_to(forward[0], 0.0f, 0.02f), "star ball: 15 degrees forward is Y +0.5");
    expect(left_back[0] < -0.4f && left_back[1] < -0.4f, "star ball: left and back are negative");
    // The tutorial's raise check: within 30 degrees of (0,-1,0).
    const Vec3 held = remote_gravity(Section::StarBall, {0.0f, 1.0f, 0.0f});
    expect(-held[1] > std::cos(30.0f * 3.14159265f / 180.0f), "star ball: the neutral passes the raise check");
    // A neutral captured in the player's own hold (30 degrees toward the
    // face) is zero; 15 degrees more forward is +0.5.
    const Vec3 hold = up_from_angles(0.0f, -30.0f);
    const auto from_hold = star_ball_clac_xy(remote_gravity(Section::StarBall, relative_up(hold, hold)));
    const auto more = star_ball_clac_xy(remote_gravity(Section::StarBall, relative_up(up_from_angles(0.0f, -15.0f), hold)));
    expect(close_to(from_hold[0], 0.0f, 1e-3f) && close_to(from_hold[1], 0.0f, 1e-3f), "star ball: the captured hold is neutral");
    expect(close_to(more[1], 0.5f, 0.03f), "star ball: tilt is measured from the captured hold");
    // Stick: full deflection is the full range.
    const auto stick_right = star_ball_clac_xy(remote_gravity(Section::StarBall, up_from_stick(Section::StarBall, 1.0f, 0.0f)));
    const auto stick_down = star_ball_clac_xy(remote_gravity(Section::StarBall, up_from_stick(Section::StarBall, 0.0f, -1.0f)));
    expect(close_to(stick_right[0], 1.0f, 0.02f) && close_to(stick_right[1], 0.0f, 0.02f),
           "star ball: stick right is full right");
    expect(close_to(stick_down[1], -1.0f, 0.02f) && close_to(stick_down[0], 0.0f, 0.02f),
           "star ball: stick down is full back");
    // Ray: flat neutral does not turn; a twist right turns positive, left
    // negative, proportionally.
    expect(ray_turn(remote_gravity(Section::Ray, {0.0f, 1.0f, 0.0f})) == 0.0f, "ray: the neutral hold does not turn");
    const float twist = ray_turn(remote_gravity(Section::Ray, up_from_angles(20.0f, 0.0f)));
    expect(close_to(twist, std::sin(20.0f * 3.14159265f / 180.0f), 0.01f), "ray: a 20-degree twist right turns +sin 20");
    expect(ray_turn(remote_gravity(Section::Ray, up_from_angles(-20.0f, 0.0f))) < -0.3f, "ray: a twist left turns negative");
    expect(close_to(ray_turn(remote_gravity(Section::Ray, up_from_stick(Section::Ray, 1.0f, 0.0f))),
                std::sin(kRayStickDegrees * 3.14159265f / 180.0f), 0.01f),
           "ray: full stick is a 50-degree twist");
    // Counts: the flat remote is the rest reading.
    const Counts rest = accel_counts({0.0f, 0.0f, 1.0f});
    expect(rest.x == kWiimoteRestAccelX && rest.y == kWiimoteRestAccelY && rest.z == kWiimoteRestAccelZ,
           "the flat remote is the rest reading");
    // State: inactive outside sections; the neutral is captured at the start
    // and again on recenter; a motion controller steers by tilt only, a
    // controller without motion by the stick.
    Tilt t{};
    expect(!t.update(Section::None, nullptr, 0.0f, 0.0f, false).active, "no section: no tilt");
    const Vec3 start = up_from_angles(5.0f, -20.0f);
    auto out = t.update(Section::StarBall, &start, 0.0f, 0.0f, false);
    auto xy = star_ball_clac_xy(out.game);
    expect(out.active && !out.from_stick && close_to(xy[0], 0.0f, 1e-3f) && close_to(xy[1], 0.0f, 1e-3f),
           "section start captures the hold as neutral");
    const Vec3 tilted = up_from_angles(5.0f, -5.0f);
    xy = star_ball_clac_xy(t.update(Section::StarBall, &tilted, 0.0f, 0.0f, false).game);
    expect(xy[1] > 0.4f, "tilting forward from the hold rolls forward");
    xy = star_ball_clac_xy(t.update(Section::StarBall, &tilted, 0.0f, 0.0f, true).game);
    expect(close_to(xy[1], 0.0f, 1e-3f), "recenter takes the current hold as neutral");
    out = t.update(Section::StarBall, &tilted, -1.0f, 0.0f, false);
    xy = star_ball_clac_xy(out.game);
    expect(!out.from_stick && close_to(xy[0], 0.0f, 1e-3f), "a Pro Controller's stick does not tilt");
    out = t.update(Section::StarBall, nullptr, -1.0f, 0.0f, false);
    xy = star_ball_clac_xy(out.game);
    expect(out.from_stick && xy[0] < -0.9f, "without motion (Xbox, keyboard) the stick steers");
    out = t.update(Section::StarBall, nullptr, 0.1f, 0.05f, false);
    xy = star_ball_clac_xy(out.game);
    expect(close_to(xy[0], 0.0f, 1e-3f) && close_to(xy[1], 0.0f, 1e-3f), "a stick at rest is level");
    expect(!t.update(Section::None, &tilted, 0.0f, 0.0f, false).active && !t.neutral_valid, "leaving resets the neutral");
}

void test_recenter_hold() {
    RecenterHold hold{};
    auto out = hold.update(true);
    expect(out.pressed && out.pinned && !out.released, "press: recenter and pin");
    out = hold.update(true);
    expect(!out.pressed && out.pinned && !out.released, "held: stays pinned at the centre");
    out = hold.update(false);
    expect(!out.pressed && !out.pinned && out.released, "release: motion resumes from the centre");
    out = hold.update(false);
    expect(!out.pressed && !out.pinned && !out.released, "idle");
    // A tap (one report down) still recenters.
    out = hold.update(true);
    expect(out.pressed && out.pinned, "tap recenters");
    out = hold.update(false);
    expect(out.released, "tap releases on the next report");
}

void test_jump_back() {
    JumpBackLatch latch{};
    auto out = latch.update(true, PlayContext::Play);
    expect(out.a && !out.b, "B jumps while Mario is under control");
    out = latch.update(true, PlayContext::Menu);
    expect(out.a && !out.b, "a held jump stays a jump when a menu opens");
    out = latch.update(false, PlayContext::Menu);
    expect(!out.a && !out.b, "release");
    out = latch.update(true, PlayContext::Menu);
    expect(!out.a && out.b, "B backs out of menus");
    out = latch.update(true, PlayContext::Play);
    expect(!out.a && out.b, "a held back press stays back when play resumes");
    for (const std::uint32_t play : {10u, 11u, 12u, 13u, 14u, 15u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 99u}) {
        expect(play_context_from_star_pointer_mode(play) == PlayContext::Play,
               "game, demos, targets, launch stars, base and unknown modes keep B as jump");
    }
    for (const std::uint32_t menu : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 16u, 17u}) {
        expect(play_context_from_star_pointer_mode(menu) == PlayContext::Menu,
               "HOME, error, pause, logo, title, file select, scenario select, yes/no, picture book "
               "and sphere selector are menus");
    }
}

void test_defaults_and_migration() {
    const ControlsConfig config = default_controls_config();
    const BindingTable& sw = config.table(ControlScheme::SwitchPro);
    const BindingTable& xb = config.table(ControlScheme::Xbox);
    const auto only = [](const TargetBindings& b, InputSource s) { return b[0] == s && b[1].empty(); };
    const InputSource zr{SourceKind::AxisPositive, static_cast<std::uint8_t>(PadAxis::RightTrigger)};
    const InputSource zl{SourceKind::AxisPositive, static_cast<std::uint8_t>(PadAxis::LeftTrigger)};
    expect(only(sw[WiiTarget::A], {SourceKind::Button, pad::kEastBit}), "Switch: A is Wii A");
    expect(only(sw[WiiTarget::JumpBack], {SourceKind::Button, pad::kSouthBit}), "Switch: B is jump/back");
    expect(only(sw[WiiTarget::B], {SourceKind::Button, pad::kLeftShoulderBit}), "Switch: only L shoots Star Bits (Wii B)");
    expect(only(sw[WiiTarget::Z], zl), "Switch: ZL crouches (Wii Z)");
    expect(only(sw[WiiTarget::C], {SourceKind::Button, pad::kLeftStickBit}), "Switch: left-stick click resets the camera");
    expect(only(sw[WiiTarget::PointerRecenter], {SourceKind::Button, pad::kRightShoulderBit}), "Switch: R recenters");
    expect(sw[WiiTarget::Shake][0] == InputSource{SourceKind::Button, pad::kWestBit} &&
               sw[WiiTarget::Shake][1] == InputSource{SourceKind::Button, pad::kNorthBit} &&
               sw[WiiTarget::Shake][2] == zr && sw[WiiTarget::Shake][3].empty(),
           "Switch: Y, X and ZR spin (with motion), not R");
    expect(sw[WiiTarget::DpadLeft][1] == InputSource{SourceKind::AxisNegative, static_cast<std::uint8_t>(PadAxis::RightX)},
           "Switch: right stick left is D-pad left (camera)");
    expect(config.tune(ControlScheme::SwitchPro).gyro_pointer && config.tune(ControlScheme::SwitchPro2).gyro_pointer,
           "gyro pointer on by default for both Switch schemes");
    expect(only(xb[WiiTarget::JumpBack], {SourceKind::Button, pad::kEastBit}), "Xbox: B is jump/back");
    expect(only(xb[WiiTarget::B], {SourceKind::Button, pad::kLeftShoulderBit}), "Xbox: only LB shoots Star Bits");
    expect(only(xb[WiiTarget::Z], zl), "Xbox: LT crouches");
    expect(only(xb[WiiTarget::C], {SourceKind::Button, pad::kLeftStickBit}), "Xbox: LS click resets the camera");
    expect(xb[WiiTarget::Shake][2] == InputSource{SourceKind::Button, pad::kRightShoulderBit} &&
               xb[WiiTarget::Shake][3] == zr,
           "Xbox: RB and RT spin");
    // A trigger bound to a level target presses at the trigger threshold.
    {
        PadSample pad_sample{};
        pad_sample.connected = true;
        pad_sample.scheme = ControlScheme::Xbox;
        pad_sample.axes[static_cast<std::size_t>(PadAxis::RightTrigger)] = 0.6f;
        const LogicalInput rt = evaluate_pad_bindings(xb, pad_sample, 0.12f);
        expect(rt.held(WiiTarget::Shake) && !rt.held(WiiTarget::Z) && !rt.held(WiiTarget::B),
               "Xbox: RT spins, does not crouch or shoot");
        pad_sample.axes[static_cast<std::size_t>(PadAxis::RightTrigger)] = 0.0f;
        pad_sample.axes[static_cast<std::size_t>(PadAxis::LeftTrigger)] = 0.6f;
        const LogicalInput lt = evaluate_pad_bindings(xb, pad_sample, 0.12f);
        expect(lt.held(WiiTarget::Z) && !lt.held(WiiTarget::Shake), "Xbox: LT crouches");
    }
    expect(config.tune(ControlScheme::SwitchPro).pointer_hide_seconds == 4.0f &&
               config.tune(ControlScheme::SwitchPro).pointer_hide_radius == kDefaultPointerHideRadius &&
               config.tune(ControlScheme::Xbox).pointer_hide_seconds == kDefaultPointerHideSeconds,
           "still pointer hides after 4 s within 0.12 units by default");
    expect(only(xb[WiiTarget::PointerRecenter], {SourceKind::Button, pad::kRightStickBit}), "Xbox: RS recenters");
    expect(!config.tune(ControlScheme::Xbox).gyro_pointer, "Xbox has no gyro");

    // Right stick role: pointer with the gyro off, camera (D-pad) with it on.
    PadSample s{};
    s.connected = true;
    s.scheme = ControlScheme::SwitchPro;
    s.axes[static_cast<std::size_t>(PadAxis::RightX)] = -0.8f;
    const LogicalInput gyro_off = evaluate_pad_bindings(sw, s, 0.4f, 0u, false);
    const LogicalInput gyro_on = evaluate_pad_bindings(sw, s, 0.4f, 0u, true);
    expect(approx(gyro_off.pointer_x, -0.8f) && !gyro_off.held(WiiTarget::DpadLeft),
           "gyro off: the right stick is the pointer");
    expect(gyro_on.pointer_x == 0.0f && gyro_on.held(WiiTarget::DpadLeft),
           "gyro on: the right stick turns the camera");
    // Stick-fed digital targets release with hysteresis.
    s.axes[static_cast<std::size_t>(PadAxis::RightX)] = -0.4f;
    expect(evaluate_pad_bindings(sw, s, 0.4f, gyro_on.pressed, true).held(WiiTarget::DpadLeft),
           "a held stick direction stays pressed down to the release level");
    expect(!evaluate_pad_bindings(sw, s, 0.4f, 0u, true).held(WiiTarget::DpadLeft),
           "0.4 does not press from rest");
    s.axes[static_cast<std::size_t>(PadAxis::RightX)] = -0.3f;
    expect(!evaluate_pad_bindings(sw, s, 0.4f, gyro_on.pressed, true).held(WiiTarget::DpadLeft),
           "below the release level the direction releases");

    // Migration: a file holding the earlier defaults moves to the new ones. A binding the player
    // changed stays.
    ControlsConfig old{};
    for (std::size_t i = 0; i < kControlSchemeCount; ++i) {
        old.tables[i] = legacy_binding_table(static_cast<ControlScheme>(i));
        old.tuning[i] = legacy_scheme_tuning(static_cast<ControlScheme>(i));
    }
    old.tables[static_cast<std::size_t>(ControlScheme::Xbox)][WiiTarget::B] = {};
    add_binding(old.tables[static_cast<std::size_t>(ControlScheme::Xbox)][WiiTarget::B],
                {SourceKind::Button, pad::kWestBit});
    ControlsConfig migrated = old;
    // Old files have no jump_back line: the loader starts from the defaults.
    for (std::size_t i = 0; i < kControlSchemeCount; ++i) {
        migrated.tables[i][WiiTarget::JumpBack] =
            default_binding_table(static_cast<ControlScheme>(i))[WiiTarget::JumpBack];
    }
    const unsigned changed = migrate_legacy_controls(migrated, 1u);
    expect(changed > 0u, "migration changes the old defaults");
    expect(migrated.table(ControlScheme::SwitchPro) == default_binding_table(ControlScheme::SwitchPro),
           "an untouched Switch table becomes the new defaults");
    expect(migrated.tune(ControlScheme::SwitchPro).gyro_pointer, "gyro pointer turns on");
    expect(migrated.table(ControlScheme::Xbox)[WiiTarget::B][0] == InputSource{SourceKind::Button, pad::kWestBit},
           "a binding the player changed stays");
    expect(migrated.table(ControlScheme::Xbox)[WiiTarget::Z] == default_binding_table(ControlScheme::Xbox)[WiiTarget::Z],
           "other Xbox defaults move");
    ControlsConfig again = migrated;
    expect(migrate_legacy_controls(again, kControlsVersion) == 0u && again == migrated,
           "migration is idempotent on new defaults");
    // A version-3 file: ZL+ZR crouch and the 4 s / 0.03 still pointer move to the version-4 defaults.
    // Values the player changed stay.
    {
        ControlsConfig v3{};
        for (std::size_t i = 0; i < kControlSchemeCount; ++i) {
            const auto scheme = static_cast<ControlScheme>(i);
            v3.tables[i] = version3_binding_table(scheme);
            v3.tuning[i] = default_scheme_tuning(scheme);
            if (scheme_is_gamepad(scheme)) {
                v3.tuning[i].pointer_hide_seconds = kVersion3PointerHideSeconds;
                v3.tuning[i].pointer_hide_radius = kVersion3PointerHideRadius;
            }
        }
        v3.tune(ControlScheme::Xbox).pointer_hide_seconds = 7.0f;
        v3.table(ControlScheme::SwitchPro)[WiiTarget::Z] = {};
        add_binding(v3.table(ControlScheme::SwitchPro)[WiiTarget::Z], {SourceKind::Button, pad::kRightShoulderBit});
        expect(migrate_legacy_controls(v3, 3u) > 0u, "version-3 defaults migrate");
        expect(v3.table(ControlScheme::Xbox) == default_binding_table(ControlScheme::Xbox) &&
                   v3.table(ControlScheme::SwitchPro2) == default_binding_table(ControlScheme::SwitchPro2),
               "untouched version-3 tables become the version-4 defaults");
        expect(v3.table(ControlScheme::SwitchPro)[WiiTarget::Shake] ==
                   default_binding_table(ControlScheme::SwitchPro)[WiiTarget::Shake],
               "version-3 Switch spin gains ZR");
        expect(v3.table(ControlScheme::SwitchPro)[WiiTarget::Z][0] == InputSource{SourceKind::Button, pad::kRightShoulderBit},
               "a changed version-3 crouch binding stays");
        expect(v3.tune(ControlScheme::SwitchPro).pointer_hide_seconds == kDefaultPointerHideSeconds &&
                   v3.tune(ControlScheme::SwitchPro).pointer_hide_radius == kDefaultPointerHideRadius,
               "version-3 still-pointer timing moves to 4 s / 0.12");
        expect(v3.tune(ControlScheme::Xbox).pointer_hide_seconds == 7.0f &&
                   v3.tune(ControlScheme::Xbox).pointer_hide_radius == kDefaultPointerHideRadius,
               "a changed hide time stays");
    }
    // A version-4 file: a 2 s still pointer and 100% vibration move to 4 s and 50%. Values the player
    // changed stay.
    {
        ControlsConfig v4 = default_controls_config();
        for (std::size_t i = 0; i < kControlSchemeCount; ++i) {
            if (!scheme_is_gamepad(static_cast<ControlScheme>(i))) continue;
            v4.tuning[i].pointer_hide_seconds = kVersion4PointerHideSeconds;
            v4.tuning[i].rumble = kVersion4Rumble;
        }
        v4.tune(ControlScheme::Xbox).rumble = 30;
        v4.tune(ControlScheme::SwitchPro2).pointer_hide_seconds = 9.0f;
        expect(migrate_legacy_controls(v4, 4u) > 0u, "version-4 defaults migrate");
        expect(v4.tune(ControlScheme::SwitchPro).pointer_hide_seconds == kDefaultPointerHideSeconds &&
                   v4.tune(ControlScheme::SwitchPro).rumble == rb_default_strength(),
               "version-4 still pointer and vibration move to 4 s and 50%");
        expect(v4.tune(ControlScheme::Xbox).rumble == 30u && v4.tune(ControlScheme::SwitchPro2).pointer_hide_seconds == 9.0f,
               "changed version-4 values stay");
        expect(v4 == [&] {
            ControlsConfig again = v4;
            (void)migrate_legacy_controls(again, kControlsVersion);
            return again;
        }(), "a current file does not migrate");
    }
    // A version-2 file: its defaults move. The gyro switch the player turned off stays off.
    ControlsConfig v2{};
    for (std::size_t i = 0; i < kControlSchemeCount; ++i) {
        v2.tables[i] = version2_binding_table(static_cast<ControlScheme>(i));
        v2.tuning[i] = default_scheme_tuning(static_cast<ControlScheme>(i));
    }
    v2.tune(ControlScheme::SwitchPro).gyro_pointer = false;
    v2.table(ControlScheme::SwitchPro)[WiiTarget::Shake] = {};
    add_binding(v2.table(ControlScheme::SwitchPro)[WiiTarget::Shake], {SourceKind::Button, pad::kNorthBit});
    expect(migrate_legacy_controls(v2, 2u) > 0u, "version-2 defaults migrate");
    expect(v2.table(ControlScheme::Xbox) == default_binding_table(ControlScheme::Xbox),
           "an untouched version-2 Xbox table becomes the current defaults");
    expect(v2.table(ControlScheme::SwitchPro)[WiiTarget::B] == default_binding_table(ControlScheme::SwitchPro)[WiiTarget::B],
           "version-2 ZR Star Bits move to L");
    expect(!v2.tune(ControlScheme::SwitchPro).gyro_pointer, "a version-2 gyro choice stays");
    expect(v2.table(ControlScheme::SwitchPro)[WiiTarget::Shake][0] == InputSource{SourceKind::Button, pad::kNorthBit} &&
               v2.table(ControlScheme::SwitchPro)[WiiTarget::Shake][1].empty(),
           "a changed version-2 binding stays");
}

std::string bindings_text(ControlScheme scheme, WiiTarget target) {
    return format_target_bindings(scheme, default_binding_table(scheme)[target]);
}

// PlayStation (DualShock 4, DualSense, DualSense Edge) and Steam Controller schemes.
void test_playstation_and_steam_schemes() {
    expect(kControlSchemeCount == 7, "seven schemes (with generic)");
    expect(std::string(control_scheme_id(ControlScheme::PlayStation)) == "playstation" &&
               std::string(control_scheme_id(ControlScheme::Steam)) == "steam",
           "scheme ids");
    expect(parse_control_scheme("playstation") == ControlScheme::PlayStation &&
               parse_control_scheme("steam") == ControlScheme::Steam,
           "scheme ids parse");
    expect(scheme_is_gamepad(ControlScheme::PlayStation) && scheme_is_gamepad(ControlScheme::Steam) &&
               scheme_has_motion(ControlScheme::PlayStation) && scheme_has_motion(ControlScheme::Steam) &&
               !scheme_has_motion(ControlScheme::Xbox),
           "family traits");

    expect(bindings_text(ControlScheme::PlayStation, WiiTarget::A) == "Cross" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::JumpBack) == "Circle" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::B) == "L1" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::Z) == "L2" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::C) == "L3" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::Shake) == "Square, Triangle, R2" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::PointerRecenter) == "R1" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::Plus) == "Options" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::Minus) == "Share" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::Home) == "PS" &&
               bindings_text(ControlScheme::PlayStation, WiiTarget::DpadUp) == "DpadUp, RY+",
           "PlayStation defaults");
    expect(bindings_text(ControlScheme::Steam, WiiTarget::A) == "A" &&
               bindings_text(ControlScheme::Steam, WiiTarget::JumpBack) == "B" &&
               bindings_text(ControlScheme::Steam, WiiTarget::B) == "LB" &&
               bindings_text(ControlScheme::Steam, WiiTarget::Z) == "LT" &&
               bindings_text(ControlScheme::Steam, WiiTarget::Shake) == "X, Y, RT" &&
               bindings_text(ControlScheme::Steam, WiiTarget::PointerRecenter) == "RB" &&
               bindings_text(ControlScheme::Steam, WiiTarget::Home) == "Steam",
           "Steam defaults");
    for (ControlScheme scheme : {ControlScheme::PlayStation, ControlScheme::Steam}) {
        expect(legacy_binding_table(scheme) == default_binding_table(scheme) &&
                   version2_binding_table(scheme) == default_binding_table(scheme) &&
                   version3_binding_table(scheme) == default_binding_table(scheme),
               "new schemes have no older defaults to migrate");
        const SchemeTuning tuning = default_scheme_tuning(scheme);
        expect(tuning.gyro_pointer && tuning.motion_shake == MotionShake::Remote && tuning.vibration &&
                   tuning.pointer_hide_seconds == kDefaultPointerHideSeconds,
               "new schemes start with the gyro pointer, shake and vibration on");
        expect(sanitize_controls_config(default_controls_config()).tune(scheme).gyro_pointer,
               "sanitising keeps the gyro of a motion scheme");
    }

    expect(parse_input_source(ControlScheme::PlayStation, "create") == InputSource{SourceKind::Button, pad::kBackBit},
           "Create is the Share button");
    expect(parse_input_source(ControlScheme::PlayStation, "R2") == InputSource{SourceKind::AxisPositive, 5} &&
               !parse_input_source(ControlScheme::PlayStation, "ZR").has_value(),
           "PlayStation trigger names");
    expect(parse_input_source(ControlScheme::Steam, "L5") == InputSource{SourceKind::Button, pad::kGripLeft2Bit} &&
               parse_input_source(ControlScheme::Steam, "RPad") == InputSource{SourceKind::Button, pad::kRightPadBit},
           "Steam button names");

    // Which buttons exist on which controller.
    const auto allowed = [](ControlScheme scheme, unsigned bit_index) {
        return binding_source_allowed(scheme, WiiTarget::A,
                                      {SourceKind::Button, static_cast<std::uint8_t>(bit_index)});
    };
    expect(allowed(ControlScheme::PlayStation, pad::kCaptureBit) && allowed(ControlScheme::PlayStation, pad::kChatBit) &&
               allowed(ControlScheme::PlayStation, pad::kGripLeftBit) &&
               !allowed(ControlScheme::PlayStation, pad::kLeftPadBit) &&
               !allowed(ControlScheme::PlayStation, pad::kGripLeft2Bit),
           "PlayStation buttons");
    expect(allowed(ControlScheme::Steam, pad::kLeftPadBit) && allowed(ControlScheme::Steam, pad::kGripRight2Bit) &&
               allowed(ControlScheme::Steam, pad::kGripLeftBit) && !allowed(ControlScheme::Steam, pad::kChatBit),
           "Steam buttons");
    expect(!allowed(ControlScheme::Xbox, pad::kLeftPadBit) && !allowed(ControlScheme::SwitchPro2, pad::kLeftPadBit) &&
               allowed(ControlScheme::SwitchPro2, pad::kChatBit) && !allowed(ControlScheme::SwitchPro, pad::kGripLeftBit) &&
               !allowed(ControlScheme::Xbox, pad::kHomeBit) && allowed(ControlScheme::SwitchPro, pad::kHomeBit) &&
               !allowed(ControlScheme::Keyboard, pad::kSouthBit) && !allowed(ControlScheme::Steam, pad::kButtonBitCount),
           "the other schemes keep their button sets");
    for (std::size_t s = 0; s < kControlSchemeCount; ++s) {
        const auto scheme = static_cast<ControlScheme>(s);
        for (unsigned index = 0; index < pad::kButtonBitCount; ++index) {
            const InputSource source{SourceKind::Button, static_cast<std::uint8_t>(index)};
            if (!binding_source_allowed(scheme, WiiTarget::A, source)) continue;
            expect(parse_input_source(scheme, format_input_source(scheme, source)) == source,
                   "every allowed button has a distinct name");
        }
    }

    // Round trip, and a file from before the two schemes existed.
    const auto apply_all = [](ControlsConfig& target, const std::string& text) {
        std::size_t start = 0;
        while (start < text.size()) {
            const std::size_t end = text.find('\n', start);
            const std::string line = text.substr(start, end - start);
            start = end + 1u;
            const std::size_t eq = line.find('=');
            (void)apply_controls_setting(target, line.substr(0, eq), line.substr(eq + 1u));
        }
    };
    ControlsConfig config = default_controls_config();
    config.table(ControlScheme::PlayStation)[WiiTarget::A] = {};
    add_binding(config.table(ControlScheme::PlayStation)[WiiTarget::A], {SourceKind::Button, pad::kCaptureBit});
    config.table(ControlScheme::Steam)[WiiTarget::Z] = {};
    add_binding(config.table(ControlScheme::Steam)[WiiTarget::Z], {SourceKind::Button, pad::kLeftPadBit});
    config.tune(ControlScheme::PlayStation).rumble = 80;
    config.tune(ControlScheme::Steam).gyro_sensitivity = 2.0f;
    const std::string text = format_controls_settings(config);
    expect(text.find("bind.playstation.a=Touchpad\n") != std::string::npos &&
               text.find("bind.steam.z=LPad\n") != std::string::npos &&
               text.find("tune.playstation.rumble=80\n") != std::string::npos &&
               text.find("tune.steam.gyro_sensitivity=2\n") != std::string::npos,
           "PlayStation and Steam lines are stable text");
    ControlsConfig parsed = default_controls_config();
    apply_all(parsed, text);
    expect(parsed == config, "the new schemes survive the ini round trip");

    ControlsConfig old_file = default_controls_config();
    apply_all(old_file, "bind.xbox.a=B\nbind.switch_pro.b=Y\ntune.switch_pro2.rumble=30\n");
    expect(old_file.table(ControlScheme::Xbox)[WiiTarget::A][0] == InputSource{SourceKind::Button, pad::kEastBit} &&
               old_file.tune(ControlScheme::SwitchPro2).rumble == 30u,
           "lines of the older schemes apply");
    expect(old_file.table(ControlScheme::PlayStation) == default_binding_table(ControlScheme::PlayStation) &&
               old_file.table(ControlScheme::Steam) == default_binding_table(ControlScheme::Steam) &&
               old_file.tune(ControlScheme::PlayStation) == default_scheme_tuning(ControlScheme::PlayStation),
           "a file without the new schemes leaves them at their defaults");
    const ControlsConfig before_migration = old_file;
    (void)migrate_legacy_controls(old_file, 1u);
    expect(old_file.table(ControlScheme::PlayStation) == before_migration.table(ControlScheme::PlayStation) &&
               old_file.table(ControlScheme::Steam) == before_migration.table(ControlScheme::Steam),
           "migration leaves the new schemes alone");
    ControlsConfig unknown = default_controls_config();
    apply_all(unknown, "bind.nes.a=A\ntune.nes.rumble=20\n");
    expect(unknown == default_controls_config(), "unknown scheme ids are skipped");

    // The overlay chord and capture work on the new bits.
    expect(settings_pad_buttons(ControlScheme::PlayStation, pad::bit(pad::kSouthBit)) == XINPUT_GAMEPAD_A &&
               settings_pad_buttons(ControlScheme::Steam, pad::bit(pad::kSouthBit)) == XINPUT_GAMEPAD_A,
           "Cross and A confirm: no face swap off Nintendo controllers");
    PadSample before{};
    before.connected = true;
    PadSample now = before;
    now.buttons = pad::bit(pad::kRightPadBit);
    const auto captured = detect_pad_capture(before, now);
    expect(captured && *captured == InputSource{SourceKind::Button, pad::kRightPadBit},
           "the trackpad click can be captured");

    PadSample sample{};
    sample.connected = true;
    sample.scheme = ControlScheme::PlayStation;
    sample.buttons = pad::bit(pad::kSouthBit) | pad::bit(pad::kLeftShoulderBit);
    const LogicalInput logic =
        evaluate_pad_bindings(default_binding_table(ControlScheme::PlayStation), sample, 0.4f);
    expect(logic.held(WiiTarget::A) && logic.held(WiiTarget::B) && !logic.held(WiiTarget::JumpBack),
           "Cross is Wii A and L1 is Wii B");
}

void test_kpad_dots_stable() {
    // A still pointer produces identical IR dots report after report, and a
    // sub-unit move never jumps more than one raw unit.
    WiimoteInputSnapshot a{}, b{};
    publish_wiimote_ir_dots(a, 0.31234f, -0.12f, true);
    publish_wiimote_ir_dots(b, 0.31234f, -0.12f, true);
    expect(a.ir[0].x == b.ir[0].x && a.ir[0].y == b.ir[0].y && a.ir[1].x == b.ir[1].x,
           "a still pointer gives identical dots");
    int previous = -1;
    bool monotonic = true;
    for (int i = 0; i <= 200; ++i) {
        WiimoteInputSnapshot s{};
        publish_wiimote_ir_dots(s, -0.5f + i * 0.005f, 0.0f, true);
        const int x = s.ir[0].x;
        if (previous >= 0 && (x > previous || previous - x > 2)) monotonic = false;
        previous = x;
    }
    expect(monotonic, "a steady sweep moves the dots in single small steps");
}

}  // namespace

// The game's button icons: each Wii button's first controller input in the style asked; B in menus the button that
// goes back there; the Wii's own icons where nothing on the controller is bound.
void test_button_icons() {
    const BindingTable xbox = default_binding_table(ControlScheme::Xbox);
    const auto icon = [](const ButtonIconSpec& spec, ButtonIcon which) {
        return std::pair<ButtonGlyph, unsigned>(spec.glyphs[static_cast<std::size_t>(which)],
                                                spec.places[static_cast<std::size_t>(which)]);
    };
    using Glyph = std::pair<ButtonGlyph, unsigned>;
    const ButtonIconSpec play = button_icon_spec(ButtonIconStyle::Xbox, xbox, false);
    expect(play.style == ButtonIconStyle::Xbox, "the style asked");
    expect(icon(play, ButtonIcon::A) == Glyph{ButtonGlyph::Face, 0u}, "Xbox: A is the south button");
    expect(icon(play, ButtonIcon::B) == Glyph{ButtonGlyph::Shoulder, 0u}, "Xbox in play: B is LB (Star Bits)");
    expect(icon(play, ButtonIcon::Z) == Glyph{ButtonGlyph::Trigger, 0u}, "Xbox: Z is LT");
    expect(icon(play, ButtonIcon::C) == Glyph{ButtonGlyph::Stick, 0u}, "Xbox: C is the left stick's click");
    expect(icon(play, ButtonIcon::Plus) == Glyph{ButtonGlyph::Start, 0u}, "Xbox: Plus is Menu");
    expect(icon(play, ButtonIcon::Minus) == Glyph{ButtonGlyph::Back, 0u}, "Xbox: Minus is View");
    expect(icon(play, ButtonIcon::Pad) == Glyph{ButtonGlyph::Keep, 0u}, "the Control Pad keeps the Wii's own");
    expect(icon(play, ButtonIcon::One) == Glyph{ButtonGlyph::Keep, 0u}, "Xbox: 1 has no button: the Wii's own icon");
    const ButtonIconSpec menu = button_icon_spec(ButtonIconStyle::Xbox, xbox, true);
    expect(icon(menu, ButtonIcon::B) == Glyph{ButtonGlyph::Face, 1u}, "Xbox in menus: B is the east button (back)");
    const ButtonIconSpec nintendo =
        button_icon_spec(ButtonIconStyle::Nintendo, default_binding_table(ControlScheme::SwitchPro), true);
    expect(icon(nintendo, ButtonIcon::A) == Glyph{ButtonGlyph::Face, 1u}, "Switch: A is the east button");
    expect(icon(nintendo, ButtonIcon::B) == Glyph{ButtonGlyph::Face, 0u}, "Switch in menus: B is the south button");
    const ButtonIconSpec wii = button_icon_spec(ButtonIconStyle::Wii, xbox, false);
    bool kept = true;
    for (const ButtonGlyph glyph : wii.glyphs) kept = kept && glyph == ButtonGlyph::Keep;
    expect(kept, "the Wii style keeps every icon");
    BindingTable remapped = xbox;
    remapped[WiiTarget::A] = {};
    remapped[WiiTarget::A][0] = InputSource{SourceKind::Button, static_cast<std::uint8_t>(pad::kNorthBit)};
    expect(icon(button_icon_spec(ButtonIconStyle::Xbox, remapped, false), ButtonIcon::A) == Glyph{ButtonGlyph::Face, 3u},
           "a rebound A shows the button it is bound to");
}

// The keyboard's icons: every key and mouse button has one; each Wii button shows its input as it is reached for there.
void test_keyboard_icons() {
    const auto key = [](std::uint8_t vk) { return key_icon(InputSource{SourceKind::Key, vk}); };
    bool all = true;
    for (unsigned vk = 1u; vk < 256u; ++vk) {
        const std::optional<std::uint8_t> icon = key(static_cast<std::uint8_t>(vk));
        all = all && icon.has_value() && *icon < kKeyIconCount;
    }
    expect(all, "every virtual key has an icon");
    expect(key('A') == 0u && key('Z') == 25u && key('0') == 26u && key('9') == 35u, "letters and digits");
    expect(key(VK_F1) == 36u && key(VK_F24) == 59u && key(VK_NUMPAD0) == 60u && key(VK_DECIMAL) == 74u,
           "F1 to F24 and the number pad");
    expect(key(VK_SPACE) == 75u && key(VK_RWIN) == 106u, "the named keys from Space to the right Windows key");
    expect(key(VK_OEM_1) == kKeyIconLayoutFirst && key(VK_OEM_102) == kKeyIconLayoutFirst + 12u &&
               key_icon_layout_key(0) == VK_OEM_1 && key_icon_layout_key(12) == VK_OEM_102,
           "the layout keys, in order");
    expect(key(VK_BROWSER_BACK) == kKeyIconOther && key(0xFF) == kKeyIconOther, "a key without its own: the unnamed key");
    expect(key_icon(InputSource{SourceKind::Mouse, 0u}) == kKeyIconMouseLeft &&
               key_icon(InputSource{SourceKind::Mouse, 4u}) == kKeyIconMouseLeft + 4u &&
               !key_icon(InputSource{SourceKind::Mouse, 5u}).has_value() &&
               !key_icon(InputSource{SourceKind::Button, 0u}).has_value(),
           "mouse buttons; nothing else");

    const BindingTable keyboard = default_binding_table(ControlScheme::Keyboard);
    using Glyph = std::pair<ButtonGlyph, unsigned>;
    const auto icon = [](const ButtonIconSpec& spec, ButtonIcon which) {
        return Glyph(spec.glyphs[static_cast<std::size_t>(which)], spec.places[static_cast<std::size_t>(which)]);
    };
    const ButtonIconSpec play = button_icon_spec(ButtonIconStyle::Keyboard, keyboard, false);
    expect(play.style == ButtonIconStyle::Keyboard, "the keyboard's style");
    expect(icon(play, ButtonIcon::A) == Glyph{ButtonGlyph::Key, 75u}, "in play A is Space");
    expect(icon(play, ButtonIcon::B) == Glyph{ButtonGlyph::Key, kKeyIconMouseLeft + 1u},
           "B is the right click (Star Bits fly where the pointer aims)");
    expect(icon(play, ButtonIcon::C) == Glyph{ButtonGlyph::Key, 2u} && icon(play, ButtonIcon::Z) == Glyph{ButtonGlyph::Key, 25u},
           "C and Z are their keys");
    expect(icon(play, ButtonIcon::One) == Glyph{ButtonGlyph::Key, 27u} && icon(play, ButtonIcon::Two) == Glyph{ButtonGlyph::Key, 28u},
           "1 and 2");
    expect(icon(play, ButtonIcon::Plus) == Glyph{ButtonGlyph::Key, kKeyIconLayoutFirst + 1u} &&
               icon(play, ButtonIcon::Minus) == Glyph{ButtonGlyph::Key, kKeyIconLayoutFirst + 3u},
           "Plus and Minus are the = and - keys");
    expect(icon(play, ButtonIcon::Home) == Glyph{ButtonGlyph::Key, 7u}, "HOME is H");
    expect(play.glyphs[static_cast<std::size_t>(ButtonIcon::Pad)] == ButtonGlyph::Dpad && play.pad_keys[0] == 89u &&
               play.pad_keys[1] == 90u && play.pad_keys[2] == 91u && play.pad_keys[3] == 92u,
           "the Control Pad is the arrows");
    const ButtonIconSpec menu = button_icon_spec(ButtonIconStyle::Keyboard, keyboard, true);
    expect(icon(menu, ButtonIcon::A) == Glyph{ButtonGlyph::Key, kKeyIconMouseLeft}, "in menus A is the left click");
    expect(icon(menu, ButtonIcon::B) == Glyph{ButtonGlyph::Key, kKeyIconMouseLeft + 1u}, "and B the right click");

    BindingTable keys_only = keyboard;
    keys_only[WiiTarget::A] = {};
    keys_only[WiiTarget::A][0] = InputSource{SourceKind::Key, VK_RETURN};
    keys_only[WiiTarget::C] = {};
    keys_only[WiiTarget::DpadLeft] = {};
    const ButtonIconSpec rebound = button_icon_spec(ButtonIconStyle::Keyboard, keys_only, true);
    expect(icon(rebound, ButtonIcon::A) == Glyph{ButtonGlyph::Key, 76u}, "A without a mouse button shows its key in menus too");
    expect(icon(rebound, ButtonIcon::C) == Glyph{ButtonGlyph::Keep, 0u}, "an unbound button keeps the Wii's icon");
    expect(rebound.pad_keys[2] == kKeyIconNone && rebound.pad_keys[0] == 89u, "an unbound direction shows no key");
    expect(std::string(key_icon_name(VK_VOLUME_UP).data()) == "VOL +" &&
               std::string(key_icon_name(VK_CONVERT).data()) == "CONV" &&
               std::string(key_icon_name(0x88).data()) == "VK88",
           "a key without its own icon carries the name keyboards print, else its code");
    keys_only[WiiTarget::Z] = {};
    keys_only[WiiTarget::Z][0] = InputSource{SourceKind::Key, VK_MEDIA_PLAY_PAUSE};
    keys_only[WiiTarget::C][0] = InputSource{SourceKind::Key, VK_VOLUME_MUTE};
    const ButtonIconSpec named = button_icon_spec(ButtonIconStyle::Keyboard, keys_only, false);
    expect(icon(named, ButtonIcon::Z) == Glyph{ButtonGlyph::Key, kKeyIconOther} &&
               std::string(named.other[static_cast<std::size_t>(ButtonIcon::Z)].data()) == "PLAY" &&
               std::string(named.other[static_cast<std::size_t>(ButtonIcon::C)].data()) == "MUTE" &&
               named.other[static_cast<std::size_t>(ButtonIcon::A)][0] == 0,
           "each such icon names its own key");
    keys_only[WiiTarget::JumpBack][0] = InputSource{SourceKind::Key, VK_BACK};
    expect(icon(button_icon_spec(ButtonIconStyle::Keyboard, keys_only, true), ButtonIcon::B) == Glyph{ButtonGlyph::Key, 78u},
           "in menus B is the back key's first (JumpBack)");
}

void test_shared_bindings() {
    for (std::size_t i = 0; i < kControlSchemeCount; ++i) {
        const auto scheme = static_cast<ControlScheme>(i);
        const std::uint32_t shared = shared_binding_targets(default_binding_table(scheme));
        if (shared != 0u) {
            std::cerr << control_scheme_id(scheme) << " defaults share an input: targets 0x" << std::hex << shared
                      << std::dec << '\n';
        }
        expect(shared == 0u, "no default binding list shares an input with another");
    }
    BindingTable table = default_binding_table(ControlScheme::Keyboard);
    const InputSource space{SourceKind::Key, 0x20};
    table[WiiTarget::A] = {space};
    table[WiiTarget::Two] = {InputSource{SourceKind::Key, 'X'}, space};
    const std::uint32_t both = wii_target_bit(WiiTarget::A) | wii_target_bit(WiiTarget::Two);
    expect((shared_binding_targets(table) & both) == both, "a key bound to two targets marks both");
    BindingTable pad = default_binding_table(ControlScheme::Xbox);
    const InputSource right_up{SourceKind::AxisPositive, static_cast<std::uint8_t>(PadAxis::RightY)};
    pad[WiiTarget::PointerUp] = {right_up};
    pad[WiiTarget::DpadUp] = {right_up};
    expect((shared_binding_targets(pad) & wii_target_bit(WiiTarget::PointerUp)) == 0u,
           "a stick direction shared by the pointer and another target drives one at a time");
    pad[WiiTarget::PointerDown] = {right_up};
    expect((shared_binding_targets(pad) & wii_target_bit(WiiTarget::PointerDown)) != 0u,
           "two pointer directions on one stick direction act together");
}

void test_touch_tuning() {
    const ControlsConfig defaults = default_controls_config();
    expect(defaults.tune(ControlScheme::PlayStation).touch_pointer && defaults.tune(ControlScheme::Steam).touch_pointer,
           "the touchpad points by default where there is one");
    expect(!defaults.tune(ControlScheme::Xbox).touch_pointer && !defaults.tune(ControlScheme::SwitchPro).touch_pointer,
           "no touch pointer without a touchpad");
    ControlsConfig config = defaults;
    expect(apply_controls_setting(config, "tune.playstation.touch_pointer", "0") &&
               !config.tune(ControlScheme::PlayStation).touch_pointer,
           "touch_pointer reads");
    expect(apply_controls_setting(config, "tune.steam.touch_sensitivity", "2.5") &&
               config.tune(ControlScheme::Steam).touch_sensitivity == 2.5f,
           "touch_sensitivity reads");
    expect(apply_controls_setting(config, "tune.xbox.touch_pointer", "1") && !config.tune(ControlScheme::Xbox).touch_pointer,
           "a controller without a touchpad never takes the touch pointer");
    const std::string text = format_controls_settings(config);
    expect(text.find("tune.playstation.touch_pointer=0\n") != std::string::npos &&
               text.find("tune.steam.touch_sensitivity=2.5\n") != std::string::npos &&
               text.find("tune.xbox.touch_pointer") == std::string::npos,
           "touch settings are written for touchpad controllers only");
    ControlsConfig read = defaults;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos) (void)apply_controls_setting(read, line.substr(0, eq), line.substr(eq + 1u));
    }
    expect(read.tune(ControlScheme::PlayStation) == config.tune(ControlScheme::PlayStation) &&
               read.tune(ControlScheme::Steam) == config.tune(ControlScheme::Steam),
           "touch settings round-trip");

    PointerOwner owner{};
    PointerActivity gyro{};
    gyro.gyro = true;
    expect(owner.update(gyro, 1000u) == PointerSource::Gyro, "the gyro points");
    PointerActivity both{};
    both.gyro = true;
    both.touch = true;
    expect(owner.update(both, 2000u) == PointerSource::Touch, "a finger on the touchpad takes the pointer at once");
    expect(owner.update(gyro, 3000u) == PointerSource::Touch, "the gyro waits for the touchpad's handoff");
    expect(std::string(pointer_source_name(PointerSource::Touch)) == "touch", "the touch source's name");
}

int main(int argc, char** argv) {
    // The launcher's ControlsSettings mirrors these defaults; this prints them
    // for a cross-check.
    if (argc > 1 && std::strcmp(argv[1], "--print-defaults") == 0) {
        std::cout << format_controls_settings(default_controls_config());
        return 0;
    }
    test_names_round_trip();
    test_ini_round_trip();
    test_evaluation();
    test_chord_and_capture();
    test_switch_pro_decoding();
    test_joycons();
    time_joycon_reports();
    test_switch2_decoding();
    test_motion();
    test_pointer_ownership();
    test_stick_pointer();
    test_jump_back();
    test_rest_hide();
    test_mouse_rest_hide();
    test_rest_hide_gyro();
    test_recenter_hold();
    test_rumble_encoding();
    test_rumble_gate();
    test_tilt();
    test_defaults_and_migration();
    test_playstation_and_steam_schemes();
    test_kpad_dots_stable();
    test_button_icons();
    test_keyboard_icons();
    test_shared_bindings();
    test_touch_tuning();
    if (g_failures != 0) {
        std::cerr << g_failures << " controls test(s) failed\n";
        return 1;
    }
    std::cout << "Controls tests passed\n";
    return 0;
}
