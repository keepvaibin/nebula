// SPDX-License-Identifier: GPL-3.0-only
// Curved surfaces: displacement normals for Phong tessellation and the registry of models (curved_surfaces.h).
#include "galaxy/gx/curved_surfaces.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

namespace galaxy::gx::curved {
namespace {

float dot(const Vec3& a, const Vec3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

Vec3 sub(const Vec3& a, const Vec3& b) noexcept { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

Vec3 normalized(const Vec3& v) noexcept {
    const float length = std::sqrt(dot(v, v));
    return length > 1e-20f ? Vec3{v[0] / length, v[1] / length, v[2] / length} : Vec3{0.0f, 0.0f, 0.0f};
}

// Normals within 2 degrees of each other, either sign (Phong's projection does not see the sign).
bool parallel(const Vec3& a, const Vec3& b) noexcept {
    static const float kCos = std::cos(2.0f * 3.14159265f / 180.0f);
    return std::fabs(dot(a, b)) >= kCos;
}

// A position's hash, -0 taken as 0 (they compare equal).
std::uint64_t bits_of(const Vec3& p) noexcept {
    const float c[3] = {p[0] + 0.0f, p[1] + 0.0f, p[2] + 0.0f};
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t z;
    std::memcpy(&x, &c[0], 4u);
    std::memcpy(&y, &c[1], 4u);
    std::memcpy(&z, &c[2], 4u);
    std::uint64_t h = 0x9E3779B97F4A7C15ull;
    for (const std::uint32_t w : {x, y, z}) {
        h ^= w + 0x9E3779B97F4A7C15ull + (h << 6u) + (h >> 2u);
    }
    return h;
}

struct VecHash {
    std::size_t operator()(const Vec3& p) const noexcept { return static_cast<std::size_t>(bits_of(p)); }
};

struct Registry {
    std::shared_mutex lock;
    std::unordered_map<std::uint32_t, std::shared_ptr<const Model>> models;
};

Registry& registry() {
    static Registry r;
    return r;
}

}  // namespace

std::vector<Vec3> displacement_normals(std::span<const Vec3> positions, std::span<const Vec3> normals,
                                       std::span<const Corner> corners) {
    std::vector<Vec3> out(positions.size(), Vec3{0.0f, 0.0f, 0.0f});
    // Positions by value: each index's place, and the places' coordinates.
    std::unordered_map<Vec3, std::uint32_t, VecHash> place_of;
    std::vector<std::uint32_t> place(positions.size());
    std::vector<Vec3> places;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto [it, added] = place_of.try_emplace(positions[i], static_cast<std::uint32_t>(places.size()));
        if (added) places.push_back(positions[i]);
        place[i] = it->second;
    }
    // Per place, the normals its triangles give it and the triangles' own (geometric) normals; per (place, neighbour)
    // edge end, the normals of the triangles holding that edge.
    std::vector<std::vector<Vec3>> at(places.size());
    std::vector<std::vector<Vec3>> faces(places.size());
    std::unordered_map<std::uint64_t, std::vector<Vec3>> edge_ends;
    for (std::size_t t = 0; t + 2u < corners.size(); t += 3u) {
        std::uint32_t p[3];
        Vec3 n[3];
        bool valid = true;
        for (unsigned k = 0; k < 3u; ++k) {
            const Corner& c = corners[t + k];
            if (c.position >= positions.size() || c.normal >= normals.size()) {
                valid = false;
                break;
            }
            p[k] = place[c.position];
            n[k] = normalized(normals[c.normal]);
        }
        if (!valid || p[0] == p[1] || p[1] == p[2] || p[0] == p[2]) continue;
        const Vec3 e1 = sub(places[p[1]], places[p[0]]);
        const Vec3 e2 = sub(places[p[2]], places[p[0]]);
        const Vec3 face = normalized(Vec3{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                          e1[0] * e2[1] - e1[1] * e2[0]});
        for (unsigned k = 0; k < 3u; ++k) {
            at[p[k]].push_back(n[k]);
            faces[p[k]].push_back(face);
            for (const unsigned j : {(k + 1u) % 3u, (k + 2u) % 3u}) {
                edge_ends[(static_cast<std::uint64_t>(p[k]) << 32u) | p[j]].push_back(n[k]);
            }
        }
    }
    // Each place's crease neighbours: edges whose triangles disagree on its normal there, or held by one triangle.
    std::vector<std::vector<std::uint32_t>> creases(places.size());
    for (const auto& [key, ends] : edge_ends) {
        const auto from = static_cast<std::uint32_t>(key >> 32u);
        const bool crease = ends.size() == 1u ||
                            !std::all_of(ends.begin(), ends.end(), [&](const Vec3& n) { return parallel(ends[0], n); });
        if (crease) creases[from].push_back(static_cast<std::uint32_t>(key & 0xFFFFFFFFu));
    }
    std::vector<Vec3> bend(places.size(), Vec3{0.0f, 0.0f, 0.0f});
    for (std::size_t q = 0; q < places.size(); ++q) {
        const std::vector<Vec3>& ns = at[q];
        if (ns.empty()) continue;
        if (std::all_of(ns.begin(), ns.end(), [&](const Vec3& n) { return parallel(ns[0], n); })) {
            // Shape, not shading: a sampled curved surface's faces meet at small angles around each of its points (a
            // 16-sided pipe's at 22.5 degrees) and its normal stays near them. A box shaded soft, its corner normals
            // tilted for the look, has faces at right angles there: rounded, it would puff into a pillow.
            static const float kFaceToFace = std::cos(40.0f * 3.14159265f / 180.0f);
            static const float kNormalToFace = std::cos(35.0f * 3.14159265f / 180.0f);
            const std::vector<Vec3>& fs = faces[q];
            bool sampled = true;
            for (std::size_t i = 0; i < fs.size() && sampled; ++i) {
                if (dot(fs[i], fs[i]) == 0.0f) continue;
                sampled = std::fabs(dot(fs[i], ns[0])) >= kNormalToFace;
                for (std::size_t j = i + 1; j < fs.size() && sampled; ++j) {
                    sampled = dot(fs[j], fs[j]) == 0.0f || std::fabs(dot(fs[i], fs[j])) >= kFaceToFace;
                }
            }
            if (sampled) bend[q] = ns[0];
            continue;
        }
        if (creases[q].size() != 2u) continue;
        const Vec3& a = places[creases[q][0]];
        const Vec3& b = places[creases[q][1]];
        const Vec3 chord = sub(b, a);
        const float chord_length = std::sqrt(dot(chord, chord));
        if (!(chord_length > 0.0f)) continue;
        const Vec3 along{chord[0] / chord_length, chord[1] / chord_length, chord[2] / chord_length};
        Vec3 offset = sub(Vec3{(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f}, places[q]);
        const float d = dot(offset, along);
        offset = {offset[0] - d * along[0], offset[1] - d * along[1], offset[2] - d * along[2]};
        // A straight crease (its midpoint on the line through its ends) does not bend.
        if (std::sqrt(dot(offset, offset)) > 1e-3f * chord_length) bend[q] = normalized(offset);
    }
    for (std::size_t i = 0; i < positions.size(); ++i) {
        out[i] = bend[place[i]];
    }
    return out;
}

std::uint64_t fingerprint(std::span<const std::byte> bytes) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ull;
    const auto word = [&h, &bytes](std::size_t index) {
        std::uint64_t w;
        std::memcpy(&w, bytes.data() + index * 8u, 8u);
        h = (h ^ w) * 0x100000001B3ull;
    };
    const std::size_t words = bytes.size() / 8u;
    if (words <= kFingerprintWords) {
        for (std::size_t i = 0; i < words; ++i) {
            word(i);
        }
    } else {
        for (std::size_t k = 0; k < kFingerprintWords; ++k) {
            word(k * (words - 1u) / (kFingerprintWords - 1u));
        }
    }
    for (std::size_t i = words * 8u; i < bytes.size(); ++i) {
        h = (h ^ static_cast<std::uint64_t>(bytes[i])) * 0x100000001B3ull;
    }
    return h ^ bytes.size();
}

void register_model(std::uint32_t array_address, std::span<const std::byte> array_bytes, std::vector<Vec3> normals) {
    const bool bends = std::any_of(normals.begin(), normals.end(),
                                   [](const Vec3& n) { return n[0] != 0.0f || n[1] != 0.0f || n[2] != 0.0f; });
    if (!bends) {
        Registry& r = registry();
        std::unique_lock guard(r.lock);
        r.models.erase(array_address);
        return;
    }
    auto model = std::make_shared<Model>();
    model->fingerprint = fingerprint(array_bytes);
    model->bytes = static_cast<std::uint32_t>(array_bytes.size());
    model->normals = std::move(normals);
    Registry& r = registry();
    std::unique_lock guard(r.lock);
    r.models[array_address] = std::move(model);
}

std::shared_ptr<const Model> find_model(std::uint32_t array_address) {
    Registry& r = registry();
    std::shared_lock guard(r.lock);
    const auto it = r.models.find(array_address);
    return it != r.models.end() ? it->second : nullptr;
}

void forget_model(std::uint32_t array_address, const Model* model) {
    Registry& r = registry();
    std::unique_lock guard(r.lock);
    const auto it = r.models.find(array_address);
    if (it != r.models.end() && it->second.get() == model) r.models.erase(it);
}

void clear_models() {
    Registry& r = registry();
    std::unique_lock guard(r.lock);
    r.models.clear();
}

}  // namespace galaxy::gx::curved
