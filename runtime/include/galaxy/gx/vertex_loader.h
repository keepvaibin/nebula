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
// that the vertex shader uses to index the XF matrix palette; bend the position's displacement
// normal for curved surfaces (curved_surfaces.h), zero where it does not bend.
struct GxVertexOut {
    float position[3];
    float normal[3];
    float tangent[3];
    float binormal[3];
    std::uint32_t color0;           // RGBA8
    std::uint32_t color1;           // RGBA8
    float uv[8][2];
    std::uint32_t mtx_indices[3];   // x: pn/tex0-2, y: tex3-6, z: tex7
    float bend[3];
};
static_assert(sizeof(GxVertexOut) == 144, "fixed 144-byte vertex stride");
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

struct DecodedPacketRunVertices {
    std::uint32_t total_vertices = 0;
    std::uint32_t total_indices = 0;
    std::vector<GxVertexOut> vertices;
    std::vector<VertexDecodeGuestRange> guest_array_reads;
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

    // `record_indexed_read_ranges` fills guest_array_reads, which only the decoded-vertex cache
    // uses; false skips the range tracking on every indexed attribute read.
    DecodedPacketRunVertices decode_cached_packet_run_vertices_with_layout(
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets,
        PrimitiveClass primitive,
        const GxState& state,
        const VertexDescriptor& desc,
        std::size_t source_vertex_size,
        GuestMemory* memory,
        std::uint32_t precomputed_total_vertices = 0,
        std::uint32_t precomputed_total_indices = 0,
        bool record_indexed_read_ranges = true,
        std::vector<GxVertexOut>* reusable_vertices = nullptr);

    LoadedPrimitive upload_cached_packet_run_vertices(
        std::span<const GxVertexOut> vertices,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets,
        PrimitiveClass primitive,
        UploadRing& vertex_ring,
        UploadRing& index_ring,
        std::uint32_t index_batch_base_vertex = 0,
        bool rebase_indices_to_batch_base = false,
        std::uint32_t total_indices = 0,
        std::span<const std::uint16_t> precomputed_indices = {},
        ImmutableUploadToken* upload_token = nullptr);

    // The index half of upload_cached_packet_run_vertices, for vertices already on the GPU:
    // `vertex_count` vertices start at `vertex_byte_offset` in a caller-owned buffer, so
    // base_vertex = vertex_byte_offset / sizeof(GxVertexOut).
    LoadedPrimitive upload_cached_packet_run_indices(
        std::size_t vertex_byte_offset,
        std::uint32_t vertex_count,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets,
        PrimitiveClass primitive,
        UploadRing& index_ring,
        std::uint32_t index_batch_base_vertex = 0,
        bool rebase_indices_to_batch_base = false,
        std::uint32_t total_indices = 0,
        std::span<const std::uint16_t> precomputed_indices = {});

    // Byte size of one source vertex, so a packet's vertex data is taken as one window.
    [[nodiscard]] static std::size_t source_vertex_size(
        const VertexDescriptor& desc);
};

}  // namespace galaxy::gx
