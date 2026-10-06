#include "galaxy/native_input.h"
#include "galaxy/gx/pointer_response.h"
#include "galaxy/synthetic_kpad_motion.h"
#include "galaxy/synthetic_kpad_buttons.h"
#include "galaxy/native_input_anomaly.h"
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

bool synthetic_kpad_preserves_a_real_host_shake_pulse() {
    galaxy::input::WiimoteInputSnapshot snapshot;
    galaxy::input::SyntheticKpadMotionState motion_state;
    auto motion = galaxy::input::sample_synthetic_kpad_motion(snapshot.accel, motion_state);
    bool ok = expect_close(motion.magnitude, 1.0f, 0.001f, "rest is one gravity");
    ok &= expect_close(motion.speed, 0.0f, 0.001f, "rest has no motion speed");
    ok &= expect_close(motion.acceleration[1], -1.0f, 0.001f, "KPAD rest axis matches SDK");
    bool previously_pressed = false;
    std::uint8_t remaining = 0;
    unsigned active = 0;
    for (unsigned i = 0; i < 12; ++i) {
        snapshot = {};
        ok &= expect(galaxy::input::advance_wiimote_shake_accel(
            snapshot, i == 0, previously_pressed, remaining), "host shake lasts twelve samples");
        motion = galaxy::input::sample_synthetic_kpad_motion(snapshot.accel, motion_state);
        active += motion.magnitude > 1.5f;
        ok &= expect(motion.speed > 1.0f, "shake reaches KPAD with nonzero sample difference");
    }
    ok &= expect(active == 12, "KPAD must not replace the host shake pulse with constant gravity");
    snapshot = {};
    motion = galaxy::input::sample_synthetic_kpad_motion(snapshot.accel, motion_state);
    ok &= expect_close(motion.magnitude, 1.0f, 0.001f, "shake returns to rest");
    motion = galaxy::input::sample_synthetic_kpad_motion(snapshot.accel, motion_state);
    ok &= expect_close(motion.speed, 0.0f, 0.001f, "repeated rest has no residual shake");
    return ok;
}

bool native_input_anomalies_retain_missing_slots_without_changing_cadence() {
    using namespace galaxy::input;
    struct Evidence {
        std::uint64_t sequence{};
        std::uint64_t skipped{};
        NativeInputDeferralEpisode deferral{};
    };
    NativeInputAnomalyLedger<Evidence, 2u> retained;
    NativeHidReportCadence cadence;
    NativeInputDeferralEpisode deferral;
    std::uint64_t ticks = 100u;
    cadence.arm(ticks);
    cadence.note_delivery(*cadence.collect_latest_due(ticks), ticks);
    for (std::uint64_t i = 0u; i < 40u; ++i) {
        // Model a protected transaction and a missed real device slot, then
        // drain the latest sample as the runtime does. Missing start
        // QPC must remain unknown even when the second observation is known.
        deferral.observe({ticks, 0u, 100u, 0x80001000u});
        deferral.observe({ticks + 1u, ticks + 2u, 100u, 0x80002000u});
        ticks += 2u * kNativeHidReportPeriodTicks;
        const auto sample = cadence.collect_latest_due(ticks);
        const auto episode = deferral.take();
        retained.append({sample->sequence, cadence.stats().skipped_due_slots, episode});
        cadence.note_delivery(*sample, ticks);
    }
    for (std::uint64_t i = 0u; i < 1000u; ++i) {
        ticks += kNativeHidReportPeriodTicks;
        cadence.note_delivery(*cadence.collect_latest_due(ticks), ticks);
    }
    const auto records = retained.records();
    bool passed = expect(
        records.size() == 2u && retained.overflow() == 38u &&
            records[0].sequence == 3u && records[0].skipped == 1u &&
            records[1].sequence == 5u && records[1].skipped == 2u,
        "initial missing-slot evidence survives overflow and later normal delivery");
    passed &= expect(
        records[0].deferral.attempts == 2u &&
            records[0].deferral.first.qpc == 0u &&
            records[0].deferral.last.qpc == 102u &&
            deferral.attempts == 0u && deferral.first.pc == 0u,
        "deferral consumption preserves unknown fields and clears prior episode");
    passed &= expect(
        cadence.stats().skipped_due_slots == 40u &&
            cadence.stats().delivery_gaps == 40u &&
            cadence.stats().produced_samples == 1041u &&
            cadence.stats().delivered_samples == 1041u &&
            !cadence.stats().exact_cadence_valid,
        "diagnostic retention never repairs or suppresses real skipped reports");
    deferral.observe({7u, 8u, 9u, 10u});
    const auto next = deferral.take();
    passed &= expect(
        next.attempts == 1u && next.first.ticks == 7u && next.last.qpc == 8u,
        "a new protected-owner episode cannot inherit an earlier sample");
    return passed;
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

template <typename Predicate>
bool wait_until(
    Predicate predicate,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2'000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

std::vector<std::byte> fake_input_report(std::uint8_t marker) {
    return {
        std::byte{galaxy::input::kWiimoteHidInput},
        std::byte{0x31u},
        std::byte{marker}};
}

std::vector<std::byte> fake_output_report(std::uint8_t marker) {
    return {
        std::byte{galaxy::input::kWiimoteHidOutput},
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

class FakeNativeHidBackend final
    : public galaxy::input::NativeHidIoBackend {
public:
    explicit FakeNativeHidBackend(
        std::shared_ptr<FakeNativeHidState> state)
        : state_(std::move(state)) {}

    void open() override {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->open_entered = true;
        state_->wake.notify_all();
        state_->wake.wait(lock, [&] {
            return !state_->block_open || state_->stop_requested;
        });
        if (state_->stop_requested) {
            return;
        }
        if (state_->fail_open) {
            throw std::runtime_error("deterministic fake HID open failure");
        }
        state_->open_succeeded = true;
        state_->wake.notify_all();
    }

    bool poll_input(std::vector<std::byte>& report) override {
        std::unique_lock<std::mutex> lock(state_->mutex);
        // A bounded fixture pause models descheduling inside an already-entered
        // backend call, before the producer captures completion time.
        if (state_->pause_second_poll && state_->input_reports_polled == 1u) {
            state_->second_poll_entered = true;
            state_->wake.notify_all();
            state_->wake.wait_for(lock, std::chrono::seconds(2), [&] {
                return state_->release_second_poll || state_->stop_requested;
            });
            state_->pause_second_poll = false;
        }
        if (state_->stop_requested || state_->input_reports.empty()) {
            return false;
        }
        report = std::move(state_->input_reports.front());
        state_->input_reports.pop_front();
        ++state_->input_reports_polled;
        state_->wake.notify_all();
        return true;
    }

    bool write_output(std::span<const std::byte> report) override {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->write_entered = true;
        state_->wake.notify_all();
        state_->wake.wait(lock, [&] {
            return !state_->block_writes || state_->stop_requested;
        });
        if (state_->stop_requested) {
            return false;
        }
        if (state_->fail_write) {
            throw std::runtime_error("deterministic fake HID write failure");
        }
        state_->output_reports.emplace_back(report.begin(), report.end());
        state_->wake.notify_all();
        return true;
    }

    void request_stop() noexcept override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->stop_requested = true;
        state_->wake.notify_all();
    }

    void close() noexcept override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->closed = true;
        state_->wake.notify_all();
    }

private:
    std::shared_ptr<FakeNativeHidState> state_;
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

bool functional_key_levels_are_coherent_and_lazy() {
    constexpr std::array<int, 6> virtual_keys{11, 22, 33, 44, 55, 66};
    std::size_t query_count = 0u;
    const auto suppressed = galaxy::input::collect_native_key_levels(
        false, virtual_keys, [&](int) {
            ++query_count;
            return true;
        });
    bool passed = expect(
        query_count == 0u &&
            std::none_of(suppressed.begin(), suppressed.end(),
                         [](bool level) { return level; }),
        "disabled functional-key acquisition performs no provider calls");

    std::array<int, virtual_keys.size()> observed_keys{};
    query_count = 0u;
    const auto levels = galaxy::input::collect_native_key_levels(
        true, virtual_keys, [&](int virtual_key) {
            observed_keys[query_count++] = virtual_key;
            return virtual_key == 22 || virtual_key == 55;
        });
    passed &= expect(
        query_count == virtual_keys.size() && observed_keys == virtual_keys,
        "functional-key acquisition queries every unique key exactly once in order");
    passed &= expect(
        !galaxy::input::native_key_level(virtual_keys, levels, 11) &&
            galaxy::input::native_key_level(virtual_keys, levels, 22) &&
            galaxy::input::native_key_level(virtual_keys, levels, 55) &&
            !galaxy::input::native_key_level(virtual_keys, levels, 66) &&
            !galaxy::input::native_key_level(virtual_keys, levels, 77),
        "functional-key lookup preserves sampled levels and rejects unknown keys");
    return passed;
}

bool maps_native_stick_axis_to_nunchuk_byte() {
    return expect(
               galaxy::input::native_stick_axis_byte(-1.0f, 35, 228) == 35,
               "native stick negative full-scale maps to Nunchuk min") &&
           expect(
               galaxy::input::native_stick_axis_byte(0.0f, 35, 228) == 128,
               "native stick neutral maps to Nunchuk center") &&
           expect(
               galaxy::input::native_stick_axis_byte(
                   0.0f, 0x15, 0x7b, 0xe0) == 0x7b,
               "captured Nunchuk neutral maps to calibrated X center") &&
           expect(
               galaxy::input::native_stick_axis_byte(
                   0.0f, 0x1d, 0x81, 0xe6) == 0x81,
               "captured Nunchuk neutral maps to calibrated Y center") &&
           expect(
               galaxy::input::native_stick_axis_byte(1.0f, 35, 228) == 228,
               "native stick positive full-scale maps to Nunchuk max") &&
           expect(
               galaxy::input::native_stick_axis_byte(-2.0f, 35, 228) == 35,
               "native stick clamps below negative full-scale") &&
           expect(
               galaxy::input::native_stick_axis_byte(2.0f, 35, 228) == 228,
               "native stick clamps above positive full-scale") &&
           expect(
               galaxy::input::native_stick_axis_byte(-0.5f, 35, 228) == 82,
               "native stick rounds negative half-scale") &&
           expect(
               galaxy::input::native_stick_axis_byte(0.5f, 35, 228) == 178,
               "native stick rounds positive half-scale");
}

bool isolates_keyboard_mouse_and_controller_pointer_axes() {
    const galaxy::input::NativePointerAxes keyboard_mode_controller =
        galaxy::input::merge_native_pointer_axes(
            0.0f,
            0.0f,
            0.5f,
            -0.25f,
            true,
            false);
    bool passed = expect_close(
        keyboard_mode_controller.x,
        0.0f,
        0.0001f,
        "keyboard_mouse mode ignores controller right-stick x");
    passed &= expect_close(
        keyboard_mode_controller.y,
        0.0f,
        0.0001f,
        "keyboard_mouse mode ignores controller right-stick y");
    passed &= expect(
        !keyboard_mode_controller.moving,
        "keyboard_mouse mode does not attribute movement to a controller");
    passed &= expect(
        !keyboard_mode_controller.hold_visible,
        "keyboard_mouse mode does not keep IR visible for controller presence");
    passed &= expect(
        keyboard_mode_controller.source ==
            galaxy::input::NativePointerAxisSource::None,
        "keyboard_mouse mode has no controller pointer source");

    const galaxy::input::NativePointerAxes isolated_keyboard =
        galaxy::input::merge_native_pointer_axes(
            0.75f,
            -0.75f,
            0.75f,
            -0.75f,
            true,
            false);
    passed &= expect_close(
        isolated_keyboard.x,
        0.75f,
        0.0001f,
        "keyboard_mouse pointer x is independent of controller x");
    passed &= expect_close(
        isolated_keyboard.y,
        -0.75f,
        0.0001f,
        "keyboard_mouse pointer y is independent of controller y");
    passed &= expect(
        isolated_keyboard.moving && !isolated_keyboard.hold_visible &&
            isolated_keyboard.source ==
                galaxy::input::NativePointerAxisSource::KeyboardMouse,
        "keyboard_mouse movement is attributed only to keyboard/mouse axes");

    const galaxy::input::NativePointerAxes controller_mode =
        galaxy::input::merge_native_pointer_axes(
            1.0f,
            1.0f,
            -0.5f,
            0.25f,
            true,
            true);
    passed &= expect_close(
        controller_mode.x,
        -0.5f,
        0.0001f,
        "controller mode ignores keyboard pointer x");
    passed &= expect_close(
        controller_mode.y,
        0.25f,
        0.0001f,
        "controller mode ignores keyboard pointer y");

    const galaxy::input::NativePointerAxes controller_mode_disconnected =
        galaxy::input::merge_native_pointer_axes(
            1.0f,
            -1.0f,
            1.0f,
            -1.0f,
            false,
            true);
    passed &= expect_close(
        controller_mode_disconnected.x,
        0.0f,
        0.0001f,
        "controller mode disconnected pointer x is neutral");
    passed &= expect_close(
        controller_mode_disconnected.y,
        0.0f,
        0.0001f,
        "controller mode disconnected pointer y is neutral");
    passed &= expect(
        !controller_mode_disconnected.moving,
        "controller mode disconnected pointer is not moving");
    passed &= expect(
        !controller_mode_disconnected.hold_visible,
        "controller mode disconnected pointer does not hold IR visible");
    passed &= expect(
        controller_mode_disconnected.source ==
            galaxy::input::NativePointerAxisSource::None,
        "controller mode disconnected pointer has no source");

    const galaxy::input::NativePointerAxes controller_mode_neutral =
        galaxy::input::merge_native_pointer_axes(
            1.0f,
            -1.0f,
            0.0f,
            0.0f,
            true,
            true);
    passed &= expect_close(
        controller_mode_neutral.x,
        0.0f,
        0.0001f,
        "controller mode connected neutral pointer x is neutral");
    passed &= expect_close(
        controller_mode_neutral.y,
        0.0f,
        0.0001f,
        "controller mode connected neutral pointer y is neutral");
    passed &= expect(
        !controller_mode_neutral.moving,
        "controller mode connected neutral pointer is not moving");
    passed &= expect(
        controller_mode_neutral.hold_visible,
        "controller mode connected neutral pointer holds IR visible");
    passed &= expect(
        controller_mode_neutral.source ==
            galaxy::input::NativePointerAxisSource::None,
        "controller mode connected neutral pointer has no movement source");

    const galaxy::input::NativePointerAxes disconnected =
        galaxy::input::merge_native_pointer_axes(
            0.0f,
            0.0f,
            1.0f,
            1.0f,
            false,
            false);
    passed &= expect(
        !disconnected.moving && !disconnected.hold_visible &&
            disconnected.source ==
                galaxy::input::NativePointerAxisSource::None,
        "keyboard_mouse mode does not keep stale IR visible without keyboard/mouse input");
    return passed;
}

bool fits_required_content_viewports() {
    using galaxy::ContentViewport;

    const auto expect_viewport = [](
                                     const ContentViewport& actual,
                                     int left,
                                     int top,
                                     int width,
                                     int height,
                                     const char* message) {
        return expect(
            actual.left == left && actual.top == top &&
                actual.width == width && actual.height == height,
            message);
    };

    bool passed = expect_viewport(
        galaxy::fit_content_viewport(1920, 1080),
        0, 0, 1920, 1080,
        "1080p 16:9 content fills the surface");
    passed &= expect_viewport(
        galaxy::fit_content_viewport(1920, 1200),
        0, 60, 1920, 1080,
        "16:10 surface is letterboxed to 16:9");
    passed &= expect_viewport(
        galaxy::fit_content_viewport(2560, 1080),
        320, 0, 1920, 1080,
        "21:9 surface is pillarboxed to 16:9");
    passed &= expect_viewport(
        galaxy::fit_content_viewport(1440, 1080),
        0, 135, 1440, 810,
        "4:3 surface is letterboxed to 16:9");
    passed &= expect_viewport(
        galaxy::fit_content_viewport(2560, 1440),
        0, 0, 2560, 1440,
        "1440p 16:9 content fills the surface");
    passed &= expect_viewport(
        galaxy::fit_content_viewport(3840, 2160),
        0, 0, 3840, 2160,
        "4K 16:9 content fills the surface");
    passed &= expect_viewport(
        galaxy::fit_content_viewport(641, 481),
        0, 60, 641, 361,
        "odd-sized 4:3 surface has deterministic integer letterboxing");
    passed &= expect_viewport(
        galaxy::fit_content_viewport(1920, 1080, 4.0 / 3.0),
        240, 0, 1440, 1080,
        "explicit 4:3 content aspect is pillarboxed deterministically");

    const ContentViewport native_wrapper =
        galaxy::input::fit_native_pointer_viewport(2560, 1080);
    const ContentViewport authoritative =
        galaxy::fit_content_viewport(2560, 1080);
    passed &= expect_viewport(
        native_wrapper,
        320, 0, 1920, 1080,
        "pointer compatibility wrapper uses the authoritative content fit");
    passed &= expect(
        native_wrapper.left == authoritative.left &&
            native_wrapper.top == authoritative.top &&
            native_wrapper.width == authoritative.width &&
            native_wrapper.height == authoritative.height,
        "pointer and renderer callers receive byte-identical fit fields");
    passed &= expect_viewport(
        galaxy::fit_content_viewport(1, 1080),
        0, 0, 0, 0,
        "degenerate surface fails closed");
    passed &= expect_viewport(
        galaxy::fit_content_viewport(
            1920, 1080, std::numeric_limits<double>::quiet_NaN()),
        0, 0, 0, 0,
        "non-finite content aspect fails closed");
    return passed;
}

bool maps_opt_in_ultrawide_logical_and_pointer_space() {
    const auto aspect = galaxy::parse_experimental_ultrawide_aspect("21:9");
    bool passed = expect(
        aspect.has_value() && std::fabs(*aspect - 21.0 / 9.0) < 1e-12,
        "21:9 aspect parses exactly");
    const auto policy = aspect
        ? galaxy::make_experimental_ultrawide_aspect(*aspect)
        : std::nullopt;
    passed &= expect(
        policy.has_value() && policy->guest_screen_width == 1092u,
        "21:9 scales RMGE01's 832-unit widescreen logical width");
    if (policy) {
        const double expansion = policy->content_aspect /
            galaxy::kWiiContentAspect;
        const double canvas = galaxy::experimental_nw4r_canvas_width(*policy);
        passed &= expect(
            std::fabs(canvas - 798.0) < 0.0001 &&
                std::fabs(expansion * 608.0 / canvas - 1.0) < 0.0001,
            "21:9 XFB expansion cancels the fixed NW4R canvas widening");
        const auto viewport = galaxy::experimental_ultrawide_viewport(
            2560, 1080, *policy);
        passed &= expect(
            viewport.left == 20 && viewport.top == 0 &&
                viewport.width == 2520 && viewport.height == 1080,
            "present and pointer share a 21:9 content rectangle");
        const auto pointer = galaxy::input::project_native_mouse_pointer({
            false, true, true, 20, 0, 2560, 1080, viewport, 1.0f, 1.0f});
        passed &= expect(
            pointer.visible && std::fabs(pointer.x + 1.0f) < 0.0001f &&
                std::fabs(pointer.y + 1.0f) < 0.0001f,
            "unfocused IR uses the same ultrawide left edge");
    }
    const auto sixteen_ten =
        galaxy::parse_experimental_ultrawide_aspect("16:10");
    const auto sixteen_ten_policy = sixteen_ten
        ? galaxy::make_experimental_ultrawide_aspect(*sixteen_ten)
        : std::nullopt;
    passed &= expect(
        sixteen_ten_policy.has_value() &&
            sixteen_ten_policy->guest_screen_width == 748u &&
            std::fabs(galaxy::experimental_nw4r_canvas_width(
                          *sixteen_ten_policy) - 608.0f) < 0.001f,
        "16:10 fits the complete native UI canvas with proportional height");
    if (sixteen_ten_policy) {
        const auto viewport = galaxy::experimental_ultrawide_viewport(
            1920, 1200, *sixteen_ten_policy);
        passed &= expect(
            viewport.left == 0 && viewport.top == 0 &&
                viewport.width == 1920 && viewport.height == 1200,
            "16:10 present and IR use the whole matching client");
    }
    passed &= expect(
        galaxy::parse_experimental_ultrawide_aspect("16:9").has_value() &&
            !galaxy::parse_experimental_ultrawide_aspect("4:3") &&
            !galaxy::parse_experimental_ultrawide_aspect("21:0") &&
            !galaxy::parse_experimental_ultrawide_aspect("invalid"),
        "native 4:3 uses its separate mode and malformed aspect fails closed");
    return passed;
}

bool latches_one_dynamic_aspect_generation_for_guest_and_ir() {
    const auto old = galaxy::g_experimental_dynamic_aspect_word.load();
    galaxy::g_experimental_dynamic_aspect_word.store(0u);
    bool passed = expect(
        galaxy::experimental_dynamic_aspect_change_pending(2560, 1080),
        "first supported client extent needs a frame latch");
    galaxy::experimental_latch_dynamic_aspect(2560, 1080);
    const auto first = galaxy::g_experimental_dynamic_aspect_word.load();
    const auto wide = galaxy::experimental_aspect_from_dynamic_word(first);
    passed &= expect(
        first >> 32u == 1u && wide &&
            wide->guest_screen_width == 1110u &&
            std::fabs(wide->content_aspect - 2560.0 / 1080.0) < 1e-12,
        "one packed generation derives matching guest width and client aspect");
    galaxy::experimental_latch_dynamic_aspect(2560, 1080);
    passed &= expect(
        galaxy::g_experimental_dynamic_aspect_word.load() == first,
        "unchanged client does not publish a new generation");
    if (wide) {
        const auto viewport = galaxy::experimental_ultrawide_viewport(
            2560, 1080, *wide);
        const auto pointer = galaxy::input::project_native_mouse_pointer({
            false, true, true, viewport.left, viewport.top,
            2560, 1080, viewport, 1.0f, 1.0f});
        passed &= expect(
            pointer.visible && std::fabs(pointer.x + 1.0f) < 0.0001f,
            "frame-latched aspect gives IR the matching presentation edge");
    }
    galaxy::experimental_latch_dynamic_aspect(1600, 1200);
    const auto fallback = galaxy::g_experimental_dynamic_aspect_word.load();
    passed &= expect(
        fallback >> 32u == 2u &&
            !galaxy::experimental_aspect_from_dynamic_word(fallback) &&
            !galaxy::experimental_dynamic_aspect_change_pending(1600, 1200),
        "unsupported 4:3 resize falls back without stretching or mid-frame mutation");
    galaxy::g_experimental_dynamic_aspect_word.store(old);
    return passed;
}

bool preserves_camera_aspect_guest_load_at_interior_entries() {
    struct Context {
        std::uint32_t gpr[32]{};
        std::uint64_t fpr_bits[32]{};
        std::uint64_t ps1_bits[32]{};
        std::uint32_t hid2 = 0;
    };
    const auto wide = galaxy::make_experimental_ultrawide_aspect(21.0 / 9.0);
    bool passed = expect(wide.has_value(), "camera aspect test policy exists");
    if (!wide) {
        return false;
    }
    constexpr std::uint32_t kCanonicalF32 = 0x3FE38E39u;
    const std::uint64_t original = std::bit_cast<std::uint64_t>(
        static_cast<double>(std::bit_cast<float>(kCanonicalF32)));
    const std::uint64_t requested = std::bit_cast<std::uint64_t>(
        static_cast<double>(static_cast<float>(wide->content_aspect)));
    Context context{};
    context.gpr[2] = 0x806AB280u;
    context.fpr_bits[1] = original;
    context.ps1_bits[1] = original;
    context.hid2 = 0x20000000u;
    galaxy::apply_experimental_rmge01_camera_aspect(&context, wide);
    passed &= expect(
        context.fpr_bits[1] == requested && context.ps1_bits[1] == requested,
        "canonical wide camera load replaces both paired-single lanes");

    context.fpr_bits[1] = original;
    context.ps1_bits[1] = original;
    context.hid2 = 0u;
    galaxy::apply_experimental_rmge01_camera_aspect(&context, wide);
    passed &= expect(
        context.fpr_bits[1] == requested && context.ps1_bits[1] == original,
        "camera load leaves inactive paired-single lane unchanged");
    context.hid2 = 0x20000000u;

    context.gpr[2] = 0x806AB284u;
    context.fpr_bits[1] = original;
    context.ps1_bits[1] = original;
    galaxy::apply_experimental_rmge01_camera_aspect(&context, wide);
    passed &= expect(
        context.fpr_bits[1] == original && context.ps1_bits[1] == original,
        "noncanonical legal interior SDA load stays guest-owned");

    context.gpr[2] = 0x806AB280u;
    context.fpr_bits[1] = std::bit_cast<std::uint64_t>(2.0);
    context.ps1_bits[1] = context.fpr_bits[1];
    galaxy::apply_experimental_rmge01_camera_aspect(&context, wide);
    passed &= expect(
        context.fpr_bits[1] == std::bit_cast<std::uint64_t>(2.0),
        "noncanonical valid interior float stays guest-owned");

    const galaxy::ExperimentalUltrawideAspect native_four_three{
        4.0 / 3.0, 608u};
    context.fpr_bits[1] = original;
    context.ps1_bits[1] = original;
    galaxy::apply_experimental_rmge01_camera_aspect(
        &context, std::optional{native_four_three});
    passed &= expect(
        context.fpr_bits[1] == original && context.ps1_bits[1] == original,
        "native 4:3 legal interior load stays guest-owned");
    return passed;
}

bool preserves_nw4r_canvas_guest_load_at_interior_entries() {
    struct Context {
        std::uint32_t gpr[32]{};
        std::uint64_t fpr_bits[32]{};
        std::uint64_t ps1_bits[32]{};
        std::uint32_t hid2 = 0;
    };
    const auto wide = galaxy::make_experimental_ultrawide_aspect(21.0 / 9.0);
    bool passed = expect(wide.has_value(), "NW4R canvas test policy exists");
    if (!wide) {
        return false;
    }
    const std::uint64_t original = std::bit_cast<std::uint64_t>(608.0);
    const std::uint64_t requested = std::bit_cast<std::uint64_t>(
        static_cast<double>(galaxy::experimental_nw4r_canvas_width(*wide)));
    Context context{};
    context.gpr[2] = 0x806AB280u;
    context.fpr_bits[3] = original;
    context.ps1_bits[3] = original;
    context.hid2 = 0x20000000u;
    galaxy::apply_experimental_rmge01_nw4r_canvas_width(&context, wide);
    passed &= expect(
        context.fpr_bits[3] == requested &&
            context.ps1_bits[3] == requested,
        "canonical NW4R load replaces both paired-single lanes");

    context.fpr_bits[3] = original;
    context.ps1_bits[3] = original;
    context.hid2 = 0u;
    galaxy::apply_experimental_rmge01_nw4r_canvas_width(&context, wide);
    passed &= expect(
        context.fpr_bits[3] == requested &&
            context.ps1_bits[3] == original,
        "NW4R load leaves inactive paired-single lane unchanged");
    context.hid2 = 0x20000000u;

    context.gpr[2] = 0x806AB284u;
    context.fpr_bits[3] = original;
    context.ps1_bits[3] = original;
    galaxy::apply_experimental_rmge01_nw4r_canvas_width(&context, wide);
    passed &= expect(
        context.fpr_bits[3] == original &&
            context.ps1_bits[3] == original,
        "noncanonical NW4R SDA interior load stays guest-owned");

    context.gpr[2] = 0x806AB280u;
    context.fpr_bits[3] = std::bit_cast<std::uint64_t>(600.0);
    context.ps1_bits[3] = context.fpr_bits[3];
    galaxy::apply_experimental_rmge01_nw4r_canvas_width(&context, wide);
    passed &= expect(
        context.fpr_bits[3] == std::bit_cast<std::uint64_t>(600.0) &&
            context.ps1_bits[3] == std::bit_cast<std::uint64_t>(600.0),
        "noncanonical NW4R float interior load stays guest-owned");

    const galaxy::ExperimentalUltrawideAspect native_four_three{
        4.0 / 3.0, 608u};
    context.fpr_bits[3] = original;
    context.ps1_bits[3] = original;
    galaxy::apply_experimental_rmge01_nw4r_canvas_width(
        &context, std::optional{native_four_three});
    passed &= expect(
        context.fpr_bits[3] == original &&
            context.ps1_bits[3] == original,
        "native 4:3 NW4R interior load stays guest-owned");
    return passed;
}

bool keeps_aspect_layout_projection_and_hits_inverse() {
    struct Context {
        std::uint32_t gpr[32]{};
        std::uint64_t fpr_bits[32]{};
        std::uint64_t ps1_bits[32]{};
        std::uint32_t hid2 = 0x20000000u;
    };
    bool passed = true;
    for (double aspect : {1.6, 16.0 / 9.0, 21.0 / 9.0, 32.0 / 9.0}) {
        const auto policy = galaxy::make_experimental_ultrawide_aspect(aspect);
        if (!policy) { return false; }
        Context c{}; c.gpr[2] = 0x806AB280u;
        c.fpr_bits[2] = std::bit_cast<std::uint64_t>(608.0);
        c.fpr_bits[0] = std::bit_cast<std::uint64_t>(304.0);
        galaxy::apply_experimental_rmge01_layout_constant(&c, 2u, false, policy);
        galaxy::apply_experimental_rmge01_layout_constant(&c, 0u, true, policy);
        const double canvas = std::bit_cast<double>(c.fpr_bits[2]);
        const double half = std::bit_cast<double>(c.fpr_bits[0]);
        passed &= expect(canvas == galaxy::experimental_nw4r_canvas_width(*policy)
            && half == canvas * 0.5, "layout conversion uses its drawing canvas");
        for (double fraction : {0.0, 0.125, 0.5, 0.875, 1.0}) {
            const double screen = fraction * policy->guest_screen_width;
            const double layout = screen * canvas / policy->guest_screen_width - half;
            const double hit = layout * policy->guest_screen_width / canvas
                + policy->guest_screen_width * 0.5;
            passed &= expect(std::fabs(hit - screen) < 1e-6
                && std::fabs((layout + half) / canvas - fraction) < 1e-6,
                "edge and center draw coordinates agree with inverse pane hits");
        }
        if (aspect != 16.0 / 9.0) {
            passed &= expect(c.ps1_bits[2] == c.fpr_bits[2], "layout lfs keeps PSE lanes coherent");
        }
        c.fpr_bits[1] = std::bit_cast<std::uint64_t>(456.0);
        galaxy::apply_experimental_rmge01_layout_vertical_fit(&c, 1u, false, policy);
        const double height = std::bit_cast<double>(c.fpr_bits[1]);
        passed &= expect(std::fabs(canvas / height - (608.0 / 456.0)
            * (aspect / (16.0 / 9.0))) < 1e-6,
            "wide UI fits with equal physical X/Y scaling at the selected aspect");
        for (double y : {-228.0, -100.0, 0.0, 100.0, 228.0}) {
            c.fpr_bits[0] = std::bit_cast<std::uint64_t>(y);
            galaxy::apply_experimental_rmge01_layout_vertical_fit(&c, 0u, false, policy);
            const double draw_y = std::bit_cast<double>(c.fpr_bits[0]);
            passed &= expect(std::fabs(draw_y / height - y / 456.0) < 1e-6,
                "pointer Y reaches the full physical content rectangle");
            galaxy::apply_experimental_rmge01_layout_vertical_fit(&c, 0u, true, policy);
            passed &= expect(std::fabs(std::bit_cast<double>(c.fpr_bits[0]) - y) < 0.0001,
                "vertical fit inverse preserves pane hit positions");
        }
        c.gpr[2] += 4u; c.fpr_bits[2] = std::bit_cast<std::uint64_t>(608.0);
        galaxy::apply_experimental_rmge01_layout_constant(&c, 2u, false, policy);
        passed &= expect(std::bit_cast<double>(c.fpr_bits[2]) == 608.0,
            "noncanonical interior load stays guest-owned");
    }
    return passed;
}

bool keeps_screen_to_efb_depth_coordinates_coherent() {
    struct Context { std::uint32_t gpr[32]{}; };
    bool passed = true;
    for (double aspect : {4.0 / 3.0, 1.6, 16.0 / 9.0, 21.0 / 9.0, 32.0 / 9.0}) {
        const std::optional<galaxy::ExperimentalUltrawideAspect> policy =
            aspect == 4.0 / 3.0
            ? std::optional<galaxy::ExperimentalUltrawideAspect>{{aspect, 608u}}
            : galaxy::make_experimental_ultrawide_aspect(aspect);
        Context c{}; c.gpr[2] = 0x806AB280u;
        c.gpr[0] = aspect == 4.0 / 3.0 ? 608u : 832u;
        galaxy::apply_experimental_rmge01_efb_screen_width(&c, policy);
        passed &= expect(c.gpr[0] == policy->guest_screen_width,
            "retail depth conversion uses selected logical width");
        for (double fraction : {0.0, 0.125, 0.5, 0.875, 0.999}) {
            const double query = fraction * policy->guest_screen_width / c.gpr[0] * 640.0;
            passed &= expect(query >= 0.0 && query < 640.0 &&
                std::fabs(query - fraction * 640.0) < 1e-6,
                "depth query and owned native pointer field agree at content edges");
        }
        c.gpr[2] += 4u; c.gpr[0] = 832u;
        galaxy::apply_experimental_rmge01_efb_screen_width(&c, policy);
        passed &= expect(c.gpr[0] == 832u, "noncanonical depth caller remains guest owned");
        c.gpr[2] = 0x806AB280u; c.gpr[0] = 608u;
        galaxy::apply_experimental_rmge01_efb_screen_width(&c, policy);
        passed &= expect(c.gpr[0] == 608u, "original narrow depth branch stays unchanged");
    }
    Context c{}; c.gpr[2] = 0x806AB280u; c.gpr[0] = 832u;
    galaxy::apply_experimental_rmge01_efb_screen_width(&c,
        std::optional<galaxy::ExperimentalUltrawideAspect>{});
    passed &= expect(c.gpr[0] == 832u, "unselected native depth conversion stays unchanged");
    return passed;
}

bool keeps_home_projection_and_pointer_coherent() {
    struct Context {
        std::uint32_t gpr[32]{};
        std::uint64_t fpr_bits[32]{}, ps1_bits[32]{};
        std::uint32_t hid2 = 0x20000000u;
    };
    bool passed = true;
    for (double aspect : {1.6, 16.0 / 9.0, 21.0 / 9.0, 32.0 / 9.0}) {
        const auto policy = galaxy::make_experimental_ultrawide_aspect(aspect);
        Context c{}; c.gpr[2] = 0x806AB280u;
        c.fpr_bits[3] = std::bit_cast<std::uint64_t>(double(policy->guest_screen_width));
        c.fpr_bits[1] = std::bit_cast<std::uint64_t>(456.0);
        galaxy::apply_experimental_rmge01_home_geometry(&c, 0u, policy);
        galaxy::apply_experimental_rmge01_home_geometry(&c, 1u, policy);
        const double width = std::bit_cast<double>(c.fpr_bits[3]);
        const double height = std::bit_cast<double>(c.fpr_bits[1]);
        passed &= expect(width >= 832.0 && height >= 456.0,
            "custom Home projection retains the entire authored wide menu");
        for (double normalized : {-1.0, -0.5, 0.0, 0.5, 1.0}) {
            for (unsigned kind : {2u, 3u}) {
                c.fpr_bits[0] = std::bit_cast<std::uint64_t>(normalized);
                galaxy::apply_experimental_rmge01_home_geometry(&c, kind, policy);
                const double input = std::bit_cast<double>(c.fpr_bits[0]);
                const double drawn = input * (kind == 2u ? 416.0 : 228.0);
                passed &= expect(std::fabs(drawn / (kind == 2u ? width : height)
                    - normalized * 0.5) < 1e-6,
                    "Home cursor and pane hit geometry map to the same physical position");
                passed &= expect(c.ps1_bits[0] == c.fpr_bits[0] || aspect == 16.0 / 9.0,
                    "Home geometry preserves PSE lane coherence");
            }
        }
        c.gpr[2] += 4u; c.fpr_bits[0] = std::bit_cast<std::uint64_t>(0.5);
        galaxy::apply_experimental_rmge01_home_geometry(&c, 3u, policy);
        passed &= expect(std::bit_cast<double>(c.fpr_bits[0]) == 0.5,
            "noncanonical Home interior caller remains guest-owned");
    }
    Context c{}; c.gpr[2] = 0x806AB280u;
    c.fpr_bits[0] = std::bit_cast<std::uint64_t>(0.5);
    galaxy::apply_experimental_rmge01_home_geometry(&c, 3u,
        std::optional<galaxy::ExperimentalUltrawideAspect>{
            galaxy::ExperimentalUltrawideAspect{4.0 / 3.0, 608u}});
    passed &= expect(std::bit_cast<double>(c.fpr_bits[0]) == 0.5,
        "native four-three Home geometry remains unchanged");
    return passed;
}

bool projects_client_mouse_through_wii_viewport() {
    using galaxy::input::NativeMousePointerSample;
    using galaxy::input::NativePointerViewport;

    const NativePointerViewport letterboxed =
        galaxy::input::fit_native_pointer_viewport(1920, 1200);
    bool passed = expect(
        letterboxed.left == 0 && letterboxed.top == 60 &&
            letterboxed.width == 1920 && letterboxed.height == 1080,
        "16:10 client fits a centered 16:9 Wii viewport");

    const auto top_left = galaxy::input::project_native_mouse_pointer({
        true, true, true, 0, 60, 1920, 1200, letterboxed, 1.0f, 1.0f});
    passed &= expect(top_left.visible, "viewport top-left is a valid IR sample");
    passed &= expect_close(
        top_left.x, -1.0f, 0.0001f,
        "viewport top-left maps to native pointer x minimum");
    passed &= expect_close(
        top_left.y, -1.0f, 0.0001f,
        "viewport top-left maps to native pointer y minimum");

    const auto bottom_right = galaxy::input::project_native_mouse_pointer({
        true, true, true, 1919, 1139, 1920, 1200, letterboxed, 1.0f, 1.0f});
    passed &= expect(
        bottom_right.visible,
        "viewport bottom-right is a valid IR sample");
    passed &= expect_close(
        bottom_right.x, 1.0f, 0.0001f,
        "viewport bottom-right maps to native pointer x maximum");
    passed &= expect_close(
        bottom_right.y, 1.0f, 0.0001f,
        "viewport bottom-right maps to native pointer y maximum");

    const auto letterbox_bar = galaxy::input::project_native_mouse_pointer({
        true, true, true, 960, 30, 1920, 1200, letterboxed, 1.0f, 1.0f});
    passed &= expect(
        !letterbox_bar.visible,
        "client pixels above the Wii viewport do not publish IR");

    const NativePointerViewport pillarboxed =
        galaxy::input::fit_native_pointer_viewport(2000, 900);
    passed &= expect(
        pillarboxed.left == 200 && pillarboxed.top == 0 &&
            pillarboxed.width == 1600 && pillarboxed.height == 900,
        "wide client fits a centered 16:9 Wii viewport");
    const auto pillarbox_bar = galaxy::input::project_native_mouse_pointer({
        true, true, true, 100, 450, 2000, 900, pillarboxed, 1.0f, 1.0f});
    passed &= expect(
        !pillarbox_bar.visible,
        "client pixels beside the Wii viewport do not publish IR");

    const NativePointerViewport four_three =
        galaxy::input::fit_native_pointer_viewport(1440, 1080);
    const auto four_three_bar =
        galaxy::input::project_native_mouse_pointer({
            true, true, true, 720, 100, 1440, 1080, four_three, 1.0f, 1.0f});
    passed &= expect(
        !four_three_bar.visible,
        "4:3 letterbox pixels do not publish IR");
    const auto calibrated_top_left =
        galaxy::input::project_native_mouse_pointer({
            true, true, true, 0, 135, 1440, 1080, four_three, 0.35f, 0.40f});
    const auto calibrated_bottom_right =
        galaxy::input::project_native_mouse_pointer({
            true, true, true, 1439, 944, 1440, 1080, four_three, 0.35f, 0.40f});
    passed &= expect(
        calibrated_top_left.visible && calibrated_bottom_right.visible,
        "calibrated viewport edges remain visible");
    passed &= expect_close(
        calibrated_top_left.x, -1.0f, 0.0001f,
        "calibration preserves left-edge reach");
    passed &= expect_close(
        calibrated_top_left.y, -1.0f, 0.0001f,
        "calibration preserves top-edge reach");
    passed &= expect_close(
        calibrated_bottom_right.x, 1.0f, 0.0001f,
        "calibration preserves right-edge reach");
    passed &= expect_close(
        calibrated_bottom_right.y, 1.0f, 0.0001f,
        "calibration preserves bottom-edge reach");

    constexpr NativePointerViewport explicit_viewport{10, 20, 101, 101};
    const NativeMousePointerSample scaled_sample{
        true, true, true, 85, 70, 200, 200, explicit_viewport, 0.5f, 0.5f};
    const auto scaled =
        galaxy::input::project_native_mouse_pointer(scaled_sample);
    const auto stationary_repeat =
        galaxy::input::project_native_mouse_pointer(scaled_sample);
    passed &= expect(scaled.visible && stationary_repeat.visible,
                     "stationary acquired mouse remains IR-visible");
    passed &= expect_close(
        scaled.x, 0.25f, 0.0001f,
        "explicit viewport applies configured center-band x gain");
    passed &= expect_close(
        scaled.y, 0.0f, 0.0001f,
        "explicit viewport center maps to neutral y");
    passed &= expect(
        scaled.x == stationary_repeat.x && scaled.y == stationary_repeat.y,
        "identical host acquisition projects deterministically");

    NativeMousePointerSample outside = scaled_sample;
    outside.client_x = -1;
    outside.inside_client = false;
    passed &= expect(
        !galaxy::input::project_native_mouse_pointer(outside).visible,
        "known off-window coordinate is not clamped into visible IR");
    NativeMousePointerSample unfocused = scaled_sample;
    unfocused.window_focused = false;
    passed &= expect(
        galaxy::input::project_native_mouse_pointer(unfocused).visible,
        "unfocused in-client coordinate remains visible IR");
    NativeMousePointerSample unavailable = scaled_sample;
    unavailable.absolute_valid = false;
    passed &= expect(
        !galaxy::input::project_native_mouse_pointer(unavailable).visible,
        "invalid acquisition remains caller-owned transient-loss policy");

    galaxy::input::WiimoteInputSnapshot buttons{};
    buttons.buttons.a = true;
    buttons.buttons.b = true;
    galaxy::input::publish_wiimote_ir_dots(
        buttons, scaled.x, scaled.y, scaled.visible);
    passed &= expect(
        buttons.buttons.a && buttons.buttons.b,
        "mouse projection and IR publication leave native A+B untouched");
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

bool repeated_ir_reports_have_no_formatter_jitter() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    galaxy::input::publish_wiimote_ir_dots(
        snapshot, -0.234375f, 0.4140625f, true);

    constexpr std::array<std::uint8_t, 3> kIrModes{0x33u, 0x36u, 0x37u};
    std::array<
        std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes>,
        kIrModes.size()>
        references{};
    std::array<std::size_t, kIrModes.size()> reference_sizes{};
    bool passed = true;
    for (std::size_t mode = 0; mode < kIrModes.size(); ++mode) {
        const auto built = galaxy::input::build_wiimote_input_report(
            kIrModes[mode], snapshot, references[mode]);
        passed &= expect(
            built.status == galaxy::input::WiimoteReportBuildStatus::Ok,
            "stationary native IR reference report builds");
        reference_sizes[mode] = built.size;
    }

    // Ten seconds' worth of formatter calls at the provisional 100 Hz host
    // acquisition profile. Delivery timing is owned by the device scheduler,
    // but the formatter itself must be stateless at every profiled slot.
    constexpr std::size_t kProfiledSlots =
        10u * galaxy::input::kProvisionalVirtualHidReportRateHz;
    for (std::size_t frame = 0; frame < kProfiledSlots; ++frame) {
        const std::size_t mode = frame % kIrModes.size();
        std::array<
            std::uint8_t,
            galaxy::input::kMaxWiimoteInputReportBytes>
            report{};
        const auto built = galaxy::input::build_wiimote_input_report(
            kIrModes[mode], snapshot, report);
        if (built.status != galaxy::input::WiimoteReportBuildStatus::Ok ||
            built.size != reference_sizes[mode] ||
            !std::equal(
                report.begin(),
                report.begin() + static_cast<std::ptrdiff_t>(built.size),
                references[mode].begin())) {
            std::cerr << "FAILED: stationary native IR formatter jitter at "
                         "profiled slot="
                      << frame << " mode=0x" << std::hex
                      << static_cast<unsigned>(kIrModes[mode]) << std::dec
                      << '\n';
            passed = false;
            break;
        }
    }
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

bool encrypts_rmge01_nunchuk_extension_payload() {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    snapshot.ir[0] = {true, 0x0155u, 0x02aau, 8u};
    snapshot.ir[1] = {true, 0x03feu, 0x0001u, 8u};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_input_report(0x37, snapshot, report);
    if (!expect(
            result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
            "0x37 report builds before extension encryption")) {
        return false;
    }
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes>
        plain = report;
    // Captured from an active RMGE01 WPAD channel. The retail handshake
    // derives these per-session tables; the input packer must use the caller's
    // live tables instead of a hardcoded global key.
    constexpr std::array<std::uint8_t, 8> additive_key{
        0x78u, 0xDDu, 0x81u, 0xBBu, 0xEAu, 0x77u, 0x5Cu, 0x06u};
    constexpr std::array<std::uint8_t, 8> xor_key{
        0x50u, 0x3Eu, 0xD2u, 0x78u, 0x6Du, 0x40u, 0xFDu, 0xAFu};
    constexpr std::array<std::uint8_t, 6> expected_wire{
        0x53u, 0x9Au, 0x2Du, 0xBDu, 0xFBu, 0xCCu};
    return expect(
               galaxy::input::encrypt_rmge01_nunchuk_extension_payload(
                   std::span<std::uint8_t>(report).first(result.size),
                   additive_key,
                   xor_key),
               "RMGE01 Nunchuk extension report encrypts") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).subspan(17u, 6u),
               expected_wire,
               "RMGE01 Nunchuk extension wire bytes use the reciprocal WPAD cipher") &&
           expect(
               std::equal(
                   report.begin(), report.begin() + 17, plain.begin()),
               "RMGE01 Nunchuk cipher leaves core, accelerometer, and IR bytes intact") &&
           expect(
               !galaxy::input::encrypt_rmge01_nunchuk_extension_payload(
                   std::span<std::uint8_t>(plain).first(4u),
                   additive_key,
                   xor_key),
               "RMGE01 Nunchuk cipher rejects a non-extension input report");
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

bool builds_status_report() {
    galaxy::input::WiimoteDeviceStatus status{};
    status.buttons.a = true;
    status.buttons.plus = true;
    status.led_mask = 0x10;
    status.battery_low = false;
    status.extension_connected = true;
    status.speaker_enabled = true;
    status.ir_enabled = true;
    status.battery_level = 0xc0;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_status_report(status, report);
    constexpr std::array<std::uint8_t, 8> expected{
        0xa1, 0x20, 0x10, 0x08, 0x1e, 0x00, 0x00, 0xc0};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x20 status report builds") &&
           expect(result.size == expected.size(), "0x20 status report size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x20 status report bytes");
}

bool builds_ack_report() {
    galaxy::input::WiimoteButtons buttons{};
    buttons.b = true;
    buttons.minus = true;
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::build_wiimote_ack_report(buttons, 0x12, 0x00, report);
    constexpr std::array<std::uint8_t, 6> expected{
        0xa1, 0x22, 0x00, 0x14, 0x12, 0x00};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x22 ack report builds") &&
           expect(result.size == expected.size(), "0x22 ack report size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x22 ack report bytes");
}

bool builds_read_memory_report() {
    galaxy::input::WiimoteButtons buttons{};
    buttons.one = true;
    const std::array<std::uint8_t, 3> data{0xde, 0xad, 0xbe};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result = galaxy::input::build_wiimote_read_memory_report(
        buttons, 0x40fa, 0x00, data, report);
    constexpr std::array<std::uint8_t, 23> expected{
        0xa1, 0x21, 0x00, 0x02, 0x20, 0x40, 0xfa,
        0xde, 0xad, 0xbe, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x21 read-memory report builds") &&
           expect(result.size == expected.size(), "0x21 read-memory size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x21 read-memory report bytes");
}

bool builds_read_memory_error_report() {
    galaxy::input::WiimoteButtons buttons{};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result = galaxy::input::build_wiimote_read_memory_report(
        buttons, 0x40fe, 0x08, std::span<const std::uint8_t>{}, report);
    constexpr std::array<std::uint8_t, 23> expected{
        0xa1, 0x21, 0x00, 0x00, 0xf8, 0x40, 0xfe,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "0x21 read-memory error report builds") &&
           expect(result.size == expected.size(), "0x21 read-memory error size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "0x21 read-memory error bytes");
}

bool normalizes_already_framed_hid_input() {
    constexpr std::array<std::uint8_t, 7> raw{
        0xa1, 0x31, 0x20, 0x60, 0x80, 0x80, 0x80};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::normalize_wiimote_hid_input_report(raw, report);
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "framed HID input normalizes") &&
           expect(result.size == raw.size(), "framed HID input size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               raw,
               "framed HID input bytes");
}

bool normalizes_report_id_first_hid_input() {
    constexpr std::array<std::uint8_t, 3> raw{0x30, 0x19, 0x8c};
    constexpr std::array<std::uint8_t, 4> expected{
        0xa1, 0x30, 0x19, 0x8c};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::normalize_wiimote_hid_input_report(raw, report);
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "report-id-first HID input normalizes") &&
           expect(result.size == expected.size(), "report-id-first HID size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "report-id-first HID bytes");
}

bool normalizes_zero_prefixed_hid_input_buffer() {
    constexpr std::array<std::uint8_t, 24> raw{
        0x00, 0x35, 0x00, 0x00, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x03,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x99};
    constexpr std::array<std::uint8_t, 23> expected{
        0xa1, 0x35, 0x00, 0x00, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x03,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::normalize_wiimote_hid_input_report(raw, report);
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "zero-prefixed HID input normalizes") &&
           expect(result.size == expected.size(), "zero-prefixed HID size") &&
           expect_bytes(
               std::span<const std::uint8_t>(report).first(result.size),
               expected,
               "zero-prefixed HID bytes");
}

bool rejects_unknown_real_hid_report_id() {
    constexpr std::array<std::uint8_t, 3> raw{0x3e, 0x00, 0x00};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::normalize_wiimote_hid_input_report(raw, report);
    return expect(
        result.status ==
            galaxy::input::WiimoteReportBuildStatus::UnsupportedReportMode,
        "unknown real HID report id is rejected");
}

bool rejects_truncated_framed_hid_input() {
    constexpr std::array<std::uint8_t, 3> raw{0xa1, 0x30, 0x00};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::normalize_wiimote_hid_input_report(raw, report);
    return expect(
               result.status ==
                   galaxy::input::WiimoteReportBuildStatus::OutputTooSmall,
               "truncated framed HID input is rejected") &&
           expect(result.size == 4u,
                  "truncated framed HID input reports required size");
}

bool rejects_truncated_report_id_first_hid_input() {
    constexpr std::array<std::uint8_t, 2> raw{0x30, 0x00};
    std::array<std::uint8_t, galaxy::input::kMaxWiimoteInputReportBytes> report{};
    const auto result =
        galaxy::input::normalize_wiimote_hid_input_report(raw, report);
    return expect(
               result.status ==
                   galaxy::input::WiimoteReportBuildStatus::OutputTooSmall,
               "truncated report-id-first HID input is rejected") &&
           expect(result.size == 4u,
                  "truncated report-id-first HID input reports required size");
}

bool builds_padded_real_hid_output_report() {
    constexpr std::array<std::uint8_t, 4> raw{
        galaxy::input::kWiimoteHidOutput, 0x12, 0x00, 0x37};
    std::array<std::uint8_t, 23> report{};
    report.fill(0xcc);
    const auto result =
        galaxy::input::build_wiimote_hid_output_report(raw, report);
    std::array<std::uint8_t, 23> expected{};
    expected[0] = 0x12;
    expected[1] = 0x00;
    expected[2] = 0x37;
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "padded real HID output builds") &&
           expect(result.size == expected.size(), "padded real HID output size") &&
           expect_bytes(report, expected, "padded real HID output bytes");
}

bool builds_exact_real_hid_output_report() {
    constexpr std::array<std::uint8_t, 4> raw{
        galaxy::input::kWiimoteHidOutput, 0x12, 0x00, 0x37};
    std::array<std::uint8_t, 3> report{};
    const auto result =
        galaxy::input::build_wiimote_hid_output_report(raw, report);
    constexpr std::array<std::uint8_t, 3> expected{0x12, 0x00, 0x37};
    return expect(
               result.status == galaxy::input::WiimoteReportBuildStatus::Ok,
               "exact real HID output builds") &&
           expect(result.size == expected.size(), "exact real HID output size") &&
           expect_bytes(report, expected, "exact real HID output bytes");
}

bool rejects_unframed_real_hid_output_report() {
    constexpr std::array<std::uint8_t, 3> raw{0x12, 0x00, 0x37};
    std::array<std::uint8_t, 23> report{};
    const auto result =
        galaxy::input::build_wiimote_hid_output_report(raw, report);
    return expect(
        result.status ==
            galaxy::input::WiimoteReportBuildStatus::UnsupportedReportMode,
        "unframed real HID output is rejected");
}

bool rejects_empty_real_hid_output_report() {
    constexpr std::array<std::uint8_t, 0> raw{};
    std::array<std::uint8_t, 23> report{};
    const auto result =
        galaxy::input::build_wiimote_hid_output_report(raw, report);
    return expect(
        result.status ==
            galaxy::input::WiimoteReportBuildStatus::UnsupportedReportMode,
        "empty real HID output is rejected");
}

bool rejects_reportless_real_hid_output_report() {
    constexpr std::array<std::uint8_t, 1> raw{galaxy::input::kWiimoteHidOutput};
    std::array<std::uint8_t, 23> report{};
    const auto result =
        galaxy::input::build_wiimote_hid_output_report(raw, report);
    return expect(
        result.status ==
            galaxy::input::WiimoteReportBuildStatus::UnsupportedReportMode,
        "reportless real HID output is rejected");
}

bool reports_small_real_hid_output_buffer() {
    constexpr std::array<std::uint8_t, 8> raw{
        galaxy::input::kWiimoteHidOutput, 0x16, 0x04, 0xa4,
        0x00, 0xf0, 0x01, 0x55};
    std::array<std::uint8_t, 4> report{};
    const auto result =
        galaxy::input::build_wiimote_hid_output_report(raw, report);
    return expect(
               result.status ==
                   galaxy::input::WiimoteReportBuildStatus::OutputTooSmall,
               "small real HID output buffer is rejected") &&
           expect(result.size == raw.size() - 1u,
                  "small real HID output reports required size");
}

bool native_hid_cadence_is_device_clocked_and_exact() {
    constexpr std::uint64_t origin = 55'000u;
    constexpr std::uint64_t period =
        galaxy::input::kNativeHidReportPeriodTicks;
    galaxy::input::NativeHidReportCadence cadence;
    cadence.arm(origin);

    bool passed = expect(
        !cadence.collect_latest_due(origin - 1u).has_value(),
        "native HID cadence does not publish before its device deadline");
    const auto first = cadence.collect_latest_due(origin);
    passed &= expect(
        first.has_value() && first->epoch == 1u && first->sequence == 1u &&
            first->deadline_ticks == origin && first->due_slots == 1u,
        "native HID cadence publishes exactly one first device sample");
    passed &= expect(
        !cadence.collect_latest_due(origin).has_value(),
        "native HID cadence cannot publish twice in one deadline slot");
    passed &= expect(
        !cadence.collect_latest_due(origin + period - 1u).has_value(),
        "native HID cadence keeps the following immutable deadline");
    const auto second = cadence.collect_latest_due(origin + period);
    passed &= expect(
        second.has_value() && second->sequence == 2u &&
            second->deadline_ticks == origin + period &&
            second->due_slots == 1u,
        "native HID cadence advances by one exact profiled slot");

    cadence.note_delivery(*first, first->deadline_ticks + 10u);
    cadence.note_delivery(*second, second->deadline_ticks + 30u);
    const auto& stats = cadence.stats();
    passed &= expect(
        stats.exact_cadence_valid && stats.scheduled_samples == 2u &&
            stats.produced_samples == 2u &&
            stats.delivered_samples == 2u &&
            stats.skipped_due_slots == 0u &&
            stats.delivery_gaps == 0u && stats.delivery_bursts == 0u &&
            stats.out_of_order_deliveries == 0u &&
            stats.delivery_age_samples == 2u &&
            stats.delivery_age_total_ticks == 40u &&
            stats.min_delivery_age_ticks == 10u &&
            stats.max_delivery_age_ticks == 30u &&
            stats.last_delivery_age_ticks == 30u &&
            stats.delivery_timing_streams_started == 1u &&
            stats.delivery_interval_samples == 1u &&
            stats.delivery_interval_total_ticks == period + 20u &&
            stats.min_delivery_interval_ticks == period + 20u &&
            stats.max_delivery_interval_ticks == period + 20u &&
            stats.max_delivery_interval_jitter_ticks == 20u &&
            stats.delivery_timing_conservation_valid(),
        "native HID exact cadence audit conserves deadline age and delivery interval timing");
    return passed;
}

bool native_hid_exposes_only_the_next_unconsumed_device_deadline() {
    constexpr std::uint64_t origin = 136'663'266u;
    constexpr std::uint64_t period =
        galaxy::input::kNativeHidReportPeriodTicks;
    galaxy::input::NativeHidReportCadence cadence;
    bool passed = expect(
        !cadence.next_unconsumed_deadline_ticks().has_value(),
        "a disarmed native HID cadence exposes no device deadline");

    cadence.arm(origin);
    passed &= expect(
        cadence.next_unconsumed_deadline_ticks() == origin,
        "an arm outside device service exposes its immediate origin slot");

    const auto origin_sample = cadence.collect_latest_due(origin);
    passed &= expect(
        origin_sample.has_value() && origin_sample->deadline_ticks == origin &&
            origin_sample->due_slots == 1u &&
            cadence.next_unconsumed_deadline_ticks() == origin + period,
        "collecting the inline origin exposes exactly the following device slot");

    cadence.arm(origin + 123u);
    passed &= expect(
        cadence.next_unconsumed_deadline_ticks() == origin + 123u,
        "a report-mode rearm exposes its unconsumed origin instead of guessing ahead");

    cadence.disarm(true);
    passed &= expect(
        !cadence.next_unconsumed_deadline_ticks().has_value(),
        "disconnect removes the broker-visible device deadline");

    bool overflow_failed = false;
    cadence.arm(std::numeric_limits<std::uint64_t>::max());
    try {
        (void)cadence.collect_latest_due(
            std::numeric_limits<std::uint64_t>::max());
    } catch (const std::overflow_error&) {
        overflow_failed = true;
    }
    passed &= expect(
        overflow_failed,
        "the cadence cannot expose a wrapped next-unconsumed deadline");
    return passed;
}

bool native_hid_cadence_coalesces_late_service_once() {
    constexpr std::uint64_t origin = 12'345u;
    constexpr std::uint64_t period =
        galaxy::input::kNativeHidReportPeriodTicks;
    galaxy::input::NativeHidReportCadence cadence;
    cadence.arm(origin);
    const auto newest = cadence.collect_latest_due(origin + period * 3u + 9u);
    bool passed = expect(
        newest.has_value() && newest->sequence == 4u &&
            newest->deadline_ticks == origin + period * 3u &&
            newest->due_slots == 4u,
        "late native HID service returns only the newest elapsed sample");
    const auto& stats = cadence.stats();
    passed &= expect(
        stats.scheduled_samples == 4u && stats.produced_samples == 1u &&
            stats.skipped_due_slots == 3u && !stats.exact_cadence_valid,
        "late native HID service explicitly audits every coalesced slot");
    passed &= expect(
        !cadence.collect_latest_due(origin + period * 4u - 1u).has_value(),
        "late native HID service does not loop or fabricate catch-up samples");
    return passed;
}

bool native_hid_published_identity_closes_next_edge_toctou() {
    constexpr std::uint64_t origin = 90'000u;
    constexpr std::uint64_t period =
        galaxy::input::kNativeHidReportPeriodTicks;
    galaxy::input::NativeHidReportCadence cadence;
    cadence.arm(origin);

    // The consumer wakes for edge 1, then wall time crosses edge 2 before the
    // broker publishes it. Only the drained identity may advance the device;
    // lateness stays invalid and skipped/coalesced counts stay zero.
    const auto first = cadence.collect_published_due(
        galaxy::input::NativeHidPublishedBatch{
            1u, 1u, 1u, origin, origin},
        origin + period);
    bool passed = expect(
        first.sequence == 1u && first.deadline_ticks == origin &&
            first.due_slots == 1u,
        "published HID edge retains its identity after wall time crosses the following edge");
    passed &= expect(
        cadence.stats().scheduled_samples == 1u &&
            cadence.stats().produced_samples == 1u &&
            cadence.stats().skipped_due_slots == 0u &&
            !cadence.stats().exact_cadence_valid &&
            cadence.next_unconsumed_sequence() == 2u &&
            cadence.next_unconsumed_deadline_ticks() == origin + period,
        "late singleton HID production records lateness without inventing a second edge");

    const auto second = cadence.collect_published_due(
        galaxy::input::NativeHidPublishedBatch{
            1u,
            2u,
            2u,
            origin + period,
            origin + period},
        origin + period);
    passed &= expect(
        second.sequence == 2u &&
            second.deadline_ticks == origin + period &&
            cadence.stats().scheduled_samples == 2u &&
            cadence.stats().produced_samples == 2u &&
            cadence.stats().skipped_due_slots == 0u,
        "later broker publication advances exactly the formerly unowned edge");
    return passed;
}

bool native_hid_published_batch_validates_phase_and_finite_fifo_loss() {
    constexpr std::uint64_t origin = 123'000u;
    constexpr std::uint64_t period =
        galaxy::input::kNativeHidReportPeriodTicks;
    bool passed = true;
    {
        galaxy::input::NativeHidReportCadence cadence;
        cadence.arm(origin);
        bool mismatched = false;
        try {
            (void)cadence.collect_published_due(
                galaxy::input::NativeHidPublishedBatch{
                    1u, 2u, 2u, origin, origin},
                origin);
        } catch (const std::runtime_error&) {
            mismatched = true;
        }
        passed &= expect(
            mismatched && cadence.stats().scheduled_samples == 0u &&
                cadence.stats().produced_samples == 0u &&
                cadence.stats().skipped_due_slots == 0u &&
                cadence.next_unconsumed_sequence() == 1u &&
                cadence.next_unconsumed_deadline_ticks() == origin,
            "mismatched broker identity fails before mutating device cadence");
    }
    {
        galaxy::input::NativeHidReportCadence cadence;
        cadence.arm(origin);
        const auto newest = cadence.collect_published_due(
            galaxy::input::NativeHidPublishedBatch{
                2u,
                1u,
                2u,
                origin,
                origin + period},
            origin + period);
        passed &= expect(
            newest.sequence == 2u && newest.due_slots == 2u &&
                cadence.stats().scheduled_samples == 2u &&
                cadence.stats().produced_samples == 1u &&
                cadence.stats().skipped_due_slots == 1u &&
                !cadence.stats().exact_cadence_valid,
            "two published HID edges remain one finite-FIFO sample plus one visible coalesced defect");
    }
    {
        galaxy::input::NativeHidReportCadence cadence(
            true, galaxy::input::kQualifiedTestHidTimingProfile);
        cadence.arm(origin);
        bool strict_failed = false;
        try {
            (void)cadence.collect_published_due(
                galaxy::input::NativeHidPublishedBatch{
                    2u,
                    1u,
                    2u,
                    origin,
                    origin + period},
                origin + period);
        } catch (const std::runtime_error&) {
            strict_failed = true;
        }
        passed &= expect(
            strict_failed && cadence.stats().scheduled_samples == 2u &&
                cadence.stats().produced_samples == 1u &&
                cadence.stats().skipped_due_slots == 1u &&
                !cadence.stats().exact_cadence_valid,
            "strict HID proof hard-fails a real two-edge finite-FIFO coalescence");
    }
    return passed;
}

bool native_hid_cadence_audit_records_every_stat() {
    galaxy::input::NativeHidCadenceStats stats{};
    stats.armed = true;
    stats.strict_proof = true;
    stats.profile_id = galaxy::input::kQualifiedTestHidTimingProfile.id;
    stats.profile_strict_proof_qualified = true;
    stats.nominal_rate_hz =
        galaxy::input::kQualifiedTestHidTimingProfile.nominal_rate_hz;
    stats.period_ticks =
        galaxy::input::kQualifiedTestHidTimingProfile.period_ticks;
    stats.maximum_production_lateness_ticks =
        galaxy::input::kQualifiedTestHidTimingProfile
            .maximum_production_lateness_ticks;
    stats.delivery_age_limit_ticks = 607'499u;
    stats.exact_cadence_valid = false;
    stats.epochs_started = 1u;
    stats.disconnect_resets = 2u;
    stats.scheduled_samples = 3u;
    stats.produced_samples = 4u;
    stats.skipped_due_slots = 5u;
    stats.queue_replacements = 6u;
    stats.mode_invalidations = 7u;
    stats.reset_queue_discards = 8u;
    stats.delivered_samples = 9u;
    stats.delivery_gaps = 10u;
    stats.delivery_bursts = 11u;
    stats.out_of_order_deliveries = 12u;
    stats.delivery_age_samples = 9u;
    stats.delivery_age_total_ticks = 90u;
    stats.min_delivery_age_ticks = 4u;
    stats.max_delivery_age_ticks = 16u;
    stats.last_delivery_age_ticks = 11u;
    stats.delivery_timing_streams_started = 2u;
    stats.delivery_interval_samples = 7u;
    stats.delivery_interval_total_ticks = 4'252'500u;
    stats.min_delivery_interval_ticks = 607'490u;
    stats.max_delivery_interval_ticks = 607'520u;
    stats.max_delivery_interval_jitter_ticks = 20u;
    stats.last_produced_epoch = 13u;
    stats.last_produced_sequence = 14u;
    stats.last_produced_deadline_ticks = 15u;
    stats.last_production_ticks = 16u;
    stats.max_production_lateness_ticks = 1u;
    stats.last_delivered_epoch = 17u;
    stats.last_delivered_sequence = 18u;
    stats.last_delivered_deadline_ticks = 19u;
    stats.last_delivery_ticks = 20u;

    std::ostringstream output;
    galaxy::input::dump_native_hid_cadence_audit(
        output, "unit-test", stats, true);
    const std::string record = output.str();
    constexpr std::array expected_fields{
        "[input-hid-cadence-audit] tag=unit-test",
        "timeline-hz=60750000",
        "profile-id=qualified-test-only-100hz",
        "profile-qualified=1",
        "nominal-rate-hz=100",
        "period-ticks=607500",
        "max-production-lateness-limit-ticks=0",
        "max-delivery-age-limit-ticks=607499",
        "strict=1",
        "armed=1",
        "exact-cadence-valid=0",
        "epochs-started=1",
        "disconnect-resets=2",
        "scheduled-samples=3",
        "produced-samples=4",
        "skipped-due-slots=5",
        "queue-replacements=6",
        "mode-invalidations=7",
        "reset-queue-discards=8",
        "fifo-occupied=1",
        "delivered-samples=9",
        "delivery-gaps=10",
        "delivery-bursts=11",
        "out-of-order-deliveries=12",
        "delivery-age-samples=9",
        "delivery-age-total-ticks=90",
        "min-delivery-age-ticks=4",
        "max-delivery-age-ticks=16",
        "last-delivery-age-ticks=11",
        "delivery-timing-streams-started=2",
        "delivery-interval-samples=7",
        "delivery-interval-total-ticks=4252500",
        "min-delivery-interval-ticks=607490",
        "max-delivery-interval-ticks=607520",
        "max-delivery-interval-jitter-ticks=20",
        "delivery-timing-conserved=1",
        "last-produced-epoch=13",
        "last-produced-sequence=14",
        "last-produced-deadline-ticks=15",
        "last-production-ticks=16",
        "max-production-lateness-ticks=1",
        "last-delivered-epoch=17",
        "last-delivered-sequence=18",
        "last-delivered-deadline-ticks=19",
        "last-delivery-ticks=20",
    };
    bool passed = true;
    for (const char* field : expected_fields) {
        passed &= expect(
            record.find(field) != std::string::npos,
            "native HID cadence audit records every statistic");
    }
    return passed;
}

bool native_hid_cadence_strict_proof_rejects_defects() {
    constexpr std::uint64_t origin = 1'000u;
    constexpr std::uint64_t period =
        galaxy::input::kNativeHidReportPeriodTicks;
    bool passed = true;
    {
        galaxy::input::NativeHidReportCadence cadence(
            true, galaxy::input::kQualifiedTestHidTimingProfile);
        cadence.arm(origin);
        bool threw = false;
        try {
            static_cast<void>(
                cadence.collect_latest_due(origin + period * 2u));
        } catch (const std::runtime_error&) {
            threw = true;
        }
        passed &= expect(
            threw && cadence.stats().skipped_due_slots == 2u &&
                !cadence.stats().exact_cadence_valid,
            "strict native HID proof hard-fails skipped device slots");
    }
    {
        galaxy::input::NativeHidReportCadence cadence(
            true, galaxy::input::kQualifiedTestHidTimingProfile);
        cadence.arm(origin);
        bool threw = false;
        try {
            cadence.note_queue_replacement();
        } catch (const std::runtime_error&) {
            threw = true;
        }
        passed &= expect(
            threw && cadence.stats().queue_replacements == 1u,
            "strict native HID proof hard-fails FIFO replacement");
    }
    {
        galaxy::input::NativeHidReportCadence cadence(
            true, galaxy::input::kQualifiedTestHidTimingProfile);
        cadence.arm(origin);
        bool threw = false;
        try {
            cadence.note_reset_queue_discard();
        } catch (const std::runtime_error&) {
            threw = true;
        }
        passed &= expect(
            threw && cadence.stats().reset_queue_discards == 1u &&
                !cadence.stats().exact_cadence_valid,
            "strict native HID proof hard-fails reset with queued input");
    }
    {
        galaxy::input::NativeHidReportCadence cadence(
            true, galaxy::input::kQualifiedTestHidTimingProfile);
        cadence.arm(origin);
        const auto first = cadence.collect_latest_due(origin);
        bool threw = false;
        try {
            cadence.note_delivery(*first, origin + period);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        passed &= expect(
            threw && cadence.stats().delivery_age_samples == 1u &&
                cadence.stats().delivered_samples == 1u &&
                cadence.stats().max_delivery_age_ticks == period &&
                cadence.stats().delivery_timing_conservation_valid() &&
                !cadence.stats().exact_cadence_valid,
            "strict native HID proof hard-fails an ACL handoff at or beyond the following report deadline");
    }
    return passed;
}

bool native_hid_strict_proof_rejects_provisional_profile() {
    const auto rejects_unqualified = [](
                                         galaxy::input::NativeHidTimingProfile
                                             profile) {
        galaxy::input::NativeHidReportCadence cadence(true, profile);
        bool threw = false;
        try {
            cadence.arm(1'000u);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        const auto& stats = cadence.stats();
        return threw && !stats.armed &&
            !stats.profile_strict_proof_qualified &&
            !stats.exact_cadence_valid && stats.epochs_started == 0u;
    };

    constexpr galaxy::input::NativeHidTimingProfile explicit_unqualified{
        "capture-pending-100hz",
        galaxy::input::kProvisionalVirtualHidReportRateHz,
        galaxy::input::kNativeHidReportPeriodTicks,
        0u,
        false};
    bool passed = expect(
        rejects_unqualified(
            galaxy::input::kProvisionalVirtualHidTimingProfile),
        "strict native HID proof rejects the provisional production profile");
    passed &= expect(
        rejects_unqualified(explicit_unqualified),
        "strict native HID proof rejects every explicitly unqualified profile");
    return passed;
}

bool native_virtual_hid_profile_is_a_strict_project_contract() {
    const auto& profile = galaxy::input::kNativeVirtualHidTimingProfile;
    bool passed = expect(
        profile.id == "native-virtual-contract-v1-100hz" &&
            profile.nominal_rate_hz == 100u &&
            profile.period_ticks == 607'500u &&
            profile.maximum_production_lateness_ticks == 607'499u &&
            profile.strict_proof_qualified,
        "production virtual HID names and qualifies its exact project-owned timeline contract");

    galaxy::input::NativeHidReportCadence cadence(true, profile);
    constexpr std::uint64_t kOrigin = 5'000u;
    cadence.arm(kOrigin);
    const auto first = cadence.collect_latest_due(kOrigin);
    passed &= expect(
        first.has_value() && first->sequence == 1u &&
            first->deadline_ticks == kOrigin && first->due_slots == 1u &&
            cadence.stats().profile_strict_proof_qualified &&
            cadence.stats().delivery_age_limit_ticks == 607'499u &&
            cadence.stats().exact_cadence_valid,
        "strict production virtual HID arms only on its exact first deadline");
    return passed;
}

bool native_hid_qualified_profiles_fail_closed_on_malformed_timing() {
    struct RejectionCase {
        galaxy::input::NativeHidTimingProfile profile;
        const char* message;
    };
    constexpr std::array rejection_cases{
        RejectionCase{
            {"", 100u, 607'500u, 0u, true},
            "qualified native HID profile rejects an empty identity"},
        RejectionCase{
            {"qualified-zero-rate", 0u, 607'500u, 0u, true},
            "qualified native HID profile rejects a zero nominal rate"},
        RejectionCase{
            {"qualified-zero-period", 100u, 0u, 0u, true},
            "qualified native HID profile rejects a zero period"},
        RejectionCase{
            {"qualified-nonintegral-rate", 128u, 474'609u, 0u, true},
            "qualified native HID profile rejects a rate that does not divide the timeline"},
        RejectionCase{
            {"qualified-wrong-period", 100u, 607'499u, 0u, true},
            "qualified native HID profile rejects a period not derived from its rate"},
        RejectionCase{
            {"qualified-period-lateness", 100u, 607'500u, 607'500u, true},
            "qualified native HID profile rejects lateness equal to its period"},
        RejectionCase{
            {"qualified-over-period-lateness", 100u, 607'500u, 607'501u, true},
            "qualified native HID profile rejects lateness greater than its period"},
    };

    bool passed = true;
    for (const auto& rejection : rejection_cases) {
        galaxy::input::NativeHidReportCadence cadence(
            true, rejection.profile);
        bool threw_invalid_argument = false;
        try {
            cadence.arm(5'000u);
        } catch (const std::invalid_argument&) {
            threw_invalid_argument = true;
        }
        const auto& stats = cadence.stats();
        passed &= expect(
            threw_invalid_argument && !stats.armed &&
                !stats.exact_cadence_valid && stats.epochs_started == 0u,
            rejection.message);
    }

    galaxy::input::NativeHidReportCadence exact_deadline_cadence(
        true, galaxy::input::kQualifiedTestHidTimingProfile);
    exact_deadline_cadence.arm(5'000u);
    passed &= expect(
        exact_deadline_cadence.stats().armed &&
            exact_deadline_cadence.stats()
                    .maximum_production_lateness_ticks == 0u &&
            exact_deadline_cadence.stats().exact_cadence_valid,
        "zero lateness remains a valid exact-deadline qualified test contract");
    return passed;
}

bool newest_only_hid_fifo_conserves_at_100hz_under_backpressure() {
    constexpr std::uint64_t period =
        galaxy::input::kNativeHidReportPeriodTicks;
    const auto packet_for = [](std::uint32_t index) {
        return std::vector<std::byte>{
            std::byte{0xA1},
            static_cast<std::byte>(static_cast<std::uint8_t>(index))};
    };

    bool passed = true;
    {
        galaxy::input::NativeNewestOnlyHidFifo fifo;
        for (std::uint32_t report = 0; report < 100u; ++report) {
            const std::uint64_t sequence = fifo.push(
                packet_for(report),
                static_cast<std::uint64_t>(report) * period);
            passed &= expect(
                sequence == static_cast<std::uint64_t>(report) + 1u,
                "newest-only HID FIFO assigns monotonic completion sequences");
        }
        const auto& stats = fifo.stats();
        const auto* newest = fifo.front();
        passed &= expect(
            stats.completed_reports == 100u &&
                stats.fifo_replacements == 99u &&
                stats.delivered_reports == 0u &&
                stats.reset_discards == 0u && fifo.occupied() &&
                fifo.conservation_valid(),
            "100 Hz HID producer remains bounded and conserved with no guest reads");
        passed &= expect(
            newest != nullptr && newest->sequence == 100u &&
                newest->completion_ticks == 99u * period &&
                newest->packet.size() == 2u &&
                newest->packet[1] == std::byte{99u},
            "no-reader HID FIFO retains only the newest physical completion");

        fifo.note_delivery(99u * period + period / 2u);
        passed &= expect(
            !fifo.occupied() && fifo.conservation_valid() &&
                fifo.stats().delivered_reports == 1u &&
                fifo.stats().last_delivered_sequence == 100u &&
                fifo.stats().max_delivery_age_ticks == period / 2u,
            "newest-only HID FIFO accounts delivery age without resurrecting replaced reports");
    }

    {
        galaxy::input::NativeNewestOnlyHidFifo fifo;
        for (std::uint32_t report = 0; report < 100u; ++report) {
            const std::uint64_t completion_ticks =
                static_cast<std::uint64_t>(report) * period;
            static_cast<void>(
                fifo.push(packet_for(report), completion_ticks));
            if ((report + 1u) % 4u == 0u) {
                const auto* newest = fifo.front();
                passed &= expect(
                    newest != nullptr &&
                        newest->sequence ==
                            static_cast<std::uint64_t>(report) + 1u,
                    "slower HID consumer observes the newest completion only");
                fifo.note_delivery(completion_ticks + period / 4u);
            }
        }
        const auto& stats = fifo.stats();
        passed &= expect(
            stats.completed_reports == 100u &&
                stats.fifo_replacements == 75u &&
                stats.delivered_reports == 25u &&
                stats.reset_discards == 0u &&
                stats.last_delivered_sequence == 100u &&
                stats.max_delivery_age_ticks == period / 4u &&
                !fifo.occupied() && fifo.conservation_valid(),
            "100 Hz HID producer conserves reports behind a 25 Hz guest consumer");

        static_cast<void>(fifo.push(packet_for(100u), 100u * period));
        fifo.discard_on_reset();
        passed &= expect(
            fifo.stats().completed_reports == 101u &&
                fifo.stats().reset_discards == 1u &&
                !fifo.occupied() && fifo.conservation_valid(),
            "newest-only HID FIFO conserves an occupied report across reset");
    }
    return passed;
}

bool native_hid_io_worker_decouples_slow_output_and_keeps_latest_input() {
    auto state = std::make_shared<FakeNativeHidState>();
    state->block_open = true;
    state->block_writes = true;
    galaxy::input::NativeHidIoWorker worker(
        std::make_unique<FakeNativeHidBackend>(state), 8u, 1u);

    const auto start_begin = std::chrono::steady_clock::now();
    worker.start();
    const auto start_elapsed =
        std::chrono::steady_clock::now() - start_begin;
    worker.enqueue_output(fake_output_report(1u));
    worker.enqueue_output(fake_output_report(2u));
    worker.enqueue_output(fake_output_report(3u));

    bool passed = expect(
        start_elapsed < std::chrono::milliseconds(500),
        "native HID worker start does not wait for slow device discovery");
    passed &= expect(
        wait_until([&] {
            std::lock_guard<std::mutex> lock(state->mutex);
            return state->open_entered;
        }),
        "fake HID discovery entered its deterministic blocked state");

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->block_open = false;
        state->wake.notify_all();
    }
    passed &= expect(
        wait_until([&] {
            const auto stats = worker.stats();
            std::lock_guard<std::mutex> lock(state->mutex);
            return stats.opened && stats.output_in_flight &&
                state->write_entered;
        }),
        "ordered HID output begins after discovery without caller polling");

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (std::uint8_t marker = 0u; marker < 100u; ++marker) {
            state->input_reports.push_back(fake_input_report(marker));
        }
    }
    passed &= expect(
        wait_until([&] {
            return worker.stats().input.completed_reports == 100u;
        }),
        "physical HID input continues while an output write is blocked");
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        passed &= expect(
            state->output_reports.empty(),
            "fake slow write remained blocked during independent input acquisition");
    }

    const auto latest = worker.take_latest_input(23u);
    passed &= expect(
        latest.status == galaxy::input::NativeHidIoTakeStatus::Ok &&
            latest.report.sequence == 100u &&
            latest.report.packet.size() == 3u &&
            latest.report.packet[2] == std::byte{99u},
        "newest-only worker mailbox delivers the latest physical completion");
    {
        const auto stats = worker.stats();
        passed &= expect(
            stats.input.completed_reports == 100u &&
                stats.input.fifo_replacements == 99u &&
                stats.input.delivered_reports == 1u &&
                !stats.input_occupied &&
                stats.input_conservation_valid,
            "worker input mailbox remains bounded and conserves every completion");
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->block_writes = false;
        state->wake.notify_all();
    }
    passed &= expect(
        wait_until([&] {
            return worker.stats().output_reports_completed == 3u;
        }),
        "all guest HID outputs complete after the slow write is released");
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const bool ordered = state->output_reports.size() == 3u &&
            state->output_reports[0].size() == 2u &&
            state->output_reports[1].size() == 2u &&
            state->output_reports[2].size() == 2u &&
            state->output_reports[0][1] == std::byte{1u} &&
            state->output_reports[1][1] == std::byte{2u} &&
            state->output_reports[2][1] == std::byte{3u};
        passed &= expect(
            ordered,
            "guest HID output mailbox preserves strict FIFO write order");
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->input_reports.push_back(fake_input_report(0xa0u));
    }
    passed &= expect(
        wait_until([&] {
            const auto stats = worker.stats();
            return stats.input.completed_reports == 101u &&
                stats.input_occupied;
        }),
        "worker acquires a report before a logical disconnect reset");
    worker.discard_input_on_reset();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->input_reports.push_back(fake_input_report(0xa1u));
    }
    passed &= expect(
        wait_until([&] {
            const auto stats = worker.stats();
            return stats.input.completed_reports == 102u &&
                stats.input_occupied;
        }),
        "worker reacquires fresh input after a logical reconnect");
    const auto reconnected = worker.take_latest_input(23u);
    const auto reconnect_stats = worker.stats();
    passed &= expect(
        reconnected.status == galaxy::input::NativeHidIoTakeStatus::Ok &&
            reconnected.report.packet.size() == 3u &&
            reconnected.report.packet[2] == std::byte{0xa1u} &&
            reconnect_stats.input.reset_discards == 1u &&
            reconnect_stats.input.completed_reports == 102u &&
            reconnect_stats.input.delivered_reports == 2u &&
            reconnect_stats.input_conservation_valid,
        "disconnect discards stale input and reconnect exposes only fresh input");

    const auto stop_begin = std::chrono::steady_clock::now();
    worker.stop();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_begin;
    const auto stopped_stats = worker.stats();
    bool closed = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        closed = state->closed;
    }
    passed &= expect(
        stop_elapsed < std::chrono::milliseconds(500) && closed &&
            stopped_stats.stopped && !stopped_stats.opened,
        "native HID worker joins and closes in bounded time");
    return passed;
}

bool native_hid_io_worker_cancels_blocked_discovery_on_stop() {
    auto state = std::make_shared<FakeNativeHidState>();
    state->block_open = true;
    galaxy::input::NativeHidIoWorker worker(
        std::make_unique<FakeNativeHidBackend>(state), 2u, 1u);
    worker.start();
    worker.enqueue_output(fake_output_report(7u));

    bool passed = expect(
        wait_until([&] {
            std::lock_guard<std::mutex> lock(state->mutex);
            return state->open_entered;
        }),
        "fake HID discovery is blocked before cancellation");
    const auto stop_begin = std::chrono::steady_clock::now();
    worker.stop();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_begin;

    bool lifecycle_valid = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        lifecycle_valid = state->closed && !state->open_succeeded &&
            !state->write_entered && state->output_reports.empty();
    }
    const auto stats = worker.stats();
    passed &= expect(
        stop_elapsed < std::chrono::milliseconds(500) && lifecycle_valid &&
            stats.stopped && stats.output_reports_enqueued == 1u &&
            stats.output_reports_completed == 0u,
        "stop cancels blocked discovery and joins without attempting queued output");
    return passed;
}

bool native_hid_io_worker_hard_fails_bounded_output_overflow() {
    auto state = std::make_shared<FakeNativeHidState>();
    state->block_writes = true;
    FakeFailureNotification notification;
    galaxy::input::NativeHidIoWorker worker(
        std::make_unique<FakeNativeHidBackend>(state), 2u, 1u);
    worker.set_failure_notifier(&note_fake_hid_failure, &notification);
    worker.start();

    bool passed = expect(
        wait_until([&] { return worker.stats().opened; }),
        "fake HID backend opens before output-bound test");
    worker.enqueue_output(fake_output_report(1u));
    passed &= expect(
        wait_until([&] {
            const auto stats = worker.stats();
            return stats.output_in_flight;
        }),
        "first output is held in-flight for deterministic backpressure");
    worker.enqueue_output(fake_output_report(2u));
    worker.enqueue_output(fake_output_report(3u));
    const auto bounded = worker.stats();
    passed &= expect(
        bounded.pending_output_reports == 2u &&
            bounded.maximum_pending_output_reports == 2u &&
            bounded.output_reports_enqueued == 3u,
        "output mailbox reaches but never exceeds its configured hard bound");

    bool overflow_threw = false;
    try {
        worker.enqueue_output(fake_output_report(4u));
    } catch (const std::runtime_error&) {
        overflow_threw = true;
    }
    passed &= expect(
        overflow_threw &&
            notification.calls.load(std::memory_order_acquire) == 1u,
        "output overflow hard-fails and publishes the runtime notifier once");

    bool failure_observable = false;
    try {
        worker.throw_if_failed();
    } catch (const std::runtime_error&) {
        failure_observable = true;
    }
    passed &= expect(
        failure_observable,
        "output overflow remains observable at the runtime health boundary");

    const auto stop_begin = std::chrono::steady_clock::now();
    worker.stop();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_begin;
    const auto stopped = worker.stats();
    passed &= expect(
        stop_elapsed < std::chrono::milliseconds(500) && stopped.failed &&
            stopped.stopped && stopped.pending_output_reports == 2u &&
            stopped.output_reports_completed == 0u,
        "overflow cancellation bounds shutdown without claiming cancelled output completion");
    return passed;
}

bool native_hid_io_worker_publishes_async_discovery_failure() {
    auto state = std::make_shared<FakeNativeHidState>();
    state->fail_open = true;
    FakeFailureNotification notification;
    galaxy::input::NativeHidIoWorker worker(
        std::make_unique<FakeNativeHidBackend>(state), 2u, 1u);
    worker.set_failure_notifier(&note_fake_hid_failure, &notification);
    worker.start();

    bool passed = expect(
        wait_until([&] {
            return notification.calls.load(std::memory_order_acquire) == 1u &&
                worker.stats().stopped;
        }),
        "background HID discovery failure publishes without a guest command");
    bool failure_observable = false;
    try {
        worker.throw_if_failed();
    } catch (const std::runtime_error&) {
        failure_observable = true;
    }
    worker.stop();
    bool closed = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        closed = state->closed;
    }
    passed &= expect(
        failure_observable && closed && worker.stats().failed,
        "asynchronous discovery failure remains latched through worker cleanup");
    return passed;
}

bool native_hid_logical_epoch_preserves_hardware_deadline() {
    constexpr std::uint64_t origin = 22'000u;
    constexpr std::uint64_t period =
        galaxy::input::kNativeHidReportPeriodTicks;
    galaxy::input::NativeHidReportCadence cadence(
        true, galaxy::input::kQualifiedTestHidTimingProfile);
    cadence.arm(origin);
    const auto invalidated = cadence.collect_latest_due(origin);
    cadence.note_mode_invalidation();
    cadence.begin_logical_epoch();

    bool passed = expect(
        invalidated.has_value() &&
            !cadence.collect_latest_due(origin + period - 1u).has_value(),
        "native HID logical epoch cannot rephase the immutable device deadline");
    const auto fresh = cadence.collect_latest_due(origin + period);
    passed &= expect(
        fresh.has_value() && fresh->epoch == 2u &&
            fresh->sequence == invalidated->sequence + 1u &&
            fresh->deadline_ticks == origin + period,
        "native HID logical epoch preserves deadline and global sample order");
    cadence.note_delivery(*fresh, fresh->deadline_ticks);
    passed &= expect(
        cadence.stats().exact_cadence_valid &&
            cadence.stats().epochs_started == 2u &&
            cadence.stats().mode_invalidations == 1u &&
            cadence.stats().delivery_gaps == 0u &&
            cadence.stats().delivery_bursts == 0u &&
            cadence.stats().out_of_order_deliveries == 0u,
        "intentional semantic invalidation starts a clean strict delivery epoch");
    return passed;
}

bool native_hid_cadence_epochs_reject_stale_samples() {
    constexpr std::uint64_t origin = 9'000u;
    galaxy::input::NativeHidReportCadence cadence;
    cadence.arm(origin);
    const auto stale = cadence.collect_latest_due(origin);
    cadence.disarm(true);
    cadence.arm(origin + 100u);
    const auto current = cadence.collect_latest_due(origin + 100u);
    bool passed = expect(
        current.has_value() && current->epoch == 2u &&
            current->sequence > stale->sequence,
        "native HID reconnect starts a new epoch with monotonic IDs");
    bool threw = false;
    try {
        cadence.note_delivery(*stale, origin + 100u);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    passed &= expect(
        threw && cadence.stats().out_of_order_deliveries == 1u,
        "native HID cadence rejects stale pre-reset samples");
    cadence.note_delivery(*current, current->deadline_ticks);
    passed &= expect(
        cadence.stats().disconnect_resets == 1u &&
            cadence.stats().last_delivered_sequence == current->sequence,
        "native HID reconnect audit records reset and current delivery");
    return passed;
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

bool physical_mailbox_retains_control_replies_and_bounds_state() {
    galaxy::input::NativeHidInputMailbox mailbox(4u);
    const std::vector<std::byte> ack{std::byte{0xa1u}, std::byte{0x22u}, std::byte{0x11u}};
    const std::vector<std::byte> read_a{std::byte{0xa1u}, std::byte{0x21u}, std::byte{0x16u}};
    const std::vector<std::byte> read_b{std::byte{0xa1u}, std::byte{0x21u}, std::byte{0x20u}};
    const std::vector<std::byte> status{std::byte{0xa1u}, std::byte{0x20u}, std::byte{0x80u}};
    static_cast<void>(mailbox.push(fake_input_report(1u), 10u));
    static_cast<void>(mailbox.push(ack, 20u));
    static_cast<void>(mailbox.push(fake_input_report(2u), 30u));
    static_cast<void>(mailbox.push(read_a, 40u));
    static_cast<void>(mailbox.push(read_b, 50u));
    static_cast<void>(mailbox.push(status, 60u));
    const std::array<std::vector<std::byte>, 5> expected{
        ack, fake_input_report(2u), read_a, read_b, status};
    bool passed = true;
    for (std::size_t index = 0; index != expected.size(); ++index) {
        const auto* front = mailbox.front();
        if (front == nullptr) return expect(false, "every retained control packet is available");
        passed &= expect(front->packet == expected[index] && front->sequence == index + 2u,
                         "reply retention preserves exact bytes and chronological sequence");
        mailbox.note_delivery(100u);
    }
    passed &= expect(!mailbox.occupied() && mailbox.stats().completed_reports == 6u &&
                     mailbox.stats().fifo_replacements == 1u &&
                     mailbox.stats().delivered_reports == 5u && mailbox.conservation_valid(),
                     "only continuous state is coalesced; every control reply is conserved");
    static_cast<void>(mailbox.push(ack, 110u));
    static_cast<void>(mailbox.push(fake_input_report(3u), 120u));
    mailbox.discard_on_reset();
    passed &= expect(mailbox.stats().reset_discards == 2u && !mailbox.occupied() &&
                     mailbox.conservation_valid(), "reset accounts for retained and latest packets");
    galaxy::input::NativeHidInputMailbox bounded(1u);
    static_cast<void>(bounded.push(ack, 1u));
    bool overflowed = false;
    try { static_cast<void>(bounded.push(read_a, 2u)); }
    catch (const std::overflow_error&) { overflowed = true; }
    passed &= expect(overflowed && bounded.front()->packet == ack &&
                     bounded.stats().completed_reports == 1u && bounded.conservation_valid(),
                     "reply overflow fails explicitly without replacing existing response");
    return passed;
}

bool worker_delivers_control_replies_before_later_continuous_samples() {
    auto state = std::make_shared<FakeNativeHidState>();
    const std::vector<std::byte> ack{
        std::byte{0xa1u}, std::byte{0x22u}, std::byte{0}, std::byte{0}, std::byte{0x11u}, std::byte{0}};
    auto read_a = std::vector<std::byte>(23u, std::byte{0});
    read_a[0] = std::byte{0xa1u}; read_a[1] = std::byte{0x21u}; read_a[6] = std::byte{0x16u};
    auto read_b = read_a; read_b[6] = std::byte{0x20u};
    const std::array<std::vector<std::byte>, 4> expected{ack, read_a, read_b, fake_input_report(99u)};
    state->input_reports.assign(expected.begin(), expected.end());
    galaxy::input::NativeHidIoWorker worker(std::make_unique<FakeNativeHidBackend>(state));
    worker.start();
    bool passed = expect(wait_until([&] { return worker.stats().input.completed_reports == 4u; }),
                         "worker acquires control responses and state before guest read");
    const auto too_large = worker.take_latest_input(5u);
    passed &= expect(too_large.status == galaxy::input::NativeHidIoTakeStatus::TooLarge &&
                     worker.stats().input.delivered_reports == 0u,
                     "small guest capacity leaves first control response queued");
    for (std::size_t index = 0; index != expected.size(); ++index) {
        const auto taken = worker.take_latest_input(23u);
        passed &= expect(taken.status == galaxy::input::NativeHidIoTakeStatus::Ok &&
                         taken.report.sequence == index + 1u && taken.report.packet == expected[index],
                         "actual worker retains ACK and both memory chunks byte-exact");
    }
    passed &= expect(worker.stats().input_conservation_valid && !worker.input_occupied(),
                     "worker control-response retirement conserves every completion");
    worker.stop();
    auto overflow_state = std::make_shared<FakeNativeHidState>();
    overflow_state->input_reports = {ack, ack};
    galaxy::input::NativeHidIoWorker overflow_worker(
        std::make_unique<FakeNativeHidBackend>(overflow_state), 64u, 1u, 1u);
    overflow_worker.start();
    passed &= expect(wait_until([&] { return overflow_worker.stats().failed; }),
                     "bounded physical reply overflow becomes worker failure");
    passed &= expect(overflow_worker.stats().input.completed_reports == 1u &&
                     overflow_worker.stats().input_conservation_valid,
                     "overflow preserves previously accepted packet accounting");
    overflow_worker.stop();
    return passed;
}

bool worker_samples_delivery_time_after_synchronized_packet_selection() {
    struct ClockState {
        std::mutex mutex;
        std::condition_variable wake;
        std::atomic<std::uint64_t> next_ticks{100u};
        std::thread::id consumer;
        bool delivery_sampled{};
        bool release_delivery{};
        bool producer_sampled_second{};
        bool timed_out{};
    } clock;
    const auto read_clock = +[](void* user) -> std::uint64_t {
        auto& clock = *static_cast<ClockState*>(user);
        // Capture before pausing: models descheduling immediately after a clock
        // observation. A later producer is allowed to acquire a later sample.
        const auto ticks = clock.next_ticks.fetch_add(50u);
        std::unique_lock<std::mutex> lock(clock.mutex);
        if (std::this_thread::get_id() == clock.consumer) {
            clock.delivery_sampled = true;
            clock.wake.notify_all();
            if (!clock.wake.wait_for(lock, std::chrono::seconds(2), [&] {
                    return clock.release_delivery;
                })) clock.timed_out = true;
        } else if (ticks >= 200u) {
            clock.producer_sampled_second = true;
            clock.wake.notify_all();
        }
        return ticks;
    };
    auto state = std::make_shared<FakeNativeHidState>();
    state->pause_second_poll = true;
    state->input_reports.push_back(fake_input_report(1u));
    galaxy::input::NativeHidIoWorker worker(
        std::make_unique<FakeNativeHidBackend>(state), 64u, 1u, 256u, {read_clock, &clock});
    worker.start();
    bool passed = expect(wait_until([&] { return worker.stats().input.completed_reports == 1u; }),
                         "first packet precedes deterministic delivery boundary");
    passed &= expect(wait_until([&] {
        std::lock_guard<std::mutex> lock(state->mutex);
        return state->second_poll_entered;
    }), "producer is already inside next poll before the consumer acquires mailbox lock");
    galaxy::input::NativeHidIoTakeResult first;
    bool threw = false;
    std::thread consumer([&] {
        { std::lock_guard<std::mutex> lock(clock.mutex); clock.consumer = std::this_thread::get_id(); }
        try { first = worker.take_latest_input(23u); } catch (...) { threw = true; }
    });
    {
        std::unique_lock<std::mutex> lock(clock.mutex);
        passed &= expect(clock.wake.wait_for(lock, std::chrono::seconds(2), [&] {
            return clock.delivery_sampled;
        }), "consumer pauses at its actual delivery clock observation");
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->input_reports.push_back(fake_input_report(2u));
        state->release_second_poll = true;
        state->wake.notify_all();
    }
    {
        std::unique_lock<std::mutex> lock(clock.mutex);
        passed &= expect(clock.wake.wait_for(lock, std::chrono::seconds(2), [&] {
            return clock.producer_sampled_second;
        }), "producer observes a later completion while consumer is paused");
        clock.release_delivery = true;
        clock.wake.notify_all();
    }
    consumer.join();
    passed &= expect(!threw && !clock.timed_out &&
                     first.status == galaxy::input::NativeHidIoTakeStatus::Ok && first.report.sequence == 1u,
                     "synchronized selection retires first packet without a false timestamp regression");
    passed &= expect(wait_until([&] { return worker.stats().input.completed_reports == 2u; }),
                     "second packet publishes after first synchronized retirement");
    const auto second = worker.take_latest_input(23u);
    passed &= expect(second.status == galaxy::input::NativeHidIoTakeStatus::Ok &&
                     second.report.sequence == 2u && worker.stats().input.delivered_reports == 2u &&
                     worker.stats().input.fifo_replacements == 0u && worker.stats().input_conservation_valid,
                     "both boundary packets retire exactly once in chronological order");
    worker.stop();
    return passed;
}

}  // namespace

bool owned_depth_field_completion_is_bounded_and_publishes_failure() {
    auto capture=std::make_shared<galaxy::gx::PointerDepthCapture>();
    bool ok=expect(capture->await_until(std::chrono::steady_clock::now()),"initial ready field never waits");
    capture->complete.store(false,std::memory_order_release);
    ok &= expect(!capture->await_until(std::chrono::steady_clock::now()),"pending field respects an expired deadline");
    std::thread worker([capture] {
        capture->pixels[0]=0x123456u;
        capture->success=true;
        capture->publish_complete();
    });
    const bool ready=capture->await_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));
    worker.join();
    ok &= expect(ready && capture->success && capture->pixels[0]==0x123456u,"awaited scene pixels are published coherently");
    capture->complete.store(false,std::memory_order_release);
    capture->success=false;
    std::thread failed_worker([capture] {
        capture->error=std::make_exception_ptr(std::runtime_error("owned capture failure"));
        capture->publish_complete();
    });
    const bool failure_ready=capture->await_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));
    failed_worker.join();
    ok &= expect(failure_ready && !capture->success && capture->error,"failed worker wakes the exact owner instead of hanging or exposing stale pixels");
    return ok;
}

int main() {
    bool ok = true;
    ok &= owned_depth_field_completion_is_bounded_and_publishes_failure();
    {
        galaxy::input::NativeWindowKeyPressLatch keys;
        galaxy::input::SyntheticKpadButtonState buttons;
        ok &= expect(
            galaxy::input::normalize_native_window_key(0x10u, 0u, false) == 0xa0u &&
            galaxy::input::normalize_native_window_key(0x10u, 0xa1u, false) == 0xa1u &&
            galaxy::input::normalize_native_window_key(0x11u, 0u, true) == 0xa3u &&
            galaxy::input::normalize_native_window_key(13u, 0u, false) == 13u,
            "modifier events without scan codes retain keyboard bindings");
        // Complete down/up can arrive between OS level polls. Its one-shot
        // acquisition must also survive a newer neutral HID sample.
        keys.publish(13u);
        keys.publish(160u);
        const auto chord = (keys.consume(13u) ? 0x0800u : 0u) |
            (keys.consume(160u) ? 0x0400u : 0u);
        buttons.capture(chord);
        buttons.capture(0u);
        auto frame = buttons.consume();
        ok &= expect(frame.hold == 0x0c00u && frame.trigger == 0x0c00u &&
                     frame.retained_press == 0x0c00u && frame.release == 0u,
                     "short A+B chord survives both host and guest polling boundaries");
        frame = buttons.consume();
        ok &= expect(frame.hold == 0u && frame.trigger == 0u && frame.release == 0x0c00u,
                     "retained press releases on the next guest read without a timer");
        ok &= expect(!keys.consume(13u) && !keys.consume(160u),
                     "window key press is consumed exactly once");
        buttons.capture(0x0800u);
        frame = buttons.consume();
        ok &= expect(frame.hold == 0x0800u && frame.trigger == 0x0800u,
                     "new held key produces one trigger");
        for (int i = 0; i < 10; ++i) buttons.capture(0x0800u);
        frame = buttons.consume();
        ok &= expect(frame.hold == 0x0800u && frame.trigger == 0u && frame.release == 0u,
                     "repeated held samples do not duplicate a trigger");
        buttons.capture(0u);
        frame = buttons.consume();
        ok &= expect(frame.hold == 0u && frame.release == 0x0800u,
                     "ordinary key release remains immediate");
        keys.publish(13u);
        keys.clear();
        ok &= expect(!keys.consume(13u), "focus/capture loss discards unconsumed window presses");
        buttons.capture(0x0800u);
        buttons.reset();
        frame = buttons.consume();
        ok &= expect(frame.hold == 0u && frame.trigger == 0u && frame.release == 0u,
                     "device reset discards stale synthetic input");
        buttons.capture(0x0800u);
        buttons.capture(0u);
        frame = buttons.consume(false);
        ok &= expect(frame.hold == 0u && frame.trigger == 0u,
                     "disabled edge experiment retains legacy newest-only semantics");
        galaxy::input::WiimoteInputSnapshot all;
        all.buttons = {true,true,true,true,true,true,true,true,true,true,true};
        all.nunchuk.c = all.nunchuk.z = true;
        ok &= expect(galaxy::input::synthetic_kpad_button_mask(all) == 0xff1fu,
                     "synthetic Wii and Nunchuk masks retain every mapped button");
    }
    ok &= basic_ir_decoder_preserves_low_ff_visible_coordinates();
    ok &= physical_mailbox_retains_control_replies_and_bounds_state();
    ok &= worker_delivers_control_replies_before_later_continuous_samples();
    ok &= worker_samples_delivery_time_after_synchronized_packet_selection();
    ok &= synthetic_kpad_preserves_a_real_host_shake_pulse();
    ok &= native_input_anomalies_retain_missing_slots_without_changing_cadence();
    ok &= native_input_script_unsigned_fields_fail_closed();
    ok &= normalizes_xinput_axes();
    ok &= raw_key_trace_mask_is_lazy_and_bit_exact();
    ok &= functional_key_levels_are_coherent_and_lazy();
    ok &= maps_native_stick_axis_to_nunchuk_byte();
    ok &= isolates_keyboard_mouse_and_controller_pointer_axes();
    ok &= fits_required_content_viewports();
    ok &= maps_opt_in_ultrawide_logical_and_pointer_space();
    ok &= latches_one_dynamic_aspect_generation_for_guest_and_ir();
    ok &= preserves_camera_aspect_guest_load_at_interior_entries();
    ok &= preserves_nw4r_canvas_guest_load_at_interior_entries();
    ok &= keeps_aspect_layout_projection_and_hits_inverse();
    ok &= keeps_screen_to_efb_depth_coordinates_coherent();
    ok &= keeps_home_projection_and_pointer_coherent();
    ok &= projects_client_mouse_through_wii_viewport();
    ok &= publishes_native_pointer_as_ir_dots();
    ok &= stress_publishes_native_pointer_ir_edges();
    ok &= publishes_camera_space_ir_for_kpad_pointer_position();
    ok &= native_pointer_ir_sweeps_are_monotonic();
    ok &= preserves_ir_coordinates_across_report_modes();
    ok &= repeated_ir_reports_have_no_formatter_jitter();
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
    ok &= encrypts_rmge01_nunchuk_extension_payload();
    ok &= packs_basic_ir_mixed_visibility_sentinels();
    ok &= packs_ir_and_extension_report();
    ok &= all_supported_input_report_modes_build();
    ok &= rejects_unsupported_modes();
    ok &= reports_small_output_buffer();
    ok &= builds_status_report();
    ok &= builds_ack_report();
    ok &= builds_read_memory_report();
    ok &= builds_read_memory_error_report();
    ok &= normalizes_already_framed_hid_input();
    ok &= normalizes_report_id_first_hid_input();
    ok &= normalizes_zero_prefixed_hid_input_buffer();
    ok &= rejects_unknown_real_hid_report_id();
    ok &= rejects_truncated_framed_hid_input();
    ok &= rejects_truncated_report_id_first_hid_input();
    ok &= builds_padded_real_hid_output_report();
    ok &= builds_exact_real_hid_output_report();
    ok &= rejects_unframed_real_hid_output_report();
    ok &= rejects_empty_real_hid_output_report();
    ok &= rejects_reportless_real_hid_output_report();
    ok &= reports_small_real_hid_output_buffer();
    ok &= native_hid_cadence_is_device_clocked_and_exact();
    ok &= native_hid_exposes_only_the_next_unconsumed_device_deadline();
    ok &= native_hid_cadence_coalesces_late_service_once();
    ok &= native_hid_published_identity_closes_next_edge_toctou();
    ok &= native_hid_published_batch_validates_phase_and_finite_fifo_loss();
    ok &= native_hid_cadence_audit_records_every_stat();
    ok &= native_hid_cadence_strict_proof_rejects_defects();
    ok &= native_hid_strict_proof_rejects_provisional_profile();
    ok &= native_virtual_hid_profile_is_a_strict_project_contract();
    ok &= native_hid_qualified_profiles_fail_closed_on_malformed_timing();
    ok &= newest_only_hid_fifo_conserves_at_100hz_under_backpressure();
    ok &= native_hid_io_worker_decouples_slow_output_and_keeps_latest_input();
    ok &= native_hid_io_worker_cancels_blocked_discovery_on_stop();
    ok &= native_hid_io_worker_hard_fails_bounded_output_overflow();
    ok &= native_hid_io_worker_publishes_async_discovery_failure();
    ok &= native_hid_logical_epoch_preserves_hardware_deadline();
    ok &= native_hid_cadence_epochs_reject_stale_samples();
    return ok ? 0 : 1;
}
