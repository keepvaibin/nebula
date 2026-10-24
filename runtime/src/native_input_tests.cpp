#include "galaxy/native_input.h"
#include "galaxy/gx/pointer_response.h"
#include "galaxy/synthetic_kpad_buttons.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/experimental_ultrawide_aspect.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
    ok &= expect(only_key(s, pad::kKeyRight), "A changes the selected setting");
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

bool expect_bytes(
    std::span<const std::uint8_t> actual,
    std::span<const std::uint8_t> expected,
    const char* message) {
    if (actual.size() != expected.size()) {
        std::cerr << "FAILED: " << message << " size actual=" << actual.size()
                  << " expected=" << expected.size() << '\n';
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::cerr << "FAILED: " << message << " byte[" << i
                      << "] actual=0x" << std::hex
                      << static_cast<unsigned>(actual[i]) << " expected=0x"
                      << static_cast<unsigned>(expected[i]) << std::dec << '\n';
            return false;
        }
    }
    return true;
}

std::vector<std::byte> fake_input_report(std::uint8_t marker) {
    return {
        std::byte{galaxy::input::kWiimoteHidInput},
        std::byte{0x31u},
        std::byte{marker}};
}

struct FakeNativeHidState {
    std::mutex mutex;
    std::condition_variable wake;
    bool block_open{};
    bool block_writes{};
    bool fail_open{};
    bool fail_write{};
    bool stop_requested{};
    bool open_entered{};
    bool open_succeeded{};
    bool write_entered{};
    bool closed{};
    std::size_t input_reports_polled{};
    bool pause_second_poll{};
    bool second_poll_entered{};
    bool release_second_poll{};
    std::deque<std::vector<std::byte>> input_reports;
    std::vector<std::vector<std::byte>> output_reports;
};

struct FakeFailureNotification {
    std::atomic<std::uint32_t> calls{};
};

void note_fake_hid_failure(void* user) noexcept {
    auto* notification = static_cast<FakeFailureNotification*>(user);
    notification->calls.fetch_add(1u, std::memory_order_release);
}

struct DecodedIrDot {
    bool visible = false;
    std::uint16_t x = 0x03ffu;
    std::uint16_t y = 0x03ffu;
    std::uint8_t size = 0;
};

std::array<DecodedIrDot, 2> decode_basic_ir_pair(
    std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 5u) {
        return {};
    }

    const std::uint8_t high = bytes[2];
    const auto first_x = static_cast<std::uint16_t>(bytes[0] | (high & 0x30u) << 4u);
    const auto first_y = static_cast<std::uint16_t>(bytes[1] | (high & 0xc0u) << 2u);
    const auto second_x = static_cast<std::uint16_t>(bytes[3] | (high & 0x03u) << 8u);
    const auto second_y = static_cast<std::uint16_t>(bytes[4] | (high & 0x0cu) << 6u);
    const bool first_visible = first_x != 0x03ffu || first_y != 0x03ffu;
    const bool second_visible = second_x != 0x03ffu || second_y != 0x03ffu;
    return {{
        {
            first_visible,
            first_visible
                ? static_cast<std::uint16_t>(
                      bytes[0] |
                      static_cast<std::uint16_t>(high & 0x30u) << 4u)
                : static_cast<std::uint16_t>(0x03ffu),
            first_visible
                ? static_cast<std::uint16_t>(
                      bytes[1] |
                      static_cast<std::uint16_t>(high & 0xc0u) << 2u)
                : static_cast<std::uint16_t>(0x03ffu),
            0,
        },
        {
            second_visible,
            second_visible
                ? static_cast<std::uint16_t>(
                      bytes[3] |
                      static_cast<std::uint16_t>(high & 0x03u) << 8u)
                : static_cast<std::uint16_t>(0x03ffu),
            second_visible
                ? static_cast<std::uint16_t>(
                      bytes[4] |
                      static_cast<std::uint16_t>(high & 0x0cu) << 6u)
                : static_cast<std::uint16_t>(0x03ffu),
            0,
        },
    }};
}

DecodedIrDot decode_extended_ir_dot(
    std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 3u ||
        (bytes[0] == 0xffu && bytes[1] == 0xffu && bytes[2] == 0xffu)) {
        return {};
    }
    return {
        true,
        static_cast<std::uint16_t>(
            bytes[0] |
            static_cast<std::uint16_t>(bytes[2] & 0x30u) << 4u),
        static_cast<std::uint16_t>(
            bytes[1] |
            static_cast<std::uint16_t>(bytes[2] & 0xc0u) << 2u),
        static_cast<std::uint8_t>(bytes[2] & 0x0fu),
    };
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

bool raw_key_trace_mask_is_lazy_and_bit_exact() {
    constexpr std::array<int, 16> virtual_keys{
        100, 101, 102, 103, 104, 105, 106, 107,
        108, 109, 110, 111, 112, 113, 114, 115,
    };

    std::size_t query_count = 0u;
    const std::uint32_t suppressed_mask =
        galaxy::input::collect_native_raw_key_trace_mask(
            false, virtual_keys, [&](int) {
                ++query_count;
                return true;
            });
    bool passed = expect(
        suppressed_mask == 0u && query_count == 0u,
        "suppressed raw-key tracing does not query the key-state provider");

    std::array<int, 16> observed_keys{};
    query_count = 0u;
    const std::uint32_t collected_mask =
        galaxy::input::collect_native_raw_key_trace_mask(
            true, virtual_keys, [&](int virtual_key) {
                observed_keys[query_count++] = virtual_key;
                return virtual_key == 100 || virtual_key == 107 ||
                    virtual_key == 115;
            });
    constexpr std::uint32_t expected_mask =
        (1u << 2u) | (1u << 9u) | (1u << 17u);
    passed &= expect(
        query_count == virtual_keys.size() && observed_keys == virtual_keys,
        "raw-key tracing queries every provider key exactly once in order");
    passed &= expect(
        collected_mask == expected_mask,
        "raw-key tracing preserves the established bits 2 through 17 mapping");
    return passed;
}

bool publishes_native_pointer_as_ir_dots() {
    galaxy::input::WiimoteInputSnapshot inactive{};
    galaxy::input::publish_wiimote_ir_dots(inactive, 0.0f, 0.0f, false);
    bool passed =
        expect(
            std::none_of(inactive.ir.begin(), inactive.ir.end(), [](const auto& dot) {
                return dot.visible;
            }),
            "inactive pointer leaves all IR dots hidden");

    galaxy::input::WiimoteInputSnapshot centered{};
    galaxy::input::publish_wiimote_ir_dots(centered, 0.0f, 0.0f, true);
    passed &= expect(
        centered.ir[0].visible && centered.ir[1].visible,
        "active pointer publishes primary IR dots");
    passed &= expect(
        !centered.ir[2].visible && !centered.ir[3].visible,
        "active pointer leaves ambiguous extra IR dots hidden");
    passed &= expect(centered.ir[0].x == 430u, "center pointer left IR x");
    passed &= expect(centered.ir[1].x == 590u, "center pointer right IR x");
    passed &= expect(centered.ir[0].y == 487u, "center pointer left IR y");
    passed &= expect(centered.ir[1].y == 487u, "center pointer right IR y");
    passed &= expect(centered.ir[0].size == 8u, "center pointer left IR size");
    passed &= expect(centered.ir[1].size == 8u, "center pointer right IR size");
    passed &= expect(centered.ir[2].size == 0u, "center pointer hidden IR size");
    passed &= expect(centered.ir[3].size == 0u, "center pointer hidden IR size");

    galaxy::input::WiimoteInputSnapshot clamped{};
    galaxy::input::publish_wiimote_ir_dots(clamped, -2.0f, 2.0f, true);
    passed &= expect(clamped.ir[0].x == 656u, "clamped pointer left IR x");
    passed &= expect(clamped.ir[1].x == 816u, "clamped pointer right IR x");
    passed &= expect(clamped.ir[0].y == 713u, "clamped pointer left IR y");
    passed &= expect(clamped.ir[1].y == 713u, "clamped pointer right IR y");
    passed &= expect(
        std::none_of(clamped.ir.begin(), clamped.ir.end(), [](const auto& dot) {
            return dot.visible && (dot.x == 1023u || dot.y == 0u);
        }),
        "clamped pointer avoids WPAD invalid IR sentinels");

    galaxy::input::WiimoteInputSnapshot top_edge{};
    galaxy::input::publish_wiimote_ir_dots(top_edge, 0.0f, -2.0f, true);
    passed &= expect(top_edge.ir[0].y == 261u, "top-edge pointer left IR y");
    passed &= expect(top_edge.ir[1].y == 261u, "top-edge pointer right IR y");
    return passed;
}

bool stress_publishes_native_pointer_ir_edges() {
    struct EdgeCase {
        const char* name;
        float pointer_x;
        float pointer_y;
        std::uint16_t left_x;
        std::uint16_t right_x;
        std::uint16_t y;
    };
    constexpr std::array<EdgeCase, 8> cases{{
        {"center", 0.0f, 0.0f, 430u, 590u, 487u},
        {"top_left", -1.0f, -1.0f, 656u, 816u, 261u},
        {"top_right", 1.0f, -1.0f, 204u, 364u, 261u},
        {"bottom_left", -1.0f, 1.0f, 656u, 816u, 713u},
        {"bottom_right", 1.0f, 1.0f, 204u, 364u, 713u},
        {"clamp_low_high", -4.0f, 4.0f, 656u, 816u, 713u},
        {"clamp_high_low", 4.0f, -4.0f, 204u, 364u, 261u},
        {"subpixel_round", 0.001f, -0.001f, 430u, 590u, 487u},
    }};

    bool passed = true;
    for (const EdgeCase& edge : cases) {
        galaxy::input::WiimoteInputSnapshot snapshot{};
        galaxy::input::publish_wiimote_ir_dots(
            snapshot, edge.pointer_x, edge.pointer_y, true);
        passed &= expect(snapshot.ir[0].visible, edge.name);
        passed &= expect(snapshot.ir[1].visible, edge.name);
        passed &= expect(!snapshot.ir[2].visible, edge.name);
        passed &= expect(!snapshot.ir[3].visible, edge.name);
        passed &= expect(snapshot.ir[0].x == edge.left_x, edge.name);
        passed &= expect(snapshot.ir[1].x == edge.right_x, edge.name);
        passed &= expect(snapshot.ir[0].y == edge.y, edge.name);
        passed &= expect(snapshot.ir[1].y == edge.y, edge.name);
        passed &= expect(
            snapshot.ir[1].x - snapshot.ir[0].x == 160u,
            "native IR primary dot separation remains stable");
        passed &= expect(
            snapshot.ir[0].size == 8u && snapshot.ir[1].size == 8u,
            "native IR primary dot sizes stay visible");
        passed &= expect(
            snapshot.ir[2].x == 0x03ffu && snapshot.ir[2].y == 0x03ffu &&
                snapshot.ir[3].x == 0x03ffu &&
                snapshot.ir[3].y == 0x03ffu,
            "native IR hidden dots stay at hardware hidden sentinel");

        std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes>
            report{};
        const auto result =
            galaxy::input::build_wiimote_input_report(0x37, snapshot, report);
        passed &= expect(
            result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
            "stress native IR 0x37 report builds");
        passed &= expect(result.size == 23u, "stress native IR 0x37 report size");
        passed &= expect(
            report[0] == 0xa1u && report[1] == 0x37u,
            "stress native IR 0x37 report header");
        bool first_pair_visible = false;
        for (std::size_t i = 7u; i < 12u; ++i) {
            first_pair_visible = first_pair_visible || report[i] != 0xffu;
        }
        bool second_pair_hidden = true;
        for (std::size_t i = 12u; i < 17u; ++i) {
            second_pair_hidden = second_pair_hidden && report[i] == 0xffu;
        }
        passed &= expect(
            first_pair_visible,
            "stress native IR report publishes first basic pair");
        passed &= expect(
            second_pair_hidden,
            "stress native IR report hides unused second basic pair");
        passed &= expect(
            report[17] == 0x7bu && report[18] == 0x81u,
            "stress native IR report leaves default nunchuk stick neutral");
    }
    return passed;
}

float approximate_kpad_pointing_x(
    const galaxy::input::WiimoteInputSnapshot& snapshot) {
    const float object_center_x =
        (static_cast<float>(snapshot.ir[0].x) +
         static_cast<float>(snapshot.ir[1].x)) * 0.5f;
    return (510.0f - object_center_x) / 226.0f;
}

float approximate_kpad_pointing_y(
    const galaxy::input::WiimoteInputSnapshot& snapshot) {
    const float object_center_y =
        (static_cast<float>(snapshot.ir[0].y) +
         static_cast<float>(snapshot.ir[1].y)) * 0.5f;
    return (object_center_y - 487.0f) / 226.0f;
}

bool publishes_camera_space_ir_for_kpad_pointer_position() {
    galaxy::input::WiimoteInputSnapshot upper_right{};
    galaxy::input::publish_wiimote_ir_dots(upper_right, 0.75f, 0.50f, true);
    bool passed = expect_close(
        approximate_kpad_pointing_x(upper_right),
        0.75f,
        0.01f,
        "positive native pointer x maps directly after WPAD/KPAD transform");
    passed &= expect_close(
        approximate_kpad_pointing_y(upper_right),
        0.50f,
        0.01f,
        "positive native pointer y stays positive after WPAD/KPAD transform");

    galaxy::input::WiimoteInputSnapshot lower_left{};
    galaxy::input::publish_wiimote_ir_dots(lower_left, -0.50f, -0.50f, true);
    passed &= expect_close(
        approximate_kpad_pointing_x(lower_left),
        -0.50f,
        0.01f,
        "negative native pointer x maps directly after WPAD/KPAD transform");
    passed &= expect_close(
        approximate_kpad_pointing_y(lower_left),
        -0.50f,
        0.01f,
        "negative native pointer y stays negative after WPAD/KPAD transform");
    return passed;
}

bool native_pointer_ir_sweeps_are_monotonic() {
    bool passed = true;
    float previous_kpad_x = -2.0f;
    std::uint16_t previous_left_raw_x = 0x03ffu;
    std::uint16_t previous_right_raw_x = 0x03ffu;
    for (int step = -1000; step <= 1000; ++step) {
        const float pointer_x = static_cast<float>(step) / 1000.0f;
        galaxy::input::WiimoteInputSnapshot snapshot{};
        galaxy::input::publish_wiimote_ir_dots(
            snapshot, pointer_x, 0.0f, true);
        const float kpad_x = approximate_kpad_pointing_x(snapshot);
        if (kpad_x + 0.000001f < previous_kpad_x ||
            snapshot.ir[0].x > previous_left_raw_x ||
            snapshot.ir[1].x > previous_right_raw_x ||
            snapshot.ir[1].x - snapshot.ir[0].x != 160u) {
            std::cerr << "FAILED: native horizontal IR sweep reversed or "
                         "changed dot geometry at step="
                      << step << " pointer=" << pointer_x
                      << " kpad=" << kpad_x << " dots=("
                      << snapshot.ir[0].x << ',' << snapshot.ir[1].x
                      << ")\n";
            passed = false;
            break;
        }
        previous_kpad_x = kpad_x;
        previous_left_raw_x = snapshot.ir[0].x;
        previous_right_raw_x = snapshot.ir[1].x;
    }

    float previous_kpad_y = -2.0f;
    std::uint16_t previous_raw_y = 0u;
    for (int step = -1000; step <= 1000; ++step) {
        const float pointer_y = static_cast<float>(step) / 1000.0f;
        galaxy::input::WiimoteInputSnapshot snapshot{};
        galaxy::input::publish_wiimote_ir_dots(
            snapshot, 0.0f, pointer_y, true);
        const float kpad_y = approximate_kpad_pointing_y(snapshot);
        if (kpad_y + 0.000001f < previous_kpad_y ||
            snapshot.ir[0].y < previous_raw_y ||
            snapshot.ir[1].y != snapshot.ir[0].y) {
            std::cerr << "FAILED: native vertical IR sweep reversed or split "
                         "dot rows at step="
                      << step << " pointer=" << pointer_y
                      << " kpad=" << kpad_y
                      << " y=" << snapshot.ir[0].y << '\n';
            passed = false;
            break;
        }
        previous_kpad_y = kpad_y;
        previous_raw_y = snapshot.ir[0].y;
    }
    return passed;
}

bool preserves_ir_coordinates_across_report_modes() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    galaxy::input::publish_wiimote_ir_dots(
        snapshot, 0.3125f, -0.2734375f, true);

    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes>
        report33{};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes>
        report36{};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes>
        report37{};
    const auto built33 = galaxy::input::build_wiimote_input_report(
        0x33u, snapshot, report33);
    const auto built36 = galaxy::input::build_wiimote_input_report(
        0x36u, snapshot, report36);
    const auto built37 = galaxy::input::build_wiimote_input_report(
        0x37u, snapshot, report37);
    bool passed = expect(
        built33.status == galaxy::input::WiimoteReportBuildStatus::Ok &&
            built36.status == galaxy::input::WiimoteReportBuildStatus::Ok &&
            built37.status == galaxy::input::WiimoteReportBuildStatus::Ok,
        "all IR-bearing modes build for cross-mode coordinate proof");

    const DecodedIrDot extended0 = decode_extended_ir_dot(
        std::span<const std::uint8_t>(report33).subspan(7u, 3u));
    const DecodedIrDot extended1 = decode_extended_ir_dot(
        std::span<const std::uint8_t>(report33).subspan(10u, 3u));
    const auto basic36 = decode_basic_ir_pair(
        std::span<const std::uint8_t>(report36).subspan(4u, 5u));
    const auto basic37 = decode_basic_ir_pair(
        std::span<const std::uint8_t>(report37).subspan(7u, 5u));
    const auto matches_snapshot = [&](const DecodedIrDot& decoded,
                                      std::size_t index) {
        return decoded.visible == snapshot.ir[index].visible &&
               decoded.x == snapshot.ir[index].x &&
               decoded.y == snapshot.ir[index].y;
    };
    passed &= expect(
        matches_snapshot(extended0, 0u) && matches_snapshot(extended1, 1u),
        "extended IR mode preserves both native pointer dots");
    passed &= expect(
        matches_snapshot(basic36[0], 0u) &&
            matches_snapshot(basic36[1], 1u),
        "0x36 basic IR mode preserves both native pointer dots");
    passed &= expect(
        matches_snapshot(basic37[0], 0u) &&
            matches_snapshot(basic37[1], 1u),
        "0x37 basic IR mode preserves both native pointer dots");
    passed &= expect(
        extended0.size == 8u && extended1.size == 8u,
        "extended IR mode preserves native pointer dot sizes");
    return passed;
}

bool clearing_ir_never_reuses_stale_dots() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    galaxy::input::publish_wiimote_ir_dots(
        snapshot, 0.625f, -0.375f, true);
    const auto first_visible = snapshot.ir;

    bool passed = true;
    for (std::size_t frame = 0; frame < 120u; ++frame) {
        galaxy::input::publish_wiimote_ir_dots(
            snapshot, -0.5f, 0.5f, false);
        std::array<
            std::uint8_t,
            galaxy::input::kMaxWiimoteInputReportBytes>
            report{};
        const auto built = galaxy::input::build_wiimote_input_report(
            0x37u, snapshot, report);
        const bool all_hidden = std::all_of(
            snapshot.ir.begin(), snapshot.ir.end(), [](const auto& dot) {
                return !dot.visible && dot.x == 0x03ffu &&
                       dot.y == 0x03ffu && dot.size == 0u;
            });
        const bool hidden_on_wire = std::all_of(
            report.begin() + 7,
            report.begin() + 17,
            [](std::uint8_t byte) { return byte == 0xffu; });
        if (built.status != galaxy::input::WiimoteReportBuildStatus::Ok ||
            !all_hidden || !hidden_on_wire) {
            std::cerr << "FAILED: inactive native IR retained stale dots at "
                         "slot="
                      << frame << '\n';
            passed = false;
            break;
        }
    }

    galaxy::input::publish_wiimote_ir_dots(
        snapshot, -0.5f, 0.5f, true);
    passed &= expect(
        snapshot.ir[0].visible && snapshot.ir[1].visible &&
            (snapshot.ir[0].x != first_visible[0].x ||
             snapshot.ir[0].y != first_visible[0].y) &&
            (snapshot.ir[1].x != first_visible[1].x ||
             snapshot.ir[1].y != first_visible[1].y),
        "reactivated native IR uses only the new pointer sample");
    return passed;
}

bool advances_shake_as_wiimote_accelerometer_report_bytes() {
    bool previous_pressed = false;
    std::uint8_t remaining_frames = 0;
    galaxy::input::WiimoteInputSnapshot idle{};
    bool passed = expect(
        !galaxy::input::advance_wiimote_shake_accel(
            idle, false, previous_pressed, remaining_frames),
        "idle shake helper does not publish acceleration");
    passed &= expect(
        idle.accel.x == galaxy::input::kWiimoteRestAccelX,
        "idle shake leaves accel x at hardware rest");
    passed &= expect(
        idle.accel.y == galaxy::input::kWiimoteRestAccelY,
        "idle shake leaves accel y at hardware rest");
    passed &= expect(
        idle.accel.z == galaxy::input::kWiimoteRestAccelZ,
        "idle shake leaves accel z at hardware rest");

    galaxy::input::WiimoteInputSnapshot first{};
    passed &= expect(
        galaxy::input::advance_wiimote_shake_accel(
            first, true, previous_pressed, remaining_frames),
        "shake edge publishes first acceleration frame");
    passed &= expect(previous_pressed, "shake edge latches previous pressed");
    passed &= expect(remaining_frames == 11u, "shake edge queues remaining frames");
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    auto result = galaxy::input::build_wiimote_input_report(0x31, first, report);
    constexpr std::array<std::uint8_t, 7> first_expected{
        0xa1, 0x31, 0x00, 0x00, 0x44, 0x80, 0xb4};
    passed &= expect(
        result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
        "first shake acceleration report builds");
    passed &= expect_bytes(
        std::span<const std::uint8_t>(report).first(result.size),
        first_expected,
        "first shake acceleration report bytes");

    galaxy::input::WiimoteInputSnapshot second{};
    passed &= expect(
        galaxy::input::advance_wiimote_shake_accel(
            second, true, previous_pressed, remaining_frames),
        "held shake publishes second acceleration frame");
    passed &= expect(remaining_frames == 10u, "held shake advances countdown");
    result = galaxy::input::build_wiimote_input_report(0x31, second, report);
    constexpr std::array<std::uint8_t, 7> second_expected{
        0xa1, 0x31, 0x00, 0x00, 0xbc, 0x80, 0x4c};
    passed &= expect(
        result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
        "second shake acceleration report builds");
    passed &= expect_bytes(
        std::span<const std::uint8_t>(report).first(result.size),
        second_expected,
        "second shake acceleration report bytes");

    for (int i = 0; i < 10; ++i) {
        galaxy::input::WiimoteInputSnapshot frame{};
        passed &= expect(
            galaxy::input::advance_wiimote_shake_accel(
                frame, true, previous_pressed, remaining_frames),
            "shake countdown publishes exactly twelve frames");
    }
    galaxy::input::WiimoteInputSnapshot expired{};
    passed &= expect(
        !galaxy::input::advance_wiimote_shake_accel(
            expired, true, previous_pressed, remaining_frames),
        "held shake does not retrigger without a new edge");
    return passed;
}

bool builds_core_button_report() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.buttons.left = true;
    snapshot.buttons.up = true;
    snapshot.buttons.plus = true;
    snapshot.buttons.b = true;
    snapshot.buttons.a = true;
    snapshot.buttons.home = true;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x30, snapshot, report);
    constexpr std::array<std::uint8_t, 4> expected{
        0xa1, 0x30, 0x19, 0x8c};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x30 core report builds") &&
           expect(result.size == expected.size(), "0x30 core report size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x30 core report bytes");
}

bool packs_hardware_accelerometer_low_bits_into_button_bytes() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.accel.x = 0x0201;
    snapshot.accel.y = 0x0202;
    snapshot.accel.z = 0x0203;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x31, snapshot, report);
    constexpr std::array<std::uint8_t, 7> expected{
        0xa1, 0x31, 0x20, 0x60, 0x80, 0x80, 0x80};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x31 accel report builds") &&
           expect(result.size == expected.size(), "0x31 accel report size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x31 virtual accel report matches hardware low-bit packing");
}

bool accel_low_bits_do_not_change_core_button_semantics() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.buttons.left = true;
    snapshot.buttons.right = true;
    snapshot.buttons.down = true;
    snapshot.buttons.up = true;
    snapshot.buttons.plus = true;
    snapshot.buttons.two = true;
    snapshot.buttons.one = true;
    snapshot.buttons.b = true;
    snapshot.buttons.a = true;
    snapshot.buttons.minus = true;
    snapshot.buttons.home = true;
    snapshot.accel.x = 0x0203;
    snapshot.accel.y = 0x0202;
    snapshot.accel.z = 0x0202;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x31, snapshot, report);
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x31 combined button/accel report builds") &&
           expect(
               report[2] == 0x7fu && report[3] == 0xffu,
               "0x31 combined report preserves hardware button and accel bits") &&
           expect(
               (report[2] & 0x1fu) == 0x1fu &&
                   (report[3] & 0x9fu) == 0x9fu,
               "RMGE01 button masks retain every pressed core button");
}

bool packs_nunchuk_extension_bytes() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.nunchuk.stick_x = 200;
    snapshot.nunchuk.stick_y = 42;
    snapshot.nunchuk.accel.x = 0x0155;
    snapshot.nunchuk.accel.y = 0x02aa;
    snapshot.nunchuk.accel.z = 0x03ff;
    snapshot.nunchuk.c = true;
    snapshot.nunchuk.z = false;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x35, snapshot, report);
    constexpr std::array<std::uint8_t, 23> expected{
        0xa1, 0x35, 0x00, 0x00, 0x7f, 0x81, 0x99,
        0xc8, 0x2a, 0x55, 0xaa, 0xff, 0xe5,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x35 nunchuk report builds") &&
           expect(result.size == expected.size(), "0x35 nunchuk report size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x35 nunchuk report bytes");
}

bool packs_core_and_short_extension_report() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.nunchuk.stick_x = 200;
    snapshot.nunchuk.stick_y = 42;
    snapshot.nunchuk.accel.x = 0x0155;
    snapshot.nunchuk.accel.y = 0x02aa;
    snapshot.nunchuk.accel.z = 0x03ff;
    snapshot.nunchuk.c = true;
    snapshot.nunchuk.z = false;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x32, snapshot, report);
    constexpr std::array<std::uint8_t, 12> expected{
        0xa1, 0x32, 0x00, 0x00,
        0xc8, 0x2a, 0x55, 0xaa, 0xff, 0xe5, 0x00, 0x00};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x32 short extension report builds") &&
           expect(result.size == expected.size(), "0x32 short extension size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x32 short extension bytes");
}

bool packs_accel_and_extended_ir_report() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.accel.x = 0x0201;
    snapshot.accel.y = 0x0202;
    snapshot.accel.z = 0x0203;
    snapshot.ir[0].visible = true;
    snapshot.ir[0].x = 0x0155;
    snapshot.ir[0].y = 0x02aa;
    snapshot.ir[0].size = 0x0d;
    snapshot.ir[1].visible = true;
    snapshot.ir[1].x = 0x03fe;
    snapshot.ir[1].y = 0x0001;
    snapshot.ir[1].size = 0x05;
    snapshot.ir[3].visible = true;
    snapshot.ir[3].x = 0x0123;
    snapshot.ir[3].y = 0x0321;
    snapshot.ir[3].size = 0x0f;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x33, snapshot, report);
    constexpr std::array<std::uint8_t, 19> expected{
        0xa1, 0x33, 0x20, 0x60, 0x80, 0x80, 0x80,
        0x55, 0xaa, 0x9d, 0xfe, 0x01, 0x35,
        0xff, 0xff, 0xff, 0x23, 0x21, 0xdf};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x33 extended IR report builds") &&
           expect(result.size == expected.size(), "0x33 extended IR size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x33 extended IR bytes");
}

bool packs_wide_extension_report() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.nunchuk.stick_x = 200;
    snapshot.nunchuk.stick_y = 42;
    snapshot.nunchuk.accel.x = 0x0155;
    snapshot.nunchuk.accel.y = 0x02aa;
    snapshot.nunchuk.accel.z = 0x03ff;
    snapshot.nunchuk.c = true;
    snapshot.nunchuk.z = false;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x34, snapshot, report);
    constexpr std::array<std::uint8_t, 23> expected{
        0xa1, 0x34, 0x00, 0x00,
        0xc8, 0x2a, 0x55, 0xaa, 0xff, 0xe5,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x34 wide extension report builds") &&
           expect(result.size == expected.size(), "0x34 wide extension size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x34 wide extension bytes");
}

bool packs_basic_ir_pairs() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.ir[0].visible = true;
    snapshot.ir[0].x = 0x0155;
    snapshot.ir[0].y = 0x02aa;
    snapshot.ir[1].visible = true;
    snapshot.ir[1].x = 0x03fe;
    snapshot.ir[1].y = 0x0001;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x37, snapshot, report);
    constexpr std::array<std::uint8_t, 23> expected{
        0xa1, 0x37, 0x00, 0x00, 0x7f, 0x81, 0x99,
        0x55, 0xaa, 0x93, 0xfe, 0x01,
        0xff, 0xff, 0xff, 0xff, 0xff,
        0x7b, 0x81, 0x80, 0x80, 0x80, 0x03};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x37 IR report builds") &&
           expect(result.size == expected.size(), "0x37 IR report size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x37 IR report bytes");
}

bool packs_basic_ir_mixed_visibility_sentinels() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.ir[0] = {true, 0x0155u, 0x02aau, 8u};
    snapshot.ir[3] = {true, 0x03feu, 0x0001u, 8u};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x37, snapshot, report);
    constexpr std::array<std::uint8_t, 10> expected_ir{
        0x55, 0xaa, 0x9f, 0xff, 0xff,
        0xff, 0xff, 0xf3, 0xfe, 0x01};
    const auto first_pair = decode_basic_ir_pair(
        std::span<const std::uint8_t>(report).subspan(7u, 5u));
    const auto second_pair = decode_basic_ir_pair(
        std::span<const std::uint8_t>(report).subspan(12u, 5u));
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x37 mixed-visibility IR report builds") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).subspan(7u, 10u),
               expected_ir,
               "0x37 mixed-visibility IR report uses exact hardware sentinels") &&
           expect(
               first_pair[0].visible && !first_pair[1].visible &&
                   !second_pair[0].visible && second_pair[1].visible &&
                   first_pair[1].x == 0x03ffu &&
                   first_pair[1].y == 0x03ffu &&
                   second_pair[0].x == 0x03ffu &&
                   second_pair[0].y == 0x03ffu,
               "both basic-IR mixed visibility orders decode losslessly");
}

bool packs_ir_and_extension_report() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.ir[0].visible = true;
    snapshot.ir[0].x = 0x0155;
    snapshot.ir[0].y = 0x02aa;
    snapshot.ir[1].visible = true;
    snapshot.ir[1].x = 0x03fe;
    snapshot.ir[1].y = 0x0001;
    snapshot.nunchuk.stick_x = 200;
    snapshot.nunchuk.stick_y = 42;
    snapshot.nunchuk.accel.x = 0x0155;
    snapshot.nunchuk.accel.y = 0x02aa;
    snapshot.nunchuk.accel.z = 0x03ff;
    snapshot.nunchuk.c = true;
    snapshot.nunchuk.z = false;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x36, snapshot, report);
    constexpr std::array<std::uint8_t, 23> expected{
        0xa1, 0x36, 0x00, 0x00,
        0x55, 0xaa, 0x93, 0xfe, 0x01,
        0xff, 0xff, 0xff, 0xff, 0xff,
        0xc8, 0x2a, 0x55, 0xaa, 0xff, 0xe5,
        0x00, 0x00, 0x00};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x36 IR extension report builds") &&
           expect(result.size == expected.size(), "0x36 IR extension size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x36 IR extension bytes");
}

bool all_supported_input_report_modes_build() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    struct ModeCase {
        std::uint8_t mode;
        std::size_t size;
    };
    constexpr std::array<ModeCase, 8> modes{{
        {0x30, 4},
        {0x31, 7},
        {0x32, 12},
        {0x33, 19},
        {0x34, 23},
        {0x35, 23},
        {0x36, 23},
        {0x37, 23},
    }};
    bool passed = true;
    for (const ModeCase& mode : modes) {
        report.fill(0);
        const auto result =
            galaxy::input::build_wiimote_input_report(mode.mode, snapshot, report);
        passed &= expect(
            result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
            "supported report mode builds");
        passed &= expect(result.size == mode.size, "supported report mode size");
        passed &= expect(report[0] == 0xa1, "supported report mode HID input tag");
        passed &= expect(report[1] == mode.mode, "supported report mode id");
        passed &= expect(
            galaxy::input::wiimote_input_report_size(mode.mode) == mode.size,
            "supported report mode advertised size matches builder");
    }
    return passed;
}

bool rejects_unsupported_modes() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x3e, snapshot, report);
    return expect(
        result.status ==
            galaxy::input::WiimoteReportBuildStatus::UnsupportedReportMode,
        "unsupported report mode is rejected");
}

bool reports_small_output_buffer() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    std::array<std::uint8_t, 3> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x30, snapshot, report);
    return expect(
               result.status ==
                   galaxy::input::WiimoteReportBuildStatus::OutputTooSmall,
               "small output buffer is rejected") &&
           expect(result.size == 4, "small output reports required size");
}

bool basic_ir_decoder_preserves_low_ff_visible_coordinates() {
    // Independent wire values: first=(255,511), second=(767,255).
    const std::array<std::uint8_t, 5> visible{0xffu, 0xffu, 0x42u, 0xffu, 0xffu};
    const auto decoded = decode_basic_ir_pair(visible);
    bool passed = expect(decoded[0].visible && decoded[0].x == 255u && decoded[0].y == 511u &&
                         decoded[1].visible && decoded[1].x == 767u && decoded[1].y == 255u,
                         "full ten-bit coordinates distinguish visible low-FF dots");
    const std::array<std::uint8_t, 5> mixed{0xffu, 0xffu, 0x1fu, 0xffu, 0xffu};
    const auto mixed_decoded = decode_basic_ir_pair(mixed);
    passed &= expect(mixed_decoded[0].visible && mixed_decoded[0].x == 511u &&
                     mixed_decoded[0].y == 255u && !mixed_decoded[1].visible,
                     "shared high byte retains independent first/second sentinel semantics");
    const std::array<std::uint8_t, 5> hidden{0xffu, 0xffu, 0xffu, 0xffu, 0xffu};
    const auto hidden_decoded = decode_basic_ir_pair(hidden);
    passed &= expect(!hidden_decoded[0].visible && !hidden_decoded[1].visible,
                     "only all-ones full coordinates are hidden");
    return passed;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= settings_pad_toggles_navigates_and_hides_buttons();
    ok &= basic_ir_decoder_preserves_low_ff_visible_coordinates();
    ok &= native_input_script_unsigned_fields_fail_closed();
    ok &= normalizes_xinput_axes();
    ok &= raw_key_trace_mask_is_lazy_and_bit_exact();
    ok &= publishes_native_pointer_as_ir_dots();
    ok &= stress_publishes_native_pointer_ir_edges();
    ok &= publishes_camera_space_ir_for_kpad_pointer_position();
    ok &= native_pointer_ir_sweeps_are_monotonic();
    ok &= preserves_ir_coordinates_across_report_modes();
    ok &= clearing_ir_never_reuses_stale_dots();
    ok &= advances_shake_as_wiimote_accelerometer_report_bytes();
    ok &= builds_core_button_report();
    ok &= packs_hardware_accelerometer_low_bits_into_button_bytes();
    ok &= accel_low_bits_do_not_change_core_button_semantics();
    ok &= packs_core_and_short_extension_report();
    ok &= packs_accel_and_extended_ir_report();
    ok &= packs_wide_extension_report();
    ok &= packs_nunchuk_extension_bytes();
    ok &= packs_basic_ir_pairs();
    ok &= packs_basic_ir_mixed_visibility_sentinels();
    ok &= packs_ir_and_extension_report();
    ok &= all_supported_input_report_modes_build();
    ok &= rejects_unsupported_modes();
    ok &= reports_small_output_buffer();
    return ok ? 0 : 1;
}
