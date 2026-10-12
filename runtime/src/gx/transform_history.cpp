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

void TransformHistory::reserve(std::size_t records) {
    current_.reserve(records);
    previous_.reserve(records);
    current_match_.reserve(records);
    previous_next_.reserve(records);
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

void TransformHistory::build_previous_index() {
    const std::size_t count = previous_.size();
    std::size_t size = 64u;
    while (size < count * 2u) size <<= 1u;
    table_.assign(size, -1);
    table_mask_ = size - 1u;
    previous_next_.assign(count, -1);
    claimed_.assign(count, 0u);
    // Reverse insertion keeps each chain in draw order.
    for (std::size_t n = count; n-- > 0;) {
        const std::uint64_t sig = previous_[n].signature;
        std::size_t slot = static_cast<std::size_t>(sig ^ (sig >> 29u)) & table_mask_;
        // Each slot heads one signature's chain; probe past other signatures.
        while (table_[slot] >= 0 &&
               previous_[static_cast<std::size_t>(table_[slot])].signature != sig) {
            slot = (slot + 1u) & table_mask_;
        }
        previous_next_[n] = table_[slot];
        table_[slot] = static_cast<std::int32_t>(n);
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
        const std::uint64_t sig = record.signature;
        std::size_t slot = static_cast<std::size_t>(sig ^ (sig >> 29u)) & table_mask_;
        while (table_[slot] >= 0 &&
               previous_[static_cast<std::size_t>(table_[slot])].signature != sig) {
            slot = (slot + 1u) & table_mask_;
        }
        for (std::int32_t n = table_[slot]; n >= 0;
             n = previous_next_[static_cast<std::size_t>(n)]) {
            const auto index = static_cast<std::size_t>(n);
            if (claimed_[index] != 0u) continue;
            const TransformRecord& prev = previous_[index];
            const float dx = prev.translation[0] - predicted[0];
            const float dy = prev.translation[1] - predicted[1];
            const float dz = prev.translation[2] - predicted[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 <= best_d2) {
                best_d2 = d2;
                best = n;
                if (d2 == 0.0f) break;
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
