// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// GxVsConstants filled from GxState on the CPU, and a cache of the last fill. Every changed draw still gets a
// complete constant-ring allocation; nothing here touches GPU upload memory.
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/line_point_raster.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace galaxy::gx {
inline void fill_vs_projection(const GxState& state, float out[4][4]) {
    std::memset(out, 0, sizeof(float) * 16);

    const float A = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 0u)));
    const float B = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 1u)));
    const float C = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 2u)));
    const float D = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 3u)));
    const float E = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 4u)));
    const float F = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 5u)));
    const std::uint32_t type = state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u));

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

inline void refresh_vs_inline_matrices(const GxState& state, GxVsConstants& out) {
    const auto copy_matrix = [&state](
                                 unsigned row,
                                 float destination[3][4]) {
        row = std::min(row, 61u);
        for (unsigned r = 0; r < 3; ++r) {
            for (unsigned c = 0; c < 4; ++c) {
                destination[r][c] = std::bit_cast<float>(
                    state.xf(static_cast<std::uint16_t>(
                        (row + r) * 4u + c)));
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

        const XfPostTexGen post = state.post_tex_gen(i);
        const unsigned post_row = post.matrix_index & 0x3Fu;
        for (unsigned r = 0; r < 3; ++r) {
            for (unsigned c = 0; c < 4; ++c) {
                out.inline_post_matrices[i][r][c] =
                    std::bit_cast<float>(state.xf(
                        static_cast<std::uint16_t>(
                            xf::kPostMatricesBase +
                            (((post_row + r) & 0x3Fu) * 4u) + c)));
            }
        }
    }

}

// Fills GxVsConstants for the current GxState. `palette_snapshot_index` is the index of
// this frame's XF matrix upload in the matrix ring (written by GxBackend).
inline void fill_vs_constants(
    const GxState& state,
    std::uint32_t palette_snapshot_index,
    unsigned efb_scale,
    GxVsConstants& out) {
    std::memset(&out, 0, sizeof(out));

    fill_vs_projection(state, out.projection);

    const GenMode gm = state.gen_mode();
    out.num_texgens = gm.num_texgens;
    out.num_chans   = gm.num_color_chans;

    // Per-vertex position matrix index flag: VCD_LO bit 0.
    const std::uint32_t vcd_lo = state.cp(cp::kVcdLo);
    const bool per_vertex_posmtx = (vcd_lo & 1u) != 0;

    // Lighting: any channel has lighting_enable.
    bool lighting = false;
    for (unsigned i = 0; i < gm.num_color_chans; ++i) {
        if (state.channel_ctrl(i).lighting_enable ||
            state.channel_ctrl(2u + i).lighting_enable) {
            lighting = true;
            break;
        }
    }
    out.flags  = (per_vertex_posmtx ? 1u : 0u)
               | (lighting           ? 2u : 0u);

    // Texgen config words (pairs of raw XF registers per gen).
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        out.texgen_config[i][0] = state.xf(
            static_cast<std::uint16_t>(xf::kTexGenBase + i));
        out.texgen_config[i][1] = state.xf(
            static_cast<std::uint16_t>(xf::kPostTexGenBase + i));
    }

    // Ambient and material colors, RGBA8 packed, converted to 0..1 float[4].
    for (unsigned i = 0; i < kMaxColorChannels; ++i) {
        // XF 0x100A / 0x100B = ambient color0 / color1 (RGBA8 packed).
        const std::uint32_t amb_raw = state.xf(
            static_cast<std::uint16_t>(xf::kAmbientColorBase + i));
        out.chan_ambient[i][0] = ((amb_raw >> 24) & 0xFFu) / 255.0f;
        out.chan_ambient[i][1] = ((amb_raw >> 16) & 0xFFu) / 255.0f;
        out.chan_ambient[i][2] = ((amb_raw >>  8) & 0xFFu) / 255.0f;
        out.chan_ambient[i][3] = ((amb_raw      ) & 0xFFu) / 255.0f;

        const std::uint32_t mat_raw = state.xf(
            static_cast<std::uint16_t>(xf::kMaterialColorBase + i));
        out.chan_material[i][0] = ((mat_raw >> 24) & 0xFFu) / 255.0f;
        out.chan_material[i][1] = ((mat_raw >> 16) & 0xFFu) / 255.0f;
        out.chan_material[i][2] = ((mat_raw >>  8) & 0xFFu) / 255.0f;
        out.chan_material[i][3] = ((mat_raw      ) & 0xFFu) / 255.0f;
    }

    out.mtx_palette_base = palette_snapshot_index;

    // Default matrix indices (CP MatrixIndexA/B). The VS resolves them per texgen when the
    // vertex stream has no per-vertex index. Dolphin resolves the same registers on the CPU
    // into I_TEXMATRICES.
    out.matrix_index_a = state.cp(cp::kMatrixIndexA);
    out.matrix_index_b = state.cp(cp::kMatrixIndexB);

    refresh_vs_inline_matrices(state, out);

    std::array<std::uint32_t, kMaxTexGens> texcoord_s_registers{};
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        texcoord_s_registers[i] = state.bp(static_cast<std::uint8_t>(
            bp::kTexCoordSizeBase + i * 2u));
    }
    const float viewport_half_width = std::bit_cast<float>(
        state.xf(xf::kViewportBase));
    const float viewport_half_height = std::bit_cast<float>(
        state.xf(static_cast<std::uint16_t>(xf::kViewportBase + 1u)));
    const LinePointRasterParams line_point = make_line_point_raster_params(
        state.bp(bp::kSuLpSize),
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
// (GxVsConstants::sky_up; the enhanced pixel shader's enh_up decodes it).
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
    GxVsConstants constants{};
    unsigned efb_scale = 0u;
    bool valid = false;
    // The scene's sky up (GxVsConstants::sky_up, sky_up_known), kept across fills.
    std::uint32_t sky_up = 0u;
    std::uint32_t sky_up_known = 0u;
    // GxVsConstants::flags above bits 0-1: the ray tracer's de-bake controls (GxBackend), kept across fills.
    std::uint32_t debake = 0u;
    // GxVsConstants::debake_sh, debake_peak (flags bit 6).
    float debake_sh[7][4]{};
    float debake_peak[4]{};

    const GxVsConstants& update(const GxState& state, std::uint32_t dirty,
        unsigned current_scale, bool frame_bindings_dirty) {
        // When only XF matrices changed, refresh the inline matrices and keep the rest of the last full fill;
        // any other change, dirty frame bindings or a new scale refill everything.
        if (valid && !frame_bindings_dirty && efb_scale == current_scale &&
            dirty == GxState::kDirtyXfMatrices) {
            refresh_vs_inline_matrices(state, constants);
        } else {
            fill_vs_constants(state, 0u, current_scale, constants);
            efb_scale = current_scale;
            valid = true;
        }
        constants.sky_up = sky_up;
        constants.sky_up_known = sky_up_known;
        constants.flags = (constants.flags & 0x3u) | debake;
        std::memcpy(constants.debake_sh, debake_sh, sizeof(debake_sh));
        std::memcpy(constants.debake_peak, debake_peak, sizeof(debake_peak));
        return constants;
    }
};
}  // namespace galaxy::gx
