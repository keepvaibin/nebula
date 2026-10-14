// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game's actor lights under ray tracing.
//
// Characters and most lit objects take GX lights 0 and 1 from an ActorLightInfo
// (LightFunction::loadActorLightInfo: LightInfo A and B; loadLightInfoCoin: A only). A light with
// the follow-camera flag is loaded at a fixed eye-space position, so it turns with the camera. With
// ray tracing on, the scene draws' palette snapshots move those lights to where the traced scene's
// light comes from: light 0 towards the sun and light 1 opposite it, or without a sun light 0 at
// the strongest point light and light 1 opposite it at the draw's object. Colours, attenuation
// (always 1: the loader's GXInitLightAttn(1, 0, 0, 1, 0, 0)) and ambient stay the game's, and so do
// world-fixed lights. The interpolated sub-frames move a light by its world position, which for a
// light along a world direction is that direction at every camera.
//
// No Windows/D3D12 includes (unit-tested offline).

#include "galaxy/gx/frame_interpolation.h"
#include "galaxy/motion_signals.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace galaxy::gx {

// Distance (eye units) of a light along a direction: within a degree of it for objects up to
// 175000 units from the camera.
inline constexpr float kActorLightDistance = 1.0e7f;
// XF words of one light: colour 3, position 10-12.
inline constexpr unsigned kXfLightWords = 16u;
inline constexpr unsigned kXfLightPosition = 10u;

// One LightLoad host marker (aux: motion::LightSite, b: follow-camera bit per LightInfo): the GX
// lights the load writes and those of them that follow the camera.
struct ActorLightLoad {
    std::uint8_t written = 0;
    std::uint8_t follow = 0;
};

[[nodiscard]] constexpr ActorLightLoad actor_light_load(std::uint16_t site, std::uint32_t follow) noexcept {
    using motion::LightSite;
    switch (static_cast<LightSite>(site)) {
    case LightSite::ActorLightInfo:  // A, B and the alpha light 2
        return {0x07u, static_cast<std::uint8_t>(follow & 0x03u)};
    case LightSite::Coin:  // A and the specular light 3
        return {0x09u, static_cast<std::uint8_t>(follow & 0x01u)};
    case LightSite::PointLight:
        return {0x10u, 0u};
    case LightSite::AllWhite:
        return {0xFFu, 0u};
    default:  // Player: its ActorLightInfo and point light loads carry their own markers
        return {0u, 0u};
    }
}

// Where one scene's follow-camera lights go, in the eye space of its camera: towards the sun
// (`key` its unit direction) or at a point light (`key` its position).
struct ActorLightAim {
    bool valid = false;
    bool point = false;
    std::array<float, 3> key{};
};

[[nodiscard]] inline ActorLightAim aim_at_sun(const Affine34& view, const std::array<float, 3>& sun) noexcept {
    ActorLightAim aim{};
    float d[3]{};
    for (unsigned r = 0; r < 3u; ++r) {
        d[r] = view[r * 4u] * sun[0] + view[r * 4u + 1u] * sun[1] + view[r * 4u + 2u] * sun[2];
    }
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(length > 1e-6f) || !std::isfinite(length)) {
        return aim;
    }
    aim.valid = true;
    aim.key = {d[0] / length, d[1] / length, d[2] / length};
    return aim;
}

[[nodiscard]] inline ActorLightAim aim_at_point(const Affine34& view, const std::array<float, 3>& position) noexcept {
    ActorLightAim aim{};
    float p[3]{};
    affine_transform_point(view, position.data(), p);
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) {
        return aim;
    }
    aim.valid = true;
    aim.point = true;
    aim.key = {p[0], p[1], p[2]};
    return aim;
}

// The ray tracer's weight of a point light (world position, colour 0-1) seen from the camera:
// its luminance over 1 + (distance / range)^2.
[[nodiscard]] inline float point_light_weight(const Affine34& view, const std::array<float, 3>& position,
                                              const std::array<float, 3>& color, float range) noexcept {
    float e[3]{};
    affine_transform_point(view, position.data(), e);
    const float distance2 = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    return (0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2]) / (1.0f + distance2 / (range * range));
}

// Writes the positions of the lights in `key` (towards the aim) and `fill` (opposite) into a palette
// snapshot (XF words 0-0x67F; written, never read). `origin`: the draw's object in eye space, where a
// point light's fill is opposite it.
inline void aim_actor_lights(std::uint32_t* palette, std::uint8_t key, std::uint8_t fill, const ActorLightAim& aim,
                             const float origin[3]) noexcept {
    float to_key[3]{aim.key[0], aim.key[1], aim.key[2]};
    if (aim.point) {
        float length = 0.0f;
        for (unsigned i = 0; i < 3u; ++i) {
            to_key[i] = aim.key[i] - origin[i];
            length += to_key[i] * to_key[i];
        }
        length = std::sqrt(length);
        if (!(length > 1e-3f) || !std::isfinite(length)) {
            fill = 0u;
            length = 1.0f;
        }
        for (float& v : to_key) v /= length;
    }
    for (unsigned n = 0; n < 8u; ++n) {
        const bool towards = (key & (1u << n)) != 0u;
        if (!towards && (fill & (1u << n)) == 0u) {
            continue;
        }
        std::uint32_t* position = palette + kXfLightBase + kXfLightWords * n + kXfLightPosition;
        for (unsigned i = 0; i < 3u; ++i) {
            const float along = to_key[i] * kActorLightDistance;
            const float at = aim.point ? (towards ? aim.key[i] : origin[i] - along) : (towards ? along : -along);
            position[i] = std::bit_cast<std::uint32_t>(at);
        }
    }
}

}  // namespace galaxy::gx
