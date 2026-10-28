// SPDX-License-Identifier: GPL-3.0-only
// Native shapes (native_shapes.h): a list's triangles corner for corner as the renderer decodes them, each distinct
// vertex once, the vertex state they were built under, and lists the builder leaves to GX.
#include "galaxy/gx/native_shapes.h"

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/vertex_loader.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using namespace galaxy::gx;

bool expect(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL %s\n", what);
    }
    return condition;
}

void append_u16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value >> 8u));
    out.push_back(static_cast<std::byte>(value & 0xFFu));
}

constexpr std::uint32_t kPositions = 0x00001000u;
constexpr std::uint32_t kList = 0x00002000u;

struct Scene {
    std::vector<std::byte> positions = std::vector<std::byte>(4u * 6u);
    std::array<galaxy::GuestMemoryRegion, 1> regions{};
    galaxy::GuestMemory memory{};
    GxState state;

    Scene() {
        // Four corners of a square, signed 16-bit XYZ, big-endian as GX reads them.
        const std::array<std::array<std::int16_t, 3>, 4> corners{{{0, 0, 0}, {16, 0, 0}, {0, 16, 0}, {16, 16, 0}}};
        for (std::size_t i = 0; i < corners.size(); ++i) {
            for (std::size_t c = 0; c < 3u; ++c) {
                const auto v = static_cast<std::uint16_t>(corners[i][c]);
                positions[i * 6u + c * 2u] = static_cast<std::byte>(v >> 8u);
                positions[i * 6u + c * 2u + 1u] = static_cast<std::byte>(v & 0xFFu);
            }
        }
        regions[0] = {kPositions, static_cast<std::uint32_t>(positions.size()), positions.data()};
        memory.regions = regions.data();
        memory.region_count = 1u;
        state.load_cp(cp::kVcdLo, 3u << 9u);           // position: 16-bit index
        state.load_cp(cp::kVatABase, 1u | (3u << 1u));  // XYZ, signed 16-bit, no fraction
        state.load_cp(cp::kArrayBaseBase, kPositions);
        state.load_cp(cp::kArrayStrideBase, 6u);
        state.load_cp(cp::kMatrixIndexA, 3u);
    }
};

// A strip over the square, then a triangle list naming three of its corners again, padded to 32 bytes.
std::vector<std::byte> square_list(std::uint8_t second_opcode) {
    std::vector<std::byte> list;
    list.push_back(std::byte{0x98});
    append_u16(list, 4u);
    for (std::uint16_t i = 0; i < 4u; ++i) append_u16(list, i);
    list.push_back(static_cast<std::byte>(second_opcode));
    append_u16(list, 3u);
    for (const std::uint16_t i : {std::uint16_t{2}, std::uint16_t{1}, std::uint16_t{3}}) append_u16(list, i);
    while (list.size() % 32u != 0u) list.push_back(std::byte{0});
    return list;
}

bool test_corners_match_decoder() {
    Scene scene;
    const std::vector<std::byte> list = square_list(0x90u);
    const auto shape = shapes::build(shapes::make_request(1u, kList, list, scene.state), &scene.memory);
    if (!expect(shape != nullptr, "a strip and a triangle list build")) return false;
    bool ok = expect(shape->vertices.size() == 4u, "each corner stored once");
    ok = expect(shape->draws == 2u, "the list's two draws counted") && ok;
    ok = expect(shape->state == shapes::vertex_state(scene.state, 0u), "built under the draw's vertex state") && ok;
    ok = expect(shape->list_address == kList && shape->list_bytes == list.size(), "the list it was built from") && ok;

    // The renderer's own decode of each run, its triangles corner by corner.
    VertexLoader loader;
    const VertexDescriptor desc = scene.state.vertex_desc(0u);
    const std::size_t size = VertexLoader::source_vertex_size(desc);
    std::vector<GxVertexOut> expected;
    const std::array<std::pair<PrimitiveClass, CachedDrawPacket>, 2> runs{{
        {PrimitiveClass::TriangleStrip, CachedDrawPacket{0x98u, 4u, 0u, 1u, 4u * size}},
        {PrimitiveClass::Triangles, CachedDrawPacket{0x90u, 3u, 3u + 4u * size, 4u + 4u * size, 3u * size}}}};
    std::vector<std::uint16_t> triangles;
    for (const auto& [primitive, packet] : runs) {
        const std::array<CachedDrawPacket, 1> packets{packet};
        const DecodedPacketRunVertices decoded = loader.decode_cached_packet_run_vertices_with_layout(
            list, 0u, packets, primitive, scene.state, desc, size, &scene.memory);
        VertexLoader::cached_packet_run_triangles(static_cast<std::uint32_t>(decoded.vertices.size()), 0u, packets,
                                                  primitive, decoded.total_indices, {}, triangles);
        for (const std::uint16_t i : triangles) expected.push_back(decoded.vertices[i]);
    }
    ok = expect(shape->indices.size() == expected.size(), "as many corners as the decoder's triangles") && ok;
    for (std::size_t k = 0; ok && k < expected.size(); ++k) {
        ok = expect(std::memcmp(&shape->vertices[shape->indices[k]], &expected[k], sizeof(GxVertexOut)) == 0,
                    "each corner the decoder's vertex");
    }
    std::size_t read = 0;
    for (const VertexDecodeGuestRange& range : shape->reads) read += range.size;
    ok = expect(!shape->reads.empty() && shape->read_bytes.size() == read, "the array bytes it was built from") && ok;
    return ok;
}

bool test_lists_left_to_gx() {
    Scene scene;
    bool ok = expect(shapes::build(shapes::make_request(1u, kList, square_list(0xA8u), scene.state),
                                   &scene.memory) == nullptr,
                     "a list with lines stays GX's");
    std::vector<std::byte> state_list = square_list(0x90u);
    state_list.insert(state_list.begin(), {std::byte{0x08}, std::byte{0x50}, std::byte{0}, std::byte{0},
                                           std::byte{0x06}, std::byte{0}});
    ok = expect(shapes::build(shapes::make_request(1u, kList, state_list, scene.state), &scene.memory) == nullptr,
                "a list that loads registers stays GX's") && ok;
    return ok;
}

bool test_registry() {
    Scene scene;
    shapes::register_later(shapes::make_request(7u, kList, square_list(0x90u), scene.state), &scene.memory);
    shapes::register_later(shapes::make_request(8u, kList, square_list(0xA8u), scene.state), &scene.memory);
    shapes::wait_idle();
    bool pending = true;
    bool ok = expect(shapes::take(7u, pending) != nullptr && !pending, "a registered shape is taken once built");
    pending = true;
    ok = expect(shapes::take(7u, pending) == nullptr && !pending, "a taken shape leaves the registry") && ok;
    pending = true;
    ok = expect(shapes::take(8u, pending) == nullptr && !pending, "one that cannot be built is not pending") && ok;
    pending = true;
    ok = expect(shapes::take(9u, pending) == nullptr && !pending, "an unknown handle is not pending") && ok;
    // The description list a J3D shape makes (J3DShape::makeVcdVatCmd): its CP loads, its XF load skipped.
    std::vector<std::byte> description{std::byte{0x08}, std::byte{0x50}, std::byte{0}, std::byte{0},
                                       std::byte{0x06}, std::byte{0}, std::byte{0x10}, std::byte{0},
                                       std::byte{0},    std::byte{0x10}, std::byte{0x08}, std::byte{0},
                                       std::byte{0},    std::byte{0},    std::byte{0x01}};
    description.resize(32u);
    GxState described;
    ok = expect(shapes::load_description(description, described) && described.cp(cp::kVcdLo) == 0x600u,
                "a description list's CP loads") && ok;
    return ok;
}

}  // namespace

int main() {
    bool ok = test_corners_match_decoder();
    ok = test_lists_left_to_gx() && ok;
    ok = test_registry() && ok;
    std::printf("native_shapes_tests: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
