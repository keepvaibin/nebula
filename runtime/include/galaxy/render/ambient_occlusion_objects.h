// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The soft occlusion a model casts on what is around it (ambient_occlusion.hlsl, object_visibility): depth alone cannot tell
// how a plant stands over the ground, so a pool of shadow under it would show as its silhouette on what lies behind. Each
// opaque model of the scene is an ellipsoid from its own vertices, under its own matrices, whose occlusion of a point follows
// from the point's position and normal in the ellipsoid's space: nothing depends on what the camera sees of the model.

#include "galaxy/render/ambient_occlusion_shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace galaxy::render::ao {

// A model's bounds under one of its position matrices (`row`: the matrix's first row, as a vertex names it).
struct ObjectSlot {
    std::uint32_t row = 0u;
    float min[3]{};
    float max[3]{};
};

// In eye space: the centre and the half axes.
struct ObjectOccluder {
    float centre[3]{};
    float axis[3][3]{};
};

// Smaller models add nothing visible; larger ones are the stage's own ground and walls, whose creases are the screen's.
inline constexpr float kObjectMinRadius = 8.0f;
inline constexpr float kObjectMaxRadius = 600.0f;
// The box's corners stand outside the ellipsoid through its faces: the ellipsoid holding most of its volume.
inline constexpr float kObjectFit = 1.15f;
// Thinner than this against its largest axis, a model (a coin, a paving stone) has no volume to block light with.
inline constexpr float kObjectFlattest = 0.25f;
// How far the occlusion reaches, in the ellipsoid's radii.
inline constexpr float kObjectReach = 3.0f;
inline constexpr float kObjectStrength = 0.85f;
// The bins: 32 occlusion pixels square (object_visibility shifts by 5), each with the largest few of what reaches it.
inline constexpr unsigned kObjectTilePixels = 32u;
inline constexpr unsigned kObjectTileMost = 32u;

// One scene's occluders, as its draws arrive. A model's groups make one occluder: a model of one matrix (its groups under that
// same matrix) in that matrix's axes, a jointed one (the same position array, touching) in the world's, whose shape follows
// no one joint.
class ObjectCollector {
public:
    void begin(const float* view_rows) {
        list_.clear();
        open_ = false;
        view_valid_ = view_rows != nullptr;
        if (view_valid_) {
            std::copy_n(view_rows, 12u, view_);
        }
    }

    // `value(row, column)`: the draw's position matrices, 3 rows of 4 each.
    template <typename Value>
    void add(std::uint64_t key, std::span<const ObjectSlot> slots, const Value& value) {
        if (slots.empty()) {
            return;
        }
        float min[3]{1e30f, 1e30f, 1e30f};
        float max[3]{-1e30f, -1e30f, -1e30f};
        const bool jointed = slots.size() > 1u;
        bool joins = open_ && key == key_ && jointed == jointed_;
        if (joins && !jointed) {
            for (unsigned i = 0u; joins && i < 12u; ++i) {
                joins = value(slots[0].row + i / 4u, i % 4u) == frame_[i];
            }
        }
        float frame[12];
        float inverse[12];
        if (joins) {
            std::copy_n(frame_, 12u, frame);
            std::copy_n(inverse_, 12u, inverse);
        } else {
            if (jointed && view_valid_) {
                std::copy_n(view_, 12u, frame);
            } else if (jointed) {
                static constexpr float kIdentity[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
                std::copy_n(kIdentity, 12u, frame);
            } else {
                for (unsigned i = 0u; i < 12u; ++i) {
                    frame[i] = value(slots[0].row + i / 4u, i % 4u);
                }
            }
            if (!invert(frame, inverse)) {
                return;
            }
        }
        for (const ObjectSlot& slot : slots) {
            for (unsigned corner = 0u; corner < 8u; ++corner) {
                const float local[3] = {(corner & 1u) != 0u ? slot.max[0] : slot.min[0],
                                        (corner & 2u) != 0u ? slot.max[1] : slot.min[1],
                                        (corner & 4u) != 0u ? slot.max[2] : slot.min[2]};
                float eye[3];
                for (unsigned r = 0u; r < 3u; ++r) {
                    eye[r] = value(slot.row + r, 0u) * local[0] + value(slot.row + r, 1u) * local[1] +
                             value(slot.row + r, 2u) * local[2] + value(slot.row + r, 3u);
                }
                for (unsigned r = 0u; r < 3u; ++r) {
                    const float v = inverse[r * 4u] * eye[0] + inverse[r * 4u + 1u] * eye[1] + inverse[r * 4u + 2u] * eye[2] +
                                    inverse[r * 4u + 3u];
                    min[r] = std::min(min[r], v);
                    max[r] = std::max(max[r], v);
                }
            }
        }
        if (joins && (!jointed || touches(min, max))) {
            for (unsigned r = 0u; r < 3u; ++r) {
                min_[r] = std::min(min_[r], min[r]);
                max_[r] = std::max(max_[r], max[r]);
            }
            return;
        }
        finish();
        open_ = true;
        key_ = key;
        jointed_ = jointed;
        std::copy_n(frame, 12u, frame_);
        std::copy_n(inverse, 12u, inverse_);
        std::copy_n(min, 3u, min_);
        std::copy_n(max, 3u, max_);
    }

    // The model still open joins the list.
    void finish() {
        if (!open_) {
            return;
        }
        open_ = false;
        ObjectOccluder o;
        float lengths[3];
        float directions[3][3];
        float largest = 0.0f;
        for (unsigned a = 0u; a < 3u; ++a) {
            const float column[3] = {frame_[a], frame_[4u + a], frame_[8u + a]};
            const float scale = std::sqrt(column[0] * column[0] + column[1] * column[1] + column[2] * column[2]);
            if (!(scale > 1e-9f)) {
                return;
            }
            for (unsigned r = 0u; r < 3u; ++r) {
                directions[a][r] = column[r] / scale;
            }
            lengths[a] = scale * 0.5f * (max_[a] - min_[a]) * kObjectFit;
            largest = std::max(largest, lengths[a]);
        }
        if (largest < kObjectMinRadius || largest > kObjectMaxRadius ||
            std::min({lengths[0], lengths[1], lengths[2]}) < largest * kObjectFlattest) {
            return;
        }
        for (unsigned a = 0u; a < 3u; ++a) {
            for (unsigned r = 0u; r < 3u; ++r) {
                o.axis[a][r] = directions[a][r] * lengths[a];
            }
        }
        const float c[3] = {0.5f * (min_[0] + max_[0]), 0.5f * (min_[1] + max_[1]), 0.5f * (min_[2] + max_[2])};
        for (unsigned r = 0u; r < 3u; ++r) {
            o.centre[r] = frame_[r * 4u] * c[0] + frame_[r * 4u + 1u] * c[1] + frame_[r * 4u + 2u] * c[2] + frame_[r * 4u + 3u];
        }
        list_.push_back(o);
    }

    [[nodiscard]] std::vector<ObjectOccluder>& list() noexcept { return list_; }

private:
    [[nodiscard]] bool touches(const float* min, const float* max) const noexcept {
        for (unsigned r = 0u; r < 3u; ++r) {
            if (min[r] > max_[r] || max[r] < min_[r]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static bool invert(const float* m, float* out) noexcept {
        const float a = m[5] * m[10] - m[6] * m[9];
        const float b = m[6] * m[8] - m[4] * m[10];
        const float c = m[4] * m[9] - m[5] * m[8];
        const float det = m[0] * a + m[1] * b + m[2] * c;
        if (!(std::abs(det) > 1e-12f)) {
            return false;
        }
        const float s = 1.0f / det;
        out[0] = a * s;
        out[1] = (m[2] * m[9] - m[1] * m[10]) * s;
        out[2] = (m[1] * m[6] - m[2] * m[5]) * s;
        out[4] = b * s;
        out[5] = (m[0] * m[10] - m[2] * m[8]) * s;
        out[6] = (m[2] * m[4] - m[0] * m[6]) * s;
        out[8] = c * s;
        out[9] = (m[1] * m[8] - m[0] * m[9]) * s;
        out[10] = (m[0] * m[5] - m[1] * m[4]) * s;
        for (unsigned r = 0u; r < 3u; ++r) {
            out[r * 4u + 3u] = -(out[r * 4u] * m[3] + out[r * 4u + 1u] * m[7] + out[r * 4u + 2u] * m[11]);
        }
        return true;
    }

    std::vector<ObjectOccluder> list_;
    bool open_ = false;
    bool jointed_ = false;
    bool view_valid_ = false;
    std::uint64_t key_ = 0u;
    float view_[12]{};
    float frame_[12]{};
    float inverse_[12]{};
    float min_[3]{};
    float max_[3]{};
};

// The shaders' buffers. objects: per occluder four float4s (each axis over its length squared with that length squared; the
// centre with the strength). tiles: columns, rows, then each tile's first index and count, then the indices.
inline void bin_objects(std::span<const ObjectOccluder> occluders, const Constants& c, std::vector<float>& objects,
                        std::vector<std::uint32_t>& tiles) {
    const std::uint32_t cols = (c.size[0] + kObjectTilePixels - 1u) / kObjectTilePixels;
    const std::uint32_t rows = (c.size[1] + kObjectTilePixels - 1u) / kObjectTilePixels;
    const std::uint32_t count = cols * rows;
    objects.clear();
    tiles.assign(2u + 2u * count, 0u);
    tiles[0] = cols;
    tiles[1] = rows;
    // Largest on the screen first: a full tile keeps those, the same ones each frame the view holds still.
    std::vector<float> reach(occluders.size());
    std::vector<float> on_screen(occluders.size());
    std::vector<std::uint32_t> order(occluders.size());
    std::iota(order.begin(), order.end(), 0u);
    for (std::size_t i = 0; i < occluders.size(); ++i) {
        float largest = 0.0f;
        for (const auto& axis : occluders[i].axis) {
            largest = std::max(largest, axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        }
        reach[i] = std::sqrt(largest) * kObjectReach;
        on_screen[i] = reach[i] / std::max(-occluders[i].centre[2], 1.0f);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return on_screen[a] > on_screen[b]; });
    std::vector<std::uint32_t> counts(count, 0u);
    std::vector<std::uint32_t> lists(static_cast<std::size_t>(count) * kObjectTileMost);
    const float divisor = static_cast<float>(c.size[2]);
    for (const std::uint32_t i : order) {
        const ObjectOccluder& o = occluders[i];
        const float distance = -o.centre[2];
        if (distance + reach[i] <= 0.0f) {
            continue;
        }
        std::uint32_t x0 = 0u, y0 = 0u, x1 = cols - 1u, y1 = rows - 1u;
        if (distance - reach[i] > 1.0f) {
            const float cx = (o.centre[0] / distance - c.ray[1]) / c.ray[0] / divisor;
            const float cy = (o.centre[1] / distance - c.ray[3]) / c.ray[2] / divisor;
            const float spread = reach[i] / (distance - reach[i]);
            const float rx = spread / std::abs(c.ray[0]) / divisor;
            const float ry = spread / std::abs(c.ray[2]) / divisor;
            const float tile = static_cast<float>(kObjectTilePixels);
            const float left = (cx - rx) / tile, right = (cx + rx) / tile, top = (cy - ry) / tile, bottom = (cy + ry) / tile;
            if (right < 0.0f || bottom < 0.0f || left >= static_cast<float>(cols) || top >= static_cast<float>(rows)) {
                continue;
            }
            x0 = static_cast<std::uint32_t>(std::max(left, 0.0f));
            y0 = static_cast<std::uint32_t>(std::max(top, 0.0f));
            x1 = std::min(static_cast<std::uint32_t>(std::max(right, 0.0f)), cols - 1u);
            y1 = std::min(static_cast<std::uint32_t>(std::max(bottom, 0.0f)), rows - 1u);
        }
        const auto index = static_cast<std::uint32_t>(objects.size() / 16u);
        bool used = false;
        for (std::uint32_t y = y0; y <= y1; ++y) {
            for (std::uint32_t x = x0; x <= x1; ++x) {
                const std::uint32_t t = y * cols + x;
                if (counts[t] < kObjectTileMost) {
                    lists[static_cast<std::size_t>(t) * kObjectTileMost + counts[t]++] = index;
                    used = true;
                }
            }
        }
        if (!used) {
            continue;
        }
        for (const auto& axis : o.axis) {
            const float length2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
            objects.insert(objects.end(), {axis[0] / length2, axis[1] / length2, axis[2] / length2, length2});
        }
        objects.insert(objects.end(), {o.centre[0], o.centre[1], o.centre[2], kObjectStrength});
    }
    for (std::uint32_t t = 0u; t < count; ++t) {
        tiles[2u + 2u * t] = static_cast<std::uint32_t>(tiles.size());
        tiles[3u + 2u * t] = counts[t];
        tiles.insert(tiles.end(), lists.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(t) * kObjectTileMost),
                     lists.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(t) * kObjectTileMost + counts[t]));
    }
    if (objects.empty()) {
        objects.assign(16u, 0.0f);
    }
}

}  // namespace galaxy::render::ao
