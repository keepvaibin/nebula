// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/transform_history.h"

#include <algorithm>
#include <cmath>

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

void affine_transform_point(const Affine34& m, const float p[3], float out[3]) noexcept {
    const float x = p[0], y = p[1], z = p[2];
    out[0] = m[0] * x + m[1] * y + m[2] * z + m[3];
    out[1] = m[4] * x + m[5] * y + m[6] * z + m[7];
    out[2] = m[8] * x + m[9] * y + m[10] * z + m[11];
}

namespace {

struct Decomposed {
    float q[4];      // rotation quaternion x, y, z, w
    float scale[3];  // per-axis (column) scale; scale[0] < 0 for mirrored
    bool valid;
};

Decomposed decompose(const Affine34& m) noexcept {
    Decomposed d{};
    float col[3][3];
    for (unsigned c = 0; c < 3u; ++c) {
        for (unsigned r = 0; r < 3u; ++r) col[c][r] = m[r * 4u + c];
    }
    for (unsigned c = 0; c < 3u; ++c) {
        d.scale[c] = std::sqrt(col[c][0] * col[c][0] + col[c][1] * col[c][1] +
                               col[c][2] * col[c][2]);
        if (!(d.scale[c] > 1e-12f)) return d;
    }
    const float det = m[0] * (m[5] * m[10] - m[6] * m[9]) -
                      m[1] * (m[4] * m[10] - m[6] * m[8]) +
                      m[2] * (m[4] * m[9] - m[5] * m[8]);
    if (det < 0.0f) d.scale[0] = -d.scale[0];
    float r[3][3];  // r[row][col]
    for (unsigned c = 0; c < 3u; ++c) {
        for (unsigned row = 0; row < 3u; ++row) r[row][c] = col[c][row] / d.scale[c];
    }
    // Orthonormal within a small tolerance, else no clean rotation.
    for (unsigned a = 0; a < 3u; ++a) {
        for (unsigned b = a + 1u; b < 3u; ++b) {
            const float dot = r[0][a] * r[0][b] + r[1][a] * r[1][b] + r[2][a] * r[2][b];
            if (std::fabs(dot) > 2e-3f) return d;
        }
    }
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
    d.valid = std::isfinite(d.q[0]) && std::isfinite(d.q[1]) &&
              std::isfinite(d.q[2]) && std::isfinite(d.q[3]);
    return d;
}

}  // namespace

Affine34 affine_interpolate(const Affine34& a, const Affine34& b, float t) noexcept {
    Affine34 out{};
    const Decomposed da = decompose(a);
    const Decomposed db = decompose(b);
    if (!da.valid || !db.valid || (da.scale[0] < 0.0f) != (db.scale[0] < 0.0f)) {
        for (unsigned i = 0; i < 12u; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
        return out;
    }
    float qb[4]{db.q[0], db.q[1], db.q[2], db.q[3]};
    float dot = da.q[0] * qb[0] + da.q[1] * qb[1] + da.q[2] * qb[2] + da.q[3] * qb[3];
    if (dot < 0.0f) {
        for (float& v : qb) v = -v;
        dot = -dot;
    }
    float wa = 1.0f - t, wb = t;
    if (dot < 0.9995f) {
        const float theta = std::acos(std::min(dot, 1.0f));
        const float sin_theta = std::sin(theta);
        wa = std::sin((1.0f - t) * theta) / sin_theta;
        wb = std::sin(t * theta) / sin_theta;
    }
    float q[4];
    float norm = 0.0f;
    for (unsigned i = 0; i < 4u; ++i) {
        q[i] = da.q[i] * wa + qb[i] * wb;
        norm += q[i] * q[i];
    }
    norm = 1.0f / std::sqrt(norm);
    for (float& v : q) v *= norm;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float rot[3][3] = {
        {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (x * z + y * w)},
        {2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w)},
        {2.0f * (x * z - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y)}};
    for (unsigned c = 0; c < 3u; ++c) {
        const float s = da.scale[c] + (db.scale[c] - da.scale[c]) * t;
        for (unsigned r = 0; r < 3u; ++r) out[r * 4u + c] = rot[r][c] * s;
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
    return s;
}

}  // namespace galaxy::gx
