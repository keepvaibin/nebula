#pragma once

// VertexLoader: converts one GX draw packet (any VCD/VAT combination, direct
// or indexed attributes) into the canonical GxVertexOut stream plus an index
// list for triangle/line/point draws written into the frame's upload rings.
//
// Quads / strips / fans are converted to indexed triangle lists with shared
// vertices (not duplicated), cutting upload bandwidth on low-end hardware.
// Fixed-stride attribute blocks are consumed via FifoCursor::take() and the
// bulk SIMD byte-swap kernels — never per-byte scalar reads.
//
// No Windows/D3D12 includes — unit-testable offline.  UploadRing is the
// renderer's linear per-frame allocator, forward-declared here.

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/native_api.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace galaxy::gx {

class UploadRing;
struct ImmutableUploadToken;
class DependencyEventSink;

namespace detail {
// Source/destination are distinct, valid native-endian index storage. Returns
// false if any mathematical sum exceeds 65535; the caller must reject the draw.
[[nodiscard]] bool rebase_cached_indices(std::span<const std::uint16_t> source,
                                        std::uint32_t bias,
                                        std::uint16_t* destination) noexcept;
}  // namespace detail

// Canonical post-load vertex.  One fixed input layout for every PSO keeps
// pipeline permutations down and the loader branch-free per attribute.
//
// uv carries every GX texture-coordinate channel.  mtx_indices carries the
// per-vertex position/normal matrix index plus all eight texture matrix
// indices; the vertex shader indexes the XF matrix palette with these.
struct GxVertexOut {
    float position[3];
    float normal[3];
    float tangent[3];
    float binormal[3];
    std::uint32_t color0;           // RGBA8
    std::uint32_t color1;           // RGBA8
    float uv[8][2];
    std::uint32_t mtx_indices[4];   // x: pn/tex0-2, y: tex3-6, z: tex7, w: unused
    std::uint32_t padding[2];
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
    // Non-owning, render-thread-only observation. Null in production's
    // default path; never changes the resolved indexed attribute pointer.
    void set_dependency_event_sink(DependencyEventSink* sink) noexcept {
        dependency_event_sink_ = sink;
    }
    // Reads one draw packet from `cursor` (positioned at the u16 vertex
    // count, immediately after the draw opcode byte).
    //
    // - Direct attributes come from the FIFO via take() + bulk swap.
    // - Indexed attributes resolve through the CP array base/stride registers
    //   into guest memory; an unmapped array is a GxFatalError.
    // - All eight GX texcoord channels are preserved; multi-texture effects
    //   must not alias absent channels to zero.
    //
    // Vertices and indices are appended to `vertex_ring` / `index_ring`.
    LoadedPrimitive load(
        FifoCursor& cursor,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        const GxState& state,
        GuestMemoryV1* memory,
        UploadRing& vertex_ring,
        UploadRing& index_ring,
        std::uint32_t index_batch_base_vertex = 0,
        bool rebase_indices_to_batch_base = false);

    LoadedPrimitive load_with_layout(
        FifoCursor& cursor,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        const GxState& state,
        const VertexDescriptor& desc,
        std::size_t source_vertex_size,
        GuestMemoryV1* memory,
        UploadRing& vertex_ring,
        UploadRing& index_ring,
        std::uint32_t index_batch_base_vertex = 0,
        bool rebase_indices_to_batch_base = false);

    // Byte size of one source vertex for the given descriptor — used to
    // take() the whole packet in a single window before conversion.
    LoadedPrimitive load_cached_packet_run_with_layout(
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        const GxState& state,
        const VertexDescriptor& desc,
        std::size_t source_vertex_size,
        GuestMemoryV1* memory,
        UploadRing& vertex_ring,
        UploadRing& index_ring,
        std::uint32_t index_batch_base_vertex = 0,
        bool rebase_indices_to_batch_base = false,
        std::uint32_t precomputed_total_vertices = 0,
        std::uint32_t precomputed_total_indices = 0,
        std::span<const std::uint16_t> precomputed_indices = {});

    // `record_indexed_read_ranges` exists because the decoded-vertex cache is
    // the only consumer of DecodedPacketRunVertices::guest_array_reads. When it
    // is false the resolver's read-range accumulator is left null, which makes
    // IndexedArrayResolver::record_read() an argument check instead of a
    // min/max update on every indexed attribute of every decoded vertex, and
    // leaves DecodedPacketRunVertices::guest_array_reads empty rather than
    // filled and discarded.
    DecodedPacketRunVertices decode_cached_packet_run_vertices_with_layout(
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        const GxState& state,
        const VertexDescriptor& desc,
        std::size_t source_vertex_size,
        GuestMemoryV1* memory,
        std::uint32_t precomputed_total_vertices = 0,
        std::uint32_t precomputed_total_indices = 0,
        bool record_indexed_read_ranges = true);

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

    [[nodiscard]] static std::size_t source_vertex_size(
        const VertexDescriptor& desc);
private:
    DependencyEventSink* dependency_event_sink_ = nullptr;
};

}  // namespace galaxy::gx
