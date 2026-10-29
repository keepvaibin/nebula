// SPDX-License-Identifier: GPL-3.0-only
// Native shapes: the game's J3D shape groups as typed records (nebula_game_api.h shape_array, shape_geometry,
// shape_draw). A model's arrays arrive as values, each group as its distinct corners (an entry of each array per
// corner) and its triangles; a draw names its group and the position, normal and colour arrays it reads. The
// renderer expands a group's corners into vertices (expand) and draws them under the state the stream has set.
#pragma once

#include "galaxy/gx/vertex_loader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace galaxy::gx::curved {
struct Model;
}

namespace galaxy::gx::shapes {

enum class ArrayKind : std::uint8_t { Position, Normal, Nbt, Color, TexCoord };

// A model's vertex array as values: 3 floats per position or normal, 9 per NBT entry (normal, tangent, binormal),
// 2 per texture coordinate; one RGBA word per colour (R << 24 | G << 16 | B << 8 | A).
struct Array {
    ArrayKind kind = ArrayKind::Position;
    std::uint32_t count = 0;
    std::vector<float> floats;
    std::vector<std::uint32_t> colors;
};

// One corner of a group: the entry it takes from each array and its matrix slots.
struct Corner {
    std::uint16_t position = 0;
    std::uint16_t normal = 0;
    std::uint16_t tangent = 0;
    std::uint16_t binormal = 0;
    std::array<std::uint16_t, 2> color{};
    std::array<std::uint16_t, 8> texcoord{};
    std::uint8_t position_matrix = 0;
    std::array<std::uint8_t, 8> texture_matrix{};
};

// Geometry::attributes (as nebula_game_api.h's NEBULA_SHAPE_*).
inline constexpr std::uint32_t kNormal = 1u << 0;
inline constexpr std::uint32_t kColor0 = 1u << 1;
inline constexpr std::uint32_t kColor1 = 1u << 2;
inline constexpr std::uint32_t kTexCoord0 = 1u << 3;
inline constexpr std::uint32_t kNbt = 1u << 11;
inline constexpr std::uint32_t kNbtThree = 1u << 12;
inline constexpr std::uint32_t kPositionMatrix = 1u << 13;
inline constexpr std::uint32_t kTextureMatrix0 = 1u << 14;

// A shape group: which arrays its corners read (by default: position, normal or NBT, colour 0 and 1, texture
// coordinates 0-7; 0 where none), its corners and its triangles.
struct Geometry {
    std::uint32_t attributes = 0;
    std::array<std::uint64_t, 12> arrays{};
    std::vector<Corner> corners;
    std::vector<std::uint16_t> indices;
};

// The arrays one draw of a group reads, by its slots (Geometry::arrays's order); null where it reads none.
using DrawArrays = std::array<const Array*, 12>;

// The group's vertices: each corner from its arrays, `position_matrix` the position matrix slot of corners that carry
// none; `curved` (or null) the curve data of the model whose positions it reads (curved_surfaces.h). False when an
// array it reads is missing, of another kind, or shorter than a corner's entry.
[[nodiscard]] bool expand(const Geometry& geometry, const DrawArrays& arrays, std::uint8_t position_matrix,
                          const curved::Model* curved, std::vector<GxVertexOut>& out);

// Records of the game's stream, in the order it made them (gx_backend.h attach_shape_record).
struct Record {
    enum class Kind : std::uint8_t { Array, Geometry, Draw };
    std::size_t offset = 0;  // bytes into the submitted GX stream: it runs after every command before it
    std::uint64_t serial = 0;  // its place among the records (and so among markers at the same offset)
    Kind kind = Kind::Draw;
    std::uint64_t key = 0;   // the array's or the group's
    std::uint64_t position = 0, normal = 0, color = 0;  // a draw's arrays
    std::shared_ptr<const Array> array;
    std::shared_ptr<const Geometry> geometry;
};

}  // namespace galaxy::gx::shapes
