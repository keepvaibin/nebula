// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/affine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace galaxy::render {

Affine34 affine_identity() noexcept {
    return {1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f};
}

Affine34 affine_multiply(const Affine34& a, const Affine34& b) noexcept {
    Affine34 r{};
    for (unsigned row = 0; row < 3u; ++row) {
        for (unsigned col = 0; col < 4u; ++col) {
            float value = a[row * 4u + 0u] * b[0u * 4u + col] +
                          a[row * 4u + 1u] * b[1u * 4u + col] +
                          a[row * 4u + 2u] * b[2u * 4u + col];
            if (col == 3u) value += a[row * 4u + 3u];
            r[row * 4u + col] = value;
        }
    }
    return r;
}

bool affine_inverse(const Affine34& m, Affine34& out) noexcept {
    const float a = m[0], b = m[1], c = m[2];
    const float d = m[4], e = m[5], f = m[6];
    const float g = m[8], h = m[9], i = m[10];
    const float co0 = e * i - f * h;
    const float co1 = f * g - d * i;
    const float co2 = d * h - e * g;
    const float det = a * co0 + b * co1 + c * co2;
    if (!(std::fabs(det) > 1e-20f) || !std::isfinite(det)) {
        out = affine_identity();
        return false;
    }
    const float inv = 1.0f / det;
    out[0] = co0 * inv;
    out[1] = (c * h - b * i) * inv;
    out[2] = (b * f - c * e) * inv;
    out[4] = co1 * inv;
    out[5] = (a * i - c * g) * inv;
    out[6] = (c * d - a * f) * inv;
    out[8] = co2 * inv;
    out[9] = (b * g - a * h) * inv;
    out[10] = (a * e - b * d) * inv;
    const float tx = m[3], ty = m[7], tz = m[11];
    out[3] = -(out[0] * tx + out[1] * ty + out[2] * tz);
    out[7] = -(out[4] * tx + out[5] * ty + out[6] * tz);
    out[11] = -(out[8] * tx + out[9] * ty + out[10] * tz);
    return true;
}

float vector_length(const float v[3]) noexcept {
    const float largest = std::max(std::max(std::fabs(v[0]), std::fabs(v[1])), std::fabs(v[2]));
    if (!(largest > 0.0f) || !std::isfinite(largest)) {
        return largest;
    }
    const float x = v[0] / largest, y = v[1] / largest, z = v[2] / largest;
    return largest * std::sqrt(x * x + y * y + z * z);
}

void affine_transform_point(const Affine34& m, const float p[3], float out[3]) noexcept {
    const float x = p[0], y = p[1], z = p[2];
    out[0] = m[0] * x + m[1] * y + m[2] * z + m[3];
    out[1] = m[4] * x + m[5] * y + m[6] * z + m[7];
    out[2] = m[8] * x + m[9] * y + m[10] * z + m[11];
}

namespace {

struct Decomposed {
    float q[4];        // rotation quaternion x, y, z, w
    float stretch[9];  // S, row-major
    bool mirrored;     // det < 0: S carries the reflection
    bool valid;
};

// The quaternion of a proper rotation matrix (r[row][col]).
bool rotation_to_quaternion(const float (&r)[3][3], float (&q)[4]) noexcept;

// Polar decomposition A = R S by the scaled Newton iteration
// R <- (g R + R^-T / g) / 2, g = |det R|^(-1/3) (Higham); in double. A
// rotation with per-axis scale (orthogonal columns) takes the direct path.
Decomposed decompose(const Affine34& m) noexcept {
    Decomposed d{};
    {
        float col[3][3];
        float scale[3];
        bool orthogonal = true;
        for (unsigned c = 0; c < 3u; ++c) {
            for (unsigned r = 0; r < 3u; ++r) col[c][r] = m[r * 4u + c];
            scale[c] = std::sqrt(col[c][0] * col[c][0] + col[c][1] * col[c][1] + col[c][2] * col[c][2]);
            orthogonal = orthogonal && scale[c] > 1e-12f && std::isfinite(scale[c]);
        }
        for (unsigned a = 0; a < 3u && orthogonal; ++a) {
            for (unsigned b = a + 1u; b < 3u; ++b) {
                const float dot = (col[a][0] * col[b][0] + col[a][1] * col[b][1] + col[a][2] * col[b][2]) /
                                  (scale[a] * scale[b]);
                orthogonal = orthogonal && std::fabs(dot) <= 2e-3f;
            }
        }
        if (orthogonal) {
            const float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                              m[2] * (m[4] * m[9] - m[5] * m[8]);
            d.mirrored = det < 0.0f;
            const float s0 = d.mirrored ? -scale[0] : scale[0];
            float r[3][3];
            for (unsigned row = 0; row < 3u; ++row) {
                r[row][0] = col[0][row] / s0;
                r[row][1] = col[1][row] / scale[1];
                r[row][2] = col[2][row] / scale[2];
            }
            for (float& v : d.stretch) v = 0.0f;
            d.stretch[0] = s0;
            d.stretch[4] = scale[1];
            d.stretch[8] = scale[2];
            d.valid = rotation_to_quaternion(r, d.q);
            return d;
        }
    }
    double a[3][3];
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 3u; ++c) a[r][c] = m[r * 4u + c];
    }
    double x[3][3];
    std::memcpy(x, a, sizeof(x));
    bool converged = false;
    for (unsigned iteration = 0; iteration < 32u && !converged; ++iteration) {
        const double cof[3][3] = {
            {x[1][1] * x[2][2] - x[1][2] * x[2][1], x[1][2] * x[2][0] - x[1][0] * x[2][2],
             x[1][0] * x[2][1] - x[1][1] * x[2][0]},
            {x[0][2] * x[2][1] - x[0][1] * x[2][2], x[0][0] * x[2][2] - x[0][2] * x[2][0],
             x[0][1] * x[2][0] - x[0][0] * x[2][1]},
            {x[0][1] * x[1][2] - x[0][2] * x[1][1], x[0][2] * x[1][0] - x[0][0] * x[1][2],
             x[0][0] * x[1][1] - x[0][1] * x[1][0]}};
        const double det = x[0][0] * cof[0][0] + x[0][1] * cof[0][1] + x[0][2] * cof[0][2];
        if (!(std::fabs(det) > 1e-24) || !std::isfinite(det)) {
            return d;
        }
        // (X^-1)^T = cofactor / det.
        const double g = std::cbrt(1.0 / std::fabs(det));
        double change = 0.0;
        for (unsigned r = 0; r < 3u; ++r) {
            for (unsigned c = 0; c < 3u; ++c) {
                const double next = 0.5 * (g * x[r][c] + cof[r][c] / (det * g));
                change = std::max(change, std::fabs(next - x[r][c]));
                x[r][c] = next;
            }
        }
        converged = change < 1e-10;
    }
    if (!converged) {
        return d;
    }
    // S = R^T A.
    double stretch[3][3];
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 3u; ++c) {
            stretch[r][c] = x[0][r] * a[0][c] + x[1][r] * a[1][c] + x[2][r] * a[2][c];
        }
    }
    const double det_r = x[0][0] * (x[1][1] * x[2][2] - x[1][2] * x[2][1]) -
                         x[0][1] * (x[1][0] * x[2][2] - x[1][2] * x[2][0]) +
                         x[0][2] * (x[1][0] * x[2][1] - x[1][1] * x[2][0]);
    d.mirrored = det_r < 0.0;
    if (d.mirrored) {
        // R = R' D, D = diag(-1, 1, 1): A = R' (D S).
        for (unsigned r = 0; r < 3u; ++r) x[r][0] = -x[r][0];
        for (unsigned c = 0; c < 3u; ++c) stretch[0][c] = -stretch[0][c];
    }
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 3u; ++c) d.stretch[r * 3u + c] = static_cast<float>(stretch[r][c]);
    }
    float r[3][3];  // r[row][col]
    for (unsigned row = 0; row < 3u; ++row) {
        for (unsigned c = 0; c < 3u; ++c) r[row][c] = static_cast<float>(x[row][c]);
    }
    d.valid = rotation_to_quaternion(r, d.q);
    return d;
}

bool rotation_to_quaternion(const float (&r)[3][3], float (&q)[4]) noexcept {
    Decomposed d{};
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0f) {
        const float s = 0.5f / std::sqrt(trace + 1.0f);
        d.q[3] = 0.25f / s;
        d.q[0] = (r[2][1] - r[1][2]) * s;
        d.q[1] = (r[0][2] - r[2][0]) * s;
        d.q[2] = (r[1][0] - r[0][1]) * s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = 2.0f * std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]);
        d.q[3] = (r[2][1] - r[1][2]) / s;
        d.q[0] = 0.25f * s;
        d.q[1] = (r[0][1] + r[1][0]) / s;
        d.q[2] = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const float s = 2.0f * std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]);
        d.q[3] = (r[0][2] - r[2][0]) / s;
        d.q[0] = (r[0][1] + r[1][0]) / s;
        d.q[1] = 0.25f * s;
        d.q[2] = (r[1][2] + r[2][1]) / s;
    } else {
        const float s = 2.0f * std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]);
        d.q[3] = (r[1][0] - r[0][1]) / s;
        d.q[0] = (r[0][2] + r[2][0]) / s;
        d.q[1] = (r[1][2] + r[2][1]) / s;
        d.q[2] = 0.25f * s;
    }
    for (unsigned i = 0; i < 4u; ++i) q[i] = d.q[i];
    return std::isfinite(q[0]) && std::isfinite(q[1]) && std::isfinite(q[2]) && std::isfinite(q[3]);
}

}  // namespace

void affine_motion(const Affine34& a, const Affine34& b, AffineMotion& out) noexcept {
    out.a = a;
    out.b = b;
    out.rigid = false;
    out.theta = 0.0f;
    out.inv_sin_theta = 0.0f;
    out.cos_turn = 1.0f;
    const Decomposed da = decompose(a);
    const Decomposed db = decompose(b);
    if (!da.valid || !db.valid) {
        return;
    }
    float dot = da.q[0] * db.q[0] + da.q[1] * db.q[1] + da.q[2] * db.q[2] + da.q[3] * db.q[3];
    out.cos_turn = std::fabs(dot);
    if (da.mirrored != db.mirrored) {
        return;
    }
    const float sign = dot < 0.0f ? -1.0f : 1.0f;
    dot *= sign;
    for (unsigned i = 0; i < 4u; ++i) {
        out.qa[i] = da.q[i];
        out.qb[i] = db.q[i] * sign;
    }
    for (unsigned i = 0; i < 9u; ++i) {
        out.stretch_a[i] = da.stretch[i];
        out.stretch_b[i] = db.stretch[i];
    }
    if (dot < 0.9995f) {
        out.theta = std::acos(std::min(dot, 1.0f));
        out.inv_sin_theta = 1.0f / std::sin(out.theta);
    }
    out.rigid = true;
}

Affine34 affine_motion_at(const AffineMotion& m, float t) noexcept {
    Affine34 out{};
    const Affine34& a = m.a;
    const Affine34& b = m.b;
    if (!m.rigid) {
        for (unsigned i = 0; i < 12u; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
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
    if (!(norm > 1e-12f)) {
        // The weights cancelled (a t far outside 0..1): the first rotation stands.
        for (unsigned i = 0; i < 4u; ++i) q[i] = m.qa[i];
        norm = 1.0f;
    }
    norm = 1.0f / std::sqrt(norm);
    for (float& v : q) v *= norm;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float rot[3][3] = {
        {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (x * z + y * w)},
        {2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w)},
        {2.0f * (x * z - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y)}};
    float s[9];
    for (unsigned i = 0; i < 9u; ++i) s[i] = m.stretch_a[i] + (m.stretch_b[i] - m.stretch_a[i]) * t;
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 3u; ++c) {
            out[r * 4u + c] = rot[r][0] * s[c] + rot[r][1] * s[3u + c] + rot[r][2] * s[6u + c];
        }
    }
    for (unsigned r = 0; r < 3u; ++r) {
        out[r * 4u + 3u] = a[r * 4u + 3u] + (b[r * 4u + 3u] - a[r * 4u + 3u]) * t;
    }
    return out;
}

}  // namespace galaxy::render
