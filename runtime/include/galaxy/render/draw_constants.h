#pragma once

// The vertex-stage constants of a native draw (root CBV b0): native_material.hlsli's NativeDraw cbuffer mirrors
// the fields it reads at their packoffsets; the static_asserts below pin the offsets.
//
// No Windows or D3D12 includes.

#include <cstddef>
#include <cstdint>

namespace galaxy::render {

inline constexpr unsigned kMaxStageStages = 16;
inline constexpr unsigned kMaxTextureMaps = 8;
inline constexpr unsigned kMaxTexGens = 8;
inline constexpr unsigned kMaxColorChannels = 2;

// alignas(256): D3D12 constant buffers start on 256-byte boundaries.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)  // structure padded due to alignment specifier
#endif

// Vertex-stage constants (root CBV b0). The matrices are in the matrix rows (a structured buffer), which every draw names.
struct alignas(256) VertexConstants {
    float projection[4][4];
    // c4-c6 (native_material.hlsli draw_rows): the draw's matrix rows, entry k of the flat twelve: 0 the position
    // matrix, 1 + i texture coordinate i's (60: the identity); 9 and 10 a particle corner's times (floats).
    std::uint32_t draw_rows[3][4];
    std::uint32_t num_texgens;
    std::uint32_t num_chans;
    // Bit 0 per-vertex position matrix, bit 1 lighting enable.
    std::uint32_t flags;
    // The first row of the draw's position block in the bound matrix rows (position place k at pos_base + 3k).
    std::uint32_t pos_base;
    std::uint32_t reserved_c8[2];  // c8.xy: read by no shader
    // Enhanced lighting's sky: the player's up in eye space (octahedral, draw_constants_fill.h
    // encode_octahedral), and nonzero when known (else the view's up).
    std::uint32_t sky_up;
    std::uint32_t sky_up_known;
    // Physical viewport width and height, line width and point size (the line width) at the scene target scale.
    float line_point_raster[4];
    // Line mask, point mask, line divisor, point divisor for texture
    // coordinate offsets.
    std::uint32_t line_point_tex_offsets[4];
    // native_material.hlsli vs_header (c11): y the frame scale, w the shape's per-vertex texture matrix indices
    // (texgen n's bit).
    std::uint32_t draw_header[4];
    // vs_post (c12-13): texgen i's post-transform row (61 the identity), | 0x100 normalize.
    std::uint32_t post_rows[kMaxTexGens];
    // The first rows of the draw's normal, texture, post-transform and light blocks (draw_matrices.h): normal row q
    // at row_bases[0] + q, texture row r (30-63) at row_bases[1] + r - 30, post-transform row q at row_bases[2] + q,
    // light n's rows at row_bases[3] + 4n.
    std::uint32_t row_bases[4];
    // Motion-vector pipelines only (temporal scenes), in the tail padding: the
    // previous projection, the previous position matrix or
    // camera delta, the draw's jitter (NDC) and half viewport (pixels), and the
    // mode (0: previous palette rows at motion_ctrl[1]; 1: motion_prev_pos applied
    // to the current eye position for palette draws, to the vertex for inline draws).
    float motion_prev_proj[2][4];
    float motion_prev_pos[3][4];
    float motion_params[4];
    std::uint32_t motion_ctrl[4];
};

static_assert(offsetof(VertexConstants, projection) == 0);
static_assert(offsetof(VertexConstants, draw_rows) == 64);
static_assert(offsetof(VertexConstants, num_texgens) == 112);
static_assert(offsetof(VertexConstants, reserved_c8) == 128);
static_assert(offsetof(VertexConstants, line_point_raster) == 144);
static_assert(offsetof(VertexConstants, line_point_tex_offsets) == 160);
static_assert(offsetof(VertexConstants, draw_header) == 176);
static_assert(offsetof(VertexConstants, post_rows) == 192);
static_assert(offsetof(VertexConstants, row_bases) == 224);
static_assert(offsetof(VertexConstants, motion_prev_proj) == 240);
static_assert(offsetof(VertexConstants, motion_ctrl) == 336);
// motion_ctrl[1] in mode 0 for the draw's own position rows (its previous rows are its rows).
inline constexpr std::uint32_t kMotionOwnRows = 0xFFFFFFFFu;
static_assert(sizeof(VertexConstants) == 512);

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace galaxy::render
