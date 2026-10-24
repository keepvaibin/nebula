// SPDX-License-Identifier: GPL-3.0-only
// Affine transform math and the per-scene transform matching (transform_history.h).
#include "galaxy/gx/transform_history.h"

#include "galaxy/motion_signals.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace galaxy::gx {

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
void TransformHistory::reserve(std::size_t records) {
    current_.reserve(records);
    previous_.reserve(records);
    current_match_.reserve(records);
    grouped_.reserve(records);
    claimed_.reserve(records);
    std::size_t table = 64u;
    while (table < records * 2u) table <<= 1u;
    table_.reserve(table);
    identity_keys_.reserve(table);
    identity_values_.reserve(table);
}

void TransformHistory::build_identity_index() {
    previous_identified_ = false;
    std::size_t identified = 0u;
    for (const TransformRecord& record : previous_) {
        identified += record.identity != 0u ? 1u : 0u;
    }
    if (identified == 0u) {
        return;
    }
    std::size_t size = 64u;
    while (size < identified * 2u) size <<= 1u;
    identity_keys_.assign(size, 0u);
    identity_values_.assign(size, -1);
    identity_mask_ = size - 1u;
    for (std::size_t n = 0; n < previous_.size(); ++n) {
        const std::uint64_t key = previous_[n].identity;
        if (key == 0u) continue;
        std::size_t slot = static_cast<std::size_t>(key ^ (key >> 31u)) & identity_mask_;
        while (identity_keys_[slot] != 0u && identity_keys_[slot] != key) {
            slot = (slot + 1u) & identity_mask_;
        }
        if (identity_keys_[slot] == 0u) {
            identity_keys_[slot] = key;
            identity_values_[slot] = static_cast<std::int32_t>(n);
        }
    }
    previous_identified_ = true;
}

std::int32_t TransformHistory::find_identity(std::uint64_t key) const noexcept {
    std::size_t slot = static_cast<std::size_t>(key ^ (key >> 31u)) & identity_mask_;
    while (identity_keys_[slot] != 0u) {
        if (identity_keys_[slot] == key) {
            return identity_values_[slot];
        }
        slot = (slot + 1u) & identity_mask_;
    }
    return -1;
}

void TransformHistory::reset() noexcept {
    current_.clear();
    previous_.clear();
    current_match_.clear();
    scene_open_ = false;
    current_valid_ = false;
    previous_valid_ = false;
    current_view_known_ = false;
    previous_view_known_ = false;
    camera_known_ = false;
    camera_delta_ = affine_identity();
    matched_ = 0;
    identified_ = 0;
    identity_matched_ = 0;
    identity_rejected_ = 0;
    signature_rejected_ = 0;
    continuity_rejected_ = 0;
    particle_age_steps_ = {};
    previous_identified_ = false;
}

void TransformHistory::end_scene() noexcept {
    if (scene_open_) {
        scene_open_ = false;
        current_valid_ = true;
    }
}

void TransformHistory::begin_scene(std::uint64_t frame, const Affine34* view) {
    end_scene();
    if (current_valid_) {
        current_.swap(previous_);
        previous_frame_ = current_frame_;
        previous_view_ = current_view_;
        previous_view_known_ = current_view_known_;
        previous_valid_ = true;
        build_previous_index();
        build_identity_index();
    }
    current_.clear();
    current_match_.clear();
    current_valid_ = false;
    current_frame_ = frame;
    current_view_known_ = view != nullptr;
    if (view != nullptr) current_view_ = *view;
    camera_known_ = false;
    camera_delta_ = affine_identity();
    if (previous_valid_ && previous_view_known_ && current_view_known_) {
        Affine34 inverse{};
        if (affine_inverse(current_view_, inverse)) {
            camera_delta_ = affine_multiply(previous_view_, inverse);
            camera_known_ = true;
        }
    }
    matched_ = 0;
    identified_ = 0;
    identity_matched_ = 0;
    identity_rejected_ = 0;
    signature_rejected_ = 0;
    continuity_rejected_ = 0;
    particle_age_steps_ = {};
    scene_open_ = true;
}

std::size_t TransformHistory::find_group(std::uint64_t signature) const noexcept {
    std::size_t slot = static_cast<std::size_t>(signature ^ (signature >> 29u)) & table_mask_;
    while (table_[slot].used && table_[slot].signature != signature) {
        slot = (slot + 1u) & table_mask_;
    }
    return slot;
}

void TransformHistory::build_previous_index() {
    const std::size_t count = previous_.size();
    std::size_t size = 64u;
    while (size < count * 2u) size <<= 1u;
    table_.assign(size, Group{});
    table_mask_ = size - 1u;
    claimed_.assign(count, 0u);
    grouped_.assign(count, -1);
    // Count per signature, lay the groups out contiguously, then fill them
    // in draw order.
    for (std::size_t n = 0; n < count; ++n) {
        Group& group = table_[find_group(previous_[n].signature)];
        group.used = true;
        group.signature = previous_[n].signature;
        ++group.end;
    }
    std::uint32_t offset = 0;
    for (Group& group : table_) {
        if (!group.used) continue;
        const std::uint32_t size_of = group.end;
        group.begin = offset;
        group.first_unclaimed = offset;
        group.end = offset;  // fill cursor
        offset += size_of;
    }
    for (std::size_t n = 0; n < count; ++n) {
        Group& group = table_[find_group(previous_[n].signature)];
        grouped_[group.end++] = static_cast<std::int32_t>(n);
    }
}

const TransformRecord* TransformHistory::add(const TransformRecord& record) {
    if (!scene_open_) return nullptr;
    current_.push_back(record);
    std::int32_t best = -1;
    if (record.identity != 0u) {
        // The game named this object: the previous record of the same
        // identity is its previous transform, or there is none.
        ++identified_;
        if (previous_valid_ && previous_identified_) {
            const std::int32_t found = find_identity(record.identity);
            if (found >= 0) {
                const TransformRecord& previous = previous_[static_cast<std::size_t>(found)];
                const bool particle = record.particle && previous.particle;
                if (particle) {
                    const std::int64_t step = static_cast<std::int64_t>(record.continuity) -
                                              static_cast<std::int64_t>(previous.continuity);
                    ++particle_age_steps_[step < 0 ? 0u : step > 2 ? 4u : static_cast<std::size_t>(step) + 1u];
                }
                if (previous.palette != record.palette || previous.layout != record.layout) {
                    ++identity_rejected_;
                } else if (previous.load_signature != record.load_signature) {
                    ++signature_rejected_;
                } else if (particle && !motion::particle_age_continues(previous.continuity, record.continuity)) {
                    ++continuity_rejected_;
                } else {
                    best = found;
                    ++matched_;
                    ++identity_matched_;
                }
            }
        }
        current_match_.push_back(best);
        return best >= 0 ? &previous_[static_cast<std::size_t>(best)] : nullptr;
    }
    if (previous_valid_ && !previous_.empty()) {
        float predicted[3]{record.translation[0], record.translation[1],
                           record.translation[2]};
        if (camera_known_) {
            affine_transform_point(camera_delta_, record.translation, predicted);
        }
        const float length = std::sqrt(record.translation[0] * record.translation[0] +
                                       record.translation[1] * record.translation[1] +
                                       record.translation[2] * record.translation[2]);
        const float limit = 0.25f * length + 1.0f;
        float best_d2 = limit * limit;
        // A static object differs from its prediction only by rounding.
        const float exact = 1e-3f * (length + 1.0f);
        const float exact_d2 = exact * exact;
        Group& group = table_[find_group(record.signature)];
        if (group.used) {
            // Draw order mostly repeats: claimed records gather at the front
            // and the match is among the first unclaimed ones. The scan is
            // bounded so large groups (coins, star bits) stay linear.
            constexpr unsigned kWindow = 48u;
            while (group.first_unclaimed < group.end &&
                   claimed_[static_cast<std::size_t>(grouped_[group.first_unclaimed])] != 0u) {
                ++group.first_unclaimed;
            }
            unsigned scanned = 0;
            for (std::uint32_t i = group.first_unclaimed; i < group.end && scanned < kWindow; ++i) {
                const std::int32_t n = grouped_[i];
                const auto index = static_cast<std::size_t>(n);
                if (claimed_[index] != 0u) continue;
                ++scanned;
                const TransformRecord& prev = previous_[index];
                const float dx = prev.translation[0] - predicted[0];
                const float dy = prev.translation[1] - predicted[1];
                const float dz = prev.translation[2] - predicted[2];
                const float d2 = dx * dx + dy * dy + dz * dz;
                if (d2 <= best_d2) {
                    best_d2 = d2;
                    best = n;
                    if (d2 <= exact_d2) break;
                }
            }
        }
        if (best >= 0) {
            claimed_[static_cast<std::size_t>(best)] = 1u;
            ++matched_;
        }
    }
    current_match_.push_back(best);
    return best >= 0 ? &previous_[static_cast<std::size_t>(best)] : nullptr;
}

TransformHistoryStats TransformHistory::stats() const noexcept {
    TransformHistoryStats s{};
    s.records = static_cast<std::uint32_t>(current_.size());
    s.matched = matched_;
    s.previous_records = previous_valid_ ? static_cast<std::uint32_t>(previous_.size()) : 0u;
    s.identified = identified_;
    s.identity_matched = identity_matched_;
    s.identity_rejected = identity_rejected_;
    s.signature_rejected = signature_rejected_;
    s.continuity_rejected = continuity_rejected_;
    s.particle_age_steps = particle_age_steps_;
    return s;
}

}  // namespace galaxy::gx
