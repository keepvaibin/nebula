// SPDX-License-Identifier: GPL-3.0-only
// Camera, transform and light motion between two game frames (frame_interpolation.h).
#include "galaxy/render/frame_interpolation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace galaxy::render {
namespace {

constexpr float kPi = 3.14159265358979f;


// Unit quaternion (x, y, z, w) of an orthonormal 3x3 (row-major, Affine34
// layout); false when the matrix is not a rotation within tolerance.
bool rotation_quaternion(const Affine34& m, float q[4]) noexcept {
    float r[3][3];
    for (unsigned row = 0; row < 3u; ++row) {
        for (unsigned col = 0; col < 3u; ++col) r[row][col] = m[row * 4u + col];
    }
    for (unsigned a = 0; a < 3u; ++a) {
        const float len = r[0][a] * r[0][a] + r[1][a] * r[1][a] + r[2][a] * r[2][a];
        if (!(std::fabs(len - 1.0f) < 2e-3f)) return false;
        for (unsigned b = a + 1u; b < 3u; ++b) {
            const float dot = r[0][a] * r[0][b] + r[1][a] * r[1][b] + r[2][a] * r[2][b];
            if (std::fabs(dot) > 2e-3f) return false;
        }
    }
    const float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
                      r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                      r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0.0f) return false;
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0f) {
        const float s = 0.5f / std::sqrt(trace + 1.0f);
        q[3] = 0.25f / s;
        q[0] = (r[2][1] - r[1][2]) * s;
        q[1] = (r[0][2] - r[2][0]) * s;
        q[2] = (r[1][0] - r[0][1]) * s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = 2.0f * std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]);
        q[3] = (r[2][1] - r[1][2]) / s;
        q[0] = 0.25f * s;
        q[1] = (r[0][1] + r[1][0]) / s;
        q[2] = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const float s = 2.0f * std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]);
        q[3] = (r[0][2] - r[2][0]) / s;
        q[0] = (r[0][1] + r[1][0]) / s;
        q[1] = 0.25f * s;
        q[2] = (r[1][2] + r[2][1]) / s;
    } else {
        const float s = 2.0f * std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]);
        q[3] = (r[1][0] - r[0][1]) / s;
        q[0] = (r[0][2] + r[2][0]) / s;
        q[1] = (r[1][2] + r[2][1]) / s;
        q[2] = 0.25f * s;
    }
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(n > 0.0f) || !std::isfinite(n)) return false;
    for (unsigned i = 0; i < 4u; ++i) q[i] /= n;
    return true;
}

void quaternion_matrix(const float q[4], Affine34& m) noexcept {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    m[0] = 1.0f - 2.0f * (y * y + z * z);
    m[1] = 2.0f * (x * y - z * w);
    m[2] = 2.0f * (x * z + y * w);
    m[4] = 2.0f * (x * y + z * w);
    m[5] = 1.0f - 2.0f * (x * x + z * z);
    m[6] = 2.0f * (y * z - x * w);
    m[8] = 2.0f * (x * z - y * w);
    m[9] = 2.0f * (y * z + x * w);
    m[10] = 1.0f - 2.0f * (x * x + y * y);
}

Affine34 rotation_only(const Affine34& m) noexcept {
    Affine34 r = m;
    r[3] = r[7] = r[11] = 0.0f;
    return r;
}

bool equal_words(const Affine34& a, const Affine34& b) noexcept {
    return std::memcmp(a.data(), b.data(), sizeof(Affine34)) == 0;
}

bool finite(const Affine34& m) noexcept {
    for (float v : m) {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

}  // namespace

// One frame of an object's motion: its origin moved less than half its
// distance from the eye plus 200 units, no axis scaled by more than 2x, and
// it turned less than 150 degrees (a teleport, a respawn or a reused slot
// otherwise). A scale step up to 8x is motion (pulsing, popping effects). A
// larger turn alone is a flip of an object that stays (a sprite spun by a
// random angle): its rotation shows b, the rest still moves.
Plausibility plausibility(const AffineMotion& m, float eye_distance) noexcept {
    float moved = 0.0f;
    for (unsigned r = 0; r < 3u; ++r) {
        const float d = m.b[r * 4u + 3u] - m.a[r * 4u + 3u];
        moved += d * d;
    }
    const float reach = 0.5f * eye_distance + 200.0f;
    if (!(moved <= reach * reach)) return Plausibility::teleport;
    for (unsigned c = 0; c < 3u; ++c) {
        const float sa = m.a[c] * m.a[c] + m.a[4u + c] * m.a[4u + c] + m.a[8u + c] * m.a[8u + c];
        const float sb = m.b[c] * m.b[c] + m.b[4u + c] * m.b[4u + c] + m.b[8u + c] * m.b[8u + c];
        // Squared norms: more than 8x is another object. Growing from or
        // shrinking to nothing (spawn, pop) is motion.
        const float lo = std::min(sa, sb), hi = std::max(sa, sb);
        if (lo > 1e-12f && hi > 64.0f * lo) return Plausibility::teleport;
    }
    // cos(75 deg): turns up to 150 degrees.
    return m.cos_turn >= 0.2588190f ? Plausibility::motion : Plausibility::flip;
}

// The motion keeps b's rotation: scale and translation still interpolate.
void take_rotation_of_b(AffineMotion& m) noexcept {
    for (unsigned i = 0; i < 4u; ++i) m.qa[i] = m.qb[i];
    m.theta = 0.0f;
    m.inv_sin_theta = 0.0f;
    m.cos_turn = 1.0f;
}

void camera_motion(const Affine34* a, const Affine34* b, float previous_move,
                   const CameraCutLimits& limits, CameraMotion& out) noexcept {
    out = CameraMotion{};
    if (a == nullptr || b == nullptr || !finite(*a) || !finite(*b) ||
        !affine_inverse(*a, out.a_inverse) || !affine_inverse(*b, out.b_inverse)) {
        if (b != nullptr) {
            out.b = *b;
            out.a = *b;
        }
        return;
    }
    out.a = *a;
    out.b = *b;
    out.known = true;
    for (unsigned i = 0; i < 3u; ++i) {
        out.eye_a[i] = out.a_inverse[i * 4u + 3u];
        out.eye_b[i] = out.b_inverse[i * 4u + 3u];
    }
    float moved = 0.0f;
    for (unsigned i = 0; i < 3u; ++i) {
        const float d = out.eye_b[i] - out.eye_a[i];
        moved += d * d;
    }
    out.moved = std::sqrt(moved);
    if (rotation_quaternion(*a, out.qa) && rotation_quaternion(*b, out.qb)) {
        out.rigid = true;
        float dot = out.qa[0] * out.qb[0] + out.qa[1] * out.qb[1] + out.qa[2] * out.qb[2] +
                    out.qa[3] * out.qb[3];
        if (dot < 0.0f) {
            dot = -dot;
            for (float& v : out.qb) v = -v;
        }
        dot = std::min(dot, 1.0f);
        out.turn_degrees = 2.0f * std::acos(dot) * 180.0f / kPi;
        if (dot < 0.9999995f) {
            out.theta = std::acos(dot);
            out.inv_sin_theta = 1.0f / std::sin(out.theta);
        }
    }
    out.cut = out.turn_degrees > limits.max_turn_degrees ||
              (out.moved > limits.max_move_units &&
               out.moved > limits.relative * previous_move + limits.slack);
}

Affine34 camera_at(const CameraMotion& m, float t) noexcept {
    if (!m.known || m.cut || t >= 1.0f) {
        return m.b;
    }
    if (!m.rigid) {
        Affine34 out{};
        for (unsigned i = 0; i < 12u; ++i) out[i] = m.a[i] + (m.b[i] - m.a[i]) * t;
        return out;
    }
    float wa = 1.0f - t, wb = t;
    if (m.theta != 0.0f) {
        wa = std::sin((1.0f - t) * m.theta) * m.inv_sin_theta;
        wb = std::sin(t * m.theta) * m.inv_sin_theta;
    }
    float q[4];
    float norm = 0.0f;
    for (unsigned i = 0; i < 4u; ++i) {
        q[i] = m.qa[i] * wa + m.qb[i] * wb;
        norm += q[i] * q[i];
    }
    norm = 1.0f / std::sqrt(norm);
    for (float& v : q) v *= norm;
    Affine34 out{};
    quaternion_matrix(q, out);
    float eye[3];
    for (unsigned i = 0; i < 3u; ++i) eye[i] = m.eye_a[i] + (m.eye_b[i] - m.eye_a[i]) * t;
    for (unsigned r = 0; r < 3u; ++r) {
        out[r * 4u + 3u] = -(out[r * 4u + 0u] * eye[0] + out[r * 4u + 1u] * eye[1] +
                             out[r * 4u + 2u] * eye[2]);
    }
    return out;
}

void eye_motion(const Affine34* a, const Affine34& b, const CameraMotion& camera,
                bool orthographic, EyeMotion& out) noexcept {
    out = EyeMotion{};
    // A cut shows b; so does an eye-space matrix that did not change (drawn
    // with its own camera or attached to the game camera: the world split
    // would bend it along the camera's chord).
    if ((camera.known && camera.cut) || (a != nullptr && equal_words(*a, b))) {
        affine_motion(b, b, out.world);
        out.still = true;
        return;
    }
    out.split = camera.known && !orthographic;
    const float eye_distance = std::sqrt(b[3] * b[3] + b[7] * b[7] + b[11] * b[11]);
    if (!out.split) {
        const Affine34& from = a != nullptr ? *a : b;
        out.still = equal_words(from, b);
        affine_motion(from, b, out.world);
        if (!out.still) {
            const Plausibility p = plausibility(out.world, eye_distance);
            if (p == Plausibility::teleport) {
                affine_motion(b, b, out.world);
                out.still = true;
                out.snapped = true;
            } else if (p == Plausibility::flip) {
                take_rotation_of_b(out.world);
                out.flipped = true;
            }
        }
        return;
    }
    const Affine34 world_b = affine_multiply(camera.b_inverse, b);
    if (a == nullptr) {
        affine_motion(world_b, world_b, out.world);
        out.still = true;
        return;
    }
    const Affine34 world_a = affine_multiply(camera.a_inverse, *a);
    affine_motion(world_a, world_b, out.world);
    out.still = equal_words(world_a, world_b);
    if (!out.still) {
        const Plausibility p = plausibility(out.world, eye_distance);
        if (p == Plausibility::teleport) {
            affine_motion(world_b, world_b, out.world);
            out.still = true;
            out.snapped = true;
        } else if (p == Plausibility::flip) {
            take_rotation_of_b(out.world);
            out.flipped = true;
        }
    }
}

Affine34 eye_at(const EyeMotion& m, const CameraMotion& camera, const Affine34& camera_t,
                float t) noexcept {
    if (!m.split) {
        return m.still ? m.world.b : affine_motion_at(m.world, t);
    }
    (void)camera;
    const Affine34 world = m.still ? m.world.b : affine_motion_at(m.world, t);
    return affine_multiply(camera_t, world);
}

void tex_matrix_at(const float* a, const float* b, bool spatial, float t, float* out) noexcept {
    bool finite = true;
    for (unsigned i = 0; i < 12u; ++i) finite = finite && std::isfinite(a[i]) && std::isfinite(b[i]);
    if (t >= 1.0f || !finite) {
        std::memcpy(out, b, 12u * sizeof(float));
        return;
    }
    if (t <= 0.0f) {
        std::memcpy(out, a, 12u * sizeof(float));
        return;
    }
    // A wrapped scroll unwraps on b's side (whole repeats show the same texels): t = 0 is a exactly.
    float to[12];
    std::memcpy(to, b, sizeof(to));
    if (!spatial) {
        for (unsigned i = 0; i < 12u; ++i) {
            if (a[i] == b[i]) continue;
            if ((i & 3u) == 3u) {
                const float d = b[i] - a[i];
                if (std::fabs(d) > 0.5f) to[i] -= std::round(d);
                if (std::fabs(to[i] - a[i]) > kTexStepLimit) {
                    std::memcpy(out, b, 12u * sizeof(float));
                    return;
                }
            } else if (std::fabs(b[i] - a[i]) > 1.0f) {
                std::memcpy(out, b, 12u * sizeof(float));
                return;
            }
        }
    }
    for (unsigned i = 0; i < 12u; ++i) out[i] = a[i] * (1.0f - t) + to[i] * t;
}

void light_at(const float* a, const float* b, const CameraMotion& camera, const Affine34& camera_t, float t, bool split,
              float* out) noexcept {
    constexpr unsigned kColor = 0u, kAngle = 4u, kPosition = 10u, kDirection = 13u;
    for (unsigned i = 0; i < 16u; ++i) out[i] = b[i];
    if (camera.known && camera.cut) {
        return;
    }
    split = split && camera.known;
    // No previous snapshot: b's light, still in the world (eye space of b).
    const bool held = a == nullptr;
    if (held) {
        if (!split) {
            return;
        }
        a = b;
    }
    for (unsigned i = kColor; i < kColor + 4u; ++i) {
        out[i] = std::clamp(a[i] + (b[i] - a[i]) * t, 0.0f, 255.0f);
    }
    // Angle and distance attenuation.
    for (unsigned i = kAngle; i < kAngle + 6u; ++i) {
        if (std::isfinite(a[i]) && std::isfinite(b[i])) out[i] = a[i] + (b[i] - a[i]) * t;
    }
    const float* pa = a + kPosition;
    const float* pb = b + kPosition;
    const float* da = a + kDirection;
    const float* db = b + kDirection;
    for (unsigned i = 0; i < 3u; ++i) {
        if (!std::isfinite(pa[i]) || !std::isfinite(pb[i]) || !std::isfinite(da[i]) ||
            !std::isfinite(db[i])) {
            return;
        }
    }
    float p[3], d[3];
    if (split) {
        const Affine34& to_world_a = held ? camera.b_inverse : camera.a_inverse;
        float wa[3], wb[3];
        affine_transform_point(to_world_a, pa, wa);
        affine_transform_point(camera.b_inverse, pb, wb);
        float w[3];
        for (unsigned i = 0; i < 3u; ++i) w[i] = wa[i] + (wb[i] - wa[i]) * t;
        affine_transform_point(camera_t, w, p);
        const Affine34 ra = rotation_only(to_world_a);
        const Affine34 rb = rotation_only(camera.b_inverse);
        const Affine34 rt = rotation_only(camera_t);
        float va[3], vb[3], v[3];
        affine_transform_point(ra, da, va);
        affine_transform_point(rb, db, vb);
        for (unsigned i = 0; i < 3u; ++i) v[i] = va[i] + (vb[i] - va[i]) * t;
        affine_transform_point(rt, v, d);
    } else {
        for (unsigned i = 0; i < 3u; ++i) {
            p[i] = pa[i] + (pb[i] - pa[i]) * t;
            d[i] = da[i] + (db[i] - da[i]) * t;
        }
    }
    // Keep the interpolated direction at the interpolated length (spot
    // directions are unit vectors; specular half-angle vectors are not).
    const float la = vector_length(da);
    const float lb = vector_length(db);
    const float ld = vector_length(d);
    if (ld > 1e-20f) {
        const float scale = (la + (lb - la) * t) / ld;
        for (float& v : d) v *= scale;
    }
    for (unsigned i = 0; i < 3u; ++i) {
        out[kPosition + i] = p[i];
        out[kDirection + i] = d[i];
    }
}

void eye_direction_at(const float* direction, const Affine34& from_view, const Affine34& to_view, float* out) noexcept {
    for (unsigned i = 0; i < 3u; ++i) out[i] = direction[i];
    Affine34 inverse;
    if (!affine_inverse(from_view, inverse)) return;
    const float x = direction[0], y = direction[1], z = direction[2];
    const float wx = inverse[0] * x + inverse[1] * y + inverse[2] * z;
    const float wy = inverse[4] * x + inverse[5] * y + inverse[6] * z;
    const float wz = inverse[8] * x + inverse[9] * y + inverse[10] * z;
    const float ex = to_view[0] * wx + to_view[1] * wy + to_view[2] * wz;
    const float ey = to_view[4] * wx + to_view[5] * wy + to_view[6] * wz;
    const float ez = to_view[8] * wx + to_view[9] * wy + to_view[10] * wz;
    const float length = std::sqrt(ex * ex + ey * ey + ez * ez);
    if (!(length > 0.0f) || !std::isfinite(length)) return;
    out[0] = ex / length;
    out[1] = ey / length;
    out[2] = ez / length;
}

}  // namespace galaxy::render
