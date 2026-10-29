#pragma once

// VertexLoader: decodes GX draw packets (any VCD/VAT layout, direct or indexed attributes) into
// GxVertexOut vertices and 16-bit index lists in the frame's upload rings. Quads, strips and
// fans become indexed triangle lists that share vertices.

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/guest_memory.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace galaxy::gx {

class UploadRing;
struct ImmutableUploadToken;

namespace detail {
// Adds `bias` to each index; source and destination must not overlap. Returns false if a sum
// exceeds 65535, and the caller must then reject the draw.
[[nodiscard]] bool rebase_cached_indices(std::span<const std::uint16_t> source,
                                        std::uint32_t bias,
                                        std::uint16_t* destination) noexcept;
}  // namespace detail

// Decoded vertex: one input layout for every pipeline. uv holds all eight texture coordinates;
// mtx_indices holds the position/normal matrix index and the eight texture matrix indices
// that the vertex shader uses to index the XF matrix palette; curve what a curved surface's
// vertex carries (curved_surfaces.h's Model): its corner's displacement normal and its place's
// crease normal (both packed, zero for none) and its place with the first crease neighbour
// (place | neighbour << 16); the second crease neighbour rides in mtx_indices[2]'s high half.
struct GxVertexOut {
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
static_assert(sizeof(GxVertexOut) == 144, "fixed 144-byte vertex stride");
// mtx_indices[2] bit 15: the corner is a flat solid's normal shaded soft (curved_surfaces.h PlaceBend::flat); ray
// tracing shades its face flat (ray_tracing.hlsl kVertexFlatCorner). Bits 0-7 hold texture matrix 7's index, bits
// 16-31 the second crease neighbour.
inline constexpr std::uint32_t kVertexFlatCorner = 0x8000u;
static_assert(offsetof(GxVertexOut, mtx_indices) + 8u == 128u, "ray_tracing.hlsl reads mtx_indices[2] at byte 128");
static_assert(sizeof(GxVertexOut) % 16u == 0u,
              "vertex upload batching requires 16-byte stride alignment");

// Result of loading one draw packet.
struct LoadedPrimitive {
    std::uint32_t base_vertex;      // diagnostic vertex index when stride-aligned
    std::size_t vertex_byte_offset; // byte offset into the frame vertex ring
    std::uint32_t vertex_count;     // canonical vertices written
    std::uint32_t first_index;      // offset into the frame index ring
    std::uint32_t index_count;      // always a triangle (or line/point) list
    bool indexed;                   // false when DrawInstanced can use vertices directly
    PrimitiveClass source_class;    // original GX topology, for stats/debug
};

struct VertexDecodeGuestRange {
    std::uint32_t guest_base = 0;
    std::uint32_t size = 0;
};

class VertexLoader {
public:
    // Reads one draw packet from `cursor`, positioned at the u16 vertex count. Indexed
    // attributes resolve through the CP array registers; an unmapped array throws
    // GxFatalError. Vertices and indices are appended to `vertex_ring` / `index_ring`.
    LoadedPrimitive load(
        FifoCursor& cursor,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        const GxState& state,
        GuestMemory* memory,
        UploadRing& vertex_ring,
        UploadRing& index_ring,
        std::uint32_t index_batch_base_vertex = 0,
        bool rebase_indices_to_batch_base = false);
    // load() with the layout already decoded from the VCD/VAT registers.
    LoadedPrimitive load_with_layout(
        FifoCursor& cursor,
        PrimitiveClass primitive,
        const GxState& state,
        const VertexDescriptor& desc,
        std::size_t source_vertex_size,
        GuestMemory* memory,
        UploadRing& vertex_ring,
        UploadRing& index_ring,
        std::uint32_t index_batch_base_vertex = 0,
        bool rebase_indices_to_batch_base = false);




    // Byte size of one source vertex, so a packet's vertex data is taken as one window.
    [[nodiscard]] static std::size_t source_vertex_size(
        const VertexDescriptor& desc);
};

}  // namespace galaxy::gx
