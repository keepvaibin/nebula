// SPDX-License-Identifier: GPL-3.0-only
// Camera, transform, palette, light and colour interpolation between two game frames (frame_interpolation.h).
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
// otherwise). A scale step up to 8x is motion (pulsing, popping effects). A
// larger turn alone is a flip of an object that stays (a sprite spun by a
// random angle): its rotation shows b, the rest still moves.
enum class Plausibility : std::uint8_t { motion, flip, teleport };

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
void normal_motion(const Affine34* a, const Affine34& b, const CameraMotion& camera,
                   NormalMotion& out) noexcept {
    out = NormalMotion{};
    if ((camera.known && camera.cut) || (a != nullptr && equal_words(*a, b))) {
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
bool texgen_spatial(std::uint32_t texgen) noexcept {
    const std::uint32_t type = (texgen >> 4u) & 7u;
    const std::uint32_t source = (texgen >> 7u) & 0x1Fu;
    return type == 0u && (source == 0u || source == 1u || source == 3u || source == 4u);
}

bool ps_colours_move(const GxPsConstants& a, const GxPsConstants& b) noexcept {
    if (std::memcmp(a.stage_config, b.stage_config, sizeof(a.stage_config)) != 0 ||
        std::memcmp(a.uber_ps_header, b.uber_ps_header, sizeof(a.uber_ps_header)) != 0 ||
        std::memcmp(a.ind_cmd, b.ind_cmd, sizeof(a.ind_cmd)) != 0 ||
        std::memcmp(a.ind_misc, b.ind_misc, sizeof(a.ind_misc)) != 0 ||
        std::memcmp(a.tex_dims, b.tex_dims, sizeof(a.tex_dims)) != 0 ||
        std::memcmp(&a.alpha_refs[2], &b.alpha_refs[2], sizeof(float)) != 0 ||
        std::memcmp(&a.fog_params[7], &b.fog_params[7], sizeof(float)) != 0) {
        return false;
    }
    return std::memcmp(a.tev_regs, b.tev_regs, sizeof(a.tev_regs)) != 0 ||
           std::memcmp(a.konst, b.konst, sizeof(a.konst)) != 0 ||
           std::memcmp(a.alpha_refs, b.alpha_refs, sizeof(a.alpha_refs)) != 0 ||
           std::memcmp(a.fog_params, b.fog_params, sizeof(a.fog_params)) != 0 ||
           std::memcmp(a.ind_mtx, b.ind_mtx, sizeof(a.ind_mtx)) != 0;
}

void ps_colours_at(const GxPsConstants& a, const GxPsConstants& b, float t, GxPsConstants& out) noexcept {
    out = b;
    if (t >= 1.0f) {
        return;
    }
    const auto lerp = [t](float x, float y) {
        return std::isfinite(x) && std::isfinite(y) ? x + (y - x) * t : y;
    };
    for (unsigned r = 0; r < 4u; ++r) {
        for (unsigned c = 0; c < 4u; ++c) {
            out.tev_regs[r][c] = lerp(a.tev_regs[r][c], b.tev_regs[r][c]);
            out.konst[r][c] = lerp(a.konst[r][c], b.konst[r][c]);
        }
    }
    // Alpha-test references and the constant destination alpha; [2] packs the
    // compare modes.
    out.alpha_refs[0] = lerp(a.alpha_refs[0], b.alpha_refs[0]);
    out.alpha_refs[1] = lerp(a.alpha_refs[1], b.alpha_refs[1]);
    out.alpha_refs[3] = lerp(a.alpha_refs[3], b.alpha_refs[3]);
    // Fog: a, b magnitude, b shift, c, colour, projection.
    for (unsigned i = 4u; i < 7u; ++i) out.fog_params[i] = lerp(a.fog_params[i], b.fog_params[i]);
    if (a.fog_params[2] == b.fog_params[2]) {
        out.fog_params[0] = lerp(a.fog_params[0], b.fog_params[0]);
        out.fog_params[1] = lerp(a.fog_params[1], b.fog_params[1]);
        out.fog_params[3] = lerp(a.fog_params[3], b.fog_params[3]);
    }
    // Indirect matrices: integer mantissas per row, w = 17 - scale.
    for (unsigned r = 0; r < 6u; ++r) {
        if (a.ind_mtx[r][3] != b.ind_mtx[r][3]) continue;
        for (unsigned c = 0; c < 3u; ++c) {
            const float x = static_cast<float>(a.ind_mtx[r][c]);
            const float y = static_cast<float>(b.ind_mtx[r][c]);
            out.ind_mtx[r][c] = static_cast<std::int32_t>(std::lround(x + (y - x) * t));
        }
    }
}
bool channel_colours_move(const GxVsConstants& a, const GxVsConstants& b) noexcept {
    return std::memcmp(a.chan_ambient, b.chan_ambient, sizeof(a.chan_ambient)) != 0 ||
           std::memcmp(a.chan_material, b.chan_material, sizeof(a.chan_material)) != 0;
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
    const float la = vector_length(da);
    const float lb = vector_length(db);
    const float ld = vector_length(d);
    if (ld > 1e-20f) {
        const float scale = (la + (lb - la) * t) / ld;
        for (float& v : d) v *= scale;
    }
    for (unsigned i = 0; i < 3u; ++i) {
        out[10u + i] = float_word(p[i]);
        out[13u + i] = float_word(d[i]);
    }
}
void channel_colours_at(const GxVsConstants& a, const GxVsConstants& b, float t, GxVsConstants& out) noexcept {
    if (t >= 1.0f) {
        return;
    }
    for (unsigned ch = 0; ch < kMaxColorChannels; ++ch) {
        for (unsigned c = 0; c < 4u; ++c) {
            const float am = a.chan_ambient[ch][c], bm = b.chan_ambient[ch][c];
            const float ma = a.chan_material[ch][c], mb = b.chan_material[ch][c];
            out.chan_ambient[ch][c] = std::isfinite(am) && std::isfinite(bm) ? am + (bm - am) * t : bm;
            out.chan_material[ch][c] = std::isfinite(ma) && std::isfinite(mb) ? ma + (mb - ma) * t : mb;
        }
    }
}
std::uint8_t used_lights(const GxVsConstants& constants) noexcept {
    std::uint32_t mask = 0u;
    const std::uint32_t channels = std::min<std::uint32_t>(constants.uber_vs_header[1], 2u);
    for (std::uint32_t ch = 0; ch < channels; ++ch) {
        for (const std::uint32_t control : {constants.uber_channel[ch], constants.uber_channel[2u + ch]}) {
            if ((control & 2u) != 0u) {
                mask |= ((control >> 2u) & 0xFu) | (((control >> 11u) & 0xFu) << 4u);
            }
        }
    }
    const std::uint32_t texgens = std::min<std::uint32_t>(constants.uber_vs_header[0], kMaxTexGens);
    for (std::uint32_t i = 0; i < texgens; ++i) {
        const std::uint32_t texgen = constants.uber_texgen[i];
        if (((texgen >> 4u) & 7u) == 1u) {
            mask |= 1u << ((texgen >> 15u) & 7u);
        }
    }
    return static_cast<std::uint8_t>(mask);
}

void palette_motion(const std::uint32_t* a, const std::uint32_t* b, const CameraMotion& camera,
                    bool orthographic, PaletteMotion& out, std::uint16_t position_mask,
                    std::uint16_t normal_mask, std::uint8_t light_mask) noexcept {
    out = PaletteMotion{};
    out.split = camera.known && !camera.cut && !orthographic;
    const bool camera_moves = camera.known && !camera.cut && !equal_words(camera.a, camera.b);
    for (unsigned k = 0; k < 10u; ++k) {
        if ((position_mask & (1u << k)) == 0u) continue;
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
    // Orthographic palettes do not use the scene camera: their normals move
    // only with their own matrices.
    const CameraMotion screen{};
    const CameraMotion& normal_camera = orthographic ? screen : camera;
    for (unsigned k = 0; k < 10u; ++k) {
        if ((normal_mask & (1u << k)) == 0u) continue;
        // A position matrix snapped to b or taking b's rotation (a flip) takes b's normals with it.
        if (out.position_changed[k] != 0u && (out.position[k].snapped || out.position[k].flipped)) continue;
        const std::uint32_t* wb = b + kXfNormalBase + k * 9u;
        const bool differs =
            a != nullptr && std::memcmp(a + kXfNormalBase + k * 9u, wb, 9u * 4u) != 0;
        if (!differs && !(camera_moves && !orthographic)) continue;
        const Affine34 nb = load_normal(wb);
        if (!finite(nb)) continue;
        Affine34 na{};
        const Affine34* pa = nullptr;
        if (a != nullptr) {
            na = load_normal(a + kXfNormalBase + k * 9u);
            if (finite(na)) pa = &na;
        }
        normal_motion(pa, nb, normal_camera, out.normal[k]);
        out.normal_changed[k] = 1u;
        out.any_changed = true;
    }
    if (a == nullptr && camera_moves && !orthographic && light_mask != 0u) {
        // Lights without a previous snapshot stay where they are in the world.
        out.light_changed = light_mask;
        out.any_changed = true;
    }
    if (a != nullptr) {
        out.tex_changed = std::memcmp(a + 120u, b + 120u, (256u - 120u) * 4u) != 0;
        out.post_changed =
            std::memcmp(a + kXfPostBase, b + kXfPostBase, (kXfLightBase - kXfPostBase) * 4u) != 0;
        for (unsigned light = 0; light < 8u; ++light) {
            const unsigned base = kXfLightBase + light * 16u;
            if ((light_mask & (1u << light)) != 0u &&
                ((camera_moves && !orthographic) || std::memcmp(a + base, b + base, 16u * 4u) != 0)) {
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
    // Texture matrices TEXMTX0-9 (rows 30-59) and post matrices 0-19 by
    // block. The texgen source of an indexed matrix is not known here: a
    // block whose rotation or scale changes is taken as spatial.
    const auto blocks = [&](unsigned first, unsigned count) {
        for (unsigned k = 0; k < count; ++k) {
            const unsigned base = first + 12u * k;
            if (std::memcmp(a + base, b + base, 12u * 4u) == 0) continue;
            float fa[12], fb[12], fo[12];
            bool spatial = false;
            for (unsigned i = 0; i < 12u; ++i) {
                fa[i] = word_float(a[base + i]);
                fb[i] = word_float(b[base + i]);
                spatial = spatial || ((i & 3u) != 3u && fa[i] != fb[i]);
            }
            tex_matrix_at(fa, fb, spatial, t, fo);
            for (unsigned i = 0; i < 12u; ++i) out[base + i] = float_word(fo[i]);
        }
    };
    if (m.tex_changed) {
        blocks(120u, 10u);
    }
    if (m.post_changed) {
        blocks(kXfPostBase, 20u);
    }
    for (unsigned light = 0; light < 8u; ++light) {
        if ((m.light_changed & (1u << light)) != 0u) {
            const unsigned base = kXfLightBase + light * 16u;
            light_at(a + base, b + base, camera, camera_t, t, m.split, out + base);
        }
    }
}
}  // namespace galaxy::gx
