#pragma once

// The vertex-stage constants of a native draw (root CBV b0): native_material.hlsli's NativeDraw cbuffer mirrors
// the fields it reads at their packoffsets; the static_asserts below pin the offsets. Rows no shader reads are
// reserved so the offsets stay.
//
// No Windows or D3D12 includes.

#include <cstddef>
#include <cstdint>

namespace galaxy::render {

inline constexpr unsigned kMaxTevStages = 16;
inline constexpr unsigned kMaxTextureMaps = 8;
inline constexpr unsigned kMaxTexGens = 8;
inline constexpr unsigned kMaxColorChannels = 2;

// alignas(256): D3D12 constant buffers start on 256-byte boundaries.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // structure padded due to alignment specifier
#endif

// Vertex-stage constants (root CBV b0). The matrix palette is a structured buffer (root SRV) instead: it is
// large and is snapshotted again whenever LOAD_INDX changes it mid-frame.
struct alignas(256) VertexConstants {
    float projection[4][4];
    // c4-c6 (native_material.hlsli draw_rows): the draw's matrix rows, entry k of the flat twelve: 0 the position
    // matrix, 1 + i texture coordinate i's (60: the identity).
    std::uint32_t draw_rows[3][4];
    std::uint32_t reserved_c7[5][4];  // c7-c11: read by no shader
    std::uint32_t num_texgens;
    std::uint32_t num_chans;
    // Bit 0 per-vertex position matrix, bit 1 lighting enable.
    std::uint32_t flags;
    // The first row of the draw's position block in the bound matrix rows (position place k at pos_base + 3k).
    std::uint32_t pos_base;
    std::uint32_t reserved_c13[2];  // c13.xy: read by no shader
    // Enhanced lighting's sky: the player's up in eye space (octahedral, draw_constants_fill.h
    // encode_octahedral), and nonzero when known (else the view's up).
    std::uint32_t sky_up;
    std::uint32_t sky_up_known;
    float inline_pos_matrix[3][4];
    float inline_tex_matrices[kMaxTexGens][3][4];
    float inline_post_matrices[kMaxTexGens][3][4];
    // Physical viewport width and height, line width and point size (the line width) at the scene target scale. Geometry shaders
    // read it at HLSL packoffset(c65).
    float line_point_raster[4];
    // Line mask, point mask, line divisor, point divisor for texture
    // coordinate offsets. Geometry shaders read this at packoffset(c66).
    std::uint32_t line_point_tex_offsets[4];
    // native_material.hlsli vs_header (c67): y the frame scale, z the PE word (native_ps), w the shape's per-vertex
    // texture matrix indices (texgen n's bit).
    std::uint32_t draw_header[4];
    std::uint32_t reserved_c68[kMaxTexGens];  // c68-c69: read by no shader
    // vs_post (c70-71): texgen i's post-transform row (61 the identity), | 0x100 normalize.
    std::uint32_t post_rows[kMaxTexGens];
    std::uint32_t reserved_c72[4];  // c72: read by no shader
    // Motion-vector pipelines only (temporal scenes), in the tail padding: the
    // previous projection, the previous position matrix or
    // camera delta, the draw's jitter (NDC) and half viewport (pixels), and the
    // mode (0: previous palette rows at motion_ctrl[1]; 1: motion_prev_pos applied
    // to the current eye position for palette draws, to the vertex for inline draws).
    float motion_prev_proj[2][4];
    float motion_prev_pos[3][4];
    float motion_params[4];
    std::uint32_t motion_ctrl[4];
    std::uint32_t reserved_c80[8][4];  // c80-c87: read by no shader
    // The first rows of the draw's normal, texture, post-transform and light blocks (draw_matrices.h): normal row q
    // at row_bases[0] + q, texture row r (30-63) at row_bases[1] + r - 30, post-transform row q at row_bases[2] + q,
    // light n's rows at row_bases[3] + 4n.
    std::uint32_t row_bases[4];
};

static_assert(offsetof(VertexConstants, projection) == 0);
static_assert(offsetof(VertexConstants, draw_rows) == 64);
static_assert(offsetof(VertexConstants, num_texgens) == 192);
static_assert(offsetof(VertexConstants, reserved_c13) == 208);
static_assert(offsetof(VertexConstants, inline_pos_matrix) == 224);
static_assert(offsetof(VertexConstants, inline_tex_matrices) == 272);
static_assert(offsetof(VertexConstants, inline_post_matrices) == 656);
static_assert(offsetof(VertexConstants, line_point_raster) == 1040);
static_assert(offsetof(VertexConstants, line_point_tex_offsets) == 1056);
static_assert(offsetof(VertexConstants, draw_header) == 1072);
static_assert(offsetof(VertexConstants, reserved_c68) == 1088);
static_assert(offsetof(VertexConstants, post_rows) == 1120);
static_assert(offsetof(VertexConstants, reserved_c72) == 1152);
static_assert(offsetof(VertexConstants, motion_prev_proj) == 1168);
static_assert(offsetof(VertexConstants, motion_ctrl) == 1264);
// motion_ctrl[1] in mode 0 for the draw's own position rows (its previous rows are its rows).
inline constexpr std::uint32_t kMotionOwnRows = 0xFFFFFFFFu;
static_assert(offsetof(VertexConstants, reserved_c80) == 1280);
static_assert(offsetof(VertexConstants, row_bases) == 1408);
static_assert(sizeof(VertexConstants) == 1536);

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace galaxy::render
