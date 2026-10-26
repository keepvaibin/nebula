// SPDX-License-Identifier: GPL-3.0-only
// Displacement normals and the model registry (curved_surfaces.h) on hand-built meshes.
#include "galaxy/gx/curved_surfaces.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using galaxy::gx::curved::Corner;
using galaxy::gx::curved::Vec3;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
bool is_zero(const Vec3& a) { return a[0] == 0.0f && a[1] == 0.0f && a[2] == 0.0f; }

// A warp pipe's end: a 16-sided open cylinder (radius 100, height 100, radial normals) closed on top by a flat cap
// (normal up) that shares the rim's positions; the side and the cap each have their own normals (a hard rim).
struct Pipe {
    std::vector<Vec3> positions;  // 0-15 rim, 16-31 bottom ring, 32 the cap's centre
    std::vector<Vec3> normals;    // 0-15 radial, 16 up
    std::vector<Corner> corners;
};

Pipe pipe() {
    Pipe p;
    constexpr int kSides = 16;
    for (int ring = 0; ring < 2; ++ring) {
        for (int i = 0; i < kSides; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / kSides;
            p.positions.push_back({100.0f * std::cos(a), ring == 0 ? 100.0f : 0.0f, 100.0f * std::sin(a)});
        }
    }
    p.positions.push_back({0.0f, 100.0f, 0.0f});
    for (int i = 0; i < kSides; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / kSides;
        p.normals.push_back({std::cos(a), 0.0f, std::sin(a)});
    }
    p.normals.push_back({0.0f, 1.0f, 0.0f});
    const auto c = [](int pos, int nrm) { return Corner{static_cast<std::uint16_t>(pos), static_cast<std::uint16_t>(nrm)}; };
    for (int i = 0; i < kSides; ++i) {
        const int j = (i + 1) % kSides;
        // Side quad: rim i, rim j, bottom j, bottom i.
        p.corners.insert(p.corners.end(), {c(i, i), c(j, j), c(16 + j, j)});
        p.corners.insert(p.corners.end(), {c(i, i), c(16 + j, j), c(16 + i, i)});
        // Cap fan.
        p.corners.insert(p.corners.end(), {c(32, 16), c(j, 16), c(i, 16)});
    }
    return p;
}

}  // namespace

int main() {
    using namespace galaxy::gx::curved;
    bool passed = true;

    {
        const Pipe p = pipe();
        const std::vector<Vec3> n = displacement_normals(p.positions, p.normals, p.corners);
        bool rim_radial = true;
        bool bottom_radial = true;
        for (int i = 0; i < 16; ++i) {
            const Vec3& radial = p.normals[static_cast<std::size_t>(i)];
            // The rim bends in the horizontal plane only (the cap stays flat, its outline rounds).
            rim_radial &= std::fabs(std::fabs(dot(n[static_cast<std::size_t>(i)], radial)) - 1.0f) < 1e-4f;
            bottom_radial &= std::fabs(std::fabs(dot(n[16u + static_cast<std::size_t>(i)], radial)) - 1.0f) < 1e-4f;
        }
        passed &= expect(rim_radial, "a pipe's rim takes the ring's bend, horizontal");
        passed &= expect(bottom_radial, "an open edge of a smooth side keeps its normal");
        passed &= expect(std::fabs(n[32][1]) > 0.9999f, "the flat cap's centre keeps the cap's normal");
    }
    {
        // A cube: every corner meets three faces (three crease edges): nothing moves.
        const std::vector<Vec3> positions = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                                             {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
        const std::vector<Vec3> normals = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
        const auto c = [](int pos, int nrm) { return Corner{static_cast<std::uint16_t>(pos), static_cast<std::uint16_t>(nrm)}; };
        const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3}, {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
        std::vector<Corner> corners;
        for (int f = 0; f < 6; ++f) {
            corners.insert(corners.end(), {c(faces[f][0], f), c(faces[f][1], f), c(faces[f][2], f)});
            corners.insert(corners.end(), {c(faces[f][0], f), c(faces[f][2], f), c(faces[f][3], f)});
        }
        const std::vector<Vec3> n = displacement_normals(positions, normals, corners);
        bool still = true;
        for (const Vec3& v : n) still &= is_zero(v);
        passed &= expect(still, "a cube's corners do not move");

        // The same box shaded soft: one normal per corner, averaged over its three faces. Still flat.
        std::vector<Vec3> averaged;
        for (const Vec3& p : positions) {
            const float s = 0.57735027f;
            averaged.push_back({p[0] > 0.5f ? s : -s, p[1] > 0.5f ? s : -s, p[2] > 0.5f ? s : -s});
        }
        std::vector<Corner> soft = corners;
        for (Corner& corner : soft) corner.normal = corner.position;
        const std::vector<Vec3> m = displacement_normals(positions, averaged, soft);
        bool flat = true;
        for (const Vec3& v : m) flat &= is_zero(v);
        passed &= expect(flat, "a box with averaged corner normals does not puff into a pillow");

        // A plank shaded soft: corner normals only 20 degrees off the faces, the faces at right angles. Still flat.
        std::vector<Vec3> tilted;
        for (const Vec3& p : positions) {
            const float t = 0.364f;  // tan 20 degrees
            const float x = p[0] > 0.5f ? t : -t;
            const float z = p[2] > 0.5f ? t : -t;
            const float y = p[1] > 0.5f ? 1.0f : -1.0f;
            const float l = std::sqrt(x * x + y * y + z * z);
            tilted.push_back({x / l, y / l, z / l});
        }
        const std::vector<Vec3> plank = displacement_normals(positions, tilted, soft);
        bool plank_flat = true;
        for (const Vec3& v : plank) plank_flat &= is_zero(v);
        passed &= expect(plank_flat, "a soft-shaded plank does not puff into a pillow");
    }
    {
        // A fold: one triangle drawn on both sides (opposite normals, shared positions) bends as a smooth surface.
        const std::vector<Vec3> positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        const std::vector<Vec3> normals = {{0, 0, 1}, {0, 0, -1}};
        const std::vector<Corner> corners = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {1, 1}};
        const std::vector<Vec3> n = displacement_normals(positions, normals, corners);
        passed &= expect(std::fabs(n[0][2]) > 0.9999f, "a fold's sides bend alike");
    }
    {
        // The same pipe with its rim repeated under new indices for the cap: welded by value, the same bends.
        Pipe p = pipe();
        const std::size_t first = p.positions.size();
        for (int i = 0; i < 16; ++i) p.positions.push_back(p.positions[static_cast<std::size_t>(i)]);
        for (std::size_t t = 0; t < p.corners.size(); t += 3u) {
            if (p.corners[t].normal == 16u) {
                for (unsigned k = 1u; k < 3u; ++k) {
                    p.corners[t + k].position = static_cast<std::uint16_t>(first + p.corners[t + k].position);
                }
            }
        }
        const std::vector<Vec3> n = displacement_normals(p.positions, p.normals, p.corners);
        bool same = true;
        for (int i = 0; i < 16; ++i) {
            same &= std::fabs(std::fabs(dot(n[static_cast<std::size_t>(i)], n[first + static_cast<std::size_t>(i)])) - 1.0f) < 1e-5f;
        }
        passed &= expect(same, "positions repeated under another index bend alike");
    }
    {
        const std::vector<Vec3> positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        const std::vector<Vec3> normals = {{0, 0, 1}};
        const std::vector<Corner> corners = {{0, 0}, {1, 0}, {7, 0}};
        const std::vector<Vec3> n = displacement_normals(positions, normals, corners);
        passed &= expect(n.size() == 3u && is_zero(n[0]), "a triangle with an index out of range is left out");
    }
    {
        clear_models();
        const std::byte a[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        const std::byte b[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{5}};
        register_model(0x1000u, a, {Vec3{0, 1, 0}});
        const auto model = find_model(0x1000u);
        passed &= expect(model != nullptr && model->fingerprint == fingerprint(a) &&
                             model->fingerprint != fingerprint(b),
                         "a registered model is found with its array's fingerprint");
        forget_model(0x1000u, nullptr);
        passed &= expect(find_model(0x1000u) != nullptr, "forgetting another model's entry keeps this one");
        forget_model(0x1000u, model.get());
        passed &= expect(find_model(0x1000u) == nullptr, "a forgotten model is gone");
        register_model(0x1000u, a, {Vec3{0, 1, 0}});
        register_model(0x1000u, b, {Vec3{0, 0, 0}});
        passed &= expect(find_model(0x1000u) == nullptr,
                         "a model in which nothing bends is not kept, and replaces the address's earlier one");
    }
    {
        // A large array: its first and last words and its tail are always among the fingerprint's.
        std::vector<std::byte> big(600003u);
        for (std::size_t i = 0; i < big.size(); ++i) {
            big[i] = static_cast<std::byte>(i * 2654435761u >> 13);
        }
        const std::uint64_t whole = fingerprint(big);
        std::vector<std::byte> other = big;
        other[0] ^= std::byte{1};
        bool differ = fingerprint(other) != whole;
        other = big;
        other[600000u - 1u] ^= std::byte{1};
        differ &= fingerprint(other) != whole;
        other = big;
        other.back() ^= std::byte{1};
        differ &= fingerprint(other) != whole;
        other.pop_back();
        differ &= fingerprint(other) != whole;
        passed &= expect(differ, "a large array's first and last words, tail and size each change its fingerprint");
    }

    if (!passed) {
        return 1;
    }
    std::cout << "curved_surfaces tests passed\n";
    return 0;
}
