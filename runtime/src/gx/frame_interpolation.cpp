// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/frame_interpolation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace galaxy::gx {
namespace {

constexpr float kPi = 3.14159265358979f;

float word_float(std::uint32_t w) noexcept { return std::bit_cast<float>(w); }
std::uint32_t float_word(float f) noexcept { return std::bit_cast<std::uint32_t>(f); }

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

Affine34 load_matrix(const std::uint32_t* words) noexcept {
    Affine34 m{};
    for (unsigned i = 0; i < 12u; ++i) m[i] = word_float(words[i]);
    return m;
}

void store_matrix(const Affine34& m, std::uint32_t* words) noexcept {
    for (unsigned i = 0; i < 12u; ++i) words[i] = float_word(m[i]);
}

// XF normal matrices are 3x3 (9 words, row-major) without translation.
Affine34 load_normal(const std::uint32_t* words) noexcept {
    Affine34 m{};
    for (unsigned row = 0; row < 3u; ++row) {
        for (unsigned col = 0; col < 3u; ++col) {
            m[row * 4u + col] = word_float(words[row * 3u + col]);
        }
    }
    return m;
}

void store_normal(const Affine34& m, std::uint32_t* words) noexcept {
    for (unsigned row = 0; row < 3u; ++row) {
        for (unsigned col = 0; col < 3u; ++col) {
            words[row * 3u + col] = float_word(m[row * 4u + col]);
        }
    }
}

// One frame of an object's motion: its origin moved less than half its
// distance from the eye plus 200 units, no axis scaled by more than 2x, and
// it turned less than 150 degrees (a teleport, a respawn or a reused slot
// otherwise). A scale step up to 8x is motion (pulsing, popping effects).
bool plausible(const AffineMotion& m, float eye_distance) noexcept {
    float moved = 0.0f;
    for (unsigned r = 0; r < 3u; ++r) {
        const float d = m.b[r * 4u + 3u] - m.a[r * 4u + 3u];
        moved += d * d;
    }
    const float reach = 0.5f * eye_distance + 200.0f;
    if (!(moved <= reach * reach)) return false;
    for (unsigned c = 0; c < 3u; ++c) {
        const float sa = m.a[c] * m.a[c] + m.a[4u + c] * m.a[4u + c] + m.a[8u + c] * m.a[8u + c];
        const float sb = m.b[c] * m.b[c] + m.b[4u + c] * m.b[4u + c] + m.b[8u + c] * m.b[8u + c];
        // Squared norms: more than 8x is another object. Growing from or
        // shrinking to nothing (spawn, pop) is motion.
        const float lo = std::min(sa, sb), hi = std::max(sa, sb);
        if (lo > 1e-12f && hi > 64.0f * lo) return false;
    }
    return m.cos_turn >= 0.2588190f;  // cos(75 deg): turns up to 150 degrees
}

}  // namespace

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
    if (camera.known && camera.cut && !orthographic) {
        affine_motion(b, b, out.world);
        out.still = true;
        return;
    }
    out.split = camera.known && !orthographic;
    if (!out.split) {
        const Affine34& from = a != nullptr ? *a : b;
        out.still = equal_words(from, b);
        affine_motion(from, b, out.world);
        if (!out.still && !plausible(out.world, std::sqrt(b[3] * b[3] + b[7] * b[7] + b[11] * b[11]))) {
            affine_motion(b, b, out.world);
            out.still = true;
            out.snapped = true;
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
    if (!out.still &&
        !plausible(out.world, std::sqrt(b[3] * b[3] + b[7] * b[7] + b[11] * b[11]))) {
        affine_motion(world_b, world_b, out.world);
        out.still = true;
        out.snapped = true;
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

void normal_motion(const Affine34* a, const Affine34& b, const CameraMotion& camera,
                   NormalMotion& out) noexcept {
    out = NormalMotion{};
    if (camera.known && camera.cut) {
        affine_motion(b, b, out.world);
        out.still = true;
        return;
    }
    out.split = camera.known;
    if (!out.split) {
        const Affine34& from = a != nullptr ? *a : b;
        out.still = equal_words(from, b);
        affine_motion(from, b, out.world);
        return;
    }
    const Affine34 world_b = affine_multiply(rotation_only(camera.b_inverse), b);
    if (a == nullptr) {
        affine_motion(world_b, world_b, out.world);
        out.still = true;
        return;
    }
    const Affine34 world_a = affine_multiply(rotation_only(camera.a_inverse), *a);
    out.still = equal_words(world_a, world_b);
    affine_motion(world_a, world_b, out.world);
    if (!out.still && out.world.cos_turn < 0.2588190f) {
        affine_motion(world_b, world_b, out.world);
        out.still = true;
    }
}

Affine34 normal_at(const NormalMotion& m, const CameraMotion& camera, const Affine34& camera_t,
                   float t) noexcept {
    (void)camera;
    Affine34 n = m.still ? m.world.b : affine_motion_at(m.world, t);
    n[3] = n[7] = n[11] = 0.0f;
    if (!m.split) {
        return n;
    }
    Affine34 out = affine_multiply(rotation_only(camera_t), n);
    out[3] = out[7] = out[11] = 0.0f;
    return out;
}

float tex_word_at(float a, float b, bool translation, float t) noexcept {
    if (a == b || !std::isfinite(a) || !std::isfinite(b) || t >= 1.0f) {
        return b;
    }
    if (t <= 0.0f) {
        return a;
    }
    if (translation) {
        const float d = b - a;
        if (std::fabs(d) > 0.5f) {
            a += std::round(d);
        }
        if (std::fabs(b - a) > kTexStepLimit) {
            return b;
        }
    } else if (std::fabs(b - a) > 1.0f) {
        return b;
    }
    return a + (b - a) * t;
}

void light_at(const std::uint32_t* a, const std::uint32_t* b, const CameraMotion& camera,
              const Affine34& camera_t, float t, bool split, std::uint32_t* out) noexcept {
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
    // Color: RGBA8 per byte.
    std::uint32_t color = 0u;
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        const float ca = static_cast<float>((a[3] >> shift) & 0xFFu);
        const float cb = static_cast<float>((b[3] >> shift) & 0xFFu);
        const float c = std::clamp(ca + (cb - ca) * t, 0.0f, 255.0f);
        color |= static_cast<std::uint32_t>(std::lround(c)) << shift;
    }
    out[3] = color;
    // Angle and distance attenuation.
    for (unsigned i = 4u; i < 10u; ++i) {
        const float fa = word_float(a[i]), fb = word_float(b[i]);
        if (std::isfinite(fa) && std::isfinite(fb)) out[i] = float_word(fa + (fb - fa) * t);
    }
    const float pa[3]{word_float(a[10]), word_float(a[11]), word_float(a[12])};
    const float pb[3]{word_float(b[10]), word_float(b[11]), word_float(b[12])};
    const float da[3]{word_float(a[13]), word_float(a[14]), word_float(a[15])};
    const float db[3]{word_float(b[13]), word_float(b[14]), word_float(b[15])};
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
    // Keep the interpolated direction at the interpolated length (GX spot
    // directions are unit vectors; specular half-angle vectors are not).
    const float la = std::sqrt(da[0] * da[0] + da[1] * da[1] + da[2] * da[2]);
    const float lb = std::sqrt(db[0] * db[0] + db[1] * db[1] + db[2] * db[2]);
    const float ld = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (ld > 1e-20f) {
        const float scale = (la + (lb - la) * t) / ld;
        for (float& v : d) v *= scale;
    }
    for (unsigned i = 0; i < 3u; ++i) {
        out[10u + i] = float_word(p[i]);
        out[13u + i] = float_word(d[i]);
    }
}

void palette_motion(const std::uint32_t* a, const std::uint32_t* b, const CameraMotion& camera,
                    bool orthographic, PaletteMotion& out) noexcept {
    out = PaletteMotion{};
    out.split = camera.known && !camera.cut && !orthographic;
    const bool camera_moves = camera.known && !camera.cut && !equal_words(camera.a, camera.b);
    for (unsigned k = 0; k < 10u; ++k) {
        const std::uint32_t* wb = b + k * 12u;
        const bool differs = a != nullptr && std::memcmp(a + k * 12u, wb, 12u * 4u) != 0;
        if (!differs && !(camera_moves && !orthographic)) continue;
        const Affine34 mb = load_matrix(wb);
        if (!finite(mb)) continue;
        Affine34 ma{};
        const Affine34* pa = nullptr;
        if (a != nullptr) {
            ma = load_matrix(a + k * 12u);
            if (finite(ma)) pa = &ma;
        }
        eye_motion(pa, mb, camera, orthographic, out.position[k]);
        out.position_changed[k] = 1u;
        out.snapped += out.position[k].snapped ? 1u : 0u;
        out.any_changed = true;
    }
    for (unsigned k = 0; k < 10u; ++k) {
        const std::uint32_t* wb = b + kXfNormalBase + k * 9u;
        const bool differs =
            a != nullptr && std::memcmp(a + kXfNormalBase + k * 9u, wb, 9u * 4u) != 0;
        if (!differs && !camera_moves) continue;
        const Affine34 nb = load_normal(wb);
        if (!finite(nb)) continue;
        Affine34 na{};
        const Affine34* pa = nullptr;
        if (a != nullptr) {
            na = load_normal(a + kXfNormalBase + k * 9u);
            if (finite(na)) pa = &na;
        }
        normal_motion(pa, nb, camera, out.normal[k]);
        out.normal_changed[k] = 1u;
        out.any_changed = true;
    }
    if (a == nullptr && camera_moves && !orthographic) {
        // Lights without a previous snapshot stay where they are in the world.
        out.light_changed = 0xFFu;
        out.any_changed = true;
    }
    if (a != nullptr) {
        out.tex_changed = std::memcmp(a + 120u, b + 120u, (256u - 120u) * 4u) != 0;
        out.post_changed =
            std::memcmp(a + kXfPostBase, b + kXfPostBase, (kXfLightBase - kXfPostBase) * 4u) != 0;
        for (unsigned light = 0; light < 8u; ++light) {
            const unsigned base = kXfLightBase + light * 16u;
            if ((camera_moves && !orthographic) || std::memcmp(a + base, b + base, 16u * 4u) != 0) {
                out.light_changed |= static_cast<std::uint8_t>(1u << light);
            }
        }
        out.any_changed = out.any_changed || out.tex_changed || out.post_changed ||
                          out.light_changed != 0u;
    }
}

void palette_at(const PaletteMotion& m, const std::uint32_t* a, const std::uint32_t* b,
                const CameraMotion& camera, const Affine34& camera_t, float t,
                std::uint32_t* out) noexcept {
    std::memcpy(out, b, kXfPaletteWords * 4u);
    for (unsigned k = 0; k < 10u; ++k) {
        if (m.position_changed[k] != 0u) {
            store_matrix(eye_at(m.position[k], camera, camera_t, t), out + k * 12u);
        }
        if (m.normal_changed[k] != 0u) {
            store_normal(normal_at(m.normal[k], camera, camera_t, t), out + kXfNormalBase + k * 9u);
        }
    }
    if (a == nullptr) {
        for (unsigned light = 0; light < 8u; ++light) {
            if ((m.light_changed & (1u << light)) != 0u) {
                const unsigned base = kXfLightBase + light * 16u;
                light_at(nullptr, b + base, camera, camera_t, t, m.split, out + base);
            }
        }
        return;
    }
    if (m.tex_changed) {
        for (unsigned w = 120u; w < 256u; ++w) {
            if (a[w] != b[w]) {
                out[w] = float_word(tex_word_at(word_float(a[w]), word_float(b[w]), (w & 3u) == 3u, t));
            }
        }
    }
    if (m.post_changed) {
        for (unsigned w = kXfPostBase; w < kXfLightBase; ++w) {
            if (a[w] != b[w]) {
                out[w] = float_word(tex_word_at(word_float(a[w]), word_float(b[w]), (w & 3u) == 3u, t));
            }
        }
    }
    for (unsigned light = 0; light < 8u; ++light) {
        if ((m.light_changed & (1u << light)) != 0u) {
            const unsigned base = kXfLightBase + light * 16u;
            light_at(a + base, b + base, camera, camera_t, t, m.split, out + base);
        }
    }
}

}  // namespace galaxy::gx
