// SPDX-License-Identifier: GPL-3.0-only
// Curved surfaces: the shape analysis behind Phong tessellation and the registry of models (curved_surfaces.h).
#include "galaxy/gx/curved_surfaces.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

namespace galaxy::gx::curved {
namespace {

// The analysis works in double precision: a 60 000-unit planet's chords against a 7-unit bevel.
using D3 = std::array<double, 3>;

constexpr double kPi = 3.14159265358979323846;
double radians(double degrees) { return degrees * kPi / 180.0; }

D3 widen(const Vec3& v) { return {v[0], v[1], v[2]}; }
double dot(const D3& a, const D3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
D3 sub(const D3& a, const D3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
D3 add(const D3& a, const D3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
D3 mul(const D3& a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
D3 cross(const D3& a, const D3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double length(const D3& a) { return std::sqrt(dot(a, a)); }
D3 normalized(const D3& v) {
    const double l = length(v);
    return l > 1e-20 ? mul(v, 1.0 / l) : D3{0.0, 0.0, 0.0};
}
bool is_zero(const D3& v) { return v[0] == 0.0 && v[1] == 0.0 && v[2] == 0.0; }
double angle_between(const D3& a, const D3& b) { return std::acos(std::clamp(dot(a, b), -1.0, 1.0)); }

// The circumcentre of the triangle (q, q + ea, q + eb), from q (zero for a degenerate triangle).
D3 circumcentre(const D3& ea, const D3& eb) {
    const D3 w = cross(ea, eb);
    const double ww = dot(w, w);
    if (!(ww > 0.0)) return {0.0, 0.0, 0.0};
    return mul(add(mul(cross(w, ea), dot(eb, eb)), mul(cross(eb, w), dot(ea, ea))), 0.5 / ww);
}

// Normals within 2 degrees of each other, either sign (Phong's projection does not see the sign).
const double kParallel = std::cos(radians(2.0));
bool parallel(const D3& a, const D3& b) { return std::fabs(dot(a, b)) >= kParallel; }

// A group's faces: within 35 degrees of its normal and 62 of each other (a hexagon's faces meet at 60).
const double kNormalToFace = std::cos(radians(35.0));
const double kFaceToFace = std::cos(radians(62.0));
// Opposite sides: tangent directions within 15 degrees of opposite.
const double kOpposite = -std::cos(radians(15.0));
// A shading normal within 2 degrees of its faces' cone (the arrays' quantisation).
const double kCone = radians(2.0);
// The fitted curvature places each neighbour within this share of its edge's length.
constexpr double kSlope = 0.05;
// An edge within 2 degrees of the tangent plane is flat.
const double kFlat = std::sin(radians(2.0));
// Curvatures, a crease's two segments: alike within this factor.
constexpr double kCurveRatio = 2.5;
// An edge longer than this many times its triangle's smallest height runs along a sliver.
constexpr double kSliver = 4.0;
// The shape operator's least-squares residual against the faces' tilts, at most.
constexpr double kFit = 0.35;
// A crease turns between 0.5 and 62 degrees at a position.
const double kStraight = std::cos(radians(0.5));
const double kTurn = std::cos(radians(62.0));
// A curved group backs a crease: its normal in the crease's plane at least half of it and within 10 degrees of the
// crease's normal.
const double kAgree = std::cos(radians(10.0));
constexpr double kInPlane = 0.5;
// A T-junction's line bends under a normal more than this far off its perpendicular.
constexpr double kBow = 1e-5;

// A position's key, -0 taken as 0 (they compare equal).
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
struct VecEqual {
    bool operator()(const Vec3& a, const Vec3& b) const noexcept {
        return a[0] + 0.0f == b[0] + 0.0f && a[1] + 0.0f == b[1] + 0.0f && a[2] + 0.0f == b[2] + 0.0f;
    }
};

std::uint64_t pair_key(std::uint32_t a, std::uint32_t b) { return (static_cast<std::uint64_t>(a) << 32u) | b; }

// A triangle's corner at a place.
struct At {
    D3 normal;                     // its shading normal, unit
    std::uint16_t normal_index;
    D3 face;                       // the triangle's own normal, unit
    double angle;                  // the triangle's angle there
    std::uint32_t other[2];        // the triangle's other two places
};

// Union-find over small index ranges.
struct Sets {
    std::vector<std::uint32_t> parent;
    explicit Sets(std::size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
    std::uint32_t root(std::uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void join(std::uint32_t a, std::uint32_t b) { parent[root(a)] = root(b); }
};

// Solves the 3x3 symmetric positive semi-definite system m x = r (Gauss-Jordan with partial pivoting); an unknown
// the system does not determine (its pivot negligible against the matrix) is taken as zero.
std::array<double, 3> solve3(std::array<std::array<double, 3>, 3> m, std::array<double, 3> r) {
    const double scale = std::max(m[0][0] + m[1][1] + m[2][2], 1e-300);
    for (int c = 0; c < 3; ++c) {
        int pivot = c;
        for (int k = c + 1; k < 3; ++k) {
            if (std::fabs(m[k][c]) > std::fabs(m[pivot][c])) pivot = k;
        }
        if (std::fabs(m[pivot][c]) < 1e-12 * scale) {
            // A semi-definite system: the rest of this row and column vanish with the pivot.
            m[c] = {0.0, 0.0, 0.0};
            m[c][c] = 1.0;
            r[c] = 0.0;
            pivot = c;
        }
        std::swap(m[c], m[pivot]);
        std::swap(r[c], r[pivot]);
        for (int k = 0; k < 3; ++k) {
            if (k == c || m[k][c] == 0.0) continue;
            const double f = m[k][c] / m[c][c];
            for (int j = 0; j < 3; ++j) m[k][j] -= f * m[c][j];
            r[k] -= f * r[c];
        }
    }
    return {r[0] / m[0][0], r[1] / m[1][1], r[2] / m[2][2]};
}

// The angle from unit n to the convex cone the unit vectors `generators` span (zero inside it): the nearest of the
// generators themselves, of the faces their pairs span, and inside where a triple holds n as a positive sum.
double cone_angle(const D3& n, std::span<const D3> generators) {
    double best = kPi;
    for (const D3& g : generators) best = std::min(best, angle_between(n, g));
    for (std::size_t i = 0; i < generators.size(); ++i) {
        for (std::size_t j = i + 1; j < generators.size(); ++j) {
            const D3& a = generators[i];
            const D3& b = generators[j];
            const double ab = dot(a, b);
            const double det = 1.0 - ab * ab;
            if (det >= 1e-12) {
                const double na = dot(n, a);
                const double nb = dot(n, b);
                const double x = (na - ab * nb) / det;
                const double y = (nb - ab * na) / det;
                if (x >= 0.0 && y >= 0.0) {
                    const D3 projection = add(mul(a, x), mul(b, y));
                    const double l = length(projection);
                    if (l > 0.0 && dot(n, projection) > 0.0) best = std::min(best, angle_between(n, mul(projection, 1.0 / l)));
                }
            }
            for (std::size_t k = j + 1; k < generators.size(); ++k) {
                const D3& c = generators[k];
                const double det3 = dot(a, cross(b, c));
                if (std::fabs(det3) < 1e-12) continue;
                if (dot(n, cross(b, c)) / det3 >= 0.0 && dot(n, cross(c, a)) / det3 >= 0.0 &&
                    dot(n, cross(a, b)) / det3 >= 0.0) {
                    return 0.0;
                }
            }
        }
    }
    return best;
}

// Whether a group of corners at place q, with normal n0, is a sampled surface (curved_surfaces.h's tests, in order).
bool sampled(std::uint32_t q, const D3& n0, std::span<const At* const> group, const std::vector<D3>& places) {
    // Its faces within 35 degrees of the normal and 62 of each other.
    for (std::size_t i = 0; i < group.size(); ++i) {
        if (std::fabs(dot(group[i]->face, n0)) < kNormalToFace) return false;
        for (std::size_t j = i + 1; j < group.size(); ++j) {
            if (std::fabs(dot(group[i]->face, group[j]->face)) < kFaceToFace) return false;
        }
    }
    // The normal within the convex cone of its faces' normals: any averaged vertex normal is a positive sum of them
    // (a box's corner normal on one face of the box, a plank's end splayed along it, are not).
    std::vector<D3> oriented;
    oriented.reserve(group.size());
    for (const At* at : group) oriented.push_back(dot(at->face, n0) >= 0.0 ? at->face : mul(at->face, -1.0));
    if (oriented.empty() || cone_angle(n0, oriented) > kCone) return false;

    // Each neighbour's normal curvature 2 h / |d|^2 (h its height over the tangent plane) along its tangent direction.
    const D3& pq = places[q];
    const D3 u = normalized(cross(n0, std::fabs(n0[0]) < 0.9 ? D3{1.0, 0.0, 0.0} : D3{0.0, 1.0, 0.0}));
    const D3 v = cross(n0, u);
    std::vector<std::uint32_t> neighbours;
    for (const At* at : group) neighbours.insert(neighbours.end(), {at->other[0], at->other[1]});
    std::sort(neighbours.begin(), neighbours.end());
    neighbours.erase(std::unique(neighbours.begin(), neighbours.end()), neighbours.end());
    struct Side {
        D3 tangent;
        double curvature;
        double edge;
        bool flat;
    };
    std::vector<Side> sides;
    for (const std::uint32_t j : neighbours) {
        const D3 d = sub(places[j], pq);
        const double dd = dot(d, d);
        const double h = dot(d, n0);
        const D3 t = sub(d, mul(n0, h));
        const double tl = length(t);
        if (tl < 1e-6 * std::sqrt(dd)) continue;
        sides.push_back({mul(t, 1.0 / tl), 2.0 * h / dd, std::sqrt(dd), std::fabs(h) <= kFlat * std::sqrt(dd)});
    }
    // The same curvature on opposite sides: against the neighbour most nearly opposite (within 15 degrees).
    for (std::size_t i = 0; i < sides.size(); ++i) {
        std::size_t best = sides.size();
        for (std::size_t j = 0; j < sides.size(); ++j) {
            const double c = dot(sides[i].tangent, sides[j].tangent);
            if (j != i && c <= kOpposite && (best == sides.size() || c < dot(sides[i].tangent, sides[best].tangent))) {
                best = j;
            }
        }
        if (best == sides.size()) continue;
        const Side& a = sides[i];
        const Side& b = sides[best];
        if (a.flat && b.flat) continue;
        if (a.flat || b.flat || a.curvature * b.curvature <= 0.0) return false;
        if (std::max(std::fabs(a.curvature), std::fabs(b.curvature)) >
            kCurveRatio * std::min(std::fabs(a.curvature), std::fabs(b.curvature))) {
            return false;
        }
    }
    // Euler: one quadratic form of the tangent direction, a x^2 + 2 b x y + c y^2, gives every neighbour's normal
    // curvature, fitted by least squares; the fit places each neighbour within 5 % of its edge's length of where it is
    // (a bevel beside a broad face is not a curve, whichever side is short; a cylinder's around, along and diagonal
    // edges are).
    if (sides.size() > 3u) {
        std::array<std::array<double, 3>, 3> m{};
        std::array<double, 3> rhs{};
        std::vector<std::array<double, 3>> forms;
        forms.reserve(sides.size());
        for (const Side& side : sides) {
            const double x = dot(side.tangent, u);
            const double y = dot(side.tangent, v);
            const std::array<double, 3> f{x * x, 2.0 * x * y, y * y};
            forms.push_back(f);
            for (int i = 0; i < 3; ++i) {
                rhs[i] += f[i] * side.curvature;
                for (int j = 0; j < 3; ++j) m[i][j] += f[i] * f[j];
            }
        }
        const std::array<double, 3> e = solve3(m, rhs);
        for (std::size_t i = 0; i < sides.size(); ++i) {
            const double fitted = e[0] * forms[i][0] + e[1] * forms[i][1] + e[2] * forms[i][2];
            if (std::fabs(sides[i].curvature - fitted) * 0.5 * sides[i].edge > kSlope) return false;
        }
    }

    // One shape operator S explains every face: a face's normal is the surface's at its circumcentre, so its tilt
    // from n0 (tangent part) is S applied to the circumcentre's offset (tangent part).
    struct Row {
        double eu, ev, tu, tv;
    };
    std::vector<Row> rows;
    for (std::size_t i = 0; i < group.size(); ++i) {
        const D3 centre = circumcentre(sub(places[group[i]->other[0]], pq), sub(places[group[i]->other[1]], pq));
        if (is_zero(centre)) continue;
        rows.push_back({dot(centre, u), dot(centre, v), dot(oriented[i], u), dot(oriented[i], v)});
    }
    double tilt2 = 0.0;
    for (const Row& r : rows) tilt2 += r.tu * r.tu + r.tv * r.tv;
    if (tilt2 > kFlat * kFlat * static_cast<double>(rows.size())) {
        // S = [[a, b], [b, c]]: t_u = a e_u + b e_v, t_v = b e_u + c e_v, least squares.
        std::array<std::array<double, 3>, 3> m{};
        std::array<double, 3> rhs{};
        for (const Row& r : rows) {
            const std::array<std::array<double, 3>, 2> coeffs{{{r.eu, r.ev, 0.0}, {0.0, r.eu, r.ev}}};
            const double t[2] = {r.tu, r.tv};
            for (int e = 0; e < 2; ++e) {
                for (int i = 0; i < 3; ++i) {
                    rhs[i] += coeffs[e][i] * t[e];
                    for (int j = 0; j < 3; ++j) m[i][j] += coeffs[e][i] * coeffs[e][j];
                }
            }
        }
        const std::array<double, 3> s = solve3(m, rhs);
        double residual = 0.0;
        for (const Row& r : rows) {
            const double du = r.tu - s[0] * r.eu - s[1] * r.ev;
            const double dv = r.tv - s[1] * r.eu - s[2] * r.ev;
            residual += du * du + dv * dv;
        }
        if (residual > kFit * kFit * tilt2) return false;
    }

    // A sliver shows curvature only across itself: along its length the normal may bend no more than the
    // position's short edges show it bending.
    std::unordered_set<std::uint32_t> lengthwise;
    std::unordered_set<std::uint32_t> across;
    for (const At* at : group) {
        const D3& pa = places[at->other[0]];
        const D3& pb = places[at->other[1]];
        const double e[3] = {length(sub(pa, pq)), length(sub(pb, pq)), length(sub(pa, pb))};
        const double height = length(cross(sub(pa, pq), sub(pb, pq))) / std::max({e[0], e[1], e[2]});
        for (int k = 0; k < 2; ++k) {
            (e[k] > kSliver * height ? lengthwise : across).insert(at->other[k]);
        }
    }
    const auto curvature = [&](std::uint32_t j) {
        const D3 d = sub(places[j], pq);
        const double dd = dot(d, d);
        const double h = dot(d, n0);
        return std::fabs(h) <= kFlat * std::sqrt(dd) ? 0.0 : std::fabs(2.0 * h / dd);
    };
    double bound = 0.0;
    for (const std::uint32_t j : across) {
        if (!lengthwise.contains(j)) bound = std::max(bound, curvature(j));
    }
    for (const std::uint32_t j : lengthwise) {
        if (curvature(j) > bound) return false;
    }
    return true;
}

// T-junctions: (inner place, edge end, edge end) where a place lies inside another edge. Places are entered in a
// grid of the median edge's size; each edge looks in the cells along it (samples half a cell apart and the cells
// around each, which hold every point of the edge).
void find_junctions(const std::vector<D3>& places, const std::vector<std::uint64_t>& edges,
                    std::vector<std::array<std::uint32_t, 3>>& out) {
    if (edges.empty()) return;
    std::vector<double> lengths;
    lengths.reserve(edges.size());
    for (const std::uint64_t e : edges) {
        lengths.push_back(length(sub(places[e >> 32u], places[e & 0xFFFFFFFFu])));
    }
    std::nth_element(lengths.begin(), lengths.begin() + static_cast<std::ptrdiff_t>(lengths.size() / 2), lengths.end());
    const double cell = std::max(lengths[lengths.size() / 2], 1e-3);
    const auto cell_of = [cell](const D3& p) {
        return std::array<std::int64_t, 3>{static_cast<std::int64_t>(std::floor(p[0] / cell)),
                                            static_cast<std::int64_t>(std::floor(p[1] / cell)),
                                            static_cast<std::int64_t>(std::floor(p[2] / cell))};
    };
    const auto key_of = [](const std::array<std::int64_t, 3>& c) {
        // 21 bits a coordinate (a model spans far fewer cells).
        return (static_cast<std::uint64_t>(c[0] & 0x1FFFFF) << 42u) | (static_cast<std::uint64_t>(c[1] & 0x1FFFFF) << 21u) |
               static_cast<std::uint64_t>(c[2] & 0x1FFFFF);
    };
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> grid;
    for (std::uint32_t i = 0; i < places.size(); ++i) grid[key_of(cell_of(places[i]))].push_back(i);
    std::unordered_set<std::uint64_t> cells;
    for (const std::uint64_t e : edges) {
        const auto a = static_cast<std::uint32_t>(e >> 32u);
        const auto b = static_cast<std::uint32_t>(e & 0xFFFFFFFFu);
        const D3& pa = places[a];
        const D3 d = sub(places[b], pa);
        const double dd = dot(d, d);
        if (!(dd > 0.0)) continue;
        const auto steps = static_cast<std::size_t>(std::sqrt(dd) / (0.5 * cell)) + 1u;
        cells.clear();
        for (std::size_t s = 0; s <= steps; ++s) {
            const auto c = cell_of(add(pa, mul(d, static_cast<double>(s) / static_cast<double>(steps))));
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                for (std::int64_t dy = -1; dy <= 1; ++dy) {
                    for (std::int64_t dz = -1; dz <= 1; ++dz) cells.insert(key_of({c[0] + dx, c[1] + dy, c[2] + dz}));
                }
            }
        }
        for (const std::uint64_t key : cells) {
            const auto it = grid.find(key);
            if (it == grid.end()) continue;
            for (const std::uint32_t p : it->second) {
                if (p == a || p == b) continue;
                const D3 w = sub(places[p], pa);
                const double t = dot(w, d) / dd;
                if (t <= 1e-4 || t >= 1.0 - 1e-4) continue;
                const D3 off = sub(w, mul(d, t));
                if (dot(off, off) <= 1e-8 * dd) out.push_back({p, a, b});
            }
        }
    }
}

Vec3 narrow(const D3& v) {
    return {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
}

struct Registry {
    std::shared_mutex lock;
    std::unordered_map<std::uint32_t, std::shared_ptr<const Model>> models;
};

Registry& registry() {
    static Registry r;
    return r;
}

}  // namespace

bool Surface::bends_anything() const noexcept {
    const auto bends_at = [](const Vec3& v) { return v[0] != 0.0f || v[1] != 0.0f || v[2] != 0.0f; };
    return std::any_of(crease.begin(), crease.end(), bends_at) ||
           std::any_of(bends.begin(), bends.end(), [&](const PlaceBend& b) { return bends_at(b.bend); });
}

Surface analyse(std::span<const Vec3> positions, std::span<const Vec3> normals, std::span<const Corner> corners) {
    Surface out;
    // Places: positions by value.
    std::unordered_map<Vec3, std::uint32_t, VecHash, VecEqual> place_of;
    std::vector<std::uint32_t> place(positions.size());
    std::vector<D3> places;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto [it, added] = place_of.try_emplace(positions[i], static_cast<std::uint32_t>(places.size()));
        if (added) places.push_back(widen(positions[i]));
        place[i] = it->second;
    }
    const auto place_count = static_cast<std::uint32_t>(places.size());
    out.place.resize(positions.size());
    out.crease.assign(places.size(), Vec3{0.0f, 0.0f, 0.0f});
    out.crease_places.assign(places.size(), {kNoPlace, kNoPlace});
    out.first.assign(places.size() + 1u, 0u);
    if (place_count >= kNoPlace) {
        // Places beyond 16 bits: nothing bends (the vertices carry places in 16 bits).
        std::fill(out.place.begin(), out.place.end(), kNoPlace);
        return out;
    }
    for (std::size_t i = 0; i < positions.size(); ++i) out.place[i] = static_cast<std::uint16_t>(place[i]);

    // Corners at each place; edges; each directed edge's corner normals (crease detection, non-degenerate triangles)
    // and normal indices (every triangle); zero-area triangles' collinear corners as junctions.
    std::vector<std::vector<At>> at(places.size());
    std::unordered_map<std::uint64_t, std::vector<D3>> edge_ends;
    std::unordered_map<std::uint64_t, std::vector<std::uint16_t>> edge_index;
    std::unordered_set<std::uint64_t> edge_set;
    std::vector<std::array<std::uint32_t, 3>> junctions;
    for (std::size_t t = 0; t + 2u < corners.size(); t += 3u) {
        std::uint32_t p[3];
        D3 n[3];
        bool valid = true;
        for (unsigned k = 0; k < 3u; ++k) {
            const Corner& c = corners[t + k];
            if (c.position >= positions.size() || c.normal >= normals.size()) {
                valid = false;
                break;
            }
            p[k] = place[c.position];
            n[k] = normalized(widen(normals[c.normal]));
        }
        if (!valid || p[0] == p[1] || p[1] == p[2] || p[0] == p[2]) continue;
        for (unsigned k = 0; k < 3u; ++k) {
            const std::uint32_t a = p[k];
            const std::uint32_t b = p[(k + 1u) % 3u];
            edge_set.insert(pair_key(std::min(a, b), std::max(a, b)));
            for (const unsigned j : {(k + 1u) % 3u, (k + 2u) % 3u}) {
                std::vector<std::uint16_t>& list = edge_index[pair_key(p[k], p[j])];
                if (std::find(list.begin(), list.end(), corners[t + k].normal) == list.end()) {
                    list.push_back(corners[t + k].normal);
                }
            }
        }
        const D3 face = normalized(cross(sub(places[p[1]], places[p[0]]), sub(places[p[2]], places[p[0]])));
        if (is_zero(face)) {
            // Collinear corners: the middle one lies inside the edge of the other two.
            for (unsigned k = 0; k < 3u; ++k) {
                const std::uint32_t a = p[(k + 1u) % 3u];
                const std::uint32_t b = p[(k + 2u) % 3u];
                const D3 d = sub(places[b], places[a]);
                const double s = dot(sub(places[p[k]], places[a]), d) / std::max(dot(d, d), 1e-300);
                if (s > 0.0 && s < 1.0) junctions.push_back({p[k], a, b});
            }
            continue;
        }
        for (unsigned k = 0; k < 3u; ++k) {
            const std::uint32_t o0 = p[(k + 1u) % 3u];
            const std::uint32_t o1 = p[(k + 2u) % 3u];
            const double angle = angle_between(normalized(sub(places[o0], places[p[k]])),
                                               normalized(sub(places[o1], places[p[k]])));
            at[p[k]].push_back({n[k], corners[t + k].normal, face, angle, {o0, o1}});
            edge_ends[pair_key(p[k], o0)].push_back(n[k]);
            edge_ends[pair_key(p[k], o1)].push_back(n[k]);
        }
    }
    // Crease edges: their triangles disagree on the normal at the near end, or one triangle holds them.
    std::unordered_set<std::uint64_t> crease_edges;
    std::vector<std::vector<std::uint32_t>> creases(places.size());
    for (const auto& [key, ends] : edge_ends) {
        const bool crease =
            ends.size() == 1u || !std::all_of(ends.begin(), ends.end(), [&](const D3& v) { return parallel(ends[0], v); });
        if (crease) {
            crease_edges.insert(key);
            creases[key >> 32u].push_back(static_cast<std::uint32_t>(key & 0xFFFFFFFFu));
        }
    }
    for (std::vector<std::uint32_t>& c : creases) std::sort(c.begin(), c.end());

    // Each place's groups, their bends and the crease's.
    std::vector<std::vector<std::pair<std::uint16_t, D3>>> bends(places.size());
    std::vector<D3> crease_normal(places.size(), D3{0.0, 0.0, 0.0});
    for (std::uint32_t q = 0; q < place_count; ++q) {
        const std::vector<At>& cs = at[q];
        if (cs.empty()) continue;
        // Groups: corners joined through their triangles' shared non-crease edges at q.
        Sets groups(cs.size());
        std::unordered_map<std::uint32_t, std::uint32_t> through;
        for (std::uint32_t i = 0; i < cs.size(); ++i) {
            for (const std::uint32_t j : cs[i].other) {
                if (crease_edges.contains(pair_key(q, j))) continue;
                const auto [it, added] = through.try_emplace(j, i);
                if (!added) groups.join(i, it->second);
            }
        }
        std::unordered_map<std::uint32_t, std::vector<const At*>> members;
        for (std::uint32_t i = 0; i < cs.size(); ++i) members[groups.root(i)].push_back(&cs[i]);
        const bool multi = members.size() > 1u;
        std::vector<std::pair<std::uint16_t, D3>>& here = bends[q];
        if (multi && creases[q].size() != 2u) {
            // A corner: nothing bends.
            for (const At& c : cs) here.push_back({c.normal_index, D3{0.0, 0.0, 0.0}});
            continue;
        }
        D3 crease_plane{0.0, 0.0, 0.0};
        if (multi) {
            out.crease_places[q] = {static_cast<std::uint16_t>(creases[q][0]), static_cast<std::uint16_t>(creases[q][1])};
            const D3& a = places[creases[q][0]];
            const D3& b = places[creases[q][1]];
            const D3 u = sub(places[q], a);
            const D3 v = sub(b, places[q]);
            const double lu = length(u);
            const double lv = length(v);
            const double turn = lu > 0.0 && lv > 0.0 ? dot(u, v) / (lu * lv) : 1.0;
            if (turn <= kStraight && turn >= kTurn && std::max(lu, lv) <= kCurveRatio * std::min(lu, lv)) {
                const D3 ea = sub(a, places[q]);
                const D3 eb = sub(b, places[q]);
                crease_normal[q] = normalized(circumcentre(ea, eb));
                crease_plane = normalized(cross(ea, eb));
            }
        }
        // Normal indices one set per group, sets joined where groups share an index: one bend a set, the groups'
        // own where they agree, else none.
        std::unordered_map<std::uint16_t, std::uint32_t> index_slot;
        std::vector<std::uint16_t> slot_index;
        for (const At& c : cs) {
            if (index_slot.try_emplace(c.normal_index, static_cast<std::uint32_t>(slot_index.size())).second) {
                slot_index.push_back(c.normal_index);
            }
        }
        Sets index_sets(slot_index.size());
        for (const auto& [root, group] : members) {
            for (const At* c : group) index_sets.join(index_slot[c->normal_index], index_slot[group[0]->normal_index]);
        }
        std::vector<std::vector<D3>> set_bends(slot_index.size());
        bool backed = false;
        for (const auto& [root, group] : members) {
            const D3& n0 = group[0]->normal;
            const bool curved = sampled(q, n0, group, places);
            set_bends[index_sets.root(index_slot[group[0]->normal_index])].push_back(curved ? n0 : D3{0.0, 0.0, 0.0});
            if (curved && !is_zero(crease_normal[q])) {
                const D3 in_plane = sub(n0, mul(crease_plane, dot(n0, crease_plane)));
                const double l = length(in_plane);
                backed = backed || (l >= kInPlane && std::fabs(dot(in_plane, crease_normal[q])) >= kAgree * l);
            }
        }
        if (!backed) crease_normal[q] = {0.0, 0.0, 0.0};
        for (std::uint32_t s = 0; s < slot_index.size(); ++s) {
            const std::vector<D3>& values = set_bends[index_sets.root(s)];
            const bool agree = !values.empty() && std::all_of(values.begin(), values.end(), [&](const D3& v) {
                return v[0] == values[0][0] && v[1] == values[0][1] && v[2] == values[0][2];
            });
            here.push_back({slot_index[s], agree ? values[0] : D3{0.0, 0.0, 0.0}});
        }
    }

    // T-junctions (and zero-area triangles' corners): where any edge along the line would bend, the three places
    // stay put.
    {
        std::vector<std::uint64_t> edges(edge_set.begin(), edge_set.end());
        find_junctions(places, edges, junctions);
    }
    const auto bend_at = [&](std::uint32_t x, std::uint16_t normal_index, std::uint32_t y) -> D3 {
        const std::array<std::uint16_t, 2>& c = out.crease_places[x];
        if (c[0] == y || c[1] == y) return crease_normal[x];
        for (const auto& [index, bend] : bends[x]) {
            if (index == normal_index) return bend;
        }
        return {0.0, 0.0, 0.0};
    };
    std::vector<bool> fixed(places.size(), false);
    for (const std::array<std::uint32_t, 3>& j : junctions) {
        const D3 line = normalized(sub(places[j[2]], places[j[1]]));
        bool bows = false;
        for (const std::uint32_t x : j) {
            for (const std::uint32_t y : j) {
                if (x == y) continue;
                const auto it = edge_index.find(pair_key(x, y));
                if (it == edge_index.end()) continue;
                for (const std::uint16_t ni : it->second) bows = bows || std::fabs(dot(bend_at(x, ni, y), line)) > kBow;
            }
        }
        if (bows) {
            for (const std::uint32_t x : j) fixed[x] = true;
        }
    }

    for (std::uint32_t q = 0; q < place_count; ++q) {
        out.first[q] = static_cast<std::uint32_t>(out.bends.size());
        std::vector<std::pair<std::uint16_t, D3>>& here = bends[q];
        std::sort(here.begin(), here.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [index, bend] : here) {
            out.bends.push_back({index, fixed[q] ? Vec3{0.0f, 0.0f, 0.0f} : narrow(bend)});
        }
        out.crease[q] = fixed[q] ? Vec3{0.0f, 0.0f, 0.0f} : narrow(crease_normal[q]);
    }
    out.first[place_count] = static_cast<std::uint32_t>(out.bends.size());
    return out;
}

Vec3 edge_normal(const Surface& surface, std::uint16_t position, std::uint16_t normal, std::uint16_t other) noexcept {
    if (position >= surface.place.size() || other >= surface.place.size()) return {0.0f, 0.0f, 0.0f};
    const std::uint16_t q = surface.place[position];
    const std::uint16_t k = surface.place[other];
    if (q == kNoPlace) return {0.0f, 0.0f, 0.0f};
    const std::array<std::uint16_t, 2>& c = surface.crease_places[q];
    if (k != kNoPlace && (c[0] == k || c[1] == k)) return surface.crease[q];
    for (std::uint32_t i = surface.first[q]; i < surface.first[q + 1u]; ++i) {
        if (surface.bends[i].normal == normal) return surface.bends[i].bend;
    }
    return {0.0f, 0.0f, 0.0f};
}

std::uint32_t pack_normal(const Vec3& n) noexcept {
    const float sum = std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]);
    if (!(sum > 0.0f)) return 0u;
    float x = n[0] / sum;
    float y = n[1] / sum;
    if (n[2] < 0.0f) {
        const float fx = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float fy = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = fx;
        y = fy;
    }
    const auto half = [](float v) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f) + 32768);
    };
    return (half(x) << 16u) | half(y);
}

Vec3 unpack_normal(std::uint32_t packed) noexcept {
    if (packed == 0u) return {0.0f, 0.0f, 0.0f};
    const float x = (static_cast<float>(packed >> 16u) - 32768.0f) / 32767.0f;
    const float y = (static_cast<float>(packed & 0xFFFFu) - 32768.0f) / 32767.0f;
    float n[3] = {x, y, 1.0f - std::fabs(x) - std::fabs(y)};
    if (n[2] < 0.0f) {
        n[0] = (1.0f - std::fabs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        n[1] = (1.0f - std::fabs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    return {n[0] / l, n[1] / l, n[2] / l};
}

std::uint32_t Model::bend_of(std::uint32_t position, std::uint32_t normal) const noexcept {
    if (position + 1u >= first.size()) return 0u;
    for (std::uint32_t i = first[position]; i < first[position + 1u]; ++i) {
        if (bend_normal[i] == normal) return bend[i];
    }
    return 0u;
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

void register_model(std::uint32_t array_address, std::span<const std::byte> array_bytes, const Surface& surface) {
    if (!surface.bends_anything()) {
        Registry& r = registry();
        std::unique_lock guard(r.lock);
        r.models.erase(array_address);
        return;
    }
    auto model = std::make_shared<Model>();
    model->fingerprint = fingerprint(array_bytes);
    model->bytes = static_cast<std::uint32_t>(array_bytes.size());
    const std::size_t positions = surface.place.size();
    model->places.resize(positions);
    model->second.resize(positions);
    model->crease.resize(positions);
    model->first.resize(positions + 1u);
    for (std::size_t i = 0; i < positions; ++i) {
        const std::uint16_t q = surface.place[i];
        model->first[i] = static_cast<std::uint32_t>(model->bend.size());
        if (q == kNoPlace) {
            model->places[i] = 0xFFFFFFFFu;
            model->second[i] = kNoPlace;
            model->crease[i] = 0u;
            continue;
        }
        model->places[i] = q | static_cast<std::uint32_t>(surface.crease_places[q][0]) << 16u;
        model->second[i] = surface.crease_places[q][1];
        model->crease[i] = pack_normal(surface.crease[q]);
        for (std::uint32_t b = surface.first[q]; b < surface.first[q + 1u]; ++b) {
            model->bend_normal.push_back(surface.bends[b].normal);
            model->bend.push_back(pack_normal(surface.bends[b].bend));
        }
    }
    model->first[positions] = static_cast<std::uint32_t>(model->bend.size());
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
