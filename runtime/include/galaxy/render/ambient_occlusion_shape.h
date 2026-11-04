// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What both renderers' ambient occlusion share: the shaders' constants from the scene's view, and the occlusion
// pixel's size. ambient_occlusion.hlsl has the method.

#include "galaxy/render/rt_types.h"

#include <algorithm>
#include <cstdint>

namespace galaxy::render::ao {

// The depth occlusion is for creases and contacts, in the game's units (Mario stands about 160 tall): a wider radius paints a
// dark copy of raised shapes over flat faces, and what stands over a surface is the models' (ambient_occlusion_objects.h).
inline constexpr float kRadius = 30.0f;
// How dark a crease goes: powers of each method's visibility, which reach their depth differently.
inline constexpr float kPower = 3.5f;
inline constexpr float kSsaoPower = 6.0f;
// The multiply on display values: the game's colours are authored for the display, so physically encoded occlusion (1 / 2.2)
// leaves half the light blocked only 27 % darker.
inline constexpr float kDisplayPower = 1.0f;
// Occlusion is soft: past this many lines more of them add cost and no visible detail. Below it, one occlusion pixel
// per scene pixel, so a small window keeps all it can show.
inline constexpr unsigned kFullLines = 720u;

struct Constants {
    float ray[4];
    float depth[4];
    float viewport[4];
    float params[4];
    std::uint32_t size[4];
};
static_assert(sizeof(Constants) == 80u);

// Scene target pixels per occlusion pixel, across and down.
[[nodiscard]] inline unsigned occlusion_divisor(unsigned scene_height) noexcept {
    return std::max(scene_height / kFullLines, 1u);
}

// The shaders' constants: false for a view with no picture.
[[nodiscard]] inline bool make_constants(const RtView& view, unsigned width, unsigned height, unsigned divisor, bool ssao,
                                         Constants& c) noexcept {
    // A scene target pixel centre's eye ray (z = -1): NDC from the viewport, then through the projection.
    const float a = view.projection[0];
    const float b = view.projection[1];
    const float cy = view.projection[2];
    const float d = view.projection[3];
    const float vx = view.viewport[0];
    const float vy = view.viewport[1];
    const float vw = view.viewport[2];
    const float vh = view.viewport[3];
    if (a == 0.0f || cy == 0.0f || vw <= 0.0f || vh <= 0.0f) {
        return false;
    }
    c.ray[0] = 2.0f / (vw * a);
    c.ray[1] = (-2.0f * vx / vw - 1.0f + b) / a;
    c.ray[2] = -2.0f / (vh * cy);
    c.ray[3] = (1.0f + 2.0f * vy / vh + d) / cy;
    for (unsigned i = 0; i < 4u; ++i) {
        c.viewport[i] = view.viewport[i];
    }
    c.depth[0] = view.z_scale;
    c.depth[1] = view.z_offset;
    c.depth[2] = view.min_depth;
    c.depth[3] = view.max_depth;
    c.params[0] = kRadius;
    c.params[1] = ssao ? kSsaoPower : kPower;
    c.params[2] = kDisplayPower;
    c.params[3] = std::max(8.0f, 0.12f * vh / static_cast<float>(divisor));
    c.size[0] = width;
    c.size[1] = height;
    c.size[2] = divisor;
    c.size[3] = 0u;
    return true;
}

}  // namespace galaxy::render::ao
