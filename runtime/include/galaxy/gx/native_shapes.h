// SPDX-License-Identifier: GPL-3.0-only
// Native shapes: a model's shape lists (J3DShapeDraw), each built once on a worker into its vertices (each distinct
// one once) and its triangles in the list's order, from the list and vertex state of its first draw (after the game's
// own edits of its models: ModelUtil rewrites some lists, descriptions and position arrays after they load). The
// shape-list marker (motion::kMarkerShapeList) then draws them by the list's handle while the list and vertex state
// are the ones they were built for; otherwise the list is called as GX would call it.
#pragma once

#include "galaxy/gx/vertex_loader.h"
#include "galaxy/guest_memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

namespace galaxy::gx {
class GxState;
}

namespace galaxy::gx::shapes {

// The CP registers a shape's vertices depend on: the VCD, its vertex format's VAT, every array's base and stride, and
// MatrixIndexA's position matrix where the vertices carry none of their own (0 where they do).
inline constexpr std::size_t kStateWords = 2u + 3u + 12u + 12u + 1u;
using VertexState = std::array<std::uint32_t, kStateWords>;
[[nodiscard]] VertexState vertex_state(const GxState& state, std::uint8_t vtxfmt);

struct Shape {
    std::uint32_t list_address = 0;  // physical, as the marker carries it
    std::uint32_t list_bytes = 0;
    std::uint8_t vtxfmt = 0;
    std::uint32_t draws = 0;  // the list's GX draws
    VertexState state{};
    std::vector<GxVertexOut> vertices;
    std::vector<std::uint16_t> indices;  // a triangle list
    // The guest array ranges the vertices came from, and their bytes then, back to back: a write to them retires the
    // shape.
    std::vector<VertexDecodeGuestRange> reads;
    std::vector<std::byte> read_bytes;
};

// The CP registers a list's draws are decoded under: the matrix indices, the VCD, every vertex format's VAT and every
// array's base and stride.
inline constexpr std::uint8_t kRequestRegisters[] = {
    0x30, 0x40, 0x50, 0x60, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85,
    0x86, 0x87, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB};

struct Request {
    std::uint32_t handle = 0;
    std::uint32_t list_address = 0;  // as the marker carries it
    std::vector<std::byte> list;
    std::array<std::uint32_t, std::size(kRequestRegisters)> registers{};
};
[[nodiscard]] Request make_request(std::uint32_t handle, std::uint32_t list_address, std::span<const std::byte> list,
                                   const GxState& state);

// The shape `request` describes, its vertices decoded from `memory` as the renderer decodes a list's draws; null when
// the lists hold anything but triangle draws (lists, strips, fans, quads) of one vertex format, or more than 65,536
// distinct vertices.
[[nodiscard]] std::shared_ptr<const Shape> build(const Request& request, GuestMemory* memory);

// Builds `request` on the shape thread, from `memory`.
void register_later(Request request, GuestMemory* memory);
// The shape built for `handle`; null with `pending` set while it is being built, null without when it could not be
// (or was never registered).
[[nodiscard]] std::shared_ptr<const Shape> find(std::uint32_t handle, bool& pending);
// Waits until every shape registered so far is built (tests).
void wait_idle();

}  // namespace galaxy::gx::shapes
