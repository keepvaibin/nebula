// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// 3x4 affine matrices and the motion between two of them. No Windows or GPU includes (unit-tested offline).

#include <array>
#include <cstdint>

namespace galaxy::render {

// Row-major 3x4 affine in row order: p' = M * [p, 1].
using Affine34 = std::array<float, 12>;

[[nodiscard]] Affine34 affine_identity() noexcept;
[[nodiscard]] Affine34 affine_multiply(const Affine34& a, const Affine34& b) noexcept;
// False (and identity) when the 3x3 part is singular.
[[nodiscard]] bool affine_inverse(const Affine34& m, Affine34& out) noexcept;
void affine_transform_point(const Affine34& m, const float p[3], float out[3]) noexcept;
// |v| without the squares overflowing (a light's direction can hold 1e31-scale values) or underflowing.
[[nodiscard]] float vector_length(const float v[3]) noexcept;
// The transform between a (t = 0) and b (t = 1): the pair's decomposition
// once, then any number of t (a sequence of generated frames evaluates each pair
// several times). Each 3x3 part is polar-decomposed, M = R S (R a rotation,
// S the stretch: scale and shear, and the reflection of a mirrored matrix):
// R slerps, S and the translation lerp. Rigid, scaled and blended (envelope)
// matrices thus interpolate alike; a pair that does not decompose or differs
// in handedness lerps element-wise.
struct AffineMotion {
    Affine34 a{};
    Affine34 b{};
    float qa[4]{};          // rotation of a (x, y, z, w)
    float qb[4]{};          // rotation of b, on qa's hemisphere
    float stretch_a[9]{};   // S of a, row-major 3x3
    float stretch_b[9]{};
    float theta = 0.0f;     // slerp angle; 0 lerps the quaternions
    float inv_sin_theta = 0.0f;
    float cos_turn = 1.0f;  // |qa . qb| when both decompose, else 1
    bool rigid = false;     // both decomposed; false: element-wise lerp of a and b
};
void affine_motion(const Affine34& a, const Affine34& b, AffineMotion& out) noexcept;

[[nodiscard]] Affine34 affine_motion_at(const AffineMotion& m, float t) noexcept;

}  // namespace galaxy::render
