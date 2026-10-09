// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// CPU preparation only. Every changed draw still receives a complete immutable
// constant-ring allocation; this cache never owns or patches GPU upload memory.
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/line_point_raster.h"
#include <algorithm>
#include <array>
#include <bit>
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

    // GX NDC z range is [-1 (near), 0 (far)].  We map it to D3D [0, 1]
    // exactly the way Dolphin does (VertexShaderGen.cpp): NEGATE the GX
    // clip z → ndc_d3d = -ndc_gx ∈ [0 (far), 1 (near)] — a REVERSED-Z
    // buffer.  Two reasons this is the only robust scheme:
    //   1. The GX far plane lands at ndc 0, AWAY from the clip boundary.
    //      (The boot-134 z_gx+1 mapping put far at exactly ndc 1.0, where
    //      float error clipped SMG's far-plane depth-clear quad and the
    //      skybox — [vtx-clip] showed ndc 1.00033..1.00078.)
    //   2. It composes with the XF viewport zRange/farZ depth range and
    //      the FLIPPED depth compare funcs (see pipeline_cache) to give
    //      hardware-exact pass/fail behavior.
    // Dolphin's (1 - 1e-7) epsilon keeps exactly-on-far geometry inside
    // the clip volume (their "Sonic" hack; we keep DepthClipEnable=TRUE).
    constexpr float kFarEps = 1.0f - 1e-7f;
    if (type == 0) {
        // Perspective.  GX: clip z = z*E + F, clip w = -z.
        // z_d3d_clip = -(z*E + F) * (1-1e-7).
        out[0][0] = A;
        out[0][2] = B;    // column 2 of row 0 (pre-divide shear)
        out[1][1] = C;
        out[1][2] = D;
        out[2][2] = -E * kFarEps;
        out[2][3] = -F * kFarEps;
        out[3][2] = -1.0f;
    } else {
        // Orthographic.  GX: clip z = z*E + F, clip w = 1.  Same negation.
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

// Fill GxVsConstants for the current GxState.
// `palette_snapshot_index` is the index of this frame's XF matrix upload
// within the matrix ring (written separately by GxBackend).
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

    // Per-vertex position matrix index flag: if VCD_LO bit 0 is set.
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

    // Ambient and material colors (RGBA8 packed → 0..1 float[4]).
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

    // Default matrix indices (CP MatrixIndexA/B) — resolved per texgen in the
    // VS when the vertex stream carries no per-vertex index (Dolphin resolves
    // the same registers into I_TEXMATRICES on the CPU instead).
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

struct VsConstantsCpuCache {
    GxVsConstants constants{};
    unsigned efb_scale = 0u;
    bool valid = false;

    const GxVsConstants& update(const GxState& state, std::uint32_t dirty,
        unsigned current_scale, bool frame_bindings_dirty) {
        // Exact matrix-only admission keeps all invariant CP/XF/BP-derived
        // fields from the last complete image. Other dirty combinations and
        // frame/scale changes take the unchanged full preparation.
        if (valid && !frame_bindings_dirty && efb_scale == current_scale &&
            dirty == GxState::kDirtyXfMatrices) {
            refresh_vs_inline_matrices(state, constants);
        } else {
            fill_vs_constants(state, 0u, current_scale, constants);
            efb_scale = current_scale;
            valid = true;
        }
        return constants;
    }
};
}  // namespace galaxy::gx
