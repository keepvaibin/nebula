// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Curved surfaces: the game's models are polygons made for a 456-line picture (a warp pipe is a 16-sided cylinder).
// Drawn at many times that, their outlines show the polygon. Phong tessellation (Boubekeur and Alexa 2008) rounds
// them on the GPU: each triangle is subdivided and each new point moved towards the tangent planes of its corners.
// That move is a sum over the triangle's edges, each term set by the edge's two ends and the normal each end bends
// it by, so the renderer picks that normal edge by edge (shader_gen.cpp's curved stages) from what this analysis
// works out when the game loads a model:
//  - a model's shading normals are drawn for the look as often as for the shape (a plank's corners splayed to light
//    it round, a box's bevels shaded soft), so a normal bends nothing unless the geometry shows the curve it
//    implies: around a position, the faces that meet through edges agreeing on the normal there form a group, and
//    the group bends by its normal only where it is a sampled surface, every test geometric:
//      its faces within 35 degrees of the normal and 62 of each other (a hexagon's faces meet at 60); the normal
//      inside the convex cone of its faces' normals (any averaged vertex normal is a positive sum of them: a flat
//      face's splayed normals, a box's corner normal on one of its faces, are not); the same curvature on opposite
//      sides of the position (a bevel beside a broad face is not a curve); one quadratic form of direction (Euler's)
//      placing every neighbour within 5 % of its edge of where it is; one shape operator explaining every face's
//      tilt from its circumcentre's offset (exact on a sphere or a cylinder); and no more curvature along a sliver
//      than the position's short edges show (a sliver shows it only across);
//  - an edge where groups meet (a crease) bends as the crease line does: where the crease is a gentle evenly
//    sampled curve (at most 62 degrees of turn, its two segments within 2.5 times of each other) at the edge of a
//    curved group whose normal, in the crease's plane, is the crease's own (a pipe's side, a cone's slope), by the
//    circle through the position and its two crease neighbours; any other crease edge stays straight, and a
//    position with more than one group and other than two crease edges (a corner) bends nothing;
//  - a T-junction (a position inside another edge) and a zero-area triangle's corners stay put where any edge along
//    the line would bend: one curve against two would part.
// Every edge two triangles share bends alike in both (the groups are joined through shared edges; a crease edge
// takes the crease's normal from both sides), so no cracks open. A normal's sign does not matter to Phong's planes,
// so a fold (a wall's two sides sharing positions, normals opposite) bends both sides alike.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace galaxy::gx::curved {

using Vec3 = std::array<float, 3>;
static_assert(sizeof(Vec3) == 12u, "positions and normals arrive as packed float triples");

// A triangle corner: its position and normal indices into the model's arrays.
struct Corner {
    std::uint16_t position = 0;
    std::uint16_t normal = 0;
};

// No place (a crease neighbour where there is none).
inline constexpr std::uint16_t kNoPlace = 0xFFFFu;

// A normal index's displacement normal at a place, and whether it is a flat solid's normal shaded soft for the look:
// its group is no sampled surface, its faces lie in at most three planes each 40 degrees or more from the others (a
// box's corner, a bevel, a plank's splayed end: one plane) and the normal stands off one of them. Ray tracing shades
// such a corner with its face's own normal (ray_tracing.hlsl GeometryCS): under a traced sun the soft normal turned
// each face of a block from lit to shaded in the same band (Bowser's Star Reactor). The raster keeps it.
struct PlaceBend {
    std::uint16_t normal = 0;
    Vec3 bend{};
    bool flat = false;
};

// A model's curved surfaces. Positions equal by value are one place (they bend alike); places are numbered below
// kNoPlace (a model with more bends nothing).
struct Surface {
    std::vector<std::uint16_t> place;              // per position
    std::vector<Vec3> crease;                      // per place: the crease normal, zero but on a rounded crease
    std::vector<std::array<std::uint16_t, 2>> crease_places;  // per place: its two crease neighbours, else kNoPlace
    std::vector<std::uint32_t> first;              // per place, and one past the last: its range in `bends`
    std::vector<PlaceBend> bends;                  // by place, then by normal index; a normal index not listed is zero
    [[nodiscard]] bool bends_anything() const noexcept;
    [[nodiscard]] bool flat_anything() const noexcept;
};

// The analysis of a model's triangles (three corners each). Triangles with an out-of-range index or a repeated
// place are left out.
[[nodiscard]] Surface analyse(std::span<const Vec3> positions, std::span<const Vec3> normals,
                              std::span<const Corner> corners);

// The normal the corner at `position` with normal index `normal` bends its edge to `other` (a position) by, as the
// curved stages pick it: the crease normal on a crease edge, else the corner's group's (zero where nothing bends).
[[nodiscard]] Vec3 edge_normal(const Surface& surface, std::uint16_t position, std::uint16_t normal,
                               std::uint16_t other) noexcept;

// A unit normal in 32 bits (octahedral, two 16-bit halves each from 1 to 65535), zero for the zero vector; and back.
[[nodiscard]] std::uint32_t pack_normal(const Vec3& n) noexcept;
[[nodiscard]] Vec3 unpack_normal(std::uint32_t packed) noexcept;

// A registered model: per position what its vertices carry (GxVertexOut::curve): the place with its
// first crease neighbour (place | neighbour << 16), the second crease neighbour (kNoPlace where none), the crease
// normal (packed), and the bends by normal index (packed).
struct Model {
    std::vector<std::uint32_t> places;         // per position: place | first crease neighbour << 16
    std::vector<std::uint16_t> second;         // per position: the second crease neighbour
    std::vector<std::uint32_t> crease;         // per position: the crease normal, packed
    std::vector<std::uint32_t> first;          // per position, and one past the last: its range in the two below
    std::vector<std::uint16_t> bend_normal;    // normal index, ascending within a position's range
    std::vector<std::uint32_t> bend;           // its displacement normal, packed
    std::vector<std::uint8_t> bend_flat;       // PlaceBend::flat
    // Whether anything bends (the curved stages draw it); a model kept only for its flat corners draws plainly.
    bool bends = false;
    // The packed displacement normal of a vertex's corner (zero where it has none).
    [[nodiscard]] std::uint32_t bend_of(std::uint32_t position, std::uint32_t normal) const noexcept;
    // Whether a vertex's corner is a flat solid's normal shaded soft (PlaceBend::flat).
    [[nodiscard]] bool flat_of(std::uint32_t position, std::uint32_t normal) const noexcept;
};

// The models by their position array's identity (the game's key for it: nebula_game_api.h curved_surface;
// thread-safe: the game registers while the renderer draws). A model in which nothing bends and no corner is flat is
// not kept: its draws stay plain triangles; the key's earlier model, if any, is forgotten all the same.
void register_model(std::uint64_t key, const Surface& surface);
// The same, with the analysis on a background thread: find_model for the key waits for the analysis (a model's first
// draw is already rounded).
void register_model_later(std::uint64_t key, std::vector<Vec3> positions, std::vector<Vec3> normals,
                          std::vector<Corner> corners);
[[nodiscard]] std::shared_ptr<const Model> find_model(std::uint64_t key);
void clear_models();

}  // namespace galaxy::gx::curved
