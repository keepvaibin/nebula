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
        case PointerSource::Touch: return activity.touch;
        case PointerSource::None: break;
        }
        return false;
    };
    if (activity.touch) {
        owner = PointerSource::Touch;
        last_active_us = now_us;
        return owner;
    }
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
    case PointerSource::Touch: return "touch";
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
                             bool reveal, bool latch, float hide_seconds, float radius) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return hidden;
    }
    const bool active = controller_owned && hide_seconds > 0.0f;
    if (!active || !anchored || reveal || now_us < last_us) {
        filtered_x = anchor_x = x;
        filtered_y = anchor_y = y;
        since_us = now_us;
        last_us = now_us;
        anchored = active;
        hidden = false;
        return false;
    }
    if (hidden && latch) {
        last_us = now_us;
        return true;
    }
    const float dt = static_cast<float>(now_us - last_us) * 1e-6f;
    last_us = now_us;
    const float blend = 1.0f - std::exp(-std::min(dt, 1.0f) / kPointerHideFilterSeconds);
    filtered_x += (x - filtered_x) * blend;
    filtered_y += (y - filtered_y) * blend;
    const float dx = filtered_x - anchor_x;
    const float dy = filtered_y - anchor_y;
    const float r = std::isfinite(radius) ? std::max(radius, 0.0f) : kDefaultPointerHideRadius;
    if (dx * dx + dy * dy > r * r) {
        // A deliberate move: the anchor follows it and the pointer shows.
        anchor_x = filtered_x;
        anchor_y = filtered_y;
        since_us = now_us;
        hidden = false;
        return false;
    }
    if (!hidden &&
        static_cast<double>(now_us - since_us) >= static_cast<double>(hide_seconds) * 1e6) {
        hidden = true;
    }
    return hidden;
}

bool MouseRestHide::update(bool mouse_owned, bool mouse_active, std::uint64_t now_us) noexcept {
    if (!mouse_owned || mouse_active || since_us == 0u || now_us < since_us) {
        since_us = now_us;
        return false;
    }
    return static_cast<double>(now_us - since_us) >= static_cast<double>(kMouseHideSeconds) * 1e6;
}

RecenterHold::Output RecenterHold::update(bool down) noexcept {
    Output out{};
    out.pressed = down && !held;
    out.released = !down && held;
    out.pinned = down;
    held = down;
    return out;
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
