// vertex_loader.cpp — VertexLoader implementation.
//
// Converts one GX draw packet (any VCD/VAT combination, direct or indexed
// attributes) into the canonical GxVertexOut stream plus an index list for
// triangle/line/point draws written into the frame's upload rings.
//
// Design invariants:
//  - Attribute streams are bounded once, then converted through local endian
//    loads; each component reads exactly its encoded byte width.
//  - Color attributes are NEVER passed through the swap kernels (see below).
//  - Unknown/unsupported configurations throw GxFatalError (hard-fail).
//  - Indexed attributes resolve through GuestMemoryV1; unmapped → GxFatalError.

#include "galaxy/gx/vertex_loader.h"
#include "galaxy/gx/vertex_dequant.h"
#include "galaxy/gx/dependency_event_capture.h"

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/native_api.h"

#include <emmintrin.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace galaxy::gx {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

struct ResolvedGuestRange {
    bool valid = false;
    std::uint32_t guest_base = 0;
    std::uint64_t guest_end = 0;
    const std::byte* host_base = nullptr;

    [[nodiscard]] bool contains(
        std::uint32_t address,
        std::uint32_t size) const {
        const std::uint64_t end =
            static_cast<std::uint64_t>(address) + size;
        return valid && address >= guest_base && end <= guest_end;
    }

    [[nodiscard]] const std::byte* pointer(std::uint32_t address) const {
        return host_base + (address - guest_base);
    }
};

struct ResolvedGuestPointer {
    const std::byte* ptr = nullptr;
    ResolvedGuestRange range{};
};

struct RecordedIndexedArrayRange {
    bool used = false;
    std::uint32_t begin = 0;
    std::uint64_t end = 0;
};

// Upper bound on one recorded dependency span. `record_read` accumulates the
// bytes an indexed attribute actually touched so the decoded-vertex cache can
// snapshot them and later detect a guest write with a memcmp. Collapsing every
// read of one attribute into a single [min,max) span made that span the whole
// indexed array whenever a run's indices were scattered — measured at ~1 ms per
// decoded-cache miss and 22 MB of cache growth in a single frame on the low-end
// target. Splitting the accumulation at this granularity keeps the snapshot
// proportional to the bytes actually read (plus bounded read-gap padding)
// instead of proportional to the array's extent.
constexpr std::uint32_t kRecordedRangeChunkBytes = 64u * 1024u;

// A read within this distance of either edge of the current chunk is absorbed
// into that chunk rather than opening a new one and padding the gap into the
// snapshot. Beyond it the gap is skipped entirely: the snapshot must cover the
// bytes that were read, and it does not have to cover bytes that were not.
constexpr std::uint32_t kRecordedRangeGapBytes = 256u;

[[noreturn]] static void throw_unmapped_indexed_array(
    std::uint32_t address,
    std::size_t fifo_offset) {
    throw GxFatalError(
        "VertexLoader: indexed array address 0x" +
            [&] {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%08X", address);
                return std::string(buf);
            }() +
            " is not mapped",
        fifo_offset, 0);
}

// Per-draw cache for CP array-backed indexed attributes. SMG gameplay replays
// many small cached display-list draws; each draw often fetches position,
// normal, color, and texcoord attributes from the same guest memory regions for
// every vertex. Cache only the resolved mapping bounds, never data contents.
class IndexedArrayResolver {
public:
    explicit IndexedArrayResolver(
        GuestMemoryV1* memory,
        std::vector<VertexDecodeGuestRange>* read_ranges = nullptr,
        DependencyEventSink* dependency_event_sink = nullptr)
        : memory_(memory), read_ranges_(read_ranges),
          dependency_event_sink_(dependency_event_sink) {}

    [[nodiscard]] const std::byte* resolve(
        unsigned attr_idx,
        std::uint32_t address,
        std::uint32_t size,
        std::size_t fifo_offset) {
        record_read(attr_idx, address, size);
        const std::byte* resolved = nullptr;
        if (attr_idx < ranges_.size()) {
            ResolvedGuestRange& cached = ranges_[attr_idx];
            if (cached.contains(address, size)) {
                resolved = cached.pointer(address);
            } else {
                const ResolvedGuestPointer uncached =
                    resolve_uncached(address, size, fifo_offset);
                cached = uncached.range;
                resolved = uncached.ptr;
            }
        } else {
            resolved = resolve_uncached(address, size, fifo_offset).ptr;
        }
        if (dependency_event_sink_ != nullptr) {
            dependency_event_sink_->guest_read(
                DependencyReadSource::IndexedVertex,
                address, std::span<const std::byte>(resolved, size));
        }
        return resolved;
    }

    void emit_recorded_ranges() {
        if (read_ranges_ == nullptr) {
            return;
        }
        // Closed chunks were already appended in read order; emit the one
        // remaining active chunk per attribute in stable attribute order.
        for (RecordedIndexedArrayRange& range : read_range_accum_) {
            finish_chunk(range);
        }
        if (read_ranges_->empty()) {
            // The decode result normally starts empty. Transfer the finished
            // vector instead of allocating and copying the same ranges again.
            *read_ranges_ = std::move(read_range_finished_);
        } else {
            read_ranges_->insert(read_ranges_->end(),
                read_range_finished_.begin(), read_range_finished_.end());
        }
    }

private:
    void record_read(
        unsigned attr_idx,
        std::uint32_t address,
        std::uint32_t size) {
        if (read_ranges_ == nullptr ||
            attr_idx >= read_range_accum_.size() ||
            size == 0u) {
            return;
        }
        const std::uint64_t end =
            static_cast<std::uint64_t>(address) + size;
        if (end > 0x1'0000'0000ull) {
            throw GxFatalError(
                "VertexLoader: indexed array dependency range overflows",
                0,
                0);
        }
        RecordedIndexedArrayRange& current = read_range_accum_[attr_idx];
        if (!current.used) {
            current = RecordedIndexedArrayRange{true, address, end};
            return;
        }
        // Extend the newest chunk while the read stays inside it or within the
        // small padding window; otherwise close it and start a new one. The
        // span always contains every byte that was read, so the snapshot still
        // covers exactly the data the decode depended on.
        const bool inside = address >= current.begin && end <= current.end;
        if (inside) {
            return;
        }
        const std::uint32_t extended_begin = std::min(current.begin, address);
        const std::uint64_t extended_end = std::max(current.end, end);
        const bool contiguous_enough =
            address <= current.end + kRecordedRangeGapBytes &&
            end + kRecordedRangeGapBytes >= current.begin;
        const bool still_bounded =
            extended_end - extended_begin <= kRecordedRangeChunkBytes;
        if (contiguous_enough && still_bounded) {
            // Indices need not be monotonic. Retain the entire union even
            // when this read precedes the first read of the active chunk.
            current.begin = extended_begin;
            current.end = extended_end;
            return;
        }
        finish_chunk(current);
        current = RecordedIndexedArrayRange{true, address, end};
    }

    [[nodiscard]] ResolvedGuestPointer resolve_uncached(
        std::uint32_t address,
        std::uint32_t size,
        std::size_t fifo_offset) const {
        if (memory_ == nullptr) {
            throw GxFatalError(
                "VertexLoader: GuestMemoryV1 is null",
                fifo_offset, 0);
        }

        if (std::byte* fast = resolve_guest_fast(memory_, address, size);
            fast != nullptr) {
            const std::uint32_t region_index = address >> 28;
            const GuestMemoryFastRegionV1& region =
                memory_->fast_regions[region_index];
            const std::uint32_t guest_base = region_index << 28;
            return ResolvedGuestPointer{
                fast,
                ResolvedGuestRange{
                    true,
                    guest_base,
                    static_cast<std::uint64_t>(guest_base) + region.size,
                    region.host_base,
                },
            };
        }

        if (memory_->regions != nullptr) {
            const std::uint64_t end =
                static_cast<std::uint64_t>(address) + size;
            for (std::uint32_t i = 0; i < memory_->region_count; ++i) {
                const auto& r = memory_->regions[i];
                const std::uint64_t r_end =
                    static_cast<std::uint64_t>(r.guest_base) + r.size;
                if (r.host_base != nullptr &&
                    address >= r.guest_base && end <= r_end) {
                    return ResolvedGuestPointer{
                        r.host_base + (address - r.guest_base),
                        ResolvedGuestRange{
                            true,
                            r.guest_base,
                            r_end,
                            r.host_base,
                        },
                    };
                }
            }
        }

        throw_unmapped_indexed_array(address, fifo_offset);
    }

    GuestMemoryV1* memory_ = nullptr;
    std::array<ResolvedGuestRange, 16> ranges_{};
    // Only the active chunk needs per-attribute state. Closed chunks already
    // live in read_range_finished_; retaining them twice adds heap churn.
    std::array<RecordedIndexedArrayRange, 16> read_range_accum_{};
    // Closed chunks, including a bounded gap-fill prefix for the span of the
    // current chunk. Emitted after the last read so ranges reach the caller in
    // a deterministic order.
    std::vector<VertexDecodeGuestRange> read_range_finished_;

    void finish_chunk(RecordedIndexedArrayRange& range) {
        if (range.used && range.end > range.begin) {
            read_range_finished_.push_back(VertexDecodeGuestRange{
                range.begin,
                static_cast<std::uint32_t>(range.end - range.begin),
            });
        }
        range.used = false;
        range.begin = 0;
        range.end = 0;
    }
    std::vector<VertexDecodeGuestRange>* read_ranges_ = nullptr;
    DependencyEventSink* dependency_event_sink_ = nullptr;
};

// Read a big-endian u16 from an arbitrary host byte pointer.
[[nodiscard]] static std::uint16_t read_be_u16(const std::byte* p)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(p[0])) << 8) |
         static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(p[1])));
}

// Byte size of one element for position / normal / texcoord component formats.
// ComponentFormat: U8=0, S8=1, U16=2, S16=3, F32=4.
[[nodiscard]] static std::size_t component_byte_size(std::uint8_t fmt)
{
    switch (static_cast<ComponentFormat>(fmt)) {
    case ComponentFormat::U8:  return 1;
    case ComponentFormat::S8:  return 1;
    case ComponentFormat::U16: return 2;
    case ComponentFormat::S16: return 2;
    case ComponentFormat::F32: return 4;
    default:
        throw GxFatalError(
            "VertexLoader: unknown ComponentFormat " + std::to_string(fmt),
            0, 0);
    }
}

// Byte size of one color element.
// ColorComponentFormat: RGB565=0, RGB888=1, RGBX8888=2, RGBA4444=3, RGBA6666=4, RGBA8888=5.
[[nodiscard]] static std::size_t color_byte_size(std::uint8_t fmt)
{
    switch (static_cast<ColorComponentFormat>(fmt)) {
    case ColorComponentFormat::RGB565:   return 2;
    case ColorComponentFormat::RGB888:   return 3;
    case ColorComponentFormat::RGBX8888: return 4;
    case ColorComponentFormat::RGBA4444: return 2;
    case ColorComponentFormat::RGBA6666: return 3;
    case ColorComponentFormat::RGBA8888: return 4;
    default:
        throw GxFatalError(
            "VertexLoader: unknown ColorComponentFormat " + std::to_string(fmt),
            0, 0);
    }
}

// VertexAttribute::count for position/texcoord:
//   0 = XY (2 components), 1 = XYZ (3 components).
// Normal always has 3 components. NBT stores normal, tangent, and binormal
// triples; normal_index_3 selects whether indexed NBT uses one shared index or
// three separate indices.
[[nodiscard]] static std::size_t pos_component_count(std::uint8_t count)
{
    return (count == 0) ? 2u : 3u;   // XY / XYZ
}

[[nodiscard]] static std::size_t tex_component_count(std::uint8_t count)
{
    return (count == 0) ? 1u : 2u;   // S / ST
}

// De-quantise a fixed-point scalar to float.
[[nodiscard]] static float dequant_u8(std::uint8_t v, std::uint8_t shift)
{
    return detail::dequantize_vertex_integer(v, shift);
}
[[nodiscard]] static float dequant_s8(std::uint8_t v, std::uint8_t shift)
{
    return detail::dequantize_vertex_integer(static_cast<std::int8_t>(v), shift);
}
[[nodiscard]] static float dequant_u16(std::uint16_t v, std::uint8_t shift)
{
    return detail::dequantize_vertex_integer(v, shift);
}
[[nodiscard]] static float dequant_s16(std::uint16_t v, std::uint8_t shift)
{
    return detail::dequantize_vertex_integer(static_cast<std::int16_t>(v), shift);
}

// Decode `count` float32 big-endian components from `src` into `dst`.
// Vertex attributes contain at most three components. The bulk swap helper's
// four-lane SIMD path cannot run here, so convert directly in one local loop.
static void decode_floats(
    const std::byte* src,
    float* dst,
    std::size_t count)
{
    assert(count <= 3);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t word;
        // Unaligned and alias-safe; exactly four bytes, including edge inputs.
        std::memcpy(&word, src + i * sizeof(word), sizeof(word));
        dst[i] = std::bit_cast<float>(galaxy::byte_swap_u32(word));
    }
}

// Decode `count` components of the given ComponentFormat from big-endian `src`
// into `dst` floats, applying fixed-point de-quantisation where appropriate.
static void decode_components(
    const std::byte* src,
    float* dst,
    std::size_t count,
    ComponentFormat fmt,
    std::uint8_t shift)
{
    switch (fmt) {
    case ComponentFormat::F32:
        decode_floats(src, dst, count);
        return;

    case ComponentFormat::U8:
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = dequant_u8(
                std::to_integer<std::uint8_t>(src[i]), shift);
        }
        return;

    case ComponentFormat::S8:
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = dequant_s8(
                std::to_integer<std::uint8_t>(src[i]), shift);
        }
        return;

    case ComponentFormat::U16: {
        assert(count <= 3);
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = dequant_u16(read_be_u16(src + i * 2u), shift);
        }
        return;
    }

    case ComponentFormat::S16: {
        assert(count <= 3);
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = dequant_s16(read_be_u16(src + i * 2u), shift);
        }
        return;
    }

    default:
        throw GxFatalError(
            "VertexLoader: unsupported ComponentFormat",
            0, 0);
    }
}

// GX normal data is not controlled by the VAT fixed-point shift field used by
// position/texcoord attributes. It is stored as a fixed fractional vector:
// U8=0.7, S8=1.6, U16=0.15, S16=1.14, or raw F32. Feeding raw S8/S16 into
// lighting/emboss makes dot products and texture offsets explode.
static void decode_normal_components(
    const std::byte* src,
    float* dst,
    ComponentFormat fmt)
{
    switch (fmt) {
    case ComponentFormat::F32:
        decode_floats(src, dst, 3);
        return;

    case ComponentFormat::U8:
        for (std::size_t i = 0; i < 3; ++i) {
            dst[i] = dequant_u8(
                std::to_integer<std::uint8_t>(src[i]), 7);
        }
        return;

    case ComponentFormat::S8:
        for (std::size_t i = 0; i < 3; ++i) {
            dst[i] = dequant_s8(
                std::to_integer<std::uint8_t>(src[i]), 6);
        }
        return;

    case ComponentFormat::U16: {
        for (std::size_t i = 0; i < 3; ++i) {
            dst[i] = dequant_u16(read_be_u16(src + i * 2u), 15);
        }
        return;
    }

    case ComponentFormat::S16: {
        for (std::size_t i = 0; i < 3; ++i) {
            dst[i] = dequant_s16(read_be_u16(src + i * 2u), 14);
        }
        return;
    }

    default:
        throw GxFatalError(
            "VertexLoader: unsupported normal ComponentFormat",
            0, 0);
    }
}

// Decode one GX color value from big-endian bytes into a packed RGBA8 uint32.
// The output is (R<<24)|(G<<16)|(B<<8)|A so that byte 0 = R in little-endian
// memory, matching DXGI_FORMAT_R8G8B8A8_UNORM.
//
// CRITICAL: do NOT swap-byte these bytes — the bytes are already in the
// right per-channel order in the FIFO stream.
[[nodiscard]] static std::uint32_t decode_color(
    const std::byte* src,
    ColorComponentFormat fmt)
{
    auto b = [&](std::size_t i) {
        return std::to_integer<std::uint32_t>(src[i]);
    };

    switch (fmt) {
    case ColorComponentFormat::RGB565: {
        const std::uint16_t v = static_cast<std::uint16_t>(
            (b(0) << 8) | b(1));
        const std::uint32_t r5 = (v >> 11) & 0x1Fu;
        const std::uint32_t g6 = (v >>  5) & 0x3Fu;
        const std::uint32_t b5 =  v        & 0x1Fu;
        // Expand to 8-bit: replicate MSBs into LSBs.
        const std::uint32_t r8 = (r5 << 3) | (r5 >> 2);
        const std::uint32_t g8 = (g6 << 2) | (g6 >> 4);
        const std::uint32_t b8 = (b5 << 3) | (b5 >> 2);
        return (r8 << 24) | (g8 << 16) | (b8 << 8) | 0xFFu;
    }
    case ColorComponentFormat::RGB888:
        return (b(0) << 24) | (b(1) << 16) | (b(2) << 8) | 0xFFu;

    case ColorComponentFormat::RGBX8888:
        return (b(0) << 24) | (b(1) << 16) | (b(2) << 8) | 0xFFu;

    case ColorComponentFormat::RGBA4444: {
        const std::uint16_t v = static_cast<std::uint16_t>(
            (b(0) << 8) | b(1));
        const std::uint32_t r4 = (v >> 12) & 0xFu;
        const std::uint32_t g4 = (v >>  8) & 0xFu;
        const std::uint32_t b4 = (v >>  4) & 0xFu;
        const std::uint32_t a4 =  v        & 0xFu;
        // Expand nibble to byte: replicate into low bits.
        return ((r4 | (r4 << 4)) << 24) |
               ((g4 | (g4 << 4)) << 16) |
               ((b4 | (b4 << 4)) <<  8) |
               ((a4 | (a4 << 4)));
    }
    case ColorComponentFormat::RGBA6666: {
        // 3 bytes holding 4x6 bits packed as RRRRRRGG GGGGBBBB BBAAAAAA.
        const std::uint32_t v = (b(0) << 16) | (b(1) << 8) | b(2);
        const std::uint32_t r6 = (v >> 18) & 0x3Fu;
        const std::uint32_t g6 = (v >> 12) & 0x3Fu;
        const std::uint32_t b6 = (v >>  6) & 0x3Fu;
        const std::uint32_t a6 =  v        & 0x3Fu;
        // Expand 6-bit to 8-bit: (val << 2) | (val >> 4).
        const std::uint32_t r8 = (r6 << 2) | (r6 >> 4);
        const std::uint32_t g8 = (g6 << 2) | (g6 >> 4);
        const std::uint32_t b8 = (b6 << 2) | (b6 >> 4);
        const std::uint32_t a8 = (a6 << 2) | (a6 >> 4);
        return (r8 << 24) | (g8 << 16) | (b8 << 8) | a8;
    }
    case ColorComponentFormat::RGBA8888:
        return (b(0) << 24) | (b(1) << 16) | (b(2) << 8) | b(3);

    default:
        throw GxFatalError(
            "VertexLoader: unsupported ColorComponentFormat",
            0, 0);
    }
}

// Decode position or normal from a big-endian source pointer into GxVertexOut.
static void decode_pos(
    const std::byte* src,
    GxVertexOut& out,
    const VertexAttribute& attr,
    bool byte_dequant)
{
    const std::size_t nc = pos_component_count(attr.count);
    const auto fmt = static_cast<ComponentFormat>(attr.format);
    const std::uint8_t shift = !byte_dequant &&
        (fmt == ComponentFormat::U8 || fmt == ComponentFormat::S8)
        ? 0u : attr.shift;
    decode_components(src, out.position, nc, fmt, shift);
    if (nc < 3) {
        out.position[2] = 0.0f;
    }
}

static void decode_nrm(
    const std::byte* src,
    GxVertexOut& out,
    const VertexAttribute& attr)
{
    const auto fmt = static_cast<ComponentFormat>(attr.format);
    decode_normal_components(src, out.normal, fmt);
}

static void decode_tangent(
    const std::byte* src,
    GxVertexOut& out,
    const VertexAttribute& attr)
{
    const auto fmt = static_cast<ComponentFormat>(attr.format);
    decode_normal_components(src, out.tangent, fmt);
}

static void decode_binormal(
    const std::byte* src,
    GxVertexOut& out,
    const VertexAttribute& attr)
{
    const auto fmt = static_cast<ComponentFormat>(attr.format);
    decode_normal_components(src, out.binormal, fmt);
}

static void zero_vec3(float (&v)[3])
{
    v[0] = 0.0f;
    v[1] = 0.0f;
    v[2] = 0.0f;
}

static bool normal_has_nbt(const VertexDescriptor& desc)
{
    return desc.normal.count != 0u;
}

static bool normal_uses_three_indices(const VertexDescriptor& desc)
{
    return normal_has_nbt(desc) && desc.normal_index_3;
}

static std::size_t normal_elem_size(const VertexAttribute& attr)
{
    return component_byte_size(attr.format) * 3u;
}

static void decode_nbt(
    const std::byte* src,
    GxVertexOut& out,
    const VertexAttribute& attr,
    std::size_t elem_size)
{
    decode_nrm(src, out, attr);
    decode_tangent(src + elem_size, out, attr);
    decode_binormal(src + elem_size * 2u, out, attr);
}

static void decode_tex(
    const std::byte* src,
    float* uv,
    const VertexAttribute& attr,
    bool byte_dequant)
{
    const std::size_t nc = tex_component_count(attr.count);
    const auto fmt = static_cast<ComponentFormat>(attr.format);
    const std::uint8_t shift = !byte_dequant &&
        (fmt == ComponentFormat::U8 || fmt == ComponentFormat::S8)
        ? 0u : attr.shift;
    decode_components(src, uv, nc, fmt, shift);
    if (nc < 2) {
        uv[1] = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// source_vertex_size helpers
// ---------------------------------------------------------------------------

static std::size_t attr_direct_size_pos(const VertexAttribute& attr)
{
    return component_byte_size(attr.format) * pos_component_count(attr.count);
}

static std::size_t attr_direct_size_nrm(const VertexAttribute& attr)
{
    // Normal count 1 is GX_NRM_NBT: direct streams one N/T/B triple.
    const std::size_t base = normal_elem_size(attr);
    return attr.count != 0u ? base * 3u : base;
}

static std::size_t attr_direct_size_tex(const VertexAttribute& attr)
{
    return component_byte_size(attr.format) * tex_component_count(attr.count);
}

[[nodiscard]] std::uint32_t primitive_index_count(
    PrimitiveClass primitive,
    std::uint32_t vtx_count,
    std::size_t fifo_error_offset) {
    switch (primitive) {
    case PrimitiveClass::Quads:
    case PrimitiveClass::Quads2:
        return (vtx_count / 4u) * 6u;
    case PrimitiveClass::Triangles:
        return (vtx_count / 3u) * 3u;
    case PrimitiveClass::TriangleStrip:
    case PrimitiveClass::TriangleFan:
        return vtx_count >= 3u ? (vtx_count - 2u) * 3u : 0u;
    case PrimitiveClass::Lines:
        return (vtx_count / 2u) * 2u;
    case PrimitiveClass::LineStrip:
        return vtx_count >= 2u ? (vtx_count - 1u) * 2u : 0u;
    case PrimitiveClass::Points:
        return vtx_count;
    default:
        throw GxFatalError(
            "VertexLoader: unknown PrimitiveClass",
            fifo_error_offset,
            0);
    }
}

struct PreparedVertexInputs {
    std::array<std::uint32_t, 12> element_bytes{};
    std::array<std::uint32_t, 12> array_bases{};
    std::array<std::uint32_t, 12> array_strides{};
    std::uint32_t normal_vector_bytes = 0u;
    std::uint8_t default_position_matrix = 0u;
    // Bit `ti` set when texture coordinate channel `ti` is present in the
    // vertex stream (Direct/Index8/Index16). The decode loops use it to skip
    // the channels that can only write two zeros and consume no bytes; SMG
    // leaves six or seven of the eight channels empty on ordinary draws.
    std::uint8_t active_texcoord_mask = 0u;
};

PreparedVertexInputs prepare_vertex_inputs(
    const VertexDescriptor& desc,
    const GxState& state) {
    PreparedVertexInputs inputs{};
    if (!desc.has_pn_matrix_index) {
        inputs.default_position_matrix = static_cast<std::uint8_t>(
            state.cp(cp::kMatrixIndexA) & 0x3fu);
    }
    // The draw/run owns one fixed CP layout. Resolve its sizes and indexed
    // array settings once, keeping per-vertex reads and byte conversion below.
    for (unsigned attr = 0u; attr < inputs.element_bytes.size(); ++attr) {
        const VertexAttribute& attribute = attr == 0u ? desc.position :
            attr == 1u ? desc.normal : attr < 4u ? desc.color[attr - 2u] :
            desc.texcoord[attr - 4u];
        if (attribute.vcd != VcdType::Direct &&
            attribute.vcd != VcdType::Index8 && attribute.vcd != VcdType::Index16) {
            continue;
        }
        std::size_t size = 0u;
        if (attr == 0u) {
            size = attr_direct_size_pos(attribute);
        } else if (attr == 1u) {
            inputs.normal_vector_bytes = static_cast<std::uint32_t>(normal_elem_size(attribute));
            size = inputs.normal_vector_bytes * (attribute.count != 0u ? 3u : 1u);
        } else if (attr < 4u) {
            size = color_byte_size(attribute.format);
        } else {
            size = attr_direct_size_tex(attribute);
        }
        inputs.element_bytes[attr] = static_cast<std::uint32_t>(size);
        if (attribute.vcd == VcdType::Index8 || attribute.vcd == VcdType::Index16) {
            inputs.array_bases[attr] = state.array_base(attr);
            inputs.array_strides[attr] = state.array_stride(attr);
        }
        if (attr >= 4u) {
            inputs.active_texcoord_mask |= static_cast<std::uint8_t>(
                1u << (attr - 4u));
        }
    }
    return inputs;
}

void decode_vertex_stream(
    const std::byte* raw_ptr,
    std::uint32_t vtx_count,
    std::size_t src_stride,
    const VertexDescriptor& desc,
    const PreparedVertexInputs& inputs,
    IndexedArrayResolver& array_resolver,
    std::size_t fifo_error_offset,
    GxVertexOut* verts) {
    std::uint8_t has_tex_matrix_index_mask = 0u;
    for (unsigned ti = 0; ti < 8; ++ti) {
        if (desc.has_tex_matrix_index[ti]) {
            has_tex_matrix_index_mask |= static_cast<std::uint8_t>(1u << ti);
        }
    }
    // Both predicates are pure functions of the per-draw layout, so evaluate
    // them once instead of once per vertex. `off != src_stride` below still
    // proves the same byte consumption.
    const bool normal_needs_three_indices = normal_uses_three_indices(desc);
    const bool normal_carries_nbt = normal_has_nbt(desc);
    const std::uint32_t normal_index_width =
        (desc.normal.vcd == VcdType::Index8) ? 1u : 2u;
    for (std::uint32_t vi = 0; vi < vtx_count; ++vi) {
        GxVertexOut& out = verts[vi];
        const std::byte* vsrc =
            raw_ptr + static_cast<std::size_t>(vi) * src_stride;
        std::size_t off = 0;

        std::uint8_t pnmtx_idx = inputs.default_position_matrix;
        std::uint8_t texmtx_idx[8]{};

        if (desc.has_pn_matrix_index) {
            pnmtx_idx = std::to_integer<std::uint8_t>(vsrc[off++]);
        }
        // `desc.has_tex_matrix_index` is a per-draw constant, so its 8-bit
        // pattern is available as a mask: iterate only the indices the stream
        // actually carries and let the zero initializer above cover the rest.
        // `off` therefore advances by exactly the same amount as before.
        for (unsigned ti = 0; ti < 8; ++ti) {
            if ((has_tex_matrix_index_mask & (1u << ti)) != 0u) {
                texmtx_idx[ti] = std::to_integer<std::uint8_t>(vsrc[off++]);
            }
        }
        out.mtx_indices[0] =
            static_cast<std::uint32_t>(pnmtx_idx) |
            (static_cast<std::uint32_t>(texmtx_idx[0]) << 8) |
            (static_cast<std::uint32_t>(texmtx_idx[1]) << 16) |
            (static_cast<std::uint32_t>(texmtx_idx[2]) << 24);
        out.mtx_indices[1] =
            static_cast<std::uint32_t>(texmtx_idx[3]) |
            (static_cast<std::uint32_t>(texmtx_idx[4]) << 8) |
            (static_cast<std::uint32_t>(texmtx_idx[5]) << 16) |
            (static_cast<std::uint32_t>(texmtx_idx[6]) << 24);
        out.mtx_indices[2] = static_cast<std::uint32_t>(texmtx_idx[7]);
        out.mtx_indices[3] = 0u;

        if (desc.position.vcd == VcdType::Direct) {
            const std::size_t sz = inputs.element_bytes[0u];
            decode_pos(vsrc + off, out, desc.position, desc.byte_dequant);
            off += sz;
        } else if (desc.position.vcd == VcdType::Index8 ||
                   desc.position.vcd == VcdType::Index16) {
            std::uint32_t idx = 0;
            if (desc.position.vcd == VcdType::Index8) {
                idx = std::to_integer<std::uint8_t>(vsrc[off]);
                off += 1;
            } else {
                idx = read_be_u16(vsrc + off);
                off += 2;
            }
            const std::uint32_t arr_base = inputs.array_bases[0u];
            const std::uint32_t arr_stride = inputs.array_strides[0u];
            const std::uint32_t addr = arr_base + idx * arr_stride;
            const std::byte* ap = array_resolver.resolve(
                0u,
                addr,
                inputs.element_bytes[0u],
                fifo_error_offset);
            decode_pos(ap, out, desc.position, desc.byte_dequant);
        } else {
            out.position[0] = 0.0f;
            out.position[1] = 0.0f;
            out.position[2] = 0.0f;
        }

        if (desc.normal.vcd == VcdType::Direct) {
            const std::size_t sz = inputs.element_bytes[1u];
            if (normal_carries_nbt) {
                decode_nbt(vsrc + off, out, desc.normal, inputs.normal_vector_bytes);
            } else {
                decode_nrm(vsrc + off, out, desc.normal);
                zero_vec3(out.tangent);
                zero_vec3(out.binormal);
            }
            off += sz;
        } else if (desc.normal.vcd == VcdType::Index8 ||
                   desc.normal.vcd == VcdType::Index16) {
            const unsigned attr_idx = 1u;
            const std::uint32_t arr_base = inputs.array_bases[attr_idx];
            const std::uint32_t arr_stride = inputs.array_strides[attr_idx];

            if (!normal_needs_three_indices) {
                std::uint32_t idx = 0;
                if (desc.normal.vcd == VcdType::Index8) {
                    idx = std::to_integer<std::uint8_t>(vsrc[off]);
                    off += 1;
                } else {
                    idx = read_be_u16(vsrc + off);
                    off += 2;
                }
                const std::uint32_t read_sz = inputs.element_bytes[1u];
                const std::uint32_t addr = arr_base + idx * arr_stride;
                const std::byte* ap = array_resolver.resolve(
                    attr_idx,
                    addr,
                    read_sz,
                    fifo_error_offset);
                if (normal_carries_nbt) {
                    decode_nbt(ap, out, desc.normal, inputs.normal_vector_bytes);
                } else {
                    decode_nrm(ap, out, desc.normal);
                    zero_vec3(out.tangent);
                    zero_vec3(out.binormal);
                }
            } else {
                const std::size_t idx_bytes = normal_index_width;
                std::uint32_t idx[3]{};
                for (unsigned ni = 0; ni < 3; ++ni) {
                    if (desc.normal.vcd == VcdType::Index8) {
                        idx[ni] = std::to_integer<std::uint8_t>(
                            vsrc[off + ni]);
                    } else {
                        idx[ni] = read_be_u16(vsrc + off + ni * idx_bytes);
                    }
                }
                off += idx_bytes * 3u;

                const std::size_t elem_sz = inputs.normal_vector_bytes;
                const std::byte* n = array_resolver.resolve(
                    attr_idx,
                    arr_base + idx[0] * arr_stride,
                    static_cast<std::uint32_t>(elem_sz),
                    fifo_error_offset);
                const std::byte* t = array_resolver.resolve(
                    attr_idx,
                    arr_base + idx[1] * arr_stride + static_cast<std::uint32_t>(elem_sz),
                    static_cast<std::uint32_t>(elem_sz),
                    fifo_error_offset);
                const std::byte* b = array_resolver.resolve(
                    attr_idx,
                    arr_base + idx[2] * arr_stride + static_cast<std::uint32_t>(elem_sz * 2u),
                    static_cast<std::uint32_t>(elem_sz),
                    fifo_error_offset);
                decode_nrm(n, out, desc.normal);
                decode_tangent(t, out, desc.normal);
                decode_binormal(b, out, desc.normal);
            }
        } else {
            zero_vec3(out.normal);
            zero_vec3(out.tangent);
            zero_vec3(out.binormal);
        }

        for (unsigned ci = 0; ci < 2; ++ci) {
            const VertexAttribute& ca = desc.color[ci];
            std::uint32_t& cdst = (ci == 0) ? out.color0 : out.color1;
            const unsigned attr_idx = 2u + ci;

            if (ca.vcd == VcdType::Direct) {
                const std::size_t sz = inputs.element_bytes[attr_idx];
                cdst = decode_color(
                    vsrc + off,
                    static_cast<ColorComponentFormat>(ca.format));
                off += sz;
            } else if (ca.vcd == VcdType::Index8 ||
                       ca.vcd == VcdType::Index16) {
                std::uint32_t idx = 0;
                if (ca.vcd == VcdType::Index8) {
                    idx = std::to_integer<std::uint8_t>(vsrc[off]);
                    off += 1;
                } else {
                    idx = read_be_u16(vsrc + off);
                    off += 2;
                }
                const std::uint32_t arr_base = inputs.array_bases[attr_idx];
                const std::uint32_t arr_stride = inputs.array_strides[attr_idx];
                const std::uint32_t addr = arr_base + idx * arr_stride;
                const std::byte* ap = array_resolver.resolve(
                    attr_idx,
                    addr,
                    inputs.element_bytes[attr_idx],
                    fifo_error_offset);
                cdst = decode_color(
                    ap, static_cast<ColorComponentFormat>(ca.format));
            } else {
                cdst = 0u;
            }
        }

        for (unsigned ti = 0; ti < 8; ++ti) {
            const VertexAttribute& ta = desc.texcoord[ti];
            float* uv = out.uv[ti];
            const unsigned attr_idx = 4u + ti;

            // A channel absent from the stream must still deliver (0, 0) to the
            // shader, which is what GX hardware and Dolphin supply for an absent
            // attribute. `out` is raw upload-ring memory, NOT a value-initialized
            // object: GxVertexOut has no default member initializers and
            // UploadRing::allocate only bumps a cursor, so whatever the previous
            // frame left at this offset is still there. Skipping the store would
            // hand that stale pair to the vertex shader.
            //
            // The cached packet-run path already zeroes the identical fields
            // (decoded.vertices.resize() value-initializes the whole 144-byte
            // vertex, then the decode fills only the present channels), so
            // restoring these stores also makes the two decode paths agree again.
            // This is eight bytes per absent channel per vertex, not per
            // component, and the D3D12 input layout binds all eight TEXCOORDs
            // unconditionally whether or not the VS samples them.
            if ((inputs.active_texcoord_mask & (1u << ti)) == 0u) {
                out.uv[ti][0] = 0.0f;
                out.uv[ti][1] = 0.0f;
                continue;
            }

            if (ta.vcd == VcdType::Direct) {
                const std::size_t sz = inputs.element_bytes[attr_idx];
                decode_tex(vsrc + off, uv, ta, desc.byte_dequant);
                off += sz;
            } else if (ta.vcd == VcdType::Index8 ||
                       ta.vcd == VcdType::Index16) {
                std::uint32_t idx = 0;
                if (ta.vcd == VcdType::Index8) {
                    idx = std::to_integer<std::uint8_t>(vsrc[off]);
                    off += 1;
                } else {
                    idx = read_be_u16(vsrc + off);
                    off += 2;
                }
                const std::uint32_t arr_base = inputs.array_bases[attr_idx];
                const std::uint32_t arr_stride = inputs.array_strides[attr_idx];
                const std::uint32_t addr = arr_base + idx * arr_stride;
                const std::size_t elem_sz = inputs.element_bytes[attr_idx];
                const std::byte* ap = array_resolver.resolve(
                    attr_idx,
                    addr,
                    static_cast<std::uint32_t>(elem_sz),
                    fifo_error_offset);
                decode_tex(ap, uv, ta, desc.byte_dequant);
            }
        }

        if (off != src_stride) {
            throw GxFatalError(
                "VertexLoader: source vertex decode size mismatch",
                fifo_error_offset,
                0);
        }
    }
}

void emit_primitive_indices(
    std::uint16_t* idx_ptr,
    std::uint32_t& idx_count,
    PrimitiveClass primitive,
    std::uint32_t packet_vertex_base,
    std::uint32_t vtx_count,
    std::uint32_t index_vertex_bias,
    std::size_t fifo_error_offset) {
    // Prove the largest index once per packet, rather than checking every
    // emitted index. Incomplete independent primitives do not reference their
    // trailing vertices; degenerate packets reference none at all.
    std::uint32_t referenced_vertices = 0u;
    switch (primitive) {
    case PrimitiveClass::Quads:
    case PrimitiveClass::Quads2:
        referenced_vertices = (vtx_count / 4u) * 4u;
        break;
    case PrimitiveClass::Triangles:
        referenced_vertices = (vtx_count / 3u) * 3u;
        break;
    case PrimitiveClass::Lines:
        referenced_vertices = (vtx_count / 2u) * 2u;
        break;
    case PrimitiveClass::TriangleStrip:
    case PrimitiveClass::TriangleFan:
        referenced_vertices = vtx_count >= 3u ? vtx_count : 0u;
        break;
    case PrimitiveClass::LineStrip:
        referenced_vertices = vtx_count >= 2u ? vtx_count : 0u;
        break;
    case PrimitiveClass::Points:
        referenced_vertices = vtx_count;
        break;
    default:
        throw GxFatalError(
            "VertexLoader: unknown PrimitiveClass", fifo_error_offset, 0);
    }
    const std::uint64_t index_base =
        static_cast<std::uint64_t>(packet_vertex_base) + index_vertex_bias;
    if (referenced_vertices != 0u &&
        index_base + referenced_vertices - 1u >
            std::numeric_limits<std::uint16_t>::max()) {
        throw GxFatalError(
            "VertexLoader: indexed batch exceeded 16-bit index range",
            fifo_error_offset,
            0);
    }
    const auto bounded_index_base = static_cast<std::uint32_t>(index_base);
    auto emit_index = [&](std::uint32_t value) {
        idx_ptr[idx_count++] = static_cast<std::uint16_t>(bounded_index_base + value);
    };
    auto emit = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        emit_index(a);
        emit_index(b);
        emit_index(c);
    };
    auto emit2 = [&](std::uint32_t a, std::uint32_t b) {
        emit_index(a);
        emit_index(b);
    };

    switch (primitive) {
    case PrimitiveClass::Quads:
    case PrimitiveClass::Quads2: {
        const std::uint32_t n = (vtx_count / 4u) * 4u;
        for (std::uint32_t i = 0; i < n; i += 4u) {
            emit(i, i + 1u, i + 2u);
            emit(i, i + 2u, i + 3u);
        }
        break;
    }
    case PrimitiveClass::Triangles:
        for (std::uint32_t i = 0; i < (vtx_count / 3u) * 3u; ++i) {
            emit_index(i);
        }
        break;
    case PrimitiveClass::TriangleStrip:
        for (std::uint32_t i = 0; i + 2u < vtx_count; ++i) {
            if ((i & 1u) == 0u) {
                emit(i, i + 1u, i + 2u);
            } else {
                emit(i + 1u, i, i + 2u);
            }
        }
        break;
    case PrimitiveClass::TriangleFan:
        for (std::uint32_t i = 1u; i + 1u < vtx_count; ++i) {
            emit(0u, i, i + 1u);
        }
        break;
    case PrimitiveClass::Lines:
        for (std::uint32_t i = 0; i + 1u < vtx_count; i += 2u) {
            emit2(i, i + 1u);
        }
        break;
    case PrimitiveClass::LineStrip:
        for (std::uint32_t i = 0; i + 1u < vtx_count; ++i) {
            emit2(i, i + 1u);
        }
        break;
    case PrimitiveClass::Points:
        for (std::uint32_t i = 0; i < vtx_count; ++i) {
            emit_index(i);
        }
        break;
    default:
        throw GxFatalError(
            "VertexLoader: unknown PrimitiveClass",
            fifo_error_offset,
            0);
    }
}

}  // anonymous namespace

bool detail::rebase_cached_indices(std::span<const std::uint16_t> source,
                                   std::uint32_t bias,
                                   std::uint16_t* destination) noexcept {
    if (source.empty()) return true;
    if (bias > std::numeric_limits<std::uint16_t>::max()) return false;
    std::size_t i = 0u;
    if (source.size() >= 8u) {
        const __m128i offsets = _mm_set1_epi16(
            std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bias)));
        do {
            const __m128i indices = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(source.data() + i));
            const __m128i wrapped = _mm_add_epi16(indices, offsets);
            const __m128i saturated = _mm_adds_epu16(indices, offsets);
            // Both additions agree exactly through 65535. Above it the saturating
            // result is 65535 and the wrapped result is smaller, in any lane.
            if (_mm_movemask_epi8(_mm_cmpeq_epi16(wrapped, saturated)) != 0xffff) {
                return false;
            }
            _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + i), wrapped);
            i += 8u;
        } while (source.size() - i >= 8u);
    }
    for (; i < source.size(); ++i) {
        const std::uint32_t value = static_cast<std::uint32_t>(source[i]) + bias;
        if (value > std::numeric_limits<std::uint16_t>::max()) return false;
        destination[i] = static_cast<std::uint16_t>(value);
    }
    return true;
}

// ---------------------------------------------------------------------------
// VertexLoader::source_vertex_size
// ---------------------------------------------------------------------------

std::size_t VertexLoader::source_vertex_size(const VertexDescriptor& desc)
{
    std::size_t sz = 0;

    // Matrix index bytes (always 1 byte each when present, regardless of
    // the attribute type — these are always Direct in the FIFO).
    if (desc.has_pn_matrix_index) {
        sz += 1;
    }
    for (unsigned i = 0; i < 8; ++i) {
        if (desc.has_tex_matrix_index[i]) {
            sz += 1;
        }
    }

    // Position.
    switch (desc.position.vcd) {
    case VcdType::None:   break;
    case VcdType::Direct: sz += attr_direct_size_pos(desc.position); break;
    case VcdType::Index8: sz += 1; break;
    case VcdType::Index16: sz += 2; break;
    }

    // Normal.
    switch (desc.normal.vcd) {
    case VcdType::None:   break;
    case VcdType::Direct: sz += attr_direct_size_nrm(desc.normal); break;
    case VcdType::Index8:
        sz += normal_uses_three_indices(desc) ? 3u : 1u;
        break;
    case VcdType::Index16:
        sz += normal_uses_three_indices(desc) ? 6u : 2u;
        break;
    }

    // Color0, Color1.
    for (unsigned c = 0; c < 2; ++c) {
        switch (desc.color[c].vcd) {
        case VcdType::None:   break;
        case VcdType::Direct: sz += color_byte_size(desc.color[c].format); break;
        case VcdType::Index8: sz += 1; break;
        case VcdType::Index16: sz += 2; break;
        }
    }

    // Texcoords 0-7.
    for (unsigned t = 0; t < 8; ++t) {
        switch (desc.texcoord[t].vcd) {
        case VcdType::None:   break;
        case VcdType::Direct: sz += attr_direct_size_tex(desc.texcoord[t]); break;
        case VcdType::Index8: sz += 1; break;
        case VcdType::Index16: sz += 2; break;
        }
    }

    return sz;
}

// ---------------------------------------------------------------------------
// VertexLoader::load — main entry point
// ---------------------------------------------------------------------------

LoadedPrimitive VertexLoader::load(
    FifoCursor& cursor,
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    const GxState& state,
    GuestMemoryV1* memory,
    UploadRing& vertex_ring,
    UploadRing& index_ring,
    std::uint32_t index_batch_base_vertex,
    bool rebase_indices_to_batch_base)
{
    const VertexDescriptor desc = state.vertex_desc(vtxfmt);
    const std::size_t src_stride = source_vertex_size(desc);
    return load_with_layout(
        cursor,
        primitive,
        vtxfmt,
        state,
        desc,
        src_stride,
        memory,
        vertex_ring,
        index_ring,
        index_batch_base_vertex,
        rebase_indices_to_batch_base);
}

LoadedPrimitive VertexLoader::load_with_layout(
    FifoCursor& cursor,
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    const GxState& state,
    const VertexDescriptor& desc,
    std::size_t src_stride,
    GuestMemoryV1* memory,
    UploadRing& vertex_ring,
    UploadRing& index_ring,
    std::uint32_t index_batch_base_vertex,
    bool rebase_indices_to_batch_base)
{
    (void)vtxfmt;
    const std::uint16_t vtx_count = cursor.read_u16();

    if (vtx_count == 0) {
        // Nothing to do; return an empty primitive.
        LoadedPrimitive empty{};
        empty.source_class = primitive;
        return empty;
    }

    // ---------------------------------------------------------------------------
    // Allocate output vertex buffer in the upload ring.
    // ---------------------------------------------------------------------------
    const std::size_t vb_size = static_cast<std::size_t>(vtx_count) *
                                 sizeof(GxVertexOut);
    const UploadRing::Allocation vb = vertex_ring.allocate(vb_size, 16);
    const auto verts = reinterpret_cast<GxVertexOut*>(vb.cpu);
    const std::uint32_t base_vertex =
        static_cast<std::uint32_t>(vb.offset / sizeof(GxVertexOut));
    if (rebase_indices_to_batch_base &&
        base_vertex < index_batch_base_vertex) {
        throw GxFatalError(
            "VertexLoader: indexed batch base is after primitive vertices",
            cursor.base_offset,
            0);
    }
    const std::uint32_t index_vertex_bias = rebase_indices_to_batch_base
        ? (base_vertex - index_batch_base_vertex)
        : 0u;

    // ---------------------------------------------------------------------------
    // Take the raw FIFO window for all vertex data in one shot.
    // ---------------------------------------------------------------------------
    const std::size_t total_raw  = src_stride * vtx_count;
    const std::span<const std::byte> raw = cursor.take(total_raw);
    const std::byte* raw_ptr = raw.data();
    const std::size_t fifo_error_offset = cursor.base_offset + cursor.offset;
    IndexedArrayResolver array_resolver(
        memory, nullptr, dependency_event_sink_);

    // ---------------------------------------------------------------------------
    // Decode each vertex.
    // ---------------------------------------------------------------------------
    const PreparedVertexInputs inputs = prepare_vertex_inputs(desc, state);
    decode_vertex_stream(
        raw_ptr, vtx_count, src_stride, desc, inputs,
        array_resolver, fifo_error_offset, verts);

    // ---------------------------------------------------------------------------
    // Build index list.
    // ---------------------------------------------------------------------------

    // Compute the maximum index count.
    std::uint32_t max_indices = 0;
    switch (primitive) {
    case PrimitiveClass::Quads:
    case PrimitiveClass::Quads2:
        // Each group of 4 verts → 2 triangles = 6 indices.
        max_indices = (static_cast<std::uint32_t>(vtx_count) / 4) * 6;
        break;
    case PrimitiveClass::Triangles:
        max_indices = (static_cast<std::uint32_t>(vtx_count) / 3u) * 3u;
        break;
    case PrimitiveClass::TriangleStrip:
        // N verts → max N-2 triangles.
        max_indices = vtx_count >= 3
            ? (static_cast<std::uint32_t>(vtx_count) - 2) * 3
            : 0;
        break;
    case PrimitiveClass::TriangleFan:
        max_indices = vtx_count >= 3
            ? (static_cast<std::uint32_t>(vtx_count) - 2) * 3
            : 0;
        break;
    case PrimitiveClass::Lines:
        max_indices = (static_cast<std::uint32_t>(vtx_count) / 2u) * 2u;
        break;
    case PrimitiveClass::LineStrip:
        max_indices = vtx_count >= 2
            ? (static_cast<std::uint32_t>(vtx_count) - 1) * 2
            : 0;
        break;
    case PrimitiveClass::Points:
        max_indices = vtx_count;
        break;
    default:
        throw GxFatalError(
            "VertexLoader: unknown PrimitiveClass",
            cursor.base_offset, 0);
    }

    if (max_indices == 0) {
        // Degenerate primitive — no index data needed.
        LoadedPrimitive prim{};
        prim.base_vertex   = base_vertex;
        prim.vertex_byte_offset = vb.offset;
        prim.vertex_count  = vtx_count;
        prim.first_index   = 0;
        prim.index_count   = 0;
        prim.indexed       = false;
        prim.source_class  = primitive;
        return prim;
    }

    if (primitive == PrimitiveClass::Triangles) {
        LoadedPrimitive prim{};
        prim.base_vertex  = base_vertex;
        prim.vertex_byte_offset = vb.offset;
        prim.vertex_count = vtx_count;
        prim.first_index  = 0;
        prim.index_count  = max_indices;
        prim.indexed      = false;
        prim.source_class = primitive;
        return prim;
    }

    const UploadRing::Allocation ib =
        index_ring.allocate(
            static_cast<std::size_t>(max_indices) * sizeof(std::uint16_t),
            sizeof(std::uint16_t));
    auto* idx_ptr = reinterpret_cast<std::uint16_t*>(ib.cpu);
    const std::uint32_t first_index =
        static_cast<std::uint32_t>(ib.offset / sizeof(std::uint16_t));

    std::uint32_t idx_count = 0;
    emit_primitive_indices(
        idx_ptr, idx_count, primitive, 0u, vtx_count,
        index_vertex_bias, cursor.base_offset);
    if (idx_count != max_indices) {
        throw GxFatalError(
            "VertexLoader: primitive emitted wrong index count",
            cursor.base_offset,
            0);
    }

    LoadedPrimitive prim{};
    prim.base_vertex  = base_vertex;
    prim.vertex_byte_offset = vb.offset;
    prim.vertex_count = vtx_count;
    prim.first_index  = first_index;
    prim.index_count  = idx_count;
    prim.indexed      = true;
    prim.source_class = primitive;

    return prim;
}

LoadedPrimitive VertexLoader::load_cached_packet_run_with_layout(
    std::span<const std::byte> bytes,
    std::size_t base_offset,
    std::span<const CachedDrawPacket> packets,
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    const GxState& state,
    const VertexDescriptor& desc,
    std::size_t src_stride,
    GuestMemoryV1* memory,
    UploadRing& vertex_ring,
    UploadRing& index_ring,
    std::uint32_t index_batch_base_vertex,
    bool rebase_indices_to_batch_base,
    std::uint32_t precomputed_total_vertices,
    std::uint32_t precomputed_total_indices,
    std::span<const std::uint16_t> precomputed_indices) {
    const DecodedPacketRunVertices decoded =
        decode_cached_packet_run_vertices_with_layout(
            bytes,
            base_offset,
            packets,
            primitive,
            vtxfmt,
            state,
            desc,
            src_stride,
            memory,
            precomputed_total_vertices,
            precomputed_total_indices);
    return upload_cached_packet_run_vertices(
        decoded.vertices,
        base_offset,
        packets,
        primitive,
        vertex_ring,
        index_ring,
        index_batch_base_vertex,
        rebase_indices_to_batch_base,
        decoded.total_indices,
        precomputed_indices);
}

DecodedPacketRunVertices
VertexLoader::decode_cached_packet_run_vertices_with_layout(
    std::span<const std::byte> bytes,
    std::size_t base_offset,
    std::span<const CachedDrawPacket> packets,
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    const GxState& state,
    const VertexDescriptor& desc,
    std::size_t src_stride,
    GuestMemoryV1* memory,
    std::uint32_t precomputed_total_vertices,
    std::uint32_t precomputed_total_indices,
    bool record_indexed_read_ranges,
    std::vector<GxVertexOut>* reusable_vertices) {
    (void)vtxfmt;
    DecodedPacketRunVertices decoded{};
    if (packets.empty()) {
        return decoded;
    }

    std::uint32_t actual_vertices = 0;
    std::uint32_t actual_indices = 0;
    for (const CachedDrawPacket& packet : packets) {
        const std::size_t expected_payload =
            src_stride * static_cast<std::size_t>(packet.vertex_count);
        if (packet.draw_payload_size != expected_payload ||
            packet.draw_cursor_offset + 2u < packet.draw_cursor_offset ||
            packet.draw_cursor_offset + 2u + packet.draw_payload_size >
                bytes.size()) {
            throw GxFatalError(
                "GX FIFO: cached packet draw run packet out of range",
                base_offset + packet.local_opcode_offset,
                packet.opcode);
        }
        if (packet.vertex_count == 0u) {
            continue;
        }
        if (actual_vertices >
            static_cast<std::uint32_t>(
                std::numeric_limits<std::uint16_t>::max()) -
                packet.vertex_count) {
            throw GxFatalError(
                "VertexLoader: cached packet run exceeded 16-bit vertex span",
                base_offset + packet.local_opcode_offset,
                packet.opcode);
        }
        const std::uint32_t packet_index_count = primitive_index_count(
            primitive,
            packet.vertex_count,
            base_offset + packet.local_opcode_offset);
        if (actual_indices >
            std::numeric_limits<std::uint32_t>::max() - packet_index_count) {
            throw GxFatalError(
                "VertexLoader: cached packet run index count overflow",
                base_offset + packet.local_opcode_offset,
                packet.opcode);
        }
        actual_vertices += packet.vertex_count;
        actual_indices += packet_index_count;
    }

    std::uint32_t total_vertices = actual_vertices;
    std::uint32_t total_indices = actual_indices;
    if (precomputed_total_vertices != 0u ||
        precomputed_total_indices != 0u) {
        if (precomputed_total_vertices != actual_vertices ||
            precomputed_total_indices != actual_indices) {
            throw GxFatalError(
                "VertexLoader: cached packet run precomputed totals mismatch",
                base_offset + packets.front().local_opcode_offset,
                packets.front().opcode);
        }
        total_vertices = precomputed_total_vertices;
        total_indices = precomputed_total_indices;
    }

    if (total_vertices >
        static_cast<std::uint32_t>(
            std::numeric_limits<std::uint16_t>::max())) {
        throw GxFatalError(
            "VertexLoader: cached packet run exceeded 16-bit vertex span",
            base_offset + packets.front().local_opcode_offset,
            packets.front().opcode);
    }

    if (total_vertices == 0u) {
        decoded.total_indices = total_indices;
        return decoded;
    }

    decoded.total_vertices = total_vertices;
    decoded.total_indices = total_indices;
    if (reusable_vertices != nullptr) {
        decoded.vertices.swap(*reusable_vertices);
        decoded.vertices.clear();
    }
    decoded.vertices.resize(total_vertices);

    IndexedArrayResolver array_resolver(
        memory,
        record_indexed_read_ranges ? &decoded.guest_array_reads : nullptr,
        dependency_event_sink_);
    const PreparedVertexInputs inputs = prepare_vertex_inputs(desc, state);
    std::uint32_t dst_vertex = 0;
    for (const CachedDrawPacket& packet : packets) {
        if (packet.vertex_count == 0u) {
            continue;
        }
        const std::byte* raw_ptr =
            bytes.data() + packet.draw_cursor_offset + 2u;
        decode_vertex_stream(
            raw_ptr,
            packet.vertex_count,
            src_stride,
            desc,
            inputs,
            array_resolver,
            base_offset + packet.local_opcode_offset,
            decoded.vertices.data() + dst_vertex);
        dst_vertex += packet.vertex_count;
    }
    if (dst_vertex != total_vertices) {
        throw GxFatalError(
            "VertexLoader: cached packet run decoded wrong vertex count",
            base_offset + packets.front().local_opcode_offset,
            packets.front().opcode);
    }
    array_resolver.emit_recorded_ranges();
    return decoded;
}

LoadedPrimitive VertexLoader::upload_cached_packet_run_vertices(
    std::span<const GxVertexOut> decoded_vertices,
    std::size_t base_offset,
    std::span<const CachedDrawPacket> packets,
    PrimitiveClass primitive,
    UploadRing& vertex_ring,
    UploadRing& index_ring,
    std::uint32_t index_batch_base_vertex,
    bool rebase_indices_to_batch_base,
    std::uint32_t total_indices,
    std::span<const std::uint16_t> precomputed_indices,
    ImmutableUploadToken* upload_token) {
    const std::uint32_t total_vertices =
        static_cast<std::uint32_t>(decoded_vertices.size());
    const std::size_t error_offset =
        base_offset +
        (!packets.empty() ? packets.front().local_opcode_offset : 0u);
    const std::uint8_t error_opcode =
        !packets.empty() ? packets.front().opcode : 0u;
    if (decoded_vertices.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        throw GxFatalError(
            "VertexLoader: cached packet run exceeded 16-bit vertex span",
            error_offset,
            error_opcode);
    }

    if (total_vertices == 0u) {
        LoadedPrimitive empty{};
        empty.source_class = primitive;
        return empty;
    }

    const std::size_t vb_size =
        static_cast<std::size_t>(total_vertices) * sizeof(GxVertexOut);
    const UploadRing::Allocation vb = upload_token != nullptr
        ? vertex_ring.upload_immutable(
              decoded_vertices.data(), vb_size, 16, *upload_token)
        : vertex_ring.allocate(vb_size, 16);
    if (upload_token == nullptr) {
        std::memcpy(vb.cpu, decoded_vertices.data(), vb_size);
    }
    const std::uint32_t base_vertex =
        static_cast<std::uint32_t>(vb.offset / sizeof(GxVertexOut));
    if (rebase_indices_to_batch_base &&
        base_vertex < index_batch_base_vertex) {
        throw GxFatalError(
            "VertexLoader: indexed batch base is after primitive vertices",
            error_offset,
            error_opcode);
    }
    const std::uint32_t index_vertex_bias = rebase_indices_to_batch_base
        ? (base_vertex - index_batch_base_vertex)
        : 0u;

    if (total_indices == 0u) {
        LoadedPrimitive prim{};
        prim.base_vertex = base_vertex;
        prim.vertex_byte_offset = vb.offset;
        prim.vertex_count = total_vertices;
        prim.first_index = 0;
        prim.index_count = 0;
        prim.indexed = false;
        prim.source_class = primitive;
        return prim;
    }

    if (primitive == PrimitiveClass::Triangles &&
        std::all_of(packets.begin(), packets.end(),
            [](const CachedDrawPacket& packet) {
                return packet.vertex_count % 3u == 0u;
            })) {
        LoadedPrimitive prim{};
        prim.base_vertex = base_vertex;
        prim.vertex_byte_offset = vb.offset;
        prim.vertex_count = total_vertices;
        prim.first_index = 0;
        prim.index_count = total_vertices;
        prim.indexed = false;
        prim.source_class = primitive;
        return prim;
    }

    const UploadRing::Allocation ib = index_ring.allocate(
        static_cast<std::size_t>(total_indices) * sizeof(std::uint16_t),
        sizeof(std::uint16_t));
    auto* idx_ptr = reinterpret_cast<std::uint16_t*>(ib.cpu);
    const std::uint32_t first_index =
        static_cast<std::uint32_t>(ib.offset / sizeof(std::uint16_t));

    std::uint32_t idx_count = 0;
    if (!precomputed_indices.empty()) {
        if (precomputed_indices.size() != total_indices) {
            throw GxFatalError(
                "VertexLoader: cached packet run index payload size mismatch",
                error_offset,
                error_opcode);
        }
        if (index_vertex_bias == 0u) {
            std::memcpy(
                idx_ptr,
                precomputed_indices.data(),
                precomputed_indices.size() * sizeof(std::uint16_t));
        } else {
            if (!detail::rebase_cached_indices(precomputed_indices, index_vertex_bias, idx_ptr)) {
                throw GxFatalError(
                    "VertexLoader: indexed batch exceeded 16-bit index range",
                    error_offset,
                    error_opcode);
            }
        }
        idx_count = static_cast<std::uint32_t>(precomputed_indices.size());
    } else {
        std::uint32_t packet_vertex_base = 0;
        for (const CachedDrawPacket& packet : packets) {
            if (packet.vertex_count != 0u) {
                emit_primitive_indices(
                    idx_ptr,
                    idx_count,
                    primitive,
                    packet_vertex_base,
                    packet.vertex_count,
                    index_vertex_bias,
                    base_offset + packet.local_opcode_offset);
            }
            packet_vertex_base += packet.vertex_count;
        }
    }
    if (idx_count != total_indices) {
        throw GxFatalError(
            "VertexLoader: cached packet run emitted wrong index count",
            error_offset,
            error_opcode);
    }

    LoadedPrimitive prim{};
    prim.base_vertex = base_vertex;
    prim.vertex_byte_offset = vb.offset;
    prim.vertex_count = total_vertices;
    prim.first_index = first_index;
    prim.index_count = idx_count;
    prim.indexed = true;
    prim.source_class = primitive;
    return prim;
}

}  // namespace galaxy::gx
