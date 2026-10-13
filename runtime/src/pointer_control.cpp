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

bool PointerRestHide::update(float x, float y, std::uint64_t now_us, bool controller_owned,
                             bool reveal, float hide_seconds, float radius) noexcept {
    const auto settle = [&] {
        anchor_x = x;
        anchor_y = y;
        since_us = now_us;
    };
    if (!controller_owned || reveal || !(hide_seconds > 0.0f)) {
        hidden = false;
        settle();
        return false;
    }
    if (hidden) {
        return true;
    }
    const float dx = x - anchor_x;
    const float dy = y - anchor_y;
    if (dx * dx + dy * dy > radius * radius || now_us < since_us) {
        settle();
        return false;
    }
    if (static_cast<double>(now_us - since_us) >= static_cast<double>(hide_seconds) * 1e6) {
        hidden = true;
    }
    return hidden;
}

PlayContext play_context_from_star_pointer_mode(std::uint32_t mode) noexcept {
    // Only screens with a Back action are menus; every other mode (game,
    // demos, base, star bit targets, launch stars, unknown values) keeps B
    // as jump, so B never shoots Star Bits.
    switch (mode) {
    case 0:   // HOME button menu
    case 1:   // error window
    case 2:   // pause menu
    case 3:   // logo
    case 4:   // title
    case 5:   // file select
    case 6:   // scenario (star) select
    case 7:   // pause menu, galaxy map
    case 8:   // yes/no choice
    case 9:   // picture book
    case 16:  // dome sphere selector (galaxy choice)
    case 17:  // dome sphere selector, finger
        return PlayContext::Menu;
    default:
        return PlayContext::Play;
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
