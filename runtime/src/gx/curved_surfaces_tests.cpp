// SPDX-License-Identifier: GPL-3.0-only
// The curved-surface analysis and the model registry (curved_surfaces.h) on hand-built meshes.
#include "galaxy/gx/curved_surfaces.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

namespace {

using galaxy::gx::curved::Corner;
using galaxy::gx::curved::Surface;
using galaxy::gx::curved::Vec3;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
bool is_zero(const Vec3& a) { return a[0] == 0.0f && a[1] == 0.0f && a[2] == 0.0f; }

Corner c(int position, int normal) {
    return Corner{static_cast<std::uint16_t>(position), static_cast<std::uint16_t>(normal)};
}

// The move Phong tessellation gives an edge's midpoint from its two ends' normals (shape factor one half).
float midpoint_move(const Surface& s, const std::vector<Vec3>& positions, Corner a, Corner b) {
    using galaxy::gx::curved::edge_normal;
    const Vec3 e = sub(positions[b.position], positions[a.position]);
    const Vec3 na = edge_normal(s, a.position, a.normal, b.position);
    const Vec3 nb = edge_normal(s, b.position, b.normal, a.position);
    const float ea = dot(e, na);
    const float eb = dot(e, nb);
    const Vec3 m{0.125f * (eb * nb[0] - ea * na[0]), 0.125f * (eb * nb[1] - ea * na[1]),
                 0.125f * (eb * nb[2] - ea * na[2])};
    return std::sqrt(dot(m, m));
}

// The largest move of any edge midpoint.
float largest_move(const Surface& s, const std::vector<Vec3>& positions, const std::vector<Corner>& corners) {
    float worst = 0.0f;
    for (std::size_t t = 0; t + 2u < corners.size(); t += 3u) {
        for (unsigned k = 0; k < 3u; ++k) {
            worst = std::fmax(worst, midpoint_move(s, positions, corners[t + k], corners[t + (k + 1u) % 3u]));
        }
    }
    return worst;
}

// Every edge two triangles share takes the same term of the move from each end in both: no cracks.
bool crack_free(const Surface& s, const std::vector<Vec3>& positions, const std::vector<Corner>& corners) {
    using galaxy::gx::curved::edge_normal;
    std::map<std::pair<int, int>, Vec3> seen;
    for (std::size_t t = 0; t + 2u < corners.size(); t += 3u) {
        for (unsigned i = 0; i < 3u; ++i) {
            for (unsigned k = 0; k < 3u; ++k) {
                if (i == k) continue;
                const Corner a = corners[t + i];
                const Corner b = corners[t + k];
                const Vec3 n = edge_normal(s, a.position, a.normal, b.position);
                const float h = dot(sub(positions[b.position], positions[a.position]), n);
                const Vec3 term{h * n[0], h * n[1], h * n[2]};
                const std::pair<int, int> key{s.place[a.position], s.place[b.position]};
                const auto [it, added] = seen.try_emplace(key, term);
                if (!added && std::fabs(it->second[0] - term[0]) + std::fabs(it->second[1] - term[1]) +
                                      std::fabs(it->second[2] - term[2]) > 1e-4f) {
                    return false;
                }
            }
        }
    }
    return true;
}

// A warp pipe's end: a 16-sided open cylinder (radius 100, height 100, radial normals) closed on top by a flat cap
// (normal up) that shares the rim's positions; the side and the cap each have their own normals (a hard rim).
struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Corner> corners;
};

Mesh pipe() {
    Mesh p;
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

// A box from (0,0,0) to `size`, each face its own normals, given per corner by `normal_of(position, face)`.
template <typename NormalOf>
Mesh box(const Vec3& size, NormalOf normal_of) {
    Mesh m;
    for (int i = 0; i < 8; ++i) {
        m.positions.push_back({(i & 1) ? size[0] : 0.0f, (i & 2) ? size[1] : 0.0f, (i & 4) ? size[2] : 0.0f});
    }
    const int faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}};
    for (int f = 0; f < 6; ++f) {
        int idx[4];
        for (int k = 0; k < 4; ++k) {
            idx[k] = static_cast<int>(m.normals.size());
            m.normals.push_back(normal_of(m.positions[static_cast<std::size_t>(faces[f][k])], f));
        }
        m.corners.insert(m.corners.end(), {c(faces[f][0], idx[0]), c(faces[f][1], idx[1]), c(faces[f][2], idx[2])});
        m.corners.insert(m.corners.end(), {c(faces[f][0], idx[0]), c(faces[f][2], idx[2]), c(faces[f][3], idx[3])});
    }
    return m;
}

Vec3 unit(Vec3 v) {
    const float l = std::sqrt(dot(v, v));
    return {v[0] / l, v[1] / l, v[2] / l};
}

}  // namespace

int main() {
    using namespace galaxy::gx::curved;
    bool passed = true;

    {
        const Mesh p = pipe();
        const Surface s = analyse(p.positions, p.normals, p.corners);
        bool rim_round = true;
        bool side_round = true;
        bool bottom_round = true;
        bool cap_flat = true;
        for (int i = 0; i < 16; ++i) {
            const int j = (i + 1) % 16;
            const Vec3& radial = p.normals[static_cast<std::size_t>(i)];
            const auto u = static_cast<std::uint16_t>(i);
            // The rim edge bends by the ring's circle (horizontal, radial); the side by its normal; the cap's own
            // edges stay in its plane.
            rim_round &= std::fabs(std::fabs(dot(edge_normal(s, u, u, static_cast<std::uint16_t>(j)), radial)) - 1.0f) < 1e-4f;
            side_round &= std::fabs(std::fabs(dot(edge_normal(s, u, u, static_cast<std::uint16_t>(16 + i)), radial)) - 1.0f) < 1e-4f;
            bottom_round &= std::fabs(std::fabs(dot(edge_normal(s, static_cast<std::uint16_t>(16 + i), u,
                                                                static_cast<std::uint16_t>(16 + j)), radial)) - 1.0f) < 1e-4f;
            const Vec3 cap = edge_normal(s, u, 16u, 32u);
            cap_flat &= std::fabs(dot(sub(p.positions[32], p.positions[static_cast<std::size_t>(i)]), cap)) < 1e-3f;
        }
        passed &= expect(rim_round, "a pipe's rim bends by the ring's circle");
        passed &= expect(side_round, "a pipe's side bends by its own normal");
        passed &= expect(bottom_round, "an open edge of a smooth side keeps its normal");
        passed &= expect(cap_flat, "the flat cap's edges stay in its plane");
        passed &= expect(crack_free(s, p.positions, p.corners), "a pipe's shared edges bend alike on both sides");
        passed &= expect(!s.flat_anything(), "a pipe's round side and rim are no flat solid");
        // A 16-gon's edge of 39.0 rounds onto its circle: the sagitta 100 (1 - cos(11.25 deg)) = 1.92.
        const float rim = midpoint_move(s, p.positions, c(0, 0), c(1, 1));
        passed &= expect(std::fabs(rim - 1.92f) < 0.1f, "a 16-gon's edge rounds onto its circle");
    }
    {
        // A cube: every corner meets three faces (three crease edges): nothing moves.
        const Mesh m = box({1, 1, 1}, [](const Vec3&, int f) {
            static const Vec3 n[6] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
            return n[f];
        });
        const Surface s = analyse(m.positions, m.normals, m.corners);
        passed &= expect(largest_move(s, m.positions, m.corners) == 0.0f, "a cube's edges do not move");
        passed &= expect(!s.flat_anything(), "a cube shaded by its faces needs nothing shaded flat");
    }
    {
        // The same box shaded soft: one normal per corner, averaged over its three faces. Still flat.
        const Mesh m = box({1, 1, 1}, [](const Vec3& p, int) {
            return unit({p[0] > 0.5f ? 1.0f : -1.0f, p[1] > 0.5f ? 1.0f : -1.0f, p[2] > 0.5f ? 1.0f : -1.0f});
        });
        Mesh soft = m;
        // One normal per corner position, shared by its three faces.
        for (Corner& corner : soft.corners) corner.normal = corner.position;
        soft.normals.assign(m.positions.size(), Vec3{});
        for (std::size_t i = 0; i < m.positions.size(); ++i) {
            const Vec3& p = m.positions[i];
            soft.normals[i] = unit({p[0] > 0.5f ? 1.0f : -1.0f, p[1] > 0.5f ? 1.0f : -1.0f, p[2] > 0.5f ? 1.0f : -1.0f});
        }
        const Surface s = analyse(soft.positions, soft.normals, soft.corners);
        passed &= expect(largest_move(s, soft.positions, soft.corners) == 0.0f,
                         "a box with averaged corner normals does not puff into a pillow");
        passed &= expect(!s.bends_anything() &&
                             std::all_of(s.bends.begin(), s.bends.end(), [](const PlaceBend& b) { return b.flat; }),
                         "a box shaded soft is shaded flat by the ray tracer, every corner");
    }
    {
        // A flat plank face drawn alone, its corner normals splayed 31 degrees for the look (the airship bridge's
        // planks): shading, not shape. It stays flat.
        const std::vector<Vec3> positions = {{0, 0, -245}, {0, 0, 245}, {0, 115, -245}, {0, 115, 245}};
        const std::vector<Vec3> normals = {unit({0.855f, -0.367f, -0.367f}), unit({0.855f, -0.367f, 0.367f}),
                                           unit({0.855f, 0.367f, -0.367f}), unit({0.855f, 0.367f, 0.367f})};
        const std::vector<Corner> corners = {c(0, 0), c(1, 1), c(2, 2), c(1, 1), c(3, 3), c(2, 2)};
        const Surface s = analyse(positions, normals, corners);
        passed &= expect(largest_move(s, positions, corners) == 0.0f, "a flat face's splayed normals do not dome it");
        passed &= expect(s.flat_anything(), "a flat face's splayed normals are shaded flat");
    }
    {
        // The bridge's plank whole: a 50 x 115 x 490 box with 7-unit 45-degree bevels on its long edges, shaded soft
        // across them. A broad face beside a narrow bevel is not a curve: nothing domes.
        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<Corner> corners;
        // The cross-section (x, y), counter-clockwise: an octagon with 7-unit bevels at the corners.
        const float b = 5.0f;
        const float xs[8] = {25, 25, 25 - b, -25 + b, -25, -25, -25 + b, 25 - b};
        const float ys[8] = {b, 115 - b, 115, 115, 115 - b, b, 0, 0};
        for (int z = 0; z < 2; ++z) {
            for (int i = 0; i < 8; ++i) positions.push_back({xs[i], ys[i], z == 0 ? -245.0f : 245.0f});
        }
        // Each section vertex's normal: the mean of its two faces' (soft across the bevels).
        for (int i = 0; i < 8; ++i) {
            const int prev = (i + 7) % 8;
            const int next = (i + 1) % 8;
            const Vec3 e0 = sub(positions[static_cast<std::size_t>(i)], positions[static_cast<std::size_t>(prev)]);
            const Vec3 e1 = sub(positions[static_cast<std::size_t>(next)], positions[static_cast<std::size_t>(i)]);
            const Vec3 n0 = unit({e0[1], -e0[0], 0.0f});
            const Vec3 n1 = unit({e1[1], -e1[0], 0.0f});
            normals.push_back(unit({n0[0] + n1[0], n0[1] + n1[1], 0.0f}));
        }
        for (int i = 0; i < 8; ++i) {
            const int j = (i + 1) % 8;
            corners.insert(corners.end(), {c(i, i), c(8 + i, i), c(8 + j, j)});
            corners.insert(corners.end(), {c(i, i), c(8 + j, j), c(j, j)});
        }
        const Surface s = analyse(positions, normals, corners);
        passed &= expect(largest_move(s, positions, corners) < 0.5f, "a bevelled plank keeps its broad faces flat");
        passed &= expect(crack_free(s, positions, corners), "a bevelled plank's shared edges bend alike");
        passed &= expect(s.flat_anything(), "a bevelled plank shaded soft across its bevels is shaded flat");
    }
    {
        // An octagonal prism shaded round (the art's coarse rings and barrels): its side faces at 45 degrees round.
        constexpr int kSides = 8;
        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<Corner> corners;
        for (int ring = 0; ring < 2; ++ring) {
            for (int i = 0; i < kSides; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / kSides;
                positions.push_back({100.0f * std::cos(a), ring == 0 ? 100.0f : 0.0f, 100.0f * std::sin(a)});
            }
        }
        for (int i = 0; i < kSides; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / kSides;
            normals.push_back({std::cos(a), 0.0f, std::sin(a)});
        }
        for (int i = 0; i < kSides; ++i) {
            const int j = (i + 1) % kSides;
            corners.insert(corners.end(), {c(i, i), c(j, j), c(kSides + j, j)});
            corners.insert(corners.end(), {c(i, i), c(kSides + j, j), c(kSides + i, i)});
        }
        const Surface s = analyse(positions, normals, corners);
        bool round = true;
        for (int i = 0; i < kSides && round; ++i) {
            const auto u = static_cast<std::uint16_t>(i);
            const Vec3 n = edge_normal(s, u, u, static_cast<std::uint16_t>((i + 1) % kSides));
            round = std::fabs(dot(n, normals[static_cast<std::size_t>(i)]) - 1.0f) < 1e-4f;
        }
        passed &= expect(round, "an octagonal prism shaded round rounds");
        passed &= expect(!s.flat_anything(), "an octagonal prism shaded round is no flat solid");
        // Onto its circle within 11 %: the sagitta 100 (1 - cos(22.5 deg)) = 7.61.
        const float move = midpoint_move(s, positions, c(0, 0), c(1, 1));
        passed &= expect(move > 0.85f * 7.61f && move < 7.61f, "an octagon's edge rounds onto its circle");
    }
    {
        // A T-junction on a curved strip: a 16-gon's band split into two rings on one side only, its middle ring's
        // position lying inside the other side's long edge. Those places stay put; nothing parts.
        constexpr int kSides = 16;
        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<Corner> corners;
        for (int ring = 0; ring < 3; ++ring) {
            for (int i = 0; i < kSides; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / kSides;
                positions.push_back({100.0f * std::cos(a), 50.0f * static_cast<float>(ring), 100.0f * std::sin(a)});
            }
        }
        for (int i = 0; i < kSides; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / kSides;
            normals.push_back({std::cos(a), 0.0f, std::sin(a)});
        }
        // Quads 0-7 two rings high (through the middle ring); quads 8-15 one tall quad, except quad 8 whose left
        // edge (at position 8) runs from ring 0 to ring 2 past position 16 + 8 of its neighbour's middle ring.
        for (int i = 0; i < kSides; ++i) {
            const int j = (i + 1) % kSides;
            if (i < 8) {
                for (int r = 0; r < 2; ++r) {
                    const int lo = r * kSides;
                    const int hi = (r + 1) * kSides;
                    corners.insert(corners.end(), {c(lo + i, i), c(lo + j, j), c(hi + j, j)});
                    corners.insert(corners.end(), {c(lo + i, i), c(hi + j, j), c(hi + i, i)});
                }
            } else {
                corners.insert(corners.end(), {c(i, i), c(j, j), c(2 * kSides + j, j)});
                corners.insert(corners.end(), {c(i, i), c(2 * kSides + j, j), c(2 * kSides + i, i)});
            }
        }
        const Surface s = analyse(positions, normals, corners);
        // Position 16 + 8 lies inside edge (8, 32 + 8) of quad 8, and 16 + 0 inside (0, 32 + 0) of quad 15: each a
        // fan of one side only, which shows no curve, so they do not bend; the band between rounds, and nothing
        // parts.
        passed &= expect(crack_free(s, positions, corners), "a T-junction's shared edges bend alike");
        passed &= expect(is_zero(edge_normal(s, 16u + 8u, 8u, 16u + 7u)), "a one-sided fan at a T-junction stays put");
        const Vec3 n = edge_normal(s, 16u + 4u, 4u, 16u + 5u);
        passed &= expect(std::fabs(dot(n, normals[4]) - 1.0f) < 1e-4f, "the band beside a T-junction rounds");
    }
    {
        // A fold: one triangle drawn on both sides (opposite normals, shared positions) bends as a smooth surface.
        const std::vector<Vec3> positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        const std::vector<Vec3> normals = {{0, 0, 1}, {0, 0, -1}};
        const std::vector<Corner> corners = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {1, 1}};
        const Surface s = analyse(positions, normals, corners);
        passed &= expect(std::fabs(edge_normal(s, 0u, 0u, 1u)[2]) > 0.9999f &&
                             std::fabs(edge_normal(s, 0u, 1u, 1u)[2]) > 0.9999f,
                         "a fold's sides bend alike");
        passed &= expect(!s.flat_anything(), "a fold is no flat solid");
    }
    {
        // A spike's tip (a nose, a horn): five faces meet at the apex under one averaged normal. More planes than a
        // solid's corner holds: its shading stays smooth.
        std::vector<Vec3> positions = {{0, 100, 0}};
        for (int i = 0; i < 5; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 5.0f;
            positions.push_back({30.0f * std::cos(a), 0.0f, 30.0f * std::sin(a)});
        }
        std::vector<Vec3> normals = {{0, 1, 0}};
        for (int i = 0; i < 5; ++i) {
            const float a = 6.2831853f * (static_cast<float>(i) + 0.5f) / 5.0f;
            normals.push_back(unit({std::cos(a), 0.3f, std::sin(a)}));
        }
        std::vector<Corner> corners;
        for (int i = 0; i < 5; ++i) {
            const int j = i % 5 + 1;
            const int k = (i + 1) % 5 + 1;
            corners.insert(corners.end(), {c(0, 0), c(k, k), c(j, j)});
        }
        const Surface s = analyse(positions, normals, corners);
        bool apex_flat = false;
        for (std::uint32_t b = s.first[s.place[0]]; b < s.first[s.place[0] + 1u]; ++b) apex_flat |= s.bends[b].flat;
        passed &= expect(!apex_flat, "a spike's tip, five faces under one normal, stays smooth");
    }
    {
        // The same pipe with its rim repeated under new indices for the cap: welded by value, the same bends.
        Mesh p = pipe();
        const std::size_t first = p.positions.size();
        for (int i = 0; i < 16; ++i) p.positions.push_back(p.positions[static_cast<std::size_t>(i)]);
        for (std::size_t t = 0; t < p.corners.size(); t += 3u) {
            if (p.corners[t].normal == 16u) {
                for (unsigned k = 1u; k < 3u; ++k) {
                    p.corners[t + k].position = static_cast<std::uint16_t>(first + p.corners[t + k].position);
                }
            }
        }
        const Surface s = analyse(p.positions, p.normals, p.corners);
        bool same = true;
        for (int i = 0; i < 16; ++i) {
            const auto u = static_cast<std::uint16_t>(i);
            const auto w = static_cast<std::uint16_t>(first + static_cast<std::size_t>(i));
            same &= s.place[u] == s.place[w];
            same &= dot(edge_normal(s, u, 16u, static_cast<std::uint16_t>((i + 1) % 16)),
                        edge_normal(s, w, 16u, static_cast<std::uint16_t>(first + static_cast<std::size_t>((i + 1) % 16)))) > 0.9999f;
        }
        passed &= expect(same, "positions repeated under another index bend alike");
        passed &= expect(crack_free(s, p.positions, p.corners), "welded positions' shared edges bend alike");
    }
    {
        const std::vector<Vec3> positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        const std::vector<Vec3> normals = {{0, 0, 1}};
        const std::vector<Corner> corners = {{0, 0}, {1, 0}, {7, 0}};
        const Surface s = analyse(positions, normals, corners);
        passed &= expect(s.place.size() == 3u && !s.bends_anything(), "a triangle with an index out of range is left out");
    }
    {
        // Packed normals: unit vectors within 1e-4, the zero vector as zero, nothing else packing to zero.
        bool exact = pack_normal({0.0f, 0.0f, 0.0f}) == 0u && is_zero(unpack_normal(0u));
        for (int i = 0; i < 200 && exact; ++i) {
            const float a = 0.37f * static_cast<float>(i);
            const float b = 0.11f * static_cast<float>(i) - 1.0f;
            const Vec3 n = unit({std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b)});
            const std::uint32_t packed = pack_normal(n);
            const Vec3 back = unpack_normal(packed);
            exact = packed != 0u && dot(back, n) > 1.0f - 1e-6f;
        }
        passed &= expect(exact, "normals pack to 32 bits and back");
    }
    {
        clear_models();
        const Mesh p = pipe();
        const Surface round = analyse(p.positions, p.normals, p.corners);
        register_model(0x1000u, round);
        const auto model = find_model(0x1000u);
        passed &= expect(model != nullptr && model->places.size() == p.positions.size() &&
                             model->bend_of(0u, 0u) == pack_normal(edge_normal(round, 0u, 0u, 16u)) &&
                             (model->places[0] & 0xFFFFu) == round.place[0],
                         "a registered model carries each position's place and each corner's bend");
        const Mesh cube = box({1, 1, 1}, [](const Vec3&, int f) {
            static const Vec3 n[6] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
            return n[f];
        });
        register_model(0x1000u, analyse(cube.positions, cube.normals, cube.corners));
        passed &= expect(find_model(0x1000u) == nullptr,
                         "a model in which nothing bends is not kept, and replaces the key's earlier one");

        // Analysed on the background thread: found once it is done, the same model.
        register_model_later(0x2000u, p.positions, p.normals, p.corners);
        const auto later = find_model(0x2000u);
        passed &= expect(later != nullptr && model != nullptr && later->places == model->places &&
                             later->second == model->second && later->crease == model->crease &&
                             later->first == model->first && later->bend_normal == model->bend_normal &&
                             later->bend == model->bend && later->bend_flat == model->bend_flat,
                         "a model analysed in the background is the one analysed at once");
        // A later registration of the key wins over one still being analysed.
        for (int round_trip = 0; round_trip < 20; ++round_trip) {
            register_model_later(0x3000u, p.positions, p.normals, p.corners);
            register_model_later(0x3000u, cube.positions, cube.normals, cube.corners);
            passed &= expect(find_model(0x3000u) == nullptr, "the key's latest registration decides");
            register_model_later(0x3000u, cube.positions, cube.normals, cube.corners);
            register_model_later(0x3000u, p.positions, p.normals, p.corners);
            const auto found = find_model(0x3000u);
            passed &= expect(found != nullptr && found->places == model->places,
                             "the key's latest registration decides, whichever finishes first");
        }
    }

    if (!passed) {
        return 1;
    }
    std::cout << "curved_surfaces tests passed\n";
    return 0;
}
