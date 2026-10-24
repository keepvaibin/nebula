// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game's actor lights under ray tracing.
//
// Characters and most lit objects take GX lights 0 and 1 from an ActorLightInfo
// (LightFunction::loadActorLightInfo: LightInfo A and B; loadLightInfoCoin: A only). A light with the
// follow-camera flag is loaded at a fixed eye-space position, so it turns with the camera; one without it sits at
// a world position. With ray tracing on and GALAXY_RT_ACTOR_LIGHTS set, the scene draws' palette snapshots move
// the follow-camera lights to the scene's key light: light 0 towards it and light 1 opposite. The key is the
// game's visible sun, else the sun its sky shows, else the world-fixed light most lit draws take; the ray tracer
// lights the scene from the same key. Colours, attenuation (always 1: the loader's
// GXInitLightAttn(1, 0, 0, 1, 0, 0)), ambient and world-fixed lights stay the game's. The interpolated sub-frames
// move a light by its world position, which for a light along a world direction is that direction at every camera.
//
// No Windows or D3D12 includes, so unit tests can use it.

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
// A world-fixed light this far from the camera (eye units) or farther counts as a directional key light
// (GxBackend::note_rt_world_key).
inline constexpr float kActorLightFar = 20000.0f;
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

// Where one scene's follow-camera lights go, in the eye space of its camera: towards the key light
// (`key` its unit direction); `blend` of the way from the game's positions (a key that comes or
// goes moves the lights over a few scenes).
struct ActorLightAim {
    bool valid = false;
    std::array<float, 3> key{};
    float blend = 1.0f;
};
// Scenes a key takes to come or go.
inline constexpr float kActorLightRampScenes = 16.0f;

// The aim at a key light's world direction (towards it) under a world-to-eye camera.
[[nodiscard]] inline ActorLightAim aim_at_key(const Affine34& view, const std::array<float, 3>& key) noexcept {
    ActorLightAim aim{};
    float d[3]{};
    for (unsigned r = 0; r < 3u; ++r) {
        d[r] = view[r * 4u] * key[0] + view[r * 4u + 1u] * key[1] + view[r * 4u + 2u] * key[2];
    }
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(length > 1e-6f) || !std::isfinite(length)) {
        return aim;
    }
    aim.valid = true;
    aim.key = {d[0] / length, d[1] / length, d[2] / length};
    return aim;
}

// Writes the positions of the lights in `key` (towards the aim) and `fill` (opposite) into a palette
// snapshot (XF words 0-0x67F; written, never read). `game`: the XF words the game loaded. While the
// aim's blend is below 1 a light turns from the game's direction (normalized lerp) and its distance
// goes from the game's to kActorLightDistance: the game's lights stand 1e5 out, so a lerp of the
// positions would turn nearly all the way in the first scenes.
inline void aim_actor_lights(std::uint32_t* palette, const std::uint32_t* game, std::uint8_t key, std::uint8_t fill,
                             const ActorLightAim& aim) noexcept {
    for (unsigned n = 0; n < 8u; ++n) {
        const bool towards = (key & (1u << n)) != 0u;
        if (!towards && (fill & (1u << n)) == 0u) {
            continue;
        }
        const unsigned word = kXfLightBase + kXfLightWords * n + kXfLightPosition;
        const float sign = towards ? 1.0f : -1.0f;
        float d[3]{aim.key[0] * sign, aim.key[1] * sign, aim.key[2] * sign};
        float distance = kActorLightDistance;
        if (aim.blend < 1.0f) {
            float from[3]{};
            float from_length = 0.0f;
            for (unsigned i = 0; i < 3u; ++i) {
                from[i] = std::bit_cast<float>(game[word + i]);
                from_length += from[i] * from[i];
            }
            from_length = std::sqrt(from_length);
            const float inverse = from_length > 1e-3f && std::isfinite(from_length) ? 1.0f / from_length : 0.0f;
            float length = 0.0f;
            for (unsigned i = 0; i < 3u; ++i) {
                d[i] = from[i] * inverse * (1.0f - aim.blend) + d[i] * aim.blend;
                length += d[i] * d[i];
            }
            length = std::sqrt(length);
            for (unsigned i = 0; i < 3u; ++i) {
                d[i] = length > 1e-3f ? d[i] / length : aim.key[i] * sign;
            }
            if (inverse > 0.0f) {
                distance = from_length + (kActorLightDistance - from_length) * aim.blend;
            }
        }
        for (unsigned i = 0; i < 3u; ++i) {
            palette[word + i] = std::bit_cast<std::uint32_t>(d[i] * distance);
        }
    }
}

}  // namespace galaxy::gx
