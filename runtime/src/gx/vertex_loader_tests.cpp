// vertex_loader_tests.cpp -- regression tests for canonical GX vertex output.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#pragma warning(pop)

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/vertex_loader.h"
#include "galaxy/gx/vertex_dequant.h"

#include <bit>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

bool fixed_vertex_dequantization_preserves_bits() {
    // Independent division oracle for every U16/S16 source bit pattern and
    // VAT fraction. U8/S8 are subsets of the same exact integer domain.
    for (unsigned shift = 0u; shift <= 31u; ++shift) {
        const volatile float divisor = static_cast<float>(1u << shift);
        for (std::uint32_t raw = 0u; raw <= 0xffffu; ++raw) {
            for (const std::int32_t value : {
                    static_cast<std::int32_t>(raw),
                    raw < 0x8000u ? static_cast<std::int32_t>(raw)
                                  : static_cast<std::int32_t>(raw) - 0x10000}) {
                const auto expected = std::bit_cast<std::uint32_t>(
                    static_cast<float>(value) / divisor);
                const auto actual = std::bit_cast<std::uint32_t>(
                    galaxy::gx::detail::dequantize_vertex_integer(
                        value, static_cast<std::uint8_t>(shift)));
                if (!expect(actual == expected,
                            "fixed vertex dequantization changed binary32 bits")) return false;
            }
        }
    }
    return true;
}

ComPtr<ID3D12Device> create_warp_device() {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        return {};
    }

    ComPtr<IDXGIAdapter1> warp;
    if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
        return {};
    }

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(
            warp.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device)))) {
        return {};
    }
    return device;
}

void append_u16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
}

void append_s16(std::vector<std::byte>& bytes, std::int16_t value) {
    append_u16(bytes, static_cast<std::uint16_t>(value));
}

void append_f32_be(std::vector<std::byte>& bytes, float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<std::byte>((bits >> 24) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((bits >> 16) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((bits >> 8) & 0xFFu));
    bytes.push_back(static_cast<std::byte>(bits & 0xFFu));
}

bool expect_all_defaulted_fields_zero(const galaxy::gx::GxVertexOut& vertex) {
    bool ok = true;
    for (float component : vertex.normal) {
        ok = expect(component == 0.0f, "absent normal inherited stale data") && ok;
    }
    for (float component : vertex.tangent) {
        ok = expect(component == 0.0f, "absent tangent inherited stale data") && ok;
    }
    for (float component : vertex.binormal) {
        ok = expect(component == 0.0f, "absent binormal inherited stale data") && ok;
    }
    ok = expect(vertex.color0 == 0u, "absent color0 inherited stale data") && ok;
    ok = expect(vertex.color1 == 0u, "absent color1 inherited stale data") && ok;
    for (const auto& uv : vertex.uv) {
        ok = expect(uv[0] == 0.0f, "absent texcoord S inherited stale data") && ok;
        ok = expect(uv[1] == 0.0f, "absent texcoord T inherited stale data") && ok;
    }
    return ok;
}

bool load_position_only_clears_reused_upload_memory(
    galaxy::gx::UploadRing& vertex_ring,
    galaxy::gx::UploadRing& index_ring) {
    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);
    const galaxy::gx::UploadRing::Allocation dirty =
        vertex_ring.allocate(sizeof(galaxy::gx::GxVertexOut), 16);
    std::memset(dirty.cpu, 0xA5, sizeof(galaxy::gx::GxVertexOut));

    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);

    galaxy::gx::GxState state;
    state.load_cp(
        galaxy::gx::cp::kVcdLo,
        1u << 9);  // position direct
    state.load_cp(
        galaxy::gx::cp::kVatABase,
        1u | (static_cast<std::uint32_t>(
                  galaxy::gx::ComponentFormat::F32) << 1));

    std::vector<std::byte> fifo;
    append_u16(fifo, 1);
    append_f32_be(fifo, 1.0f);
    append_f32_be(fifo, 2.0f);
    append_f32_be(fifo, 3.0f);

    galaxy::gx::FifoCursor cursor{
        std::span<const std::byte>{fifo.data(), fifo.size()}};
    galaxy::gx::VertexLoader loader;
    const galaxy::gx::LoadedPrimitive prim = loader.load(
        cursor,
        galaxy::gx::PrimitiveClass::Triangles,
        0,
        state,
        nullptr,
        vertex_ring,
        index_ring);

    const auto* vertex =
        reinterpret_cast<const galaxy::gx::GxVertexOut*>(dirty.cpu);
    bool ok = true;
    ok = expect(prim.vertex_byte_offset == dirty.offset,
                "vertex loader did not reuse the dirtied upload slot") && ok;
    ok = expect(vertex->position[0] == 1.0f, "position x did not decode") && ok;
    ok = expect(vertex->position[1] == 2.0f, "position y did not decode") && ok;
    ok = expect(vertex->position[2] == 3.0f, "position z did not decode") && ok;
    ok = expect_all_defaulted_fields_zero(*vertex) && ok;
    return ok;
}

bool direct_texcoord7_decodes_without_polluting_other_channels(
    galaxy::gx::UploadRing& vertex_ring,
    galaxy::gx::UploadRing& index_ring) {
    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);
    const galaxy::gx::UploadRing::Allocation dirty =
        vertex_ring.allocate(sizeof(galaxy::gx::GxVertexOut), 16);
    std::memset(dirty.cpu, 0xCC, sizeof(galaxy::gx::GxVertexOut));

    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);

    galaxy::gx::GxState state;
    state.load_cp(
        galaxy::gx::cp::kVcdLo,
        1u << 9);  // position direct
    state.load_cp(
        galaxy::gx::cp::kVcdHi,
        1u << 14);  // texcoord7 direct
    state.load_cp(
        galaxy::gx::cp::kVatABase,
        1u | (static_cast<std::uint32_t>(
                  galaxy::gx::ComponentFormat::F32) << 1));
    state.load_cp(
        galaxy::gx::cp::kVatCBase,
        (1u | (static_cast<std::uint32_t>(
                    galaxy::gx::ComponentFormat::F32) << 1)) << 23);

    std::vector<std::byte> fifo;
    append_u16(fifo, 1);
    append_f32_be(fifo, 1.0f);
    append_f32_be(fifo, 2.0f);
    append_f32_be(fifo, 3.0f);
    append_f32_be(fifo, 4.0f);
    append_f32_be(fifo, 5.0f);

    galaxy::gx::FifoCursor cursor{
        std::span<const std::byte>{fifo.data(), fifo.size()}};
    galaxy::gx::VertexLoader loader;
    (void)loader.load(
        cursor,
        galaxy::gx::PrimitiveClass::Triangles,
        0,
        state,
        nullptr,
        vertex_ring,
        index_ring);

    const auto* vertex =
        reinterpret_cast<const galaxy::gx::GxVertexOut*>(dirty.cpu);
    bool ok = true;
    for (unsigned i = 0; i < 7; ++i) {
        ok = expect(vertex->uv[i][0] == 0.0f,
                    "lower texcoord S inherited stale data") && ok;
        ok = expect(vertex->uv[i][1] == 0.0f,
                    "lower texcoord T inherited stale data") && ok;
    }
    ok = expect(vertex->uv[7][0] == 4.0f, "texcoord7 S did not decode") && ok;
    ok = expect(vertex->uv[7][1] == 5.0f, "texcoord7 T did not decode") && ok;
    return ok;
}

bool direct_nbt_decodes_normal_tangent_and_binormal(
    galaxy::gx::UploadRing& vertex_ring,
    galaxy::gx::UploadRing& index_ring) {
    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);
    const galaxy::gx::UploadRing::Allocation dirty =
        vertex_ring.allocate(sizeof(galaxy::gx::GxVertexOut), 16);
    std::memset(dirty.cpu, 0xDD, sizeof(galaxy::gx::GxVertexOut));

    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);

    galaxy::gx::GxState state;
    state.load_cp(
        galaxy::gx::cp::kVcdLo,
        (1u << 9) | (1u << 11));  // position direct, normal direct
    state.load_cp(
        galaxy::gx::cp::kVatABase,
        1u |
            (static_cast<std::uint32_t>(
                 galaxy::gx::ComponentFormat::F32) << 1) |
            (1u << 9) |
            (static_cast<std::uint32_t>(
                 galaxy::gx::ComponentFormat::F32) << 10));

    std::vector<std::byte> fifo;
    append_u16(fifo, 1);
    append_f32_be(fifo, 1.0f);
    append_f32_be(fifo, 2.0f);
    append_f32_be(fifo, 3.0f);
    append_f32_be(fifo, 4.0f);
    append_f32_be(fifo, 5.0f);
    append_f32_be(fifo, 6.0f);
    append_f32_be(fifo, 7.0f);
    append_f32_be(fifo, 8.0f);
    append_f32_be(fifo, 9.0f);
    append_f32_be(fifo, 10.0f);
    append_f32_be(fifo, 11.0f);
    append_f32_be(fifo, 12.0f);

    galaxy::gx::FifoCursor cursor{
        std::span<const std::byte>{fifo.data(), fifo.size()}};
    galaxy::gx::VertexLoader loader;
    (void)loader.load(
        cursor,
        galaxy::gx::PrimitiveClass::Triangles,
        0,
        state,
        nullptr,
        vertex_ring,
        index_ring);

    const auto* vertex =
        reinterpret_cast<const galaxy::gx::GxVertexOut*>(dirty.cpu);
    bool ok = true;
    ok = expect(vertex->normal[0] == 4.0f, "normal x did not decode") && ok;
    ok = expect(vertex->normal[1] == 5.0f, "normal y did not decode") && ok;
    ok = expect(vertex->normal[2] == 6.0f, "normal z did not decode") && ok;
    ok = expect(vertex->tangent[0] == 7.0f, "tangent x did not decode") && ok;
    ok = expect(vertex->tangent[1] == 8.0f, "tangent y did not decode") && ok;
    ok = expect(vertex->tangent[2] == 9.0f, "tangent z did not decode") && ok;
    ok = expect(vertex->binormal[0] == 10.0f, "binormal x did not decode") && ok;
    ok = expect(vertex->binormal[1] == 11.0f, "binormal y did not decode") && ok;
    ok = expect(vertex->binormal[2] == 12.0f, "binormal z did not decode") && ok;
    return ok;
}

bool direct_nbt_fixed_point_decodes_to_unit_scale(
    galaxy::gx::UploadRing& vertex_ring,
    galaxy::gx::UploadRing& index_ring) {
    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);
    const galaxy::gx::UploadRing::Allocation dirty =
        vertex_ring.allocate(sizeof(galaxy::gx::GxVertexOut), 16);
    std::memset(dirty.cpu, 0xDD, sizeof(galaxy::gx::GxVertexOut));

    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);

    galaxy::gx::GxState state;
    state.load_cp(
        galaxy::gx::cp::kVcdLo,
        (1u << 9) | (1u << 11));  // position direct, normal direct
    state.load_cp(
        galaxy::gx::cp::kVatABase,
        1u |
            (static_cast<std::uint32_t>(
                 galaxy::gx::ComponentFormat::F32) << 1) |
            (1u << 9) |
            (static_cast<std::uint32_t>(
                 galaxy::gx::ComponentFormat::S16) << 10));

    std::vector<std::byte> fifo;
    append_u16(fifo, 1);
    append_f32_be(fifo, 1.0f);
    append_f32_be(fifo, 2.0f);
    append_f32_be(fifo, 3.0f);
    append_s16(fifo, 0x4000);
    append_s16(fifo, 0);
    append_s16(fifo, 0);
    append_s16(fifo, 0);
    append_s16(fifo, 0x4000);
    append_s16(fifo, 0);
    append_s16(fifo, 0);
    append_s16(fifo, 0);
    append_s16(fifo, 0x4000);

    galaxy::gx::FifoCursor cursor{
        std::span<const std::byte>{fifo.data(), fifo.size()}};
    galaxy::gx::VertexLoader loader;
    (void)loader.load(
        cursor,
        galaxy::gx::PrimitiveClass::Triangles,
        0,
        state,
        nullptr,
        vertex_ring,
        index_ring);

    const auto* vertex =
        reinterpret_cast<const galaxy::gx::GxVertexOut*>(dirty.cpu);
    bool ok = true;
    ok = expect(vertex->normal[0] == 1.0f, "S16 normal x not unit-scaled") && ok;
    ok = expect(vertex->normal[1] == 0.0f, "S16 normal y not unit-scaled") && ok;
    ok = expect(vertex->normal[2] == 0.0f, "S16 normal z not unit-scaled") && ok;
    ok = expect(vertex->tangent[0] == 0.0f, "S16 tangent x not unit-scaled") && ok;
    ok = expect(vertex->tangent[1] == 1.0f, "S16 tangent y not unit-scaled") && ok;
    ok = expect(vertex->tangent[2] == 0.0f, "S16 tangent z not unit-scaled") && ok;
    ok = expect(vertex->binormal[0] == 0.0f, "S16 binormal x not unit-scaled") && ok;
    ok = expect(vertex->binormal[1] == 0.0f, "S16 binormal y not unit-scaled") && ok;
    ok = expect(vertex->binormal[2] == 1.0f, "S16 binormal z not unit-scaled") && ok;
    return ok;
}

bool indexed_primitive_rebases_to_batch_vertex_base(
    galaxy::gx::UploadRing& vertex_ring,
    galaxy::gx::UploadRing& index_ring) {
    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);

    galaxy::gx::GxState state;
    state.load_cp(
        galaxy::gx::cp::kVcdLo,
        1u << 9);  // position direct
    state.load_cp(
        galaxy::gx::cp::kVatABase,
        1u | (static_cast<std::uint32_t>(
                  galaxy::gx::ComponentFormat::F32) << 1));

    auto make_quad = [] {
        std::vector<std::byte> fifo;
        append_u16(fifo, 4);
        for (unsigned i = 0; i < 4; ++i) {
            append_f32_be(fifo, static_cast<float>(i));
            append_f32_be(fifo, 0.0f);
            append_f32_be(fifo, 0.0f);
        }
        return fifo;
    };

    std::vector<std::byte> first_fifo = make_quad();
    galaxy::gx::FifoCursor first_cursor{
        std::span<const std::byte>{first_fifo.data(), first_fifo.size()}};
    galaxy::gx::VertexLoader loader;
    const galaxy::gx::LoadedPrimitive first = loader.load(
        first_cursor,
        galaxy::gx::PrimitiveClass::Quads,
        0,
        state,
        nullptr,
        vertex_ring,
        index_ring);

    std::vector<std::byte> second_fifo = make_quad();
    galaxy::gx::FifoCursor second_cursor{
        std::span<const std::byte>{second_fifo.data(), second_fifo.size()}};
    const galaxy::gx::LoadedPrimitive second = loader.load(
        second_cursor,
        galaxy::gx::PrimitiveClass::Quads,
        0,
        state,
        nullptr,
        vertex_ring,
        index_ring,
        first.base_vertex,
        true);

    void* mapped = nullptr;
    const D3D12_RANGE read_range{
        static_cast<SIZE_T>(second.first_index * sizeof(std::uint16_t)),
        static_cast<SIZE_T>(
            (second.first_index + second.index_count) *
            sizeof(std::uint16_t))};
    if (!expect(
            SUCCEEDED(index_ring.resource()->Map(0, &read_range, &mapped)) &&
                mapped != nullptr,
            "could not map index upload ring")) {
        return false;
    }
    const auto* indices = reinterpret_cast<const std::uint16_t*>(
        static_cast<const std::byte*>(mapped) +
        second.first_index * sizeof(std::uint16_t));
    const std::uint16_t expected[] = {4, 5, 6, 4, 6, 7};
    bool ok = true;
    ok = expect(first.index_count == 6, "first quad index count changed") && ok;
    ok = expect(second.index_count == 6, "second quad index count changed") && ok;
    for (unsigned i = 0; i < 6; ++i) {
        ok = expect(
            indices[i] == expected[i],
            "rebased quad index did not point at the batch vertex base") && ok;
    }
    const D3D12_RANGE no_write{0, 0};
    index_ring.resource()->Unmap(0, &no_write);
    return ok;
}

bool cached_packet_run_preserves_triangle_strip_boundaries(
    galaxy::gx::UploadRing& vertex_ring,
    galaxy::gx::UploadRing& index_ring,
    bool triangle_tails = false) {
    vertex_ring.begin_frame(0);
    index_ring.begin_frame(0);

    galaxy::gx::GxState state;
    state.load_cp(
        galaxy::gx::cp::kVcdLo,
        1u << 9);  // position direct
    state.load_cp(
        galaxy::gx::cp::kVatABase,
        1u | (static_cast<std::uint32_t>(
                  galaxy::gx::ComponentFormat::F32) << 1));

    std::vector<std::byte> bytes;
    std::vector<galaxy::gx::CachedDrawPacket> packets;
    auto append_strip_packet = [&](std::uint8_t opcode, float base_x) {
        const std::size_t count_offset = bytes.size();
        append_u16(bytes, 4);
        for (unsigned i = 0; i < 4; ++i) {
            append_f32_be(bytes, base_x + static_cast<float>(i));
            append_f32_be(bytes, 0.0f);
            append_f32_be(bytes, 0.0f);
        }
        packets.push_back(galaxy::gx::CachedDrawPacket{
            opcode,
            4,
            count_offset,
            count_offset,
            4u * 3u * sizeof(float)});
    };
    append_strip_packet(triangle_tails ? 0x90u : 0x98u, 0.0f);
    append_strip_packet(triangle_tails ? 0x90u : 0x98u, 4.0f);

    galaxy::gx::VertexLoader loader;
    const galaxy::gx::VertexDescriptor desc = state.vertex_desc(0);
    const std::size_t source_size =
        galaxy::gx::VertexLoader::source_vertex_size(desc);
    const galaxy::gx::LoadedPrimitive prim =
        loader.load_cached_packet_run_with_layout(
            std::span<const std::byte>{bytes.data(), bytes.size()},
            0,
            std::span<const galaxy::gx::CachedDrawPacket>{
                packets.data(), packets.size()},
            triangle_tails ? galaxy::gx::PrimitiveClass::Triangles
                           : galaxy::gx::PrimitiveClass::TriangleStrip,
            0,
            state,
            desc,
            source_size,
            nullptr,
            vertex_ring,
            index_ring);

    bool ok = true;
    ok = expect(prim.indexed, "cached packet strip run was not indexed") && ok;
    ok = expect(prim.vertex_count == 8,
                "cached packet strip vertex count changed") && ok;
    ok = expect(prim.index_count == (triangle_tails ? 6u : 12u),
                "cached packet strip index count changed") && ok;

    void* mapped = nullptr;
    const D3D12_RANGE read_range{
        static_cast<SIZE_T>(prim.first_index * sizeof(std::uint16_t)),
        static_cast<SIZE_T>(
            (prim.first_index + prim.index_count) *
            sizeof(std::uint16_t))};
    if (!expect(
            SUCCEEDED(index_ring.resource()->Map(0, &read_range, &mapped)) &&
                mapped != nullptr,
            "could not map cached packet run index upload ring")) {
        return false;
    }
    const auto* indices = reinterpret_cast<const std::uint16_t*>(
        static_cast<const std::byte*>(mapped) +
        prim.first_index * sizeof(std::uint16_t));
    const std::vector<std::uint16_t> expected = triangle_tails
        ? std::vector<std::uint16_t>{0, 1, 2, 4, 5, 6}
        : std::vector<std::uint16_t>{0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        ok = expect(
            indices[i] == expected[i],
            "cached packet strip crossed a packet boundary") && ok;
    }
    const D3D12_RANGE no_write{0, 0};
    index_ring.resource()->Unmap(0, &no_write);
    return ok;
}

bool small_vertex_components_preserve_bits() {
    using namespace galaxy::gx;
    constexpr std::array<std::uint32_t, 20> float_words{
        0x00000000u, 0x80000000u, 0x00000001u, 0x007fffffu,
        0x00800000u, 0x3f800000u, 0xbf800000u, 0x7f7fffffu,
        0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7fc00001u,
        0xffc12345u, 0x7f800001u, 0xff800001u, 0x3f000001u,
        0x3effffffu, 0x01234567u, 0x89abcdefu, 0xdeadbeefu};
    VertexLoader loader;
    GxState state;
    const auto check = [&](ComponentFormat format, unsigned first,
                           unsigned vertex_count, unsigned prefix) {
        const auto encoded_word = [&](unsigned vertex, unsigned component) {
            return format == ComponentFormat::F32
                ? float_words[(first + vertex + component * 7u) % float_words.size()]
                : static_cast<std::uint32_t>(
                    static_cast<std::uint16_t>(first + vertex + component * 8191u));
        };
        VertexDescriptor desc{};
        const auto encoded_format = static_cast<std::uint8_t>(format);
        desc.position = {VcdType::Direct, 1u, encoded_format, 5u}; // 3 components
        desc.normal = {VcdType::Direct, 0u, encoded_format, 31u}; // 3; ignores VAT shift
        desc.texcoord[0] = {VcdType::Direct, 0u, encoded_format, 7u}; // 1 component
        desc.texcoord[7] = {VcdType::Direct, 1u, encoded_format, 9u}; // 2 components
        const unsigned word_bytes = format == ComponentFormat::F32 ? 4u : 2u;
        const unsigned source_stride = word_bytes * 9u;
        std::vector<std::byte> fifo(prefix, std::byte{0x71});
        append_u16(fifo, static_cast<std::uint16_t>(vertex_count));
        for (unsigned vertex = 0u; vertex < vertex_count; ++vertex) {
            for (unsigned component = 0u; component < 9u; ++component) {
                const std::uint32_t word = encoded_word(vertex, component);
                // Construct bytes without floating-point operations or the
                // production endian helpers, including signaling NaN inputs.
                for (unsigned byte = word_bytes; byte != 0u; --byte) {
                    fifo.push_back(static_cast<std::byte>((word >> ((byte - 1u) * 8u)) & 0xffu));
                }
            }
        }
        const std::array<CachedDrawPacket, 1> packets{{
            {0x90u, static_cast<std::uint16_t>(vertex_count), prefix, prefix,
             static_cast<std::size_t>(source_stride) * vertex_count}}};
        const auto decoded = loader.decode_cached_packet_run_vertices_with_layout(
            fifo, 0u, packets, PrimitiveClass::Triangles, 0u, state, desc, source_stride, nullptr);
        if (!expect(decoded.vertices.size() == vertex_count,
                    "small-component conversion changed the vertex count")) return false;
        for (unsigned vertex = 0u; vertex < vertex_count; ++vertex) {
            const auto& out = decoded.vertices[vertex];
            const std::array<const float*, 9> components{
                &out.position[0], &out.position[1], &out.position[2],
                &out.normal[0], &out.normal[1], &out.normal[2],
                &out.uv[0][0], &out.uv[7][0], &out.uv[7][1]};
            for (unsigned component = 0u; component < components.size(); ++component) {
                const auto word = encoded_word(vertex, component);
                std::uint32_t expected_bits = word;
                if (format != ComponentFormat::F32) {
                    const int value = format == ComponentFormat::S16 && word >= 32768u
                        ? static_cast<int>(word) - 65536 : static_cast<int>(word);
                    const unsigned shift = component < 3u ? 5u : component < 6u
                        ? (format == ComponentFormat::S16 ? 14u : 15u)
                        : component == 6u ? 7u : 9u;
                    const float expected = static_cast<float>(value) / static_cast<float>(1u << shift);
                    expected_bits = std::bit_cast<std::uint32_t>(expected);
                }
                if (!expect(std::bit_cast<std::uint32_t>(*components[component]) == expected_bits,
                            "small vertex components preserve raw float bits and exact fixed-point values"))
                    return false;
            }
        }
        return true;
    };
    for (unsigned prefix : {0u, 1u, 2u, 3u}) {
        if (!check(ComponentFormat::F32, 0u, static_cast<unsigned>(float_words.size()), prefix)) return false;
        for (const auto format : {ComponentFormat::U16, ComponentFormat::S16}) {
            // Each of nine component lanes visits all 65536 source words;
            // bounded batches stay below the 16-bit packet vertex-count limit.
            for (unsigned first = 0u; first < 65536u; first += 2048u) {
                if (!check(format, first, 2048u, prefix)) return false;
            }
        }
    }
    return true;
}

bool prepared_vertex_inputs_preserve_mixed_layouts() {
    using namespace galaxy::gx;
    constexpr std::uint32_t position_base = 0x1000u, color_base = 0x2000u, tex_base = 0x3000u;
    std::array<std::byte, 64> positions{}, colors{}, texcoords{};
    const auto put_u16 = [](auto& bytes, unsigned offset, std::int32_t value) {
        const auto bits = static_cast<std::uint16_t>(value);
        bytes[offset] = static_cast<std::byte>(bits >> 8u);
        bytes[offset + 1u] = static_cast<std::byte>(bits & 0xffu);
    };
    std::array<galaxy::GuestMemoryRegionV1, 3> regions{{
        {position_base, 64u, positions.data()}, {color_base, 64u, colors.data()},
        {tex_base, 64u, texcoords.data()}}};
    galaxy::GuestMemoryV1 memory{}; memory.regions = regions.data(); memory.region_count = 3u;
    GxState state;
    VertexLoader loader;
    VertexDescriptor desc{};
    desc.has_tex_matrix_index[7] = true;
    desc.position = {VcdType::Index16, 1u, 3u, 1u}; // S16 xyz / 2
    desc.normal = {VcdType::Direct, 0u, 4u, 0u}; // raw F32
    desc.color[1] = {VcdType::Index8, 1u, 1u, 0u}; // RGB888
    desc.texcoord[7] = {VcdType::Index8, 1u, 3u, 4u}; // S16 st / 16
    desc.texcoord[0].format = 0xffu; // absent format must never be inspected
    for (unsigned variant = 0u; variant < 2u; ++variant) {
        const unsigned pos_offset = variant * 24u, other_offset = variant * 32u;
        const unsigned pos_stride = variant == 0u ? 8u : 12u;
        const unsigned color_stride = variant == 0u ? 4u : 6u;
        const unsigned tex_stride = variant == 0u ? 4u : 6u;
        desc.color[0] = {VcdType::Direct, 1u, static_cast<std::uint8_t>(variant == 0u ? 3u : 5u), 0u};
        state.load_cp(cp::kMatrixIndexA, 9u + variant);
        for (const auto [attr, address, stride] : {
                 std::array<std::uint32_t, 3>{0u, position_base + pos_offset, pos_stride},
                 std::array<std::uint32_t, 3>{3u, color_base + other_offset, color_stride},
                 std::array<std::uint32_t, 3>{11u, tex_base + other_offset, tex_stride}}) {
            state.load_cp(static_cast<std::uint8_t>(cp::kArrayBaseBase + attr), address);
            state.load_cp(static_cast<std::uint8_t>(cp::kArrayStrideBase + attr), stride);
        }
        std::vector<std::byte> fifo;
        std::vector<CachedDrawPacket> packets;
        const unsigned stride = variant == 0u ? 19u : 21u;
        if (!expect(VertexLoader::source_vertex_size(desc) == stride,
                    "mixed layout has independent literal FIFO size")) return false;
        for (unsigned vertex = 0u; vertex < 3u; ++vertex) {
            put_u16(positions, pos_offset + vertex * pos_stride, 2 * (vertex + 1u + variant * 10u));
            put_u16(positions, pos_offset + vertex * pos_stride + 2u, -2 * static_cast<std::int32_t>(vertex + 1u));
            put_u16(positions, pos_offset + vertex * pos_stride + 4u, 6);
            const unsigned color_offset = other_offset + vertex * color_stride;
            colors[color_offset] = std::byte{0x20}; colors[color_offset + 1u] = static_cast<std::byte>(vertex);
            colors[color_offset + 2u] = std::byte{0x80};
            put_u16(texcoords, other_offset + vertex * tex_stride, 16 * (vertex + 1u));
            put_u16(texcoords, other_offset + vertex * tex_stride + 2u, -16 * static_cast<std::int32_t>(vertex));
            const std::size_t offset = fifo.size();
            append_u16(fifo, 1u);
            fifo.push_back(static_cast<std::byte>(30u + vertex));
            append_u16(fifo, static_cast<std::uint16_t>(vertex));
            append_f32_be(fifo, 0.0f); append_f32_be(fifo, 1.0f); append_f32_be(fifo, 0.0f);
            if (variant == 0u) append_u16(fifo, 0x1234u);
            else fifo.insert(fifo.end(), {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}});
            fifo.push_back(static_cast<std::byte>(vertex)); fifo.push_back(static_cast<std::byte>(vertex));
            packets.push_back(CachedDrawPacket{0xb8u, 1u, offset, offset, stride});
        }
        const auto decoded = loader.decode_cached_packet_run_vertices_with_layout(
            fifo, 0u, packets, PrimitiveClass::Points, 0u, state, desc, stride, &memory);
        if (!expect(decoded.vertices.size() == 3u && decoded.guest_array_reads.size() == 3u,
                    "mixed run preserves vertices and exactly three indexed dependencies")) return false;
        for (unsigned vertex = 0u; vertex < 3u; ++vertex) {
            const auto& out = decoded.vertices[vertex];
            if (!expect(out.position[0] == static_cast<float>(vertex + 1u + variant * 10u) &&
                        out.position[1] == -static_cast<float>(vertex + 1u) && out.position[2] == 3.0f &&
                        out.normal[0] == 0.0f && out.normal[1] == 1.0f && out.normal[2] == 0.0f &&
                        out.color0 == 0x11223344u && out.color1 == (0x200080ffu | (vertex << 16u)) &&
                        out.uv[7][0] == static_cast<float>(vertex + 1u) && out.uv[7][1] == -static_cast<float>(vertex) &&
                        out.uv[0][0] == 0.0f && out.uv[0][1] == 0.0f &&
                        out.mtx_indices[0] == 9u + variant && out.mtx_indices[2] == 30u + vertex,
                        "mixed decode preserves literal values and refreshes CP/format state between runs")) return false;
        }
    }
    return true;
}

bool byte_dequant_and_nbt3_match_cached_decode(
    galaxy::gx::UploadRing& vertex_ring,
    galaxy::gx::UploadRing& index_ring) {
    using namespace galaxy::gx;
    VertexLoader loader;
    bool ok = true;
    const auto check_routes = [&](GxState& state, std::vector<std::byte>& fifo,
                                  galaxy::GuestMemoryV1* memory,
                                  const GxVertexOut& expected,
                                  bool check_nbt) {
        vertex_ring.begin_frame(0);
        index_ring.begin_frame(0);
        FifoCursor cursor;
        cursor.data = fifo;
        const LoadedPrimitive prim = loader.load(
            cursor, PrimitiveClass::Triangles, 0, state, memory,
            vertex_ring, index_ring);
        void* mapped = nullptr;
        const D3D12_RANGE range{prim.vertex_byte_offset,
                              prim.vertex_byte_offset + sizeof(GxVertexOut)};
        if (!expect(SUCCEEDED(vertex_ring.resource()->Map(0, &range, &mapped)),
                    "could not map decoded vertex")) return false;
        GxVertexOut ordinary{};
        std::memcpy(&ordinary, static_cast<const std::byte*>(mapped) +
                    prim.vertex_byte_offset, sizeof(ordinary));
        const D3D12_RANGE no_write{0, 0};
        vertex_ring.resource()->Unmap(0, &no_write);
        const std::array<CachedDrawPacket, 1> packets{{
            {0x90u, 1u, 0u, 0u, fifo.size() - 2u}}};
        const VertexDescriptor desc = state.vertex_desc(0);
        const auto cached = loader.decode_cached_packet_run_vertices_with_layout(
            fifo, 0, packets, PrimitiveClass::Triangles, 0, state, desc,
            VertexLoader::source_vertex_size(desc), memory);
        bool pass = expect(cursor.offset == fifo.size() && prim.index_count == 0,
                           "incomplete packet consumption/draw count changed");
        pass = expect(cached.vertices.size() == 1 && cached.total_indices == 0,
                      "cached incomplete packet was not fully decoded") && pass;
        if (cached.vertices.size() != 1) return false;
        for (const GxVertexOut* actual :
             std::array<const GxVertexOut*, 2>{&ordinary, &cached.vertices[0]}) {
            if (check_nbt) {
                for (unsigned n = 0; n < 3; ++n) {
                    pass = expect(actual->normal[n] == expected.normal[n] &&
                                  actual->tangent[n] == expected.tangent[n] &&
                                  actual->binormal[n] == expected.binormal[n],
                                  "NBT3 read the wrong per-vector offset") && pass;
                }
            } else {
                for (unsigned n = 0; n < 2; ++n) {
                    pass = expect(actual->position[n] == expected.position[n] &&
                                  actual->uv[0][n] == expected.uv[0][n],
                                  "ByteDequant disagrees with known byte values") && pass;
                }
            }
        }
        return pass;
    };
    for (bool signed_bytes : {false, true}) {
        for (bool dequant : {false, true}) {
            GxState state;
            const std::uint32_t format = signed_bytes ? 1u : 0u;
            state.load_cp(cp::kVcdLo, 1u << 9u);
            state.load_cp(cp::kVcdHi, 1u);
            state.load_cp(cp::kVatABase,
                (format << 1u) | (6u << 4u) | (1u << 21u) |
                (format << 22u) | (6u << 25u) | (dequant ? 1u << 30u : 0u));
            std::vector<std::byte> fifo;
            append_u16(fifo, 1);
            const auto first = signed_bytes ? std::byte{0xC0} : std::byte{64};
            fifo.insert(fifo.end(), {first, std::byte{32}, first, std::byte{32}});
            GxVertexOut expected{};
            expected.position[0] = signed_bytes ? -64.0f : 64.0f;
            expected.position[1] = 32.0f;
            if (dequant) {
                expected.position[0] /= 64.0f;
                expected.position[1] /= 64.0f;
            }
            expected.uv[0][0] = expected.position[0];
            expected.uv[0][1] = expected.position[1];
            ok = check_routes(state, fifo, nullptr, expected, false) && ok;
        }
    }
    for (bool index16 : {false, true}) {
        GxState state;
        state.load_cp(cp::kVcdLo, (index16 ? 3u : 2u) << 11u);
        state.load_cp(cp::kVatABase, (1u << 9u) | (4u << 10u) | (1u << 31u));
        state.load_cp(cp::kArrayBaseBase + 1u, 0x80000100u);
        state.load_cp(cp::kArrayStrideBase + 1u, 36u);
        std::array<std::byte, 512> guest{};
        std::vector<std::byte> records;
        for (unsigned record = 0; record < 3; ++record) {
            for (unsigned vector = 0; vector < 3; ++vector) {
                for (unsigned component = 0; component < 3; ++component) {
                    append_f32_be(records, static_cast<float>(
                        100u * record + 10u * vector + component));
                }
            }
        }
        std::memcpy(guest.data() + 0x100u, records.data(), records.size());
        galaxy::GuestMemoryV1 memory{};
        memory.fast_regions[8].host_base = guest.data();
        memory.fast_regions[8].size = static_cast<std::uint32_t>(guest.size());
        std::vector<std::byte> fifo;
        append_u16(fifo, 1);
        for (unsigned i = 0; i < 3; ++i) {
            if (index16) append_u16(fifo, static_cast<std::uint16_t>(i));
            else fifo.push_back(static_cast<std::byte>(i));
        }
        GxVertexOut expected{};
        for (unsigned component = 0; component < 3; ++component) {
            expected.normal[component] = static_cast<float>(component);
            expected.tangent[component] = static_cast<float>(110u + component);
            expected.binormal[component] = static_cast<float>(220u + component);
        }
        ok = check_routes(state, fifo, &memory, expected, true) && ok;
    }
    return ok;
}

bool cached_index_rebasing_matches_wide_arithmetic() {
    constexpr std::uint16_t guard = 0x9bd7u;
    constexpr std::array<std::uint32_t, 11> biases{
        0u, 1u, 2u, 255u, 256u, 32767u, 32768u,
        65534u, 65535u, 65536u, 0xffffffffu};
    const auto check = [&](const std::array<std::uint16_t, 40>& input,
                           unsigned source_offset, unsigned count,
                           unsigned destination_offset, std::uint32_t bias) {
        const auto before = input;
        const std::span<const std::uint16_t> source(input.data() + source_offset, count);
        std::array<std::uint16_t, 40> output;
        output.fill(guard);
        bool expected = true;
        for (auto index : source) {
            expected &= static_cast<std::uint64_t>(index) + bias <= 65535u;
        }
        const bool result = galaxy::gx::detail::rebase_cached_indices(
            source, bias, output.data() + destination_offset);
        if (!expect(result == expected && input == before,
                    "cached index rebasing must match mathematical sums and preserve source")) return false;
        for (unsigned i = 0u; i < output.size(); ++i) {
            if (i < destination_offset || i >= destination_offset + count) {
                if (!expect(output[i] == guard, "cached index rebasing wrote outside its span")) return false;
            } else if (result) {
                const auto wide = static_cast<std::uint64_t>(source[i - destination_offset]) + bias;
                if (!expect(output[i] == wide, "cached index rebasing changed an exact index")) return false;
            }
        }
        return true;
    };
    std::array<std::uint16_t, 40> input{};
    for (unsigned offset = 0u; offset < 8u; ++offset) {
        const unsigned source_offset = 7u - offset;
        for (auto bias : biases) {
            for (unsigned first = 0u; first < 65536u; first += 8u) {
                for (unsigned lane = 0u; lane < 8u; ++lane) {
                    input[source_offset + lane] = static_cast<std::uint16_t>(first + lane);
                }
                if (!check(input, source_offset, 8u, offset, bias)) return false;
            }
            // Every vector-tail length, with distinct values and alignments.
            for (unsigned count = 0u; count <= 24u; ++count) {
                for (unsigned i = 0u; i < count; ++i) {
                    input[source_offset + i] = static_cast<std::uint16_t>(i * 8191u + count * 17u);
                }
                if (!check(input, source_offset, count, offset, bias)) return false;
            }
        }
        // Overflow independently in every lane, including later vectors and
        // scalar tails. Valid zero indices elsewhere must not mask that lane.
        for (unsigned count = 1u; count <= 24u; ++count) {
            for (unsigned bad = 0u; bad < count; ++bad) {
                input.fill(0u);
                input[source_offset + bad] = 65535u;
                if (!check(input, source_offset, count, offset, 1u)) return false;
            }
        }
    }
    return expect(galaxy::gx::detail::rebase_cached_indices({}, 0xffffffffu, nullptr),
                  "empty rebasing must not touch null destination or reject an unused bias");
}

bool generated_index_boundaries_preserve_topology(ID3D12Device* device) {
    using namespace galaxy::gx;
    struct Case {
        PrimitiveClass primitive;
        std::vector<std::uint16_t> packet_counts;
        std::vector<std::uint16_t> indices; // Literal un-biased index stream.
        bool cached_nonindexed = false;
    };
    const std::vector<Case> cases{
        {PrimitiveClass::Quads, {5}, {0, 1, 2, 0, 2, 3}},
        {PrimitiveClass::Quads2, {5}, {0, 1, 2, 0, 2, 3}},
        {PrimitiveClass::Quads, {8}, {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7}},
        {PrimitiveClass::Triangles, {5}, {0, 1, 2}},
        {PrimitiveClass::Triangles, {3}, {0, 1, 2}, true},
        {PrimitiveClass::TriangleStrip, {5}, {0, 1, 2, 2, 1, 3, 2, 3, 4}},
        {PrimitiveClass::TriangleFan, {5}, {0, 1, 2, 0, 2, 3, 0, 3, 4}},
        {PrimitiveClass::Lines, {5}, {0, 1, 2, 3}},
        {PrimitiveClass::LineStrip, {5}, {0, 1, 1, 2, 2, 3, 3, 4}},
        {PrimitiveClass::Points, {5}, {0, 1, 2, 3, 4}},
        {PrimitiveClass::Points, {1}, {0}},
        {PrimitiveClass::Points, {0}, {}},
        {PrimitiveClass::Lines, {2}, {0, 1}},
        {PrimitiveClass::Lines, {1}, {}},
        {PrimitiveClass::LineStrip, {1}, {}},
        {PrimitiveClass::Quads, {3}, {}},
        {PrimitiveClass::TriangleStrip, {2}, {}},
        {PrimitiveClass::TriangleFan, {2}, {}},
        {PrimitiveClass::Triangles, {2}, {}},
        {PrimitiveClass::Quads, {5, 5}, {0, 1, 2, 0, 2, 3, 5, 6, 7, 5, 7, 8}},
        {PrimitiveClass::Triangles, {5, 5}, {0, 1, 2, 5, 6, 7}},
        {PrimitiveClass::TriangleStrip, {3, 3}, {0, 1, 2, 3, 4, 5}},
        {PrimitiveClass::TriangleFan, {3, 3}, {0, 1, 2, 3, 4, 5}},
        {PrimitiveClass::Lines, {3, 3}, {0, 1, 3, 4}},
        {PrimitiveClass::LineStrip, {3, 3}, {0, 1, 1, 2, 3, 4, 4, 5}},
    };
    UploadRing vertices, indices;
    if (!expect(vertices.initialize(device, "index-boundary-vertices",
                    (65536u + 16u) * sizeof(GxVertexOut), 1u) &&
                indices.initialize(device, "index-boundary-indices", 4096u, 1u),
                "could not initialize boundary-test rings")) return false;
    VertexLoader loader;
    GxState state;
    const VertexDescriptor desc{}; // No FIFO attributes: only the count is consumed.
    for (const auto& test : cases) {
        std::uint32_t total_vertices = 0u, largest_index = 0u;
        std::vector<CachedDrawPacket> packets;
        for (auto count : test.packet_counts) {
            packets.push_back({0x90u, count, packets.size(), 0u, 0u});
            total_vertices += count;
        }
        for (auto index : test.indices) largest_index = index > largest_index ? index : largest_index;
        const std::vector<GxVertexOut> decoded(total_vertices);
        for (unsigned route : {0u, 1u, 2u}) {
            const bool cached = route != 0u;
            if (!cached && test.packet_counts.size() != 1u) continue;
            const bool indexed = !test.indices.empty() &&
                !(test.primitive == PrimitiveClass::Triangles && (!cached || test.cached_nonindexed));
            for (std::uint32_t bias : {0u, 1u, 65527u, 65528u, 65530u, 65531u,
                                       65532u, 65533u, 65534u, 65535u}) {
                vertices.begin_frame(0u);
                indices.begin_frame(0u);
                // Reserve an untouched prefix to exercise the actual rebasing
                // entry points at every relevant upper-bound transition.
                (void)vertices.allocate(static_cast<std::size_t>(bias) * sizeof(GxVertexOut), 16u);
                const bool should_reject = indexed && bias + largest_index > 65535u;
                LoadedPrimitive prim{};
                bool rejected = false;
                try {
                    if (cached) {
                        prim = loader.upload_cached_packet_run_vertices(decoded, 0x1200u,
                            packets, test.primitive, vertices, indices, 0u, true,
                            static_cast<std::uint32_t>(test.indices.size()),
                            route == 2u ? std::span<const std::uint16_t>{test.indices}
                                        : std::span<const std::uint16_t>{});
                    } else {
                        std::vector<std::byte> fifo;
                        append_u16(fifo, static_cast<std::uint16_t>(total_vertices));
                        FifoCursor cursor;
                        cursor.data = fifo;
                        cursor.base_offset = 0x1200u;
                        prim = loader.load_with_layout(cursor, test.primitive, 0u, state,
                            desc, 0u, nullptr, vertices, indices, 0u, true);
                        if (!expect(cursor.offset == fifo.size(), "index expansion changed FIFO consumption")) return false;
                    }
                } catch (const GxFatalError& error) {
                    rejected = true;
                    if (!expect(std::strcmp(error.what(),
                            "VertexLoader: indexed batch exceeded 16-bit index range") == 0,
                            "index-boundary rejection was caused by an unrelated failure")) return false;
                }
                if (!expect(rejected == should_reject,
                            "index bounds must use referenced vertices, excluding incomplete tails")) return false;
                if (rejected) continue;
                if (!expect(prim.vertex_count == total_vertices && prim.indexed == indexed &&
                            prim.index_count == test.indices.size(),
                            "generated topology changed its source span or index count")) return false;
                const std::size_t bytes = indexed ? test.indices.size() * sizeof(std::uint16_t) : 0u;
                if (!expect(indices.allocate(0u, 2u).offset == bytes,
                            "index allocation retained an incomplete line tail")) return false;
                if (!indexed) continue;
                void* mapped = nullptr;
                const D3D12_RANGE range{0u, bytes};
                if (!expect(SUCCEEDED(indices.resource()->Map(0u, &range, &mapped)),
                            "could not inspect generated topology")) return false;
                const auto* actual = static_cast<const std::uint16_t*>(mapped);
                bool correct = true;
                for (std::size_t i = 0u; i < test.indices.size(); ++i) {
                    correct &= actual[i] == test.indices[i] + bias;
                }
                const D3D12_RANGE no_write{0u, 0u};
                indices.resource()->Unmap(0u, &no_write);
                if (!expect(correct, "literal topology or packet boundaries changed after rebasing")) return false;
            }
        }
    }
    return true;
}

bool upload_ring_rejected_requests_preserve_state(ID3D12Device* device) {
    using galaxy::gx::UploadRing;
    UploadRing ring;
    const auto rejects = [](auto&& action, const char* message) {
        try {
            action();
        } catch (const std::runtime_error&) {
            return expect(true, message);
        }
        return expect(false, message);
    };
    bool ok = rejects([&] { ring.begin_frame(0u); },
        "uninitialized upload ring cannot begin a frame");
    if (!expect(ring.initialize(device, "bounds-test", 64u, 2u),
        "upload bounds ring initialization failed")) return false;
    ok = rejects([&] { (void)ring.allocate(1u, 1u); },
        "upload allocation requires a begun frame") && ok;
    ComPtr<ID3D12Resource> original{ring.resource()};
    const auto gpu_base = original->GetGPUVirtualAddress();
    ring.begin_frame(0u);
    const auto first = ring.allocate(31u, 1u);
    ok = expect(first.offset == 0u && first.gpu == gpu_base,
        "first upload starts at mapped resource base") && ok;
    std::memset(first.cpu, 0x36, 31u);
    ok = rejects([&] { (void)ring.allocate(1u, 0u); },
        "zero alignment is rejected without consuming upload space") && ok;
    ok = rejects([&] { (void)ring.allocate(1u, 3u); },
        "non-power-of-two alignment is rejected") && ok;
    ok = rejects([&] { (void)ring.allocate(std::numeric_limits<std::size_t>::max(), 1u); },
        "wrapped end offset cannot pass upload capacity checking") && ok;
    const auto aligned = ring.allocate(1u, 2u);
    ok = expect(aligned.offset == 32u && aligned.cpu == first.cpu + 32u &&
        aligned.gpu == gpu_base + 32u,
        "rejected requests preserve cursor and valid alignment padding") && ok;
    const std::size_t high_alignment = std::size_t{1} <<
        (std::numeric_limits<std::size_t>::digits - 1u);
    ok = rejects([&] { (void)ring.allocate(1u, high_alignment); },
        "unavailable large alignment padding is rejected") && ok;
    const auto tail = ring.allocate(31u, 1u);
    ok = expect(tail.offset == 33u,
        "failed padding request preserves the first frame tail") && ok;
    ok = rejects([&] { (void)ring.allocate(1u, 1u); },
        "full frame cannot overflow into another upload segment") && ok;
    ring.begin_frame(1u);
    const auto second = ring.allocate(31u, 1u);
    ok = expect(second.offset == 64u && second.gpu == gpu_base + 64u,
        "second upload frame starts in its own segment") && ok;
    ok = rejects([&] { ring.begin_frame(2u); },
        "invalid upload frame leaves active cursor intact") && ok;
    ok = rejects([&] { (void)ring.allocate(std::numeric_limits<std::size_t>::max(), 1u); },
        "wrapped byte request is rejected in a nonzero segment") && ok;
    ok = expect(ring.allocate(33u, 1u).offset == 95u,
        "rejected slot and size preserve second frame tail") && ok;
    ok = expect(!ring.initialize(device, "overflow", std::numeric_limits<std::size_t>::max(), 2u) &&
        !ring.initialize(device, "zero", 0u, 2u) &&
        !ring.initialize(nullptr, "null", 64u, 2u) && ring.resource() == original.Get(),
        "rejected reinitialization keeps original mapped buffer and dimensions") && ok;
    ring.begin_frame(1u);
    ok = expect(ring.allocate(64u, 1u).offset == 64u,
        "original segment limits survive rejected initialization") && ok;
    // Test-only mapped memory read; production does not read UPLOAD memory.
    for (unsigned i = 0u; i < 31u; ++i) {
        if (!expect(first.cpu[i] == std::byte{0x36},
            "other-frame allocations must not overwrite first-frame data")) return false;
    }
    if (!expect(ring.initialize(device, "replacement", 32u, 1u),
        "replacement upload buffer initialization failed")) return false;
    ok = rejects([&] { (void)ring.allocate(1u, 1u); },
        "successful upload replacement requires a new frame") && ok;
    ring.begin_frame(0u);
    const auto replacement = ring.allocate(32u, 1u);
    return expect(replacement.offset == 0u &&
        replacement.gpu == ring.resource()->GetGPUVirtualAddress(),
        "successful replacement refreshes upload addresses and cursors") && ok;
}

bool immutable_upload_reuse_preserves_bytes(ID3D12Device* device) {
    galaxy::gx::UploadRing ring;
    if (!expect(ring.initialize(device, "immutable-test", 4096, 2),
                "immutable upload ring initialization failed")) return false;
    std::array<std::byte, 32> a{};
    std::array<std::byte, 32> b{};
    a.fill(std::byte{0x36});
    b.fill(std::byte{0xa5});
    galaxy::gx::ImmutableUploadToken token_a, token_b;
    bool ok = true;
    const auto check = [&](const galaxy::gx::UploadRing::Allocation& allocation,
                           const auto& expected) {
        // Test-only read from mapped WARP upload memory. Production never
        // reads upload memory to decide reuse.
        return expect(std::memcmp(allocation.cpu, expected.data(),
                                  expected.size()) == 0,
                      "immutable upload reused stale bytes");
    };
    for (unsigned slot = 0; slot < 2; ++slot) {
        ring.begin_frame(slot);
        ok = check(ring.upload_immutable(a.data(), a.size(), 16, token_a), a) && ok;
        ok = check(ring.upload_immutable(b.data(), b.size(), 16, token_b), b) && ok;
        ok = expect(ring.reused_bytes() == 0 && ring.copied_bytes() == 64,
                    "first upload must copy each slot") && ok;
    }
    for (unsigned slot = 0; slot < 2; ++slot) {
        ring.begin_frame(slot);
        ok = check(ring.upload_immutable(a.data(), a.size(), 16, token_a), a) && ok;
        ok = check(ring.upload_immutable(b.data(), b.size(), 16, token_b), b) && ok;
        ok = expect(ring.reused_bytes() == 64 && ring.copied_bytes() == 0,
                    "unchanged consecutive segment should reuse") && ok;
    }
    ring.begin_frame(0);
    ok = check(ring.upload_immutable(b.data(), b.size(), 16, token_b), b) && ok;
    ok = check(ring.upload_immutable(a.data(), a.size(), 16, token_a), a) && ok;
    ok = expect(ring.reused_bytes() == 0 && ring.copied_bytes() == 64,
                "reordered allocations must copy") && ok;

    // A skipped generation may contain unrelated direct writes.
    ring.begin_frame(0);
    std::memset(ring.allocate(64, 16).cpu, 0xff, 64);
    ring.begin_frame(0);
    ok = check(ring.upload_immutable(b.data(), b.size(), 16, token_b), b) && ok;
    ok = expect(ring.reused_bytes() == 0,
                "skipped generation must invalidate reuse") && ok;

    // Replacing decoded cache contents creates a fresh token even at the
    // exact same allocation address.
    token_b = {};
    ring.begin_frame(0);
    ok = check(ring.upload_immutable(a.data(), a.size(), 16, token_b), a) && ok;
    ok = expect(ring.reused_bytes() == 0,
                "replacement payload must copy") && ok;
    ring.begin_frame(0);
    ok = check(ring.upload_immutable(a.data(), a.size(), 16, token_b), a) && ok;
    ok = expect(ring.reused_bytes() == 32,
                "replacement payload should subsequently reuse") && ok;

    galaxy::gx::UploadRing other;
    if (!expect(other.initialize(device, "other-immutable-test", 4096, 2),
                "second immutable upload ring initialization failed")) return false;
    for (unsigned i = 0; i < 7; ++i) {
        other.begin_frame(0);
        std::memset(other.allocate(32, 16).cpu, 0xff, 32);
    }
    other.begin_frame(0);
    ok = check(other.upload_immutable(b.data(), b.size(), 16, token_b), b) && ok;
    ok = expect(other.reused_bytes() == 0,
                "different resource must never inherit reuse") && ok;
    for (unsigned i = 0; i < 9; ++i) other.begin_frame(0);
    ok = check(other.upload_immutable(b.data(), b.size(), 16, token_b), b) && ok;
    ok = expect(other.reused_bytes() == 32,
                "empty fence-only chunks must retain untouched bytes") && ok;

    std::array<galaxy::gx::ImmutableUploadToken, 5> tokens;
    std::array<std::array<std::byte, 80>, 5> payloads{};
    for (unsigned i = 0; i < payloads.size(); ++i) {
        payloads[i].fill(static_cast<std::byte>(i + 1));
    }
    // Deterministic changing draw order, absent entries, direct writes and
    // replaced payloads exercise overlapping old/new allocation layouts.
    std::uint32_t random = 0x71f34ab2u;
    for (unsigned frame = 0; frame < 1000; ++frame) {
        ring.begin_frame(frame % 2);
        random = random * 1664525u + 1013904223u;
        if ((random & 3u) == 0) {
            std::memset(ring.allocate(16, 16).cpu, 0xcc, 16);
        }
        for (unsigned draw = 0; draw < 5; ++draw) {
            const unsigned entry = (draw + (random >> 8u)) % 5u;
            if (((random >> draw) & 1u) == 0) continue;
            if ((random & 31u) == draw) {
                tokens[entry] = {};
                payloads[entry].fill(static_cast<std::byte>(frame & 0xffu));
            }
            ok = check(ring.upload_immutable(
                           payloads[entry].data(), payloads[entry].size(),
                           16, tokens[entry]), payloads[entry]) && ok;
        }
    }
    return ok;
}

}  // namespace

int main() {
    if (!fixed_vertex_dequantization_preserves_bits()) return 1;
    if (!small_vertex_components_preserve_bits()) return 1;
    if (!cached_index_rebasing_matches_wide_arithmetic()) return 1;
    if (!prepared_vertex_inputs_preserve_mixed_layouts()) return 1;
    const ComPtr<ID3D12Device> device = create_warp_device();
    if (!expect(device != nullptr, "could not create D3D12 WARP device")) {
        return 1;
    }

    galaxy::gx::UploadRing vertex_ring;
    galaxy::gx::UploadRing index_ring;
    if (!expect(
            vertex_ring.initialize(device.Get(), "vertex-test", 4096, 1),
            "could not initialize vertex upload ring") ||
        !expect(
            index_ring.initialize(device.Get(), "index-test", 4096, 1),
            "could not initialize index upload ring")) {
        return 1;
    }

    bool ok = true;
    ok = generated_index_boundaries_preserve_topology(device.Get()) && ok;
    ok = immutable_upload_reuse_preserves_bytes(device.Get()) && ok;
    ok = upload_ring_rejected_requests_preserve_state(device.Get()) && ok;
    ok = byte_dequant_and_nbt3_match_cached_decode(vertex_ring, index_ring) && ok;
    ok = load_position_only_clears_reused_upload_memory(
             vertex_ring, index_ring) && ok;
    ok = direct_texcoord7_decodes_without_polluting_other_channels(
             vertex_ring, index_ring) && ok;
    ok = direct_nbt_decodes_normal_tangent_and_binormal(
             vertex_ring, index_ring) && ok;
    ok = direct_nbt_fixed_point_decodes_to_unit_scale(
             vertex_ring, index_ring) && ok;
    ok = indexed_primitive_rebases_to_batch_vertex_base(
             vertex_ring, index_ring) && ok;
    ok = cached_packet_run_preserves_triangle_strip_boundaries(
             vertex_ring, index_ring) && ok;
    ok = cached_packet_run_preserves_triangle_strip_boundaries(
              vertex_ring, index_ring, true) && ok;
    return ok ? 0 : 1;
}
