// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// VertexConstants filled from the drawing state on the CPU, and a cache of the last fill. Every changed draw still gets a
// complete constant-ring allocation; nothing here touches GPU upload memory.
#include "galaxy/render/draw_state.h"
#include "galaxy/render/draw_matrices.h"
#include "galaxy/render/uber_constants.h"
#include "galaxy/render/line_point_raster.h"
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

    // GX NDC z runs from -1 (near) to 0 (far). Negating the GX clip z gives D3D's [0, 1] reversed (far 0, near 1),
    // as Dolphin's VertexShaderGen.cpp does; the XF viewport depth range and the flipped depth compares
    // (pipeline_cache) complete the mapping. With far at 1.0, float error clips SMG's far-plane depth-clear quad
    // and the skybox. Dolphin's (1 - 1e-7) factor keeps far-plane geometry inside the clip volume with
    // DepthClipEnable on.
    constexpr float kFarEps = 1.0f - 1e-7f;
    if (type == 0) {
        // Perspective: GX clip z = z*E + F, clip w = -z. D3D clip z = -(z*E + F) * (1 - 1e-7).
        out[0][0] = A;
        out[0][2] = B;    // column 2 of row 0 (pre-divide shear)
        out[1][1] = C;
        out[1][2] = D;
        out[2][2] = -E * kFarEps;
        out[2][3] = -F * kFarEps;
        out[3][2] = -1.0f;
    } else {
        // Orthographic: GX clip z = z*E + F, clip w = 1. Same negation.
        out[0][0] = A;
        out[0][3] = B;
        out[1][1] = C;
        out[1][3] = D;
        out[2][2] = -E * kFarEps;
        out[2][3] = -F * kFarEps;
        out[3][3] = 1.0f;
    }
}

inline void refresh_vs_inline_matrices(const DrawMatrices& state, const NativeDrawState& draw, VertexConstants& out) {
    const auto copy_matrix = [&state](
                                 unsigned row,
                                 float destination[3][4]) {
        row = std::min(row, 61u);
        for (unsigned r = 0; r < 3; ++r) {
            for (unsigned c = 0; c < 4; ++c) {
                destination[r][c] = state.matrix_value(row + r, c);
            }
        }
    };

    copy_matrix(out.matrix_index_a & 0x3Fu, out.inline_pos_matrix);
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        const unsigned shift = i < 4u ? 6u + 6u * i : 6u * (i - 4u);
        const std::uint32_t indices =
            i < 4u ? out.matrix_index_a : out.matrix_index_b;
        copy_matrix(
            (indices >> shift) & 0x3Fu,
            out.inline_tex_matrices[i]);

        const XfPostTexGen post = draw.post_texgen[i];
        const unsigned post_row = post.matrix_index & 0x3Fu;
        for (unsigned r = 0; r < 3; ++r) {
            for (unsigned c = 0; c < 4; ++c) {
                out.inline_post_matrices[i][r][c] =
                    std::bit_cast<float>(state.row(draw_rows::kPost + ((post_row + r) & 0x3Fu))[c]);
            }
        }
    }

}

// Fills VertexConstants for the current drawing state (inline matrices from the draw's matrices; the blocks' bases
// RenderCore sets).
inline void fill_vs_constants(
    const DrawMatrices& state,
    const NativeDrawState& draw,
    unsigned efb_scale,
    VertexConstants& out) {
    std::memset(&out, 0, sizeof(out));

    fill_vs_projection(draw, out.projection);

    const GenMode gm = draw.gen;
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
    // Bit 0 (a per-vertex position matrix index) stays clear: the vertex shader key carries the vertices' matrix
    // indices (VertexShaderKey::flags).
    out.flags  = lighting ? 2u : 0u;

    // Texgen config words (each coordinate's generation and post-transform, texgen_word and post_texgen_word).
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        out.texgen_config[i][0] = texgen_word(draw.texgen[i]);
        out.texgen_config[i][1] = post_texgen_word(draw.post_texgen[i]);
    }

    // Ambient and material colours as 0..1 float[4].
    for (unsigned i = 0; i < kMaxColorChannels; ++i) {
        for (unsigned c = 0; c < 4u; ++c) {
            out.chan_ambient[i][c] = draw.ambient_color[i][c] / 255.0f;
            out.chan_material[i][c] = draw.material_color[i][c] / 255.0f;
        }
    }

    // Previous rows: the draw's own (no motion of its own) unless a motion scene names other position rows.
    out.motion_ctrl[1] = kMotionOwnRows;

    // Default matrix indices (CP MatrixIndexA/B). The VS resolves them per texgen when the
    // vertex stream has no per-vertex index. Dolphin resolves the same registers on the CPU
    // into I_TEXMATRICES.
    out.matrix_index_a = matrix_index_a(draw);
    out.matrix_index_b = matrix_index_b(draw);

    refresh_vs_inline_matrices(state, draw, out);

    std::array<std::uint32_t, kMaxTexGens> texcoord_s_registers{};
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        texcoord_s_registers[i] = coord_s_word(draw, i);
    }
    const float viewport_half_width = draw.viewport[0];
    const float viewport_half_height = draw.viewport[1];
    const LinePointRasterParams line_point = make_line_point_raster_params(
        line_point_word(draw),
        texcoord_s_registers,
        viewport_half_width,
        viewport_half_height,
        efb_scale,
        640u,
        528u);
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
    unsigned efb_scale = 0u;
    bool valid = false;
    // The scene's sky up (VertexConstants::sky_up, sky_up_known), kept across fills.
    std::uint32_t sky_up = 0u;
    std::uint32_t sky_up_known = 0u;
    // VertexConstants::flags above bits 0-1: the ray tracer's de-bake controls (RenderCore), kept across fills.
    std::uint32_t debake = 0u;
    // VertexConstants::debake_sh, debake_peak (flags bit 6).
    float debake_sh[7][4]{};
    float debake_peak[4]{};
    // The draw's matrix blocks (VertexConstants::pos_base, row_bases), RenderCore's.
    std::uint32_t pos_base = 0u;
    std::uint32_t row_bases[4]{};

    const VertexConstants& update(const DrawMatrices& state, const NativeDrawState& draw, std::uint32_t dirty,
        unsigned current_scale, bool frame_bindings_dirty) {
        // When only XF matrices changed, refresh the inline matrices and keep the rest of the last full fill;
        // any other change, dirty frame bindings or a new scale refill everything.
        if (valid && !frame_bindings_dirty && efb_scale == current_scale &&
            dirty == kStateDirtyMatrices) {
            refresh_vs_inline_matrices(state, draw, constants);
        } else {
            fill_vs_constants(state, draw, current_scale, constants);
            efb_scale = current_scale;
            valid = true;
        }
        constants.sky_up = sky_up;
        constants.sky_up_known = sky_up_known;
        constants.flags = (constants.flags & 0x3u) | debake;
        std::memcpy(constants.debake_sh, debake_sh, sizeof(debake_sh));
        std::memcpy(constants.debake_peak, debake_peak, sizeof(debake_peak));
        constants.pos_base = pos_base;
        std::memcpy(constants.row_bases, row_bases, sizeof(row_bases));
        return constants;
    }
};
}  // namespace galaxy::render
