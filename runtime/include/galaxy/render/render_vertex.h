// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The renderer's vertex as it is built. The GPU reads two streams, one input layout for every pipeline: RenderVertexCore,
// what every vertex has, and RenderVertexExtra, only for meshes that use it (others read zeros).

#include <cstddef>
#include <cstdint>

namespace galaxy::render {

// uv holds all eight texture coordinates;
// mtx_indices holds the position/normal matrix index and the eight texture matrix indices
// that the vertex shader uses to index the matrix palette; curve what a curved surface's
// vertex carries (curved_surfaces.h's Model): its corner's displacement normal and its place's
// crease normal (both packed, zero for none) and its place with the first crease neighbour
// (place | neighbour << 16); the second crease neighbour rides in mtx_indices[2]'s high half.
struct RenderVertex {
    float position[3];
    float normal[3];
    float tangent[3];
    float binormal[3];
    std::uint32_t color0;           // RGBA8
    std::uint32_t color1;           // RGBA8
    float uv[8][2];
    std::uint32_t mtx_indices[3];   // x: the position matrix's first row, y: each coordinate's matrix (a nibble), z: flags
    std::uint32_t curve[3];
};
static_assert(sizeof(RenderVertex) == 144);

struct RenderVertexCore {
    float position[3];
    float normal[3];
    std::uint32_t color0;
    std::uint32_t color1;
    float uv0[2];
    std::uint32_t position_row;
};
static_assert(sizeof(RenderVertexCore) == 44);

struct RenderVertexExtra {
    float tangent[3];
    float binormal[3];
    float uv[7][2];
    std::uint32_t mtx_more[2];  // RenderVertex::mtx_indices[1], [2]
    std::uint32_t other_row;    // a widened line corner's other end's position row
};
static_assert(sizeof(RenderVertexExtra) == 92);

inline void write_core(const RenderVertex& v, RenderVertexCore& out) noexcept {
    for (unsigned i = 0; i < 3u; ++i) {
        out.position[i] = v.position[i];
        out.normal[i] = v.normal[i];
    }
    out.color0 = v.color0;
    out.color1 = v.color1;
    out.uv0[0] = v.uv[0][0];
    out.uv0[1] = v.uv[0][1];
    out.position_row = v.mtx_indices[0];
}

inline void write_extra(const RenderVertex& v, RenderVertexExtra& out) noexcept {
    for (unsigned i = 0; i < 3u; ++i) {
        out.tangent[i] = v.tangent[i];
        out.binormal[i] = v.binormal[i];
    }
    for (unsigned i = 0; i < 7u; ++i) {
        out.uv[i][0] = v.uv[i + 1u][0];
        out.uv[i][1] = v.uv[i + 1u][1];
    }
    out.mtx_more[0] = v.mtx_indices[1];
    out.mtx_more[1] = v.mtx_indices[2];
    out.other_row = v.curve[0];
}
// mtx_indices[2] bit 15: the corner is a flat solid's normal shaded soft (curved_surfaces.h PlaceBend::flat); ray
// tracing shades its face flat (ray_tracing.hlsl kVertexFlatCorner). Bits 0-7 hold texture matrix 7's index, bits
// 16-31 the second crease neighbour.
inline constexpr std::uint32_t kVertexFlatCorner = 0x8000u;
// mtx_indices[2] bits 8-11: a corner of a widened line or point (native_shapes.h widen_lines_points). A line's corner holds
// its other end's position in tangent and that end's position row in curve[0].
inline constexpr std::uint32_t kVertexWidenPoint = 0x100u;
inline constexpr std::uint32_t kVertexWidenLine = 0x200u;
inline constexpr std::uint32_t kVertexWidenRight = 0x400u;
inline constexpr std::uint32_t kVertexWidenBottom = 0x800u;

// A point of the picture the game asks about (nebula_game_api.h NebulaScreenProbe): the frame's coordinates, negative
// no point; the user values come back with the value.
struct ScreenProbe {
    float x, y, user_x, user_y, user_z, value;
};
inline constexpr std::uint32_t kScreenProbesMax = 256u;

}  // namespace galaxy::render
