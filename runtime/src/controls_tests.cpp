// Remappable controls: binding model, ini round trip, evaluation, chord
// mapping, Switch report decoding and the motion helpers.

#include "galaxy/input_bindings.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/switch_controller.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
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
    for (ControlScheme scheme : {ControlScheme::Xbox, ControlScheme::SwitchPro, ControlScheme::SwitchPro2}) {
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
    config.tune(ControlScheme::SwitchPro).gyro_pointer = true;
    config.tune(ControlScheme::SwitchPro).shake_sensitivity = 5;
    config.tune(ControlScheme::SwitchPro2).motion_shake = MotionShake::Nunchuk;
    config.tune(ControlScheme::Xbox).pointer_speed = 3.7f;
    config.tune(ControlScheme::Keyboard).invert_pointer_y = true;
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
    expect(text.find("bind.xbox.b=B, RT\n") != std::string::npos, "xbox B line is stable text");
    expect(text.find("bind.switch_pro.a=A\n") != std::string::npos, "switch A line is stable text");
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
    expect(pad.held(WiiTarget::A) && pad.held(WiiTarget::Z), "XInput A and LB map to A and Z");
    expect(pad.held(WiiTarget::B), "right trigger at the threshold presses B");
    expect(approx(pad.stick_x, -0.4f) && !pad.held(WiiTarget::StickLeft), "analog stick passes through");
    expect(approx(pad.pointer_y, 0.8f), "right stick drives the pointer");
    sample.axes[static_cast<std::size_t>(PadAxis::RightTrigger)] = 95.0f / 255.0f;
    pad = evaluate_pad_bindings(config.table(ControlScheme::Xbox), sample, threshold);
    expect(!pad.held(WiiTarget::B), "below the threshold the trigger is released");

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
               logic.held(WiiTarget::Home) && logic.held(WiiTarget::DpadUp) && logic.held(WiiTarget::Z) && !logic.held(WiiTarget::Shake),
           "Switch defaults: A->A, ZR->B, Plus, Home, D-pad, L->Z");

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

void test_motion() {
    using namespace switch_hid;
    ShakeDetector detector{};
    std::uint64_t t = 0;
    int starts = 0;
    // At rest: 1 g, no shake.
    for (int i = 0; i < 50; ++i, t += 5000) starts += detector.feed({0.0f, 1.0f, 0.0f}, t, 3) ? 1 : 0;
    expect(starts == 0, "resting controller never shakes");
    // A 3 g swing for 40 ms.
    bool started = false;
    std::uint64_t start_time = 0;
    for (int i = 0; i < 8; ++i, t += 5000) {
        const float s = (i % 2 == 0) ? 3.0f : -1.5f;
        if (detector.feed({s, 1.0f, 0.0f}, t, 3)) {
            started = true;
            start_time = t;
        }
    }
    expect(started && detector.pressed(start_time + 1000) &&
               !detector.pressed(start_time + ShakeDetector::kHoldUs),
           "a swing starts one held shake");
    // Continued shaking inside the cooldown does not retrigger.
    int retriggers = 0;
    for (int i = 0; i < 20; ++i, t += 5000) {
        const float s = (i % 2 == 0) ? 3.0f : -1.5f;
        if (detector.feed({s, 1.0f, 0.0f}, t, 3) && t < start_time + ShakeDetector::kCooldownUs) ++retriggers;
    }
    expect(retriggers == 0, "cooldown suppresses repeats");
    // Sensitivity ordering.
    expect(ShakeDetector::threshold_g(1) > ShakeDetector::threshold_g(3) &&
               ShakeDetector::threshold_g(3) > ShakeDetector::threshold_g(5),
           "higher sensitivity needs less acceleration");
    ShakeDetector light{};
    t = 0;
    bool light_started = false;
    bool strong_started = false;
    ShakeDetector strong{};
    for (int i = 0; i < 8; ++i, t += 5000) {
        const std::array<float, 3> a{1.4f, 1.0f, 0.0f};  // ~0.72 g over rest
        light_started |= light.feed(a, t, 5);
        strong_started |= strong.feed(a, t, 1);
    }
    expect(light_started && !strong_started, "a gentle shake counts only at high sensitivity");

    ImuSample sample{};
    sample.gyro = {0.35f, 0.35f, 0.0f};  // pitch up, yaw left
    const GyroPointerDelta delta = gyro_pointer_delta(sample, 1.0f, 2.4f);
    expect(approx(delta.x, -1.0f, 1e-3f) && approx(delta.y, 1.0f, 1e-3f),
           "half a screen per 0.35 rad: yaw left moves left, pitch up moves up");
}

}  // namespace

int main() {
    test_names_round_trip();
    test_ini_round_trip();
    test_evaluation();
    test_chord_and_capture();
    test_switch_pro_decoding();
    test_switch2_decoding();
    test_motion();
    if (g_failures != 0) {
        std::cerr << g_failures << " controls test(s) failed\n";
        return 1;
    }
    std::cout << "Controls tests passed\n";
    return 0;
}
