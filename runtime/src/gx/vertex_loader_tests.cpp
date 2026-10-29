// Tests for VertexLoader's vertex and index output, the upload rings and ResidentVertexArena.

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
#include "galaxy/gx/resident_vertex_arena.h"
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

// Resident decoded-vertex blocks: first-fit keeps replay-order allocations
// contiguous (so adjacent runs still batch), and a released block is never
// handed out again until kFramesInFlight renderer frames have begun.
bool resident_vertex_arena_defers_reuse_until_gpu_retirement() {
    galaxy::gx::ResidentVertexArena arena;
    arena.reset(100u, 2u);
    const std::uint64_t epoch = arena.epoch();
    const auto a = arena.allocate(30u);
    const auto b = arena.allocate(20u);
    const auto c = arena.allocate(50u);
    if (!expect(a == 0u && b == 30u && c == 50u && !arena.allocate(1u) &&
                arena.used_units() == 100u,
                "arena must allocate first-fit and contiguously in request order")) return false;
    arena.release(*b, 20u);  // released during frame 0, which may still draw it
    if (!expect(!arena.allocate(1u), "released block reused in the same frame")) return false;
    arena.advance_frame();   // frame 1 begins: frame 0 may still be on the GPU
    if (!expect(!arena.allocate(1u), "released block reused one frame later")) return false;
    arena.advance_frame();   // frame 2 begins: its slot fence covered frame 0
    const auto reused = arena.allocate(20u);
    if (!expect(reused == 30u && arena.used_units() == 100u,
                "retired block must become reusable after frames_in_flight frames")) return false;
    // Neighbouring retirements coalesce into one span.
    arena.release(*a, 30u);
    arena.release(*reused, 20u);
    arena.advance_frame();
    arena.advance_frame();
    if (!expect(arena.free_block_count() == 1u && arena.allocate(50u) == 0u,
                "adjacent retired blocks must coalesce")) return false;
    if (!expect(arena.allocate(0u) == std::nullopt, "zero-unit allocation accepted")) return false;
    // Interpolation: one advance per game frame over four ring segments.
    arena.reset(10u, 2u);
    const auto held = arena.allocate(10u);
    arena.set_frames_in_flight(4u);
    arena.release(*held, 10u);
    for (int frame = 0; frame < 3; ++frame) {
        arena.advance_frame();
        if (!expect(!arena.allocate(1u), "block reused before its segment came round")) return false;
    }
    arena.advance_frame();
    if (!expect(arena.allocate(10u) == 0u, "block not reusable after four game frames")) return false;
    arena.reset(10u, 2u);
    return expect(arena.epoch() != epoch && arena.used_units() == 0u &&
                  arena.retired_block_count() == 0u && arena.allocate(10u) == 0u,
                  "reset must forget every block and change the epoch");
}

int main() {
    if (!resident_vertex_arena_defers_reuse_until_gpu_retirement()) return 1;
    if (!fixed_vertex_dequantization_preserves_bits()) return 1;
    if (!cached_index_rebasing_matches_wide_arithmetic()) return 1;
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
    ok = immutable_upload_reuse_preserves_bytes(device.Get()) && ok;
    ok = upload_ring_rejected_requests_preserve_state(device.Get()) && ok;
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
    return ok ? 0 : 1;
}
