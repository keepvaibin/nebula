// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// VertexConstants filled from the drawing state on the CPU, and a cache of the last fill. Every changed draw still gets a
// complete constant-ring allocation; nothing here touches GPU upload memory.
#include "galaxy/render/drawing_state.h"
#include "galaxy/render/draw_matrices.h"
#include "galaxy/render/draw_constants.h"
#include "galaxy/render/line_point_params.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace galaxy::render {
inline void fill_vs_projection(const NativeDrawState& draw, float out[4][4]) {
    std::memset(out, 0, sizeof(float) * 16);

    const float A = draw.projection[0];
    const float B = draw.projection[1];
    const float C = draw.projection[2];
    const float D = draw.projection[3];
    const float E = draw.projection[4];
    const float F = draw.projection[5];
    const std::uint32_t type = draw.projection_type;

    // NDC z runs from -1 (near) to 0 (far). Negating the clip z gives D3D's [0, 1] reversed (far 0, near 1),
    //  the viewport depth range and the flipped depth compares
    // (pipeline_cache) complete the mapping. With far at 1.0, float error clips SMG's far-plane depth-clear quad
    // and the skybox. The (1 - 1e-7) factor keeps far-plane geometry inside the clip volume with
    // DepthClipEnable on.
    constexpr float kFarEps = 1.0f - 1e-7f;
    if (type == 0) {
        // Perspective: clip z = z*E + F, clip w = -z. D3D clip z = -(z*E + F) * (1 - 1e-7).
        out[0][0] = A;
        out[0][2] = B;    // column 2 of row 0 (pre-divide shear)
        out[1][1] = C;
        out[1][2] = D;
        out[2][2] = -E * kFarEps;
        out[2][3] = -F * kFarEps;
        out[3][2] = -1.0f;
    } else {
        // Orthographic: clip z = z*E + F, clip w = 1. Same negation.
        out[0][0] = A;
        out[0][3] = B;
        out[1][1] = C;
        out[1][3] = D;
        out[2][2] = -E * kFarEps;
        out[2][3] = -F * kFarEps;
        out[3][3] = 1.0f;
    }
}

// Fills VertexConstants for the current drawing state (the blocks' bases RenderCore sets).
inline void fill_vs_constants(
    const NativeDrawState& draw,
    RasterScale scale,
    VertexConstants& out) {
    std::memset(&out, 0, sizeof(out));

    fill_vs_projection(draw, out.projection);

    const DrawCounts gm = draw.gen;
    out.num_texgens = gm.num_texgens;
    out.num_chans   = gm.num_color_chans;


    // Lighting: any channel has lighting_enable.
    bool lighting = false;
    for (unsigned i = 0; i < gm.num_color_chans; ++i) {
        if (draw.channel[i].lighting_enable || draw.channel[2u + i].lighting_enable) {
            lighting = true;
            break;
        }
    }
    // Bit 0 (the shape's vertices carry their own position matrix index) is merged in by VsConstantsCpuCache::update
    // (per_vertex, RenderCore's): native_material.hlsli native_vs reads it.
    out.flags  = lighting ? 2u : 0u;

    // Previous rows: the draw's own (no motion of its own) unless a motion scene names other position rows.
    out.motion_ctrl[1] = kMotionOwnRows;

    // The draw's matrix rows (draw_rows: position, then each texture coordinate's), for vertices without their own.
    out.draw_rows[0][0] = 3u * draw.position_matrix;  // a slot's first row: three rows a matrix
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        out.draw_rows[(i + 1u) / 4u][(i + 1u) % 4u] = 3u * draw.texture_matrix[i];
    }

    // A particle corner's time (native_material.hlsli NATIVE_PARTICLE): now, and a game frame ago for its motion vector.
    out.draw_rows[2][1] = std::bit_cast<std::uint32_t>(1.0f);
    out.draw_rows[2][2] = 0u;

    LinePointValues values{draw.line_point.line_width, draw.line_point.point_size, draw.line_point.line_divisor,
                           draw.line_point.point_divisor, 0u, 0u};
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        values.line_coords = static_cast<std::uint8_t>(values.line_coords | (draw.coord[i].line_offset ? 1u : 0u) << i);
        values.point_coords = static_cast<std::uint8_t>(values.point_coords | (draw.coord[i].point_offset ? 1u : 0u) << i);
    }
    const LinePointRasterParams line_point =
        make_line_point_raster_params(values, draw.viewport[2], draw.viewport[3], scale);
    out.line_point_raster[0] = line_point.viewport_width_pixels;
    out.line_point_raster[1] = line_point.viewport_height_pixels;
    out.line_point_raster[2] = line_point.line_width_pixels;
    out.line_point_raster[3] = line_point.point_size_pixels;
    out.line_point_tex_offsets[0] = line_point.line_texcoord_mask;
    out.line_point_tex_offsets[1] = line_point.point_texcoord_mask;
    out.line_point_tex_offsets[2] = line_point.line_texcoord_divisor;
    out.line_point_tex_offsets[3] = line_point.point_texcoord_divisor;
}

// A unit vector as a point of the octahedral map, x and y as snorm16 in the low and high halves
// (VertexConstants::sky_up; the enhanced pixel shader's enh_up decodes it).
[[nodiscard]] inline std::uint32_t encode_octahedral(const float v[3]) noexcept {
    const float sum = std::fabs(v[0]) + std::fabs(v[1]) + std::fabs(v[2]);
    if (!(sum > 0.0f)) {
        return 0u;
    }
    float x = v[0] / sum;
    float y = v[1] / sum;
    if (v[2] < 0.0f) {
        const float ox = x;
        x = (1.0f - std::fabs(y)) * (ox >= 0.0f ? 1.0f : -1.0f);
        y = (1.0f - std::fabs(ox)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    const auto snorm = [](float value) {
        return static_cast<std::uint32_t>(static_cast<std::uint16_t>(
            static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f))));
    };
    return snorm(x) | (snorm(y) << 16u);
}

// An octahedral word (encode_octahedral) back to a unit vector.
inline void decode_octahedral(std::uint32_t word, float out[3]) noexcept {
    const float x = static_cast<float>(static_cast<std::int16_t>(word & 0xFFFFu)) / 32767.0f;
    const float y = static_cast<float>(static_cast<std::int16_t>(word >> 16u)) / 32767.0f;
    float v[3] = {x, y, 1.0f - std::fabs(x) - std::fabs(y)};
    if (v[2] < 0.0f) {
        v[0] = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        v[1] = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (unsigned i = 0; i < 3u; ++i) {
        out[i] = length > 0.0f ? v[i] / length : (i == 1u ? 1.0f : 0.0f);
    }
}

struct VsConstantsCpuCache {
    VertexConstants constants{};
    RasterScale raster_scale{0.0f, 0.0f};
    bool valid = false;
    // The scene's sky up (VertexConstants::sky_up, sky_up_known), kept across fills.
    std::uint32_t sky_up = 0u;
    std::uint32_t sky_up_known = 0u;
    // The draw's matrix blocks (VertexConstants::pos_base, row_bases), RenderCore's.
    std::uint32_t pos_base = 0u;
    std::uint32_t row_bases[4]{};
    // VertexConstants::flags bit 0: the draw's vertices carry position matrix indices (the shape's attributes, the
    // source of VertexShaderKey::flags bit 0), RenderCore's.
    std::uint32_t per_vertex = 0u;
    // The draw's position matrix's first row in a model's block, or kOwnPlace: its place's.
    static constexpr std::uint32_t kOwnPlace = 0xFFFFFFFFu;
    std::uint32_t position_row = kOwnPlace;

    const VertexConstants& update(const NativeDrawState& draw, std::uint32_t dirty,
        RasterScale current_scale, bool frame_bindings_dirty) {
        // When only the matrices changed the rest of the last full fill stands (their rows are in their blocks);
        // any other change, dirty frame bindings or a new scale refill everything.
        if (!valid || frame_bindings_dirty || raster_scale.x != current_scale.x || raster_scale.y != current_scale.y ||
            dirty != kStateDirtyMatrices) {
            fill_vs_constants(draw, current_scale, constants);
            raster_scale = current_scale;
            valid = true;
        }
        constants.sky_up = sky_up;
        constants.sky_up_known = sky_up_known;
        constants.flags = (constants.flags & 0x2u) | (per_vertex & 1u);
        if (position_row != kOwnPlace) {
            constants.draw_rows[0][0] = position_row;
        } else {
            constants.draw_rows[0][0] = 3u * draw.position_matrix;
        }
        constants.pos_base = pos_base;
        std::memcpy(constants.row_bases, row_bases, sizeof(row_bases));
        return constants;
    }
};
}  // namespace galaxy::render
