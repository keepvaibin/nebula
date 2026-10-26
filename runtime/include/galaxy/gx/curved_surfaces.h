// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Curved surfaces: the game's models are polygons made for a 456-line picture (a warp pipe is a 16-sided cylinder).
// Drawn at many times that, their outlines show the polygon. Phong tessellation (Boubekeur and Alexa 2008) rounds
// them on the GPU: each triangle is subdivided and each new point moved towards the tangent planes of its corners.
// The planes come from one displacement normal per position, worked out here when the game loads a model, so that
// every triangle sharing an edge bends it the same way (no cracks) and a hard edge between faces keeps its shape:
//  - a smooth position (every normal there parallel, either sign) takes its normal where the surface is a sampled
//    curve there: its faces within 40 degrees of each other and of the normal within 35. A box shaded soft (its
//    corner normals tilted for the look, its faces at right angles) is shading, not shape: rounded, it would puff
//    into a pillow;
//  - a position on a crease, with exactly two crease edges, takes the crease line's own bend there (the chord
//    midpoint's offset less its part along the chord): a pipe's rim rounds into a circle while the flat cap and the
//    side keep their own shapes;
//  - a straight crease or a corner does not move (zero normal).
// Phong's projection onto a plane does not depend on the normal's sign, so a fold (a wall's two sides sharing
// positions with opposite normals) bends both sides alike. A corner with no displacement normal only touches faces
// whose own normals are perpendicular to its edges, which therefore stay straight on both sides.

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

// The displacement normal of each position (unit length, or zero where the surface does not bend), from the model's
// triangles (three corners each). Positions are taken by value: a position repeated under another index bends alike.
// Triangles with an out-of-range index or a repeated position are left out.
[[nodiscard]] std::vector<Vec3> displacement_normals(std::span<const Vec3> positions, std::span<const Vec3> normals,
                                                     std::span<const Corner> corners);

// A registered model: its position array's size and fingerprint as GX reads it (to tell it from whatever later
// occupies the same memory) and the displacement normal per index. Normals of all zero mean nothing bends.
struct Model {
    std::uint64_t fingerprint = 0;
    std::uint32_t bytes = 0;
    std::vector<Vec3> normals;
};

// FNV-1a over at most kFingerprintWords 8-byte words spread evenly across the bytes (the first and the last among
// them), the bytes past the last whole word and the size. Checked at every decode of a model's draws, it reads a
// fixed amount whatever the array's size (hashing every byte of a 600 KB array per draw cost tens of milliseconds a
// frame); another model's positions differ from these almost anywhere.
inline constexpr std::size_t kFingerprintWords = 64u;
[[nodiscard]] std::uint64_t fingerprint(std::span<const std::byte> bytes) noexcept;

// The models by their position array's GX address (thread-safe: the game registers while the renderer decodes). A
// model in which nothing bends (normals all zero) is not kept: its draws stay plain triangles; the address's earlier
// model, if any, is forgotten all the same.
void register_model(std::uint32_t array_address, std::span<const std::byte> array_bytes, std::vector<Vec3> normals);
[[nodiscard]] std::shared_ptr<const Model> find_model(std::uint32_t array_address);
// A model whose array no longer holds its bytes (its memory reused): forgotten.
void forget_model(std::uint32_t array_address, const Model* model);
void clear_models();

}  // namespace galaxy::gx::curved
