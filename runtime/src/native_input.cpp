#include "galaxy/native_input.h"

#include <algorithm>
#include <cmath>

namespace galaxy::input {

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

    const auto project_axis = [](int coordinate, int origin, int extent) noexcept {
        const float normalized =
            (static_cast<float>(coordinate - origin) /
             static_cast<float>(extent - 1)) *
                2.0f -
            1.0f;
        return std::clamp(normalized, -1.0f, 1.0f);
    };

    projection.x = project_axis(
        sample.client_x, sample.viewport.left, sample.viewport.width);
    projection.y = project_axis(
        sample.client_y, sample.viewport.top, sample.viewport.height);
    projection.visible = true;
    return projection;
}

std::uint32_t button_bits(const ButtonState& b) noexcept {
    return (b.a ? kButtonA : 0u) | (b.b ? kButtonB : 0u) | (b.one ? kButton1 : 0u) | (b.two ? kButton2 : 0u) |
           (b.plus ? kButtonPlus : 0u) | (b.minus ? kButtonMinus : 0u) | (b.home ? kButtonHome : 0u) |
           (b.up ? kButtonUp : 0u) | (b.down ? kButtonDown : 0u) | (b.left ? kButtonLeft : 0u) |
           (b.right ? kButtonRight : 0u) | (b.c ? kButtonC : 0u) | (b.z ? kButtonZ : 0u);
}

void set_pointer(PlayerInput& input, float x, float y, bool active) noexcept {
    input.pointer_on = active;
    if (!active) {
        return;
    }
    input.pointer_x = std::clamp(x, -1.0f, 1.0f);
    input.pointer_y = std::clamp(y, -1.0f, 1.0f);
    input.roll_x = 1.0f;
    input.roll_y = 0.0f;
    input.distance = kVirtualPointerDistance;
}

bool remote_faces_screen(const Gravity& g) noexcept {
    // The level part of gravity against the whole: above 0.7 (within ~45.6 degrees of level).
    const float level = g[0] * g[0] + g[2] * g[2];
    const float whole = level + g[1] * g[1];
    return whole <= 0.0f || level > 0.49f * whole;
}

StickValue nunchuk_stick_counts(float x, float y) noexcept {
    constexpr float kDeadZone = 15.0f;
    constexpr float kFull = 71.0f;
    const auto axis = [](float counts) noexcept {
        const float travel = std::fabs(counts);
        const float magnitude = travel <= kDeadZone ? 0.0f
            : travel >= kFull                        ? 1.0f
                                                     : (travel - kDeadZone) / (kFull - kDeadZone);
        return counts < 0.0f ? -magnitude : magnitude;
    };
    StickValue stick{axis(x), axis(y)};
    const float length = stick.x * stick.x + stick.y * stick.y;
    if (length > 1.0f) {
        const float scale = 1.0f / std::sqrt(length);
        stick.x *= scale;
        stick.y *= scale;
    }
    return stick;
}

StickValue nunchuk_stick(float x, float y) noexcept {
    constexpr float kTravel = 101.0f;
    return nunchuk_stick_counts(std::clamp(x, -1.0f, 1.0f) * kTravel, std::clamp(y, -1.0f, 1.0f) * kTravel);
}

}  // namespace galaxy::input
