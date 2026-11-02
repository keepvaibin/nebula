// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The scene camera for depth reconstruction (ambient occlusion), without any graphics API.

#include <array>
#include <cstdint>

namespace galaxy::render {

// The scene camera, for rays through scene target pixels and depth reconstruction: projection A-D,
// the D3D projection's [2][2] and [2][3], the viewport in scene target pixels and its depth range.
struct RtView {
    std::array<float, 4> projection{};
    float z_scale = 0.0f;
    float z_offset = 0.0f;
    std::array<float, 4> viewport{};
    float min_depth = 0.0f;
    float max_depth = 1.0f;
    // The temporal upscaler's jitter in the projection (render pixels: x right, y down; 0 without it).
    std::array<float, 2> jitter{};
};

}  // namespace galaxy::render
