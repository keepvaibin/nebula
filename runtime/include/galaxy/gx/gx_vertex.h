// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The renderer's vertex: what native shapes and immediate draws upload, one input layout for every pipeline.

#include <cstddef>
#include <cstdint>

namespace galaxy::gx {

// uv holds all eight texture coordinates;
// mtx_indices holds the position/normal matrix index and the eight texture matrix indices
// that the vertex shader uses to index the XF matrix palette; curve what a curved surface's
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
    std::uint32_t mtx_indices[3];   // x: pn/tex0-2, y: tex3-6, z: tex7
    std::uint32_t curve[3];
};
static_assert(sizeof(RenderVertex) == 144, "fixed 144-byte vertex stride");
// mtx_indices[2] bit 15: the corner is a flat solid's normal shaded soft (curved_surfaces.h PlaceBend::flat); ray
// tracing shades its face flat (ray_tracing.hlsl kVertexFlatCorner). Bits 0-7 hold texture matrix 7's index, bits
// 16-31 the second crease neighbour.
inline constexpr std::uint32_t kVertexFlatCorner = 0x8000u;
static_assert(offsetof(RenderVertex, mtx_indices) + 8u == 128u, "ray_tracing.hlsl reads mtx_indices[2] at byte 128");
static_assert(sizeof(RenderVertex) % 16u == 0u,
              "vertex upload batching requires 16-byte stride alignment");

}  // namespace galaxy::gx
