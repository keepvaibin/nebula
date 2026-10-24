#pragma once

// The virtual Wii Remote's input reports: the snapshot they are built from,
// IR dots for the pointer, the shake accelerometer pattern and the mouse
// pointer projection.

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

// An integer field of the input scripts: ASCII digits only, no sign or
// whitespace, and overflow fails instead of wrapping. `value` is changed only
// on success.
[[nodiscard]] bool parse_native_input_script_u64(
    std::string_view text,
    std::uint64_t& value) noexcept;

// `anchor_vi` plus the script offset `offset_text`, failing on overflow.
// `start_vi` is changed only on success.
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
    // Shake buttons held (or a controller's motion shake): the game swings once per press (nebula_game_wpad_shake);
    // the accelerometer readings stay as they are.
    bool shake = false;
    bool nunchuk_shake = false;
};

[[nodiscard]] float normalize_xinput_axis(
    std::int16_t raw,
    std::int16_t deadzone) noexcept;

[[nodiscard]] std::uint8_t native_stick_axis_byte(
    float axis,
    std::uint8_t negative,
    std::uint8_t center,
    std::uint8_t positive) noexcept;

// The picture inside the window client: letterbox and pillarbox pixels are
// not pointer coordinates.
using NativePointerViewport = galaxy::ContentViewport;

struct NativeMousePointerSample {
    // Not used by the projection, which follows the cursor over the content
    // viewport while the window is unfocused too.
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

// One client-area cursor sample as pointer coordinates, -1..1 across the
// content viewport. A sample outside the client or the viewport is not
// visible; holding the pointer through a lost sample is the caller's choice.
// Scales below 1 lower the gain in the centre band (native_input.cpp).
// Follows Dusklight's menu pointer (CC0, src/dusk/menu_pointer.cpp).
[[nodiscard]] NativeMousePointerProjection project_native_mouse_pointer(
    const NativeMousePointerSample& sample) noexcept;

void publish_wiimote_ir_dots(
    WiimoteInputSnapshot& snapshot,
    float pointer_x,
    float pointer_y,
    bool active) noexcept;

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
