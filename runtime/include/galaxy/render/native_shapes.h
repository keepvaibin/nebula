// SPDX-License-Identifier: GPL-3.0-only
// Native shapes: the game's J3D shape groups as typed records (nebula_game_api.h shape_array, shape_geometry,
// shape_draw). A model's arrays arrive as values, each group as its distinct corners (an entry of each array per
// corner) and its triangles; a draw names its group and the position, normal and colour arrays it reads. The
// renderer expands a group's corners into vertices (expand) and draws them under the state the stream has set.
#pragma once

#include "galaxy/render/native_material.h"
#include "galaxy/render/native_state.h"
#include "galaxy/render/render_vertex.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace galaxy::render::curved {
struct Model;
}

namespace galaxy::render::shapes {

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
    std::uint16_t model_matrix = 0;
};

// Geometry::attributes (as nebula_game_api.h's NEBULA_SHAPE_*).
// Corner::texture_matrix: the coordinate starts from the corner's own position matrix (as NEBULA_CORNER_POSITION_MATRIX).
inline constexpr std::uint8_t kCornerPositionMatrix = 0xFFu;
inline constexpr std::uint8_t kCornerNormalMatrix = 0xFEu;
inline constexpr std::uint32_t kModelMatrices = 1u << 23;
inline constexpr std::uint32_t kNormal = 1u << 0;
inline constexpr std::uint32_t kColor0 = 1u << 1;
inline constexpr std::uint32_t kColor1 = 1u << 2;
inline constexpr std::uint32_t kTexCoord0 = 1u << 3;
inline constexpr std::uint32_t kNbt = 1u << 11;
inline constexpr std::uint32_t kNbtThree = 1u << 12;
inline constexpr std::uint32_t kPositionMatrix = 1u << 13;
inline constexpr std::uint32_t kTextureMatrix0 = 1u << 14;
// Positions (and colours) the group's own values: geometry the game builds, not a model's arrays.
inline constexpr std::uint32_t kOwnPositions = 1u << 22;

// A shape group: which arrays its corners read (by default: position, normal or NBT, colour 0 and 1, texture
// coordinates 0-7; 0 where none), its corners and its triangles.
struct Geometry {
    std::uint32_t attributes = 0;
    std::array<std::uint64_t, 12> arrays{};
    std::vector<Corner> corners;
    std::vector<std::uint16_t> indices;
};

// Curved surfaces where no patch stages run: `vertices` (expanded with their model's curve data) and `indices` refined once.
// Each edge splits in two as many times as keep its arc within `tolerance` model units of its chords (or a fraction of the
// group's extent, if more), by what its two ends and that say, so the triangles sharing it in the group split it alike and
// share its new points; every new point moves as
// Phong tessellation moves it. An edge between two position matrices stays whole (its new points would have
// no one matrix), and so does the inside of a triangle across them. False when the result passes 16-bit indices.
[[nodiscard]] bool refine_curved(std::span<const RenderVertex> vertices, std::span<const std::uint16_t> indices, float tolerance,
                                 std::vector<RenderVertex>& out_vertices, std::vector<std::uint16_t>& out_indices);

// The arrays one draw of a group reads, by its slots (Geometry::arrays's order); null where it reads none.
using DrawArrays = std::array<const Array*, 12>;

// The group's vertices: each corner from its arrays, `position_matrix` the position matrix slot of corners that carry
// none; `curved` (or null) the curve data of the model whose positions it reads (curved_surfaces.h). False when an
// array it reads is missing, of another kind, or shorter than a corner's entry.
[[nodiscard]] bool expand(const Geometry& geometry, const DrawArrays& arrays, std::uint8_t position_matrix,
                          const curved::Model* curved, std::vector<RenderVertex>& out);

// A matrix a draw carries, by its place in the draw (as nebula_game_api.h's NebulaDrawMatrix): rows of four (a normal
// matrix's of three, a 2x4 texture matrix's two); `model_matrix` the model's draw matrix it is (index + 1), else 0.
enum class MatrixKind : std::uint8_t { Position, Normal, Texture, Texture2x4, PostTexture };
inline constexpr std::uint8_t kIdentityTexturePlace = 10u;
inline constexpr std::uint8_t kIdentityPostPlace = 20u;
struct Matrix {
    MatrixKind kind = MatrixKind::Position;
    std::uint8_t place = 0;
    std::uint16_t model_matrix = 0;
    std::array<float, 12> values{};
};

// An immediate's vertices are whole in RecordPool::corners (`vertex_first` counts there).
inline constexpr std::uint32_t kReadyCorners = 1u << 24u;

// How immediate vertices group: nebula_game_api.h NebulaPrimitive's values.
enum PrimitiveKind : std::uint8_t { kQuads = 0, kTriangles, kTriangleStrip, kTriangleFan, kLines, kLineStrip, kPoints, kWideQuads };

// The indices a primitive (NebulaPrimitive) of `count` vertices draws with: quads as (0 1 2)(0 2 3), strips with their
// odd triangles turned, fans around vertex 0, line strips as line lists, triangles, lines and points as they are.
// Lines and points as triangles (kWideQuads, four corners each); the vertex stage moves the corners out by the draw's line
// width and point size. widened_count: how many corners `count` vertices make; 0 when the primitive is not a line or point.
[[nodiscard]] std::uint32_t widened_count(std::uint8_t primitive, std::uint32_t count) noexcept;
void widen_lines_points(std::uint8_t primitive, std::uint32_t count, const RenderVertex* in, RenderVertex* out) noexcept;
void primitive_indices(std::uint8_t primitive, std::uint32_t count, std::vector<std::uint16_t>& out);

inline constexpr std::uint32_t kNoNative = 0xFFFFFFFFu;

// A scene boundary's camera, the player's up and the stage (the game's values at that boundary).
struct SceneInfo {
    std::array<float, 12> view{};
    std::uint32_t sky_up = 0;
    bool view_valid = false, sky_up_valid = false;
    std::array<char, 48> stage{};
};

// What a chunk's records point into: one allocation a frame, not one a record.
struct RecordPool {
    std::vector<SceneInfo> scenes;
    std::vector<Matrix> matrices;
    std::vector<state::Op> ops;
    std::vector<material::Native> natives;
    std::vector<RenderVertex> vertices;
    std::vector<RenderVertexCore> corners;  // Immediate with kReadyCorners
    std::vector<ScreenProbe> probes;
    std::vector<float> palette_rows;
};

// The game's records, in the order it made them (render_core.h attach_shape_record).
// What a draw does to the target (nebula_game_api.h NebulaPassState).
struct PassState {
    std::uint8_t blend_op = 0, blend_src = 0, blend_dst = 0;
    std::uint8_t depth_test = 0, depth_func = 0, depth_write = 0;
    std::uint8_t cull = 0;
    std::uint8_t color_write = 0, alpha_write = 0;
    std::uint8_t position_matrix = 0;
    std::uint8_t line_divisor = 0, point_divisor = 0;
    std::uint8_t stencil_func = 0, stencil_op = 0;
    std::uint8_t line_offsets = 0, point_offsets = 0;
    std::uint8_t reserved[3] = {};
    float line_width = 0.0f, point_size = 0.0f;
};

struct Record {
    enum class Kind : std::uint8_t {
        Array, Geometry, Draw, State, Immediate, TextureImage, Readback, Marker,
        TargetBegin, TargetEnd, TargetClear, FramePass, FrameTake, NativeMaterial, FrameShow, FrameClear, Forget, Scene, TextureHd, Palette
    };
    Kind kind = Kind::Draw;
    std::uint64_t key = 0;   // the array's or the group's
    std::uint64_t position = 0, normal = 0, color = 0;  // a draw's arrays (Forget: keys from `key` up to `position` go)
    std::shared_ptr<const Array> array;
    std::shared_ptr<const Geometry> geometry;
    // A material program (MaterialProgram) and a material's values (Material), both under `key`: the program's.
    // A native material (NativeMaterial): a shader compiled at build with its constants, textures and state.
    std::uint32_t native_index = kNoNative;
    // Drawing-state changes outside materials, in order: their place in the frame's pool (RecordPool::ops).
    std::uint32_t op_first = 0, op_count = 0;
    // Immediate: its vertices in the frame's pool; their position matrix slot is the current one unless the attributes
    // (Geometry::attributes' bits) say each has its own. `primitive` a NebulaPrimitive.
    // Readback: its probes in the frame's pool, the same way.
    std::uint32_t vertex_first = 0, vertex_count = 0, attributes = 0;
    PassState pass;  // Draw, Immediate
    // Palette: its id, its game frame, its rows in the frame's pool (palette_first, palette_count matrices).
    // Draw: the block it reads (0 none) and, a shape of one matrix, which matrix.
    std::uint32_t palette = 0, palette_matrix = 0, palette_first = 0, palette_count = 0;
    std::uint8_t primitive = 0;
    // An image's bytes (TextureImage), at `key`: its physical address. TextureHd: the record the game painted larger of
    // the image at `key`; none: it goes.
    std::shared_ptr<const std::vector<std::byte>> image;
    // A draw's (or an immediate's) matrices, and the places its corners take when they carry none (position, texture
    // coordinates 0-7) when the game set them since its last draw.
    std::uint32_t matrix_first = 0, matrix_count = 0;  // their place in the frame's pool (RecordPool::matrices)
    bool has_current = false;
    std::array<std::uint8_t, 9> current{};
    // Texture coordinates generated differently from the material: coord, type, source, normalize, post matrix id.
    std::array<std::array<std::uint8_t, 5>, 8> texgens{};
    std::uint8_t texgen_count = 0;
    // A host marker (Marker, its kind at `key`): aux, then a and b (a draw identity's objects by address).
    std::uint32_t marker_aux = 0;
    std::array<std::uint64_t, 2> marker{};
    // A target pass (TargetBegin and TargetEnd, the target at `key`): its size, clear colour (RGBA) and depth.
    std::uint32_t target_width = 0, target_height = 0;
    bool target_pixels = false;  // TargetBegin: target_width x target_height are pixels, not a share of the frame
    std::uint32_t target_clear_rgba = 0;
    // TargetBegin: the textures that are the frame and its depth during the pass (0: not sampled), the frame carrying
    // target_channels.
    std::uint64_t target_frame_key = 0, target_depth_key = 0;
    // TargetClear, FrameClear: the channels cleared (1 colour, 2 alpha, 4 depth); TargetEnd, FramePass:
    // NebulaTargetChannels. FrameClear: its rectangle (target_x, target_y, target_width x target_height).
    std::uint8_t target_channels = 0;
    // FramePass: the frame's rectangle (target_x, target_y, target_width x target_height), NebulaFramePassFlags;
    // FrameShow: the picture shown (target_width x target_height) and its NebulaFramePassFlags.
    std::uint16_t target_x = 0, target_y = 0;
    std::uint8_t target_flags = 0;
};
// Vectors of records move them when they grow (a throwing move would copy them).
static_assert(std::is_nothrow_move_constructible_v<Record>);

}  // namespace galaxy::render::shapes
