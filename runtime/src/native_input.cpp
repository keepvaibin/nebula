#include "galaxy/native_input.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace galaxy::input {

bool parse_native_input_script_u64(
    std::string_view text,
    std::uint64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }

    std::uint64_t parsed = 0u;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::uint64_t digit =
            static_cast<std::uint64_t>(character - '0');
        if (parsed >
            (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
            return false;
        }
        parsed = parsed * 10u + digit;
    }

    value = parsed;
    return true;
}

bool resolve_native_input_script_start_vi(
    std::uint64_t anchor_vi,
    std::string_view offset_text,
    std::uint64_t& start_vi) noexcept {
    std::uint64_t offset = 0u;
    if (!parse_native_input_script_u64(offset_text, offset) ||
        offset > std::numeric_limits<std::uint64_t>::max() - anchor_vi) {
        return false;
    }
    start_vi = anchor_vi + offset;
    return true;
}

namespace {

std::uint16_t clamp_axis10(std::uint16_t value) noexcept {
    return std::min<std::uint16_t>(
        value, static_cast<std::uint16_t>(0x03ffu));
}

std::uint8_t high8(std::uint16_t value) noexcept {
    return static_cast<std::uint8_t>(clamp_axis10(value) >> 2);
}

void write_core_buttons(
    const WiimoteInputSnapshot& snapshot,
    bool include_accel_lsb,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    std::uint8_t byte0 = 0;
    std::uint8_t byte1 = 0;
    const WiimoteButtons& buttons = snapshot.buttons;
    if (buttons.left) byte0 |= 0x01u;
    if (buttons.right) byte0 |= 0x02u;
    if (buttons.down) byte0 |= 0x04u;
    if (buttons.up) byte0 |= 0x08u;
    if (buttons.plus) byte0 |= 0x10u;
    if (buttons.two) byte1 |= 0x01u;
    if (buttons.one) byte1 |= 0x02u;
    if (buttons.b) byte1 |= 0x04u;
    if (buttons.a) byte1 |= 0x08u;
    if (buttons.minus) byte1 |= 0x10u;
    if (buttons.home) byte1 |= 0x80u;
    if (include_accel_lsb) {
        // The Wii Remote carries the accelerometer's low bits in unused
        // core-button bits; WPAD masks them out of the button word. Y0 and Z0
        // are not in the normal report formats.
        byte0 |= static_cast<std::uint8_t>(
            (clamp_axis10(snapshot.accel.x) & 0x03u) << 5u);
        byte1 |= static_cast<std::uint8_t>(
            (clamp_axis10(snapshot.accel.y) & 0x02u) << 4u);
        byte1 |= static_cast<std::uint8_t>(
            (clamp_axis10(snapshot.accel.z) & 0x02u) << 5u);
    }
    output[cursor++] = byte0;
    output[cursor++] = byte1;
}

void write_accel(
    const Axis10& accel,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    output[cursor++] = high8(accel.x);
    output[cursor++] = high8(accel.y);
    output[cursor++] = high8(accel.z);
}

void write_ir_basic_pair(
    const IrDot& first,
    const IrDot& second,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    std::uint8_t high = 0xffu;
    if (!first.visible) {
        output[cursor++] = 0xffu;
        output[cursor++] = 0xffu;
    } else {
        const std::uint16_t x = clamp_axis10(first.x);
        const std::uint16_t y = clamp_axis10(first.y);
        output[cursor++] = static_cast<std::uint8_t>(x);
        output[cursor++] = static_cast<std::uint8_t>(y);
    }
    if (first.visible || second.visible) {
        const std::uint16_t x1 =
            first.visible ? clamp_axis10(first.x) : 0x03ffu;
        const std::uint16_t y1 =
            first.visible ? clamp_axis10(first.y) : 0x03ffu;
        const std::uint16_t x2 =
            second.visible ? clamp_axis10(second.x) : 0x03ffu;
        const std::uint16_t y2 =
            second.visible ? clamp_axis10(second.y) : 0x03ffu;
        high = static_cast<std::uint8_t>(
            ((y1 >> 8) & 0x03u) << 6 |
            ((x1 >> 8) & 0x03u) << 4 |
            ((y2 >> 8) & 0x03u) << 2 |
            ((x2 >> 8) & 0x03u));
    }
    output[cursor++] = high;
    if (!second.visible) {
        output[cursor++] = 0xffu;
        output[cursor++] = 0xffu;
    } else {
        const std::uint16_t x = clamp_axis10(second.x);
        const std::uint16_t y = clamp_axis10(second.y);
        output[cursor++] = static_cast<std::uint8_t>(x);
        output[cursor++] = static_cast<std::uint8_t>(y);
    }
}

void write_ir_basic_10(
    const WiimoteInputSnapshot& snapshot,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    write_ir_basic_pair(snapshot.ir[0], snapshot.ir[1], output, cursor);
    write_ir_basic_pair(snapshot.ir[2], snapshot.ir[3], output, cursor);
}

void write_ir_extended_dot(
    const IrDot& dot,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    if (!dot.visible) {
        output[cursor++] = 0xffu;
        output[cursor++] = 0xffu;
        output[cursor++] = 0xffu;
        return;
    }
    const std::uint16_t x = clamp_axis10(dot.x);
    const std::uint16_t y = clamp_axis10(dot.y);
    output[cursor++] = static_cast<std::uint8_t>(x);
    output[cursor++] = static_cast<std::uint8_t>(y);
    output[cursor++] = static_cast<std::uint8_t>(
        ((y >> 8) & 0x03u) << 6 |
        ((x >> 8) & 0x03u) << 4 |
        (dot.size & 0x0fu));
}

void write_ir_extended_12(
    const WiimoteInputSnapshot& snapshot,
    std::span<std::uint8_t> output,
    std::size_t& cursor) noexcept {
    for (const IrDot& dot : snapshot.ir) {
        write_ir_extended_dot(dot, output, cursor);
    }
}

void write_nunchuk_extension(
    const NunchukState& nunchuk,
    std::span<std::uint8_t> output,
    std::size_t& cursor,
    std::size_t byte_count) noexcept {
    const std::uint16_t x = clamp_axis10(nunchuk.accel.x);
    const std::uint16_t y = clamp_axis10(nunchuk.accel.y);
    const std::uint16_t z = clamp_axis10(nunchuk.accel.z);
    const std::array<std::uint8_t, 6> packed{
        nunchuk.stick_x,
        nunchuk.stick_y,
        high8(x),
        high8(y),
        high8(z),
        static_cast<std::uint8_t>(
            ((z & 0x03u) << 6) |
            ((y & 0x03u) << 4) |
            ((x & 0x03u) << 2) |
            (nunchuk.c ? 0x00u : 0x02u) |
            (nunchuk.z ? 0x00u : 0x01u)),
    };
    const std::size_t to_copy = std::min(byte_count, packed.size());
    for (std::size_t i = 0; i < to_copy; ++i) {
        output[cursor++] = packed[i];
    }
    for (std::size_t i = to_copy; i < byte_count; ++i) {
        output[cursor++] = 0;
    }
}

}  // namespace

float normalize_xinput_axis(std::int16_t raw, std::int16_t deadzone) noexcept {
    const int value = static_cast<int>(raw);
    const int dz = static_cast<int>(deadzone);
    const int magnitude = value < 0 ? -value : value;
    if (magnitude <= dz) {
        return 0.0f;
    }
    const int range = value < 0 ? 32768 - dz : 32767 - dz;
    const float scaled =
        static_cast<float>(magnitude - dz) / static_cast<float>(range);
    return std::clamp(value < 0 ? -scaled : scaled, -1.0f, 1.0f);
}

std::uint8_t native_stick_axis_byte(
    float axis,
    std::uint8_t negative,
    std::uint8_t center,
    std::uint8_t positive) noexcept {
    axis = std::clamp(axis, -1.0f, 1.0f);
    if (axis < 0.0f) {
        return static_cast<std::uint8_t>(
            std::clamp<int>(
                static_cast<int>(
                    std::lround(
                        static_cast<float>(center) +
                        axis * static_cast<float>(center - negative))),
                0,
                255));
    }
    return static_cast<std::uint8_t>(
        std::clamp<int>(
            static_cast<int>(
                std::lround(
                    static_cast<float>(center) +
                    axis * static_cast<float>(positive - center))),
            0,
            255));
}

NativeMousePointerProjection project_native_mouse_pointer(
    const NativeMousePointerSample& sample) noexcept {
    NativeMousePointerProjection projection{};
    if (!sample.absolute_valid || !sample.inside_client ||
        sample.client_width <= 1 ||
        sample.client_height <= 1 || sample.viewport.width <= 1 ||
        sample.viewport.height <= 1 || sample.viewport.left < 0 ||
        sample.viewport.top < 0 ||
        sample.viewport.width > sample.client_width - sample.viewport.left ||
        sample.viewport.height > sample.client_height - sample.viewport.top ||
        sample.client_x < 0 || sample.client_y < 0 ||
        sample.client_x >= sample.client_width ||
        sample.client_y >= sample.client_height ||
        sample.client_x < sample.viewport.left ||
        sample.client_y < sample.viewport.top ||
        sample.client_x >= sample.viewport.left + sample.viewport.width ||
        sample.client_y >= sample.viewport.top + sample.viewport.height) {
        return projection;
    }

    const auto project_axis = [](int coordinate, int origin, int extent,
                                 float requested_scale) noexcept {
        const float normalized =
            (static_cast<float>(coordinate - origin) /
             static_cast<float>(extent - 1)) *
                2.0f -
            1.0f;
        const float clamped_normalized =
            std::clamp(normalized, -1.0f, 1.0f);
        const float scale = std::clamp(
            std::isfinite(requested_scale) ? std::fabs(requested_scale) : 1.0f,
            0.05f,
            1.0f);
        if (scale >= 0.999f) {
            return clamped_normalized;
        }

        // Low-gain center band for the game's save-select cursor, then full
        // camera reach at the edges.
        constexpr float kCenterBand = 0.50f;
        const float magnitude = std::fabs(clamped_normalized);
        const float sign = std::signbit(clamped_normalized) ? -1.0f : 1.0f;
        if (magnitude <= kCenterBand) {
            return sign * magnitude * scale;
        }
        const float center_edge = kCenterBand * scale;
        const float edge_scale =
            (1.0f - center_edge) / (1.0f - kCenterBand);
        return sign * std::clamp(
                          center_edge +
                              (magnitude - kCenterBand) * edge_scale,
                          0.0f,
                          1.0f);
    };

    projection.x = project_axis(
        sample.client_x, sample.viewport.left, sample.viewport.width,
        sample.x_scale);
    projection.y = project_axis(
        sample.client_y, sample.viewport.top, sample.viewport.height,
        sample.y_scale);
    projection.visible = true;
    return projection;
}

void publish_wiimote_ir_dots(
    WiimoteInputSnapshot& snapshot,
    float pointer_x,
    float pointer_y,
    bool active) noexcept {
    snapshot.ir = {};
    snapshot.pointer_exact = false;
    if (!active) {
        return;
    }
    snapshot.pointer_exact = true;
    snapshot.pointer_x = std::clamp(pointer_x, -1.0f, 1.0f);
    snapshot.pointer_y = std::clamp(pointer_y, -1.0f, 1.0f);
    constexpr int kKpadFrameMinRaw = 26;
    constexpr int kKpadFrameMaxXRaw = 997;
    constexpr int kKpadFrameMaxYRaw = 725;
    constexpr int kPrimaryHalfSeparation = 80;
    // The IR camera raster is not KPAD's pointing space: WPAD/KPAD apply the
    // EEPROM DPD calibration, the below-screen sensor-bar offset and their own
    // scale, 226 camera units per KPAD unit with neutral at (510, 487). The
    // dots are placed so that the game's own KPAD decoding yields the pointer.
    constexpr float kKpadPointerRawUnitsPerHostUnit = 226.0f;
    constexpr float kKpadPointerNeutralXRaw = 510.0f;
    constexpr float kKpadPointerNeutralYRaw = 487.0f;
    const int center_x = std::clamp<int>(
        static_cast<int>(
            std::lround(
                kKpadPointerNeutralXRaw -
                std::clamp(pointer_x, -1.0f, 1.0f) *
                    kKpadPointerRawUnitsPerHostUnit)),
        kKpadFrameMinRaw + kPrimaryHalfSeparation,
        kKpadFrameMaxXRaw - kPrimaryHalfSeparation);
    const int center_y = std::clamp<int>(
        static_cast<int>(
            std::lround(
                kKpadPointerNeutralYRaw +
                std::clamp(pointer_y, -1.0f, 1.0f) *
                    kKpadPointerRawUnitsPerHostUnit)),
        kKpadFrameMinRaw,
        kKpadFrameMaxYRaw);
    snapshot.ir[0] = {
        true,
        static_cast<std::uint16_t>(center_x - kPrimaryHalfSeparation),
        static_cast<std::uint16_t>(center_y),
        8};
    snapshot.ir[1] = {
        true,
        static_cast<std::uint16_t>(center_x + kPrimaryHalfSeparation),
        static_cast<std::uint16_t>(center_y),
        8};
}

std::size_t wiimote_input_report_size(std::uint8_t report_mode) noexcept {
    switch (report_mode) {
    case 0x30:
        return 4;
    case 0x31:
        return 7;
    case 0x32:
        return 12;
    case 0x33:
        return 19;
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
        return 23;
    default:
        return 0;
    }
}

WiimoteReportBuildResult build_wiimote_input_report(
    std::uint8_t report_mode,
    const WiimoteInputSnapshot& snapshot,
    std::span<std::uint8_t> output) noexcept {
    const std::size_t report_size = wiimote_input_report_size(report_mode);
    if (report_size == 0) {
        return {WiimoteReportBuildStatus::UnsupportedReportMode, 0};
    }
    if (output.size() < report_size) {
        return {WiimoteReportBuildStatus::OutputTooSmall, report_size};
    }

    std::size_t cursor = 0;
    output[cursor++] = kWiimoteHidInput;
    output[cursor++] = report_mode;

    switch (report_mode) {
    case 0x30:
        write_core_buttons(snapshot, false, output, cursor);
        break;
    case 0x31:
        write_core_buttons(snapshot, true, output, cursor);
        write_accel(snapshot.accel, output, cursor);
        break;
    case 0x32:
        write_core_buttons(snapshot, false, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 8);
        break;
    case 0x33:
        write_core_buttons(snapshot, true, output, cursor);
        write_accel(snapshot.accel, output, cursor);
        write_ir_extended_12(snapshot, output, cursor);
        break;
    case 0x34:
        write_core_buttons(snapshot, false, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 19);
        break;
    case 0x35:
        write_core_buttons(snapshot, true, output, cursor);
        write_accel(snapshot.accel, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 16);
        break;
    case 0x36:
        write_core_buttons(snapshot, false, output, cursor);
        write_ir_basic_10(snapshot, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 9);
        break;
    case 0x37:
        write_core_buttons(snapshot, true, output, cursor);
        write_accel(snapshot.accel, output, cursor);
        write_ir_basic_10(snapshot, output, cursor);
        write_nunchuk_extension(snapshot.nunchuk, output, cursor, 6);
        break;
    default:
        return {WiimoteReportBuildStatus::UnsupportedReportMode, 0};
    }

    return {WiimoteReportBuildStatus::Ok, cursor};
}

}  // namespace galaxy::input
