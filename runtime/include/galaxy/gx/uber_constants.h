#pragma once

// GPU-visible constant buffer layouts shared by the uber pipelines and every
// specialized shader.  The HLSL side mirrors these structs field-for-field;
// the static_asserts below are the single source of truth for offsets.
//
// Both shader families share the same layouts and root signature so that
// promoting a draw from the uber pipeline to a specialized PSO is a pure
// pipeline-pointer swap with no binding changes.  Specialized shaders simply
// ignore stage_config (their TEV program is baked into the code).
//
// No Windows/D3D12 includes.

#include <cstddef>
#include <cstdint>

namespace galaxy::gx {

inline constexpr unsigned kMaxTevStages = 16;
inline constexpr unsigned kMaxTextureMaps = 8;
inline constexpr unsigned kMaxTexGens = 8;
inline constexpr unsigned kMaxColorChannels = 2;

// alignas(256) matches the D3D12 CBV placement-alignment requirement; the
// trailing pad it introduces is intentional.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // structure padded due to alignment specifier
#endif

// Pixel-stage constants (root CBV b1).
struct alignas(256) GxPsConstants {
    // Bit-packed TevStagePacked words; consumed only by the uber pixel
    // shader's interpretation loop.
    std::uint32_t stage_config[kMaxTevStages][4];
    // TEV blending registers PREV, REG0-2 as signed values divided by 255.
    float tev_regs[4][4];
    // Konstant color bank K0-K3.
    float konst[4][4];
    // x = alpha ref0 / 255, y = alpha ref1 / 255,
    // z = packed comp0|comp1|logic (uber only), w = constant dst alpha.
    float alpha_refs[4];
    // a, b_magnitude, b_shift, c, color.rgb, proj flag.
    float fog_params[8];
    // Per-map texture width/height and GX texcoord S/T scale
    // (BP 0x30-0x3F scale_minus_1 + 1). Pixel shaders convert rasterized
    // texcoords through GX fixed-point space with zw, then normalize samples
    // by xy.
    float tex_dims[kMaxTextureMaps][4];
    // x = Z-texture bias (BP 0xF4, 24-bit) / 2^24; y,z,w reserved.
    float ztex_params[4];
    // Indirect TEV state: commands are raw BP 0x10-0x1F words grouped as
    // uint4s; matrices are decoded as six int4 rows where .w is 17 - scale;
    // ind_misc = BP 0x27, BP 0x25, BP 0x26, num_ind_stages.
    std::uint32_t ind_cmd[4][4];
    std::int32_t ind_mtx[6][4];
    std::uint32_t ind_misc[4];
};

static_assert(offsetof(GxPsConstants, stage_config) == 0);
static_assert(offsetof(GxPsConstants, tev_regs) == 256);
static_assert(offsetof(GxPsConstants, konst) == 320);
static_assert(offsetof(GxPsConstants, alpha_refs) == 384);
static_assert(offsetof(GxPsConstants, fog_params) == 400);
static_assert(offsetof(GxPsConstants, tex_dims) == 432);
static_assert(offsetof(GxPsConstants, ztex_params) == 560);
static_assert(offsetof(GxPsConstants, ind_cmd) == 576);
static_assert(offsetof(GxPsConstants, ind_mtx) == 640);
static_assert(offsetof(GxPsConstants, ind_misc) == 736);
static_assert(sizeof(GxPsConstants) == 768);

// Vertex-stage constants (root CBV b0).  The XF matrix palette itself rides
// in a structured buffer (root SRV), not here — it is large and re-snapshot
// whenever LOAD_INDX mutates it mid-frame.
struct alignas(256) GxVsConstants {
    float projection[4][4];
    // Raw texgen + post-texgen config words; consumed by the uber vertex
    // shader's texgen loop.
    std::uint32_t texgen_config[kMaxTexGens][2];
    float chan_ambient[kMaxColorChannels][4];
    float chan_material[kMaxColorChannels][4];
    std::uint32_t num_texgens;
    std::uint32_t num_chans;
    // bit0 per-vertex posmtx, bit1 lighting enable.
    std::uint32_t flags;
    // Index of this draw's palette snapshot within the frame's matrix buffer.
    std::uint32_t mtx_palette_base;
    // CP MatrixIndexA/B: default (non-per-vertex) matrix indices.
    //   A: bits 0-5 pos/normal, 6-11 tex0, 12-17 tex1, 18-23 tex2, 24-29 tex3
    //   B: bits 0-5 tex4, 6-11 tex5, 12-17 tex6, 18-23 tex7
    // (Dolphin: VertexShaderManager resolves these into I_TEXMATRICES; we
    // resolve in the VS instead so the palette snapshot stays the only
    // matrix upload.)
    std::uint32_t matrix_index_a;
    std::uint32_t matrix_index_b;
    std::uint32_t pad0;
    std::uint32_t pad1;
    float inline_pos_matrix[3][4];
    float inline_tex_matrices[kMaxTexGens][3][4];
    float inline_post_matrices[kMaxTexGens][3][4];
    // Physical viewport dimensions and exact BP 0x22 widths after applying
    // the authoritative EFB scale: width, height, line width, point size.
    // Geometry shaders read this at HLSL packoffset(c65).
    float line_point_raster[4];
    // Line mask, point mask, line divisor, point divisor for GX texture
    // coordinate offsets. Geometry shaders read this at packoffset(c66).
    std::uint32_t line_point_tex_offsets[4];
};

static_assert(offsetof(GxVsConstants, projection) == 0);
static_assert(offsetof(GxVsConstants, texgen_config) == 64);
static_assert(offsetof(GxVsConstants, chan_ambient) == 128);
static_assert(offsetof(GxVsConstants, chan_material) == 160);
static_assert(offsetof(GxVsConstants, num_texgens) == 192);
static_assert(offsetof(GxVsConstants, matrix_index_a) == 208);
static_assert(offsetof(GxVsConstants, inline_pos_matrix) == 224);
static_assert(offsetof(GxVsConstants, inline_tex_matrices) == 272);
static_assert(offsetof(GxVsConstants, inline_post_matrices) == 656);
static_assert(offsetof(GxVsConstants, line_point_raster) == 1040);
static_assert(offsetof(GxVsConstants, line_point_tex_offsets) == 1056);
static_assert(sizeof(GxVsConstants) == 1280);

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace galaxy::gx
