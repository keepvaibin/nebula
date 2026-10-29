#pragma once

// Constant buffer layouts shared by the uber and the specialized GX shaders. The HLSL mirrors these structs field
// for field; the static_asserts below pin the offsets.
//
// Both shader families use the same layouts and root signature, so moving a draw from the uber pipeline to a
// specialized PSO changes only the pipeline. Specialized shaders ignore stage_config (their TEV program is
// compiled in).
//
// No Windows or D3D12 includes.

#include <cstddef>
#include <cstdint>

namespace galaxy::gx {

inline constexpr unsigned kMaxTevStages = 16;
inline constexpr unsigned kMaxTextureMaps = 8;
inline constexpr unsigned kMaxTexGens = 8;
inline constexpr unsigned kMaxColorChannels = 2;

// alignas(256): D3D12 constant buffers start on 256-byte boundaries.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // structure padded due to alignment specifier
#endif

// Pixel-stage constants (root CBV b1).
struct alignas(256) GxPsConstants {
    // TevStagePacked words, read only by the uber pixel shader.
    std::uint32_t stage_config[kMaxTevStages][4];
    // TEV color registers PREV, REG0-2 as signed values divided by 255.
    float tev_regs[4][4];
    // Konstant color bank K0-K3.
    float konst[4][4];
    // x = alpha ref0 / 255, y = alpha ref1 / 255,
    // z = packed comp0|comp1|logic (uber only), w = constant dst alpha.
    float alpha_refs[4];
    // a, b_magnitude, b_shift, c, color.rgb, proj flag.
    float fog_params[8];
    // Per map: texture width and height, and the GX texcoord S/T scale (BP 0x30-0x3F scale_minus_1 + 1). Pixel
    // shaders take rasterized texcoords through GX fixed-point space with zw, then normalize samples by xy.
    float tex_dims[kMaxTextureMaps][4];
    // x = Z-texture bias (BP 0xF4, 24-bit) / 2^24;
    // y = reciprocal physical EFB scale for RGBA6 dithering;
    // z = a light source's glow gain in linear light (output_flags bit 5: its radiance times the exposure);
    // w = the lighting texture's texels per EFB pixel (bit 6: lit translucents).
    float ztex_params[4];
    // Indirect TEV state: commands are raw BP 0x10-0x1F words grouped as
    // uint4s; matrices are decoded as six int4 rows where .w is 17 - scale;
    // ind_misc = BP 0x27, BP 0x25, BP 0x26, num_ind_stages.
    std::uint32_t ind_cmd[4][4];
    std::int32_t ind_mtx[6][4];
    std::uint32_t ind_misc[4];
    // Uber pixel shader only; specialized shaders bake these from the key:
    // x num_tev_stages | num_texgens << 8 | num_chans << 16 | fog_type << 24,
    // y alpha_comp0 | alpha_comp1 << 8 | alpha_logic << 16 | flags << 24,
    // z output_flags | efb_scale_minus_one << 16, w zero.
    std::uint32_t uber_ps_header[4];
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
static_assert(offsetof(GxPsConstants, uber_ps_header) == 752);
static_assert(sizeof(GxPsConstants) == 768);

// Vertex-stage constants (root CBV b0). The XF matrix palette is a structured buffer (root SRV) instead: it is
// large and is snapshotted again whenever LOAD_INDX changes it mid-frame.
struct alignas(256) GxVsConstants {
    float projection[4][4];
    // Raw texgen and post-texgen config words, read by the uber vertex shader.
    std::uint32_t texgen_config[kMaxTexGens][2];
    float chan_ambient[kMaxColorChannels][4];
    float chan_material[kMaxColorChannels][4];
    std::uint32_t num_texgens;
    std::uint32_t num_chans;
    // Bit 0 per-vertex posmtx, bit 1 lighting enable. Physical ray tracing (the art's baked light removed): bits 2-3
    // de-bake unlit channel 0 / 1's vertex colour to its program's neutral light; bits 4-5 keep a lit channel's
    // vertex colour hue and light it at the neutral level; bit 6 de-bakes by debake_sh; bit 7 marks a draw lit by the
    // player light rig (its level, not its hue); bits 8-15 and 16-23 hold the levels.
    std::uint32_t flags;
    // Index of this draw's palette snapshot within the frame's matrix buffer.
    std::uint32_t mtx_palette_base;
    // CP MatrixIndexA/B: default (non-per-vertex) matrix indices.
    //   A: bits 0-5 pos/normal, 6-11 tex0, 12-17 tex1, 18-23 tex2, 24-29 tex3
    //   B: bits 0-5 tex4, 6-11 tex5, 12-17 tex6, 18-23 tex7
    // Dolphin resolves these in VertexShaderManager into I_TEXMATRICES. Here the
    // vertex shader resolves them, so the palette snapshot stays the only matrix upload.
    std::uint32_t matrix_index_a;
    std::uint32_t matrix_index_b;
    // Enhanced lighting's sky: the player's up in eye space (octahedral, vs_constants.h
    // encode_octahedral), and nonzero when known (else the view's up).
    std::uint32_t sky_up;
    std::uint32_t sky_up_known;
    float inline_pos_matrix[3][4];
    float inline_tex_matrices[kMaxTexGens][3][4];
    float inline_post_matrices[kMaxTexGens][3][4];
    // Physical viewport width and height, line width and point size (BP 0x22) at the EFB scale. Geometry shaders
    // read it at HLSL packoffset(c65).
    float line_point_raster[4];
    // Line mask, point mask, line divisor, point divisor for GX texture
    // coordinate offsets. Geometry shaders read this at packoffset(c66).
    std::uint32_t line_point_tex_offsets[4];
    // Uber vertex shader only: the draw's VertexShaderKey (num_texgens,
    // num_chans, flags, texmtx_idx_mask; texgen, post_texgen and channel
    // control words). Specialized shaders bake these and never read them.
    std::uint32_t uber_vs_header[4];
    std::uint32_t uber_texgen[kMaxTexGens];
    std::uint32_t uber_post_texgen[kMaxTexGens];
    std::uint32_t uber_channel[4];
    // Motion-vector pipelines only (temporal scenes), in the tail padding: the
    // previous projection (GX A..D; E, F, type), the previous position matrix or
    // camera delta, the draw's jitter (NDC) and half viewport (pixels), and the
    // mode (0: previous palette rows at motion_ctrl[1]; 1: motion_prev_pos applied
    // to the current eye position for palette draws, to the vertex for inline draws).
    float motion_prev_proj[2][4];
    float motion_prev_pos[3][4];
    float motion_params[4];
    std::uint32_t motion_ctrl[4];
    // Physical ray tracing (flags bit 6): the model's light baked into its vertex colours as a function of the
    // surface's facing (GxBackend::fit_debake_light): nine spherical-harmonic coefficients of the object-space normal
    // per channel (r 0-8, g 9-17, b 18-26), and its value at the most lit sampled facing (rgb). A de-baked channel's
    // vertex colour is raised by that over its own facing's value: the light's fall-off and colour leave it, its
    // paint and occlusion stay.
    float debake_sh[7][4];
    float debake_peak[4];
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
static_assert(offsetof(GxVsConstants, uber_vs_header) == 1072);
static_assert(offsetof(GxVsConstants, uber_texgen) == 1088);
static_assert(offsetof(GxVsConstants, uber_post_texgen) == 1120);
static_assert(offsetof(GxVsConstants, uber_channel) == 1152);
static_assert(offsetof(GxVsConstants, motion_prev_proj) == 1168);
static_assert(offsetof(GxVsConstants, motion_ctrl) == 1264);
static_assert(offsetof(GxVsConstants, debake_sh) == 1280);
static_assert(offsetof(GxVsConstants, debake_peak) == 1392);
static_assert(sizeof(GxVsConstants) == 1536);

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace galaxy::gx
