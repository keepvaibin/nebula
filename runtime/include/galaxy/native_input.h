#pragma once

#include "galaxy/content_viewport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::input {

inline constexpr std::uint8_t kWiimoteHidInput = 0xA1;
inline constexpr std::size_t kMaxWiimoteInputReportBytes = 23;
inline constexpr std::uint16_t kWiimoteRestAccelX = 0x01fc;
inline constexpr std::uint16_t kWiimoteRestAccelY = 0x0204;
inline constexpr std::uint16_t kWiimoteRestAccelZ = 0x0264;

// Parse the decimal grammar accepted by native input-script integer fields.
// The grammar is intentionally narrower than the CRT conversion routines:
// ASCII digits only, no sign or whitespace, and no modulo wrap on overflow.
// `value` is changed only on success.
[[nodiscard]] bool parse_native_input_script_u64(
    std::string_view text,
    std::uint64_t& value) noexcept;

// Resolve a marker-relative script offset without permitting either the text
// conversion or anchor addition to wrap. `start_vi` is changed only on
// success.
[[nodiscard]] bool resolve_native_input_script_start_vi(
    std::uint64_t anchor_vi,
    std::string_view offset_text,
    std::uint64_t& start_vi) noexcept;

struct WiimoteButtons {
    bool left = false;
    bool right = false;
    bool down = false;
    bool up = false;
    bool plus = false;
    bool two = false;
    bool one = false;
    bool b = false;
    bool a = false;
    bool minus = false;
    bool home = false;
};

struct Axis10 {
    std::uint16_t x = 512;
    std::uint16_t y = 512;
    std::uint16_t z = 512;
};

struct IrDot {
    bool visible = false;
    std::uint16_t x = 0x03ff;
    std::uint16_t y = 0x03ff;
    std::uint8_t size = 0;
};

struct NunchukState {
    std::uint8_t stick_x = 0x7b;
    std::uint8_t stick_y = 0x81;
    Axis10 accel{};
    bool c = false;
    bool z = false;
};

struct WiimoteInputSnapshot {
    WiimoteButtons buttons{};
    Axis10 accel{kWiimoteRestAccelX, kWiimoteRestAccelY, kWiimoteRestAccelZ};
    std::array<IrDot, 4> ir{};
    NunchukState nunchuk{};
    // The pointer the IR dots stand for, in KPAD's units, when they were made from one (publish_wiimote_ir_dots).
    bool pointer_exact = false;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
};

[[nodiscard]] float normalize_xinput_axis(
    std::int16_t raw,
    std::int16_t deadzone) noexcept;

[[nodiscard]] std::uint8_t native_stick_axis_byte(
    float axis,
    std::uint8_t negative,
    std::uint8_t center,
    std::uint8_t positive) noexcept;

// The Windows client and the Wii presentation viewport are deliberately
// separate. A resizable native window may contain letterbox/pillarbox space;
// those pixels are not valid sensor-bar coordinates.
using NativePointerViewport = galaxy::ContentViewport;

struct NativeMousePointerSample {
    // Diagnostic/button-ownership metadata only. Pointer projection is based
    // on the global cursor's geometry inside the game content viewport and
    // intentionally continues while the window is unfocused.
    bool window_focused = false;
    bool absolute_valid = false;
    bool inside_client = false;
    int client_x = 0;
    int client_y = 0;
    int client_width = 0;
    int client_height = 0;
    NativePointerViewport viewport{};
    float x_scale = 1.0f;
    float y_scale = 1.0f;
};

struct NativeMousePointerProjection {
    float x = 0.0f;
    float y = 0.0f;
    bool visible = false;
};

// Project one coherent client-area acquisition to native [-1,+1] pointer
// coordinates. Known outside-client or outside-viewport samples are invalid;
// transient acquisition-loss holding remains a caller-owned timeline policy.
//
// This generalizes the direct client-dimension normalization used by the CC0
// Dusklight menu pointer (src/dusk/menu_pointer.cpp) with explicit viewport,
// validity, and edge-gain contracts for Nebula's raw Wii HID producer.
[[nodiscard]] NativeMousePointerProjection project_native_mouse_pointer(
    const NativeMousePointerSample& sample) noexcept;

// Build the diagnostic-only raw-key mask without touching the provider when
// no trace record will be emitted. Bits 2..17 retain their established wire
// order; the provider is queried exactly once per key, in array order.
template <typename KeyDown>
[[nodiscard]] std::uint32_t collect_native_raw_key_trace_mask(
    bool collect,
    const std::array<int, 16>& virtual_keys,
    KeyDown&& key_down) {
    if (!collect) {
        return 0u;
    }

    std::uint32_t raw_key_mask = 0u;
    for (std::size_t index = 0; index < virtual_keys.size(); ++index) {
        if (key_down(virtual_keys[index])) {
            raw_key_mask |= 1u << (index + 2u);
        }
    }
    return raw_key_mask;
}

void publish_wiimote_ir_dots(
    WiimoteInputSnapshot& snapshot,
    float pointer_x,
    float pointer_y,
    bool active) noexcept;

[[nodiscard]] bool advance_wiimote_shake_accel(
    WiimoteInputSnapshot& snapshot,
    bool pressed,
    bool& previous_pressed,
    std::uint8_t& remaining_frames) noexcept;

enum class WiimoteReportBuildStatus {
    Ok,
    UnsupportedReportMode,
    OutputTooSmall,
};

struct WiimoteReportBuildResult {
    WiimoteReportBuildStatus status = WiimoteReportBuildStatus::Ok;
    std::size_t size = 0;
};

[[nodiscard]] std::size_t wiimote_input_report_size(
    std::uint8_t report_mode) noexcept;

[[nodiscard]] WiimoteReportBuildResult build_wiimote_input_report(
    std::uint8_t report_mode,
    const WiimoteInputSnapshot& snapshot,
    std::span<std::uint8_t> output) noexcept;

}  // namespace galaxy::input
