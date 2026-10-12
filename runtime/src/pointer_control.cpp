#include "galaxy/pointer_control.h"

#include <algorithm>
#include <cmath>

namespace galaxy::input {

PointerSource PointerOwner::update(const PointerActivity& activity, std::uint64_t now_us) noexcept {
    const auto active = [&activity](PointerSource source) {
        switch (source) {
        case PointerSource::Mouse: return activity.mouse;
        case PointerSource::Keys: return activity.keys;
        case PointerSource::Stick: return activity.stick;
        case PointerSource::Gyro: return activity.gyro;
        case PointerSource::None: break;
        }
        return false;
    };
    if (owner != PointerSource::None && active(owner)) {
        last_active_us = now_us;
        return owner;
    }
    const bool owner_still = owner == PointerSource::None || now_us < last_active_us ||
                             now_us - last_active_us >= kPointerHandoffUs;
    if (!owner_still) {
        return owner;
    }
    for (const PointerSource source :
         {PointerSource::Stick, PointerSource::Gyro, PointerSource::Keys, PointerSource::Mouse}) {
        if (active(source)) {
            owner = source;
            last_active_us = now_us;
            break;
        }
    }
    return owner;
}

const char* pointer_source_name(PointerSource source) noexcept {
    switch (source) {
    case PointerSource::Mouse: return "mouse";
    case PointerSource::Keys: return "keys";
    case PointerSource::Stick: return "stick";
    case PointerSource::Gyro: return "gyro";
    case PointerSource::None: break;
    }
    return "none";
}

StickVector apply_radial_deadzone(float x, float y, float deadzone) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return {};
    }
    const float magnitude = std::sqrt(x * x + y * y);
    deadzone = std::clamp(std::isfinite(deadzone) ? deadzone : 0.0f, 0.0f, 0.9f);
    if (magnitude <= deadzone || magnitude <= 0.0f) {
        return {};
    }
    const float scaled = std::min((magnitude - deadzone) / (1.0f - deadzone), 1.0f);
    return {x / magnitude * scaled, y / magnitude * scaled};
}

StickVector stick_pointer_velocity(StickVector stick, float speed) noexcept {
    const float magnitude = std::sqrt(stick.x * stick.x + stick.y * stick.y);
    if (!(magnitude > 0.0f) || !std::isfinite(speed)) {
        return {};
    }
    const float s = std::min(magnitude, 1.0f);
    const float response = 0.3f * s + 0.7f * s * s * s;
    const float scale = response * speed / magnitude;
    return {stick.x * scale, stick.y * scale};
}

PlayContext play_context_from_star_pointer_mode(std::uint32_t mode) noexcept {
    switch (mode) {
    case 15:  // 1P invalid, 2P valid (Co-Star)
    case 18:  // launch star
    case 20:  // blue star (pull star)
    case 21:  // blue star ready
    case 22:  // star bit target
    case 23:  // big bubble
    case 25:  // game
        return PlayContext::Play;
    default:
        return PlayContext::Menu;
    }
}

JumpBackLatch::Output JumpBackLatch::update(bool pressed, PlayContext context) noexcept {
    if (pressed && !held) {
        as_a = context == PlayContext::Play;
    }
    held = pressed;
    Output out{};
    if (pressed) {
        (as_a ? out.a : out.b) = true;
    }
    return out;
}

}  // namespace galaxy::input
