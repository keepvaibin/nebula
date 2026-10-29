// SPDX-License-Identifier: GPL-3.0-only
// Curved surfaces: the shape analysis behind Phong tessellation and the registry of models (curved_surfaces.h).
#include "galaxy/gx/curved_surfaces.h"

#include "galaxy/windows_power.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <numeric>
#include <thread>
#include <span>
#include <tuple>
#include <unordered_map>

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
// A flat solid shaded soft (PlaceBend::flat): its faces' planes 40 degrees or more apart, its normal 10 degrees or more
// off one of them.
const double kSolidCrease = std::cos(radians(40.0));
const double kSplayed = std::cos(radians(10.0));
// A T-junction's line bends under a normal more than this far off its perpendicular.
constexpr double kBow = 1e-5;

std::uint64_t pair_key(std::uint32_t a, std::uint32_t b) { return (static_cast<std::uint64_t>(a) << 32u) | b; }

// A triangle's corner at a place.
struct At {
    D3 normal;                     // its shading normal, unit
    std::uint16_t normal_index;
    D3 face;                       // the triangle's own normal, unit
    double angle;                  // the triangle's angle there
    std::uint32_t other[2];        // the triangle's other two places
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

// sampled()'s working arrays, kept between calls (the analysis calls it once per group of corners).
struct SampledScratch {
    struct Side {
        D3 tangent;
        double curvature;
        double edge;
        bool flat;
    };
    struct Row {
        double eu, ev, tu, tv;
    };
    std::vector<D3> oriented;
    std::vector<std::uint32_t> neighbours;
    std::vector<Side> sides;
    std::vector<std::array<double, 3>> forms;
    std::vector<Row> rows;
    std::vector<std::uint32_t> lengthwise;
    std::vector<std::uint32_t> across;
};

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
    thread_local SampledScratch scratch;
    std::vector<D3>& oriented = scratch.oriented;
    oriented.clear();
    for (const At* at : group) oriented.push_back(dot(at->face, n0) >= 0.0 ? at->face : mul(at->face, -1.0));
    if (oriented.empty() || cone_angle(n0, oriented) > kCone) return false;

    // Each neighbour's normal curvature 2 h / |d|^2 (h its height over the tangent plane) along its tangent direction.
    const D3& pq = places[q];
    const D3 u = normalized(cross(n0, std::fabs(n0[0]) < 0.9 ? D3{1.0, 0.0, 0.0} : D3{0.0, 1.0, 0.0}));
    const D3 v = cross(n0, u);
    std::vector<std::uint32_t>& neighbours = scratch.neighbours;
    neighbours.clear();
    for (const At* at : group) neighbours.insert(neighbours.end(), {at->other[0], at->other[1]});
    std::sort(neighbours.begin(), neighbours.end());
    neighbours.erase(std::unique(neighbours.begin(), neighbours.end()), neighbours.end());
    std::vector<SampledScratch::Side>& sides = scratch.sides;
    sides.clear();
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
        const SampledScratch::Side& a = sides[i];
        const SampledScratch::Side& b = sides[best];
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
        std::vector<std::array<double, 3>>& forms = scratch.forms;
        forms.clear();
        for (const SampledScratch::Side& side : sides) {
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
    std::vector<SampledScratch::Row>& rows = scratch.rows;
    rows.clear();
    for (std::size_t i = 0; i < group.size(); ++i) {
        const D3 centre = circumcentre(sub(places[group[i]->other[0]], pq), sub(places[group[i]->other[1]], pq));
        if (is_zero(centre)) continue;
        rows.push_back({dot(centre, u), dot(centre, v), dot(oriented[i], u), dot(oriented[i], v)});
    }
    double tilt2 = 0.0;
    for (const SampledScratch::Row& r : rows) tilt2 += r.tu * r.tu + r.tv * r.tv;
    if (tilt2 > kFlat * kFlat * static_cast<double>(rows.size())) {
        // S = [[a, b], [b, c]]: t_u = a e_u + b e_v, t_v = b e_u + c e_v, least squares.
        std::array<std::array<double, 3>, 3> m{};
        std::array<double, 3> rhs{};
        for (const SampledScratch::Row& r : rows) {
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
        for (const SampledScratch::Row& r : rows) {
            const double du = r.tu - s[0] * r.eu - s[1] * r.ev;
            const double dv = r.tv - s[1] * r.eu - s[2] * r.ev;
            residual += du * du + dv * dv;
        }
        if (residual > kFit * kFit * tilt2) return false;
    }

    // A sliver shows curvature only across itself: along its length the normal may bend no more than the
    // position's short edges show it bending.
    std::vector<std::uint32_t>& lengthwise = scratch.lengthwise;
    std::vector<std::uint32_t>& across = scratch.across;
    lengthwise.clear();
    across.clear();
    const auto holds = [](const std::vector<std::uint32_t>& set, std::uint32_t j) {
        return std::find(set.begin(), set.end(), j) != set.end();
    };
    for (const At* at : group) {
        const D3& pa = places[at->other[0]];
        const D3& pb = places[at->other[1]];
        const double e[3] = {length(sub(pa, pq)), length(sub(pb, pq)), length(sub(pa, pb))};
        const double height = length(cross(sub(pa, pq), sub(pb, pq))) / std::max({e[0], e[1], e[2]});
        for (int k = 0; k < 2; ++k) {
            std::vector<std::uint32_t>& set = e[k] > kSliver * height ? lengthwise : across;
            if (!holds(set, at->other[k])) set.push_back(at->other[k]);
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
        if (!holds(lengthwise, j)) bound = std::max(bound, curvature(j));
    }
    for (const std::uint32_t j : lengthwise) {
        if (curvature(j) > bound) return false;
    }
    return true;
}

// Whether a group of corners with normal n0 is a flat solid's corner shaded soft (PlaceBend::flat): its faces in at
// most three planes, each 40 degrees or more from the others (a box's corner or edge, a bevel; a cylinder's sides,
// 22.5 degrees apart, and a sphere's or a character's many facets are not), and the normal 10 degrees or more off one
// of them (a normal on its faces shades them flat already).
bool flat_solid(const D3& n0, std::span<const At* const> group) {
    std::array<D3, 3> planes{};
    std::size_t count = 0u;
    for (const At* c : group) {
        bool known = false;
        for (std::size_t i = 0; i < count && !known; ++i) known = dot(planes[i], c->face) >= kParallel;
        if (known) continue;
        if (count == planes.size()) return false;
        planes[count++] = c->face;
    }
    bool splayed = false;
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1u; j < count; ++j) {
            if (dot(planes[i], planes[j]) > kSolidCrease) return false;
        }
        splayed = splayed || std::fabs(dot(planes[i], n0)) < kSplayed;
    }
    return count != 0u && splayed;
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
    // The grid as (cell, place) pairs sorted by cell, and each occupied cell's run in it by open addressing (keys use
    // 63 bits, so all ones marks an empty slot).
    std::vector<std::pair<std::uint64_t, std::uint32_t>> grid(places.size());
    for (std::uint32_t i = 0; i < places.size(); ++i) grid[i] = {key_of(cell_of(places[i])), i};
    std::sort(grid.begin(), grid.end());
    struct Run {
        std::uint64_t key = ~std::uint64_t{0};
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
    };
    unsigned table_bits = 4u;
    while ((std::size_t{1} << table_bits) < 2u * places.size()) ++table_bits;
    std::vector<Run> runs(std::size_t{1} << table_bits);
    const std::size_t table_mask = runs.size() - 1u;
    const auto slot_of = [table_bits](std::uint64_t key) {
        return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ull) >> (64u - table_bits));
    };
    for (std::uint32_t begin = 0; begin < grid.size();) {
        std::uint32_t end = begin + 1u;
        while (end < grid.size() && grid[end].first == grid[begin].first) ++end;
        std::size_t slot = slot_of(grid[begin].first);
        while (runs[slot].key != ~std::uint64_t{0}) slot = (slot + 1u) & table_mask;
        runs[slot] = {grid[begin].first, begin, end};
        begin = end;
    }
    const auto visit = [&](std::uint64_t key, std::uint32_t a, std::uint32_t b, const D3& pa, const D3& d, double dd) {
        std::size_t slot = slot_of(key);
        while (runs[slot].key != key) {
            if (runs[slot].key == ~std::uint64_t{0}) return;
            slot = (slot + 1u) & table_mask;
        }
        for (std::uint32_t i = runs[slot].begin; i < runs[slot].end; ++i) {
            const std::uint32_t p = grid[i].second;
            if (p == a || p == b) continue;
            const D3 w = sub(places[p], pa);
            const double t = dot(w, d) / dd;
            if (t <= 1e-4 || t >= 1.0 - 1e-4) continue;
            const D3 off = sub(w, mul(d, t));
            if (dot(off, off) <= 1e-8 * dd) out.push_back({p, a, b});
        }
    };
    for (const std::uint64_t e : edges) {
        const auto a = static_cast<std::uint32_t>(e >> 32u);
        const auto b = static_cast<std::uint32_t>(e & 0xFFFFFFFFu);
        const D3& pa = places[a];
        const D3 d = sub(places[b], pa);
        const double dd = dot(d, d);
        if (!(dd > 0.0)) continue;
        const auto steps = static_cast<std::size_t>(std::sqrt(dd) / (0.5 * cell)) + 1u;
        // The samples' cells move monotonically along each axis, so a cell of a sample's 3x3x3 block that lies outside
        // the previous sample's block lies outside every earlier one: each cell of the edge's blocks is visited once.
        std::array<std::int64_t, 3> previous{};
        bool have_previous = false;
        for (std::size_t s = 0; s <= steps; ++s) {
            const auto c = cell_of(add(pa, mul(d, static_cast<double>(s) / static_cast<double>(steps))));
            if (have_previous && c == previous) continue;
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                for (std::int64_t dy = -1; dy <= 1; ++dy) {
                    for (std::int64_t dz = -1; dz <= 1; ++dz) {
                        const std::array<std::int64_t, 3> x{c[0] + dx, c[1] + dy, c[2] + dz};
                        if (have_previous && std::llabs(x[0] - previous[0]) <= 1 && std::llabs(x[1] - previous[1]) <= 1 &&
                            std::llabs(x[2] - previous[2]) <= 1) {
                            continue;
                        }
                        visit(key_of(x), a, b, pa, d, dd);
                    }
                }
            }
            previous = c;
            have_previous = true;
        }
    }
}

Vec3 narrow(const D3& v) {
    return {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
}

// An address's model, or the analysis it waits for (`pending`); `generation` tells a finished analysis whether it is
// still the address's latest registration.
struct Entry {
    std::shared_ptr<const Model> model;
    std::uint64_t generation = 0;
    bool pending = false;
};

struct Job {
    std::uint64_t key = 0;
    std::uint64_t generation = 0;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Corner> corners;
};

struct Registry {
    std::mutex lock;
    std::condition_variable done;
    std::unordered_map<std::uint64_t, Entry> models;
    std::uint64_t generation = 0;
    std::condition_variable work;
    std::deque<Job> jobs;
    bool worker = false;
};

// Never destroyed: the analysis worker may still wait on it while the process exits.
Registry& registry() {
    static Registry* r = new Registry;
    return *r;
}

// The model a registration keeps, or null when nothing in it bends and no corner is flat (its draws stay plain).
std::shared_ptr<const Model> make_model(const Surface& surface) {
    const bool bends = surface.bends_anything();
    if (!bends && !surface.flat_anything()) return nullptr;
    auto model = std::make_shared<Model>();
    model->bends = bends;
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
            model->bend_flat.push_back(surface.bends[b].flat ? 1u : 0u);
        }
    }
    model->first[positions] = static_cast<std::uint32_t>(model->bend.size());
    return model;
}

// Publishes a finished registration unless a later one for the address replaced it.
void publish(Registry& r, std::uint64_t address, std::uint64_t generation, std::shared_ptr<const Model> model) {
    {
        std::lock_guard guard(r.lock);
        const auto it = r.models.find(address);
        if (it == r.models.end() || it->second.generation != generation) return;
        if (model) {
            it->second.model = std::move(model);
            it->second.pending = false;
        } else {
            r.models.erase(it);
        }
    }
    r.done.notify_all();
}

void analysis_worker() {
    galaxy::host::name_current_thread(L"Nebula Curved Analysis");
    Registry& r = registry();
    for (;;) {
        Job job;
        {
            std::unique_lock guard(r.lock);
            r.work.wait(guard, [&r] { return !r.jobs.empty(); });
            job = std::move(r.jobs.front());
            r.jobs.pop_front();
        }
        std::shared_ptr<const Model> model;
        try {
            model = make_model(analyse(job.positions, job.normals, job.corners));
        } catch (...) {
            // Without its analysis the model draws plain, as one that bends nothing.
        }
        publish(r, job.key, job.generation, std::move(model));
    }
}

}  // namespace

bool Surface::bends_anything() const noexcept {
    const auto bends_at = [](const Vec3& v) { return v[0] != 0.0f || v[1] != 0.0f || v[2] != 0.0f; };
    return std::any_of(crease.begin(), crease.end(), bends_at) ||
           std::any_of(bends.begin(), bends.end(), [&](const PlaceBend& b) { return bends_at(b.bend); });
}

bool Surface::flat_anything() const noexcept {
    return std::any_of(bends.begin(), bends.end(), [](const PlaceBend& b) { return b.flat; });
}

Surface analyse(std::span<const Vec3> positions, std::span<const Vec3> normals, std::span<const Corner> corners) {
    Surface out;
    // Places: positions equal by value (-0 as 0; a NaN equals nothing), numbered in the order they first appear.
    std::vector<std::uint32_t> place(positions.size());
    std::vector<D3> places;
    {
        struct Key {
            std::uint32_t x, y, z, index;
        };
        const auto bits = [](float f) {
            f += 0.0f;
            std::uint32_t b;
            std::memcpy(&b, &f, 4u);
            return b;
        };
        std::vector<Key> keys(positions.size());
        for (std::uint32_t i = 0; i < positions.size(); ++i) {
            keys[i] = {bits(positions[i][0]), bits(positions[i][1]), bits(positions[i][2]), i};
        }
        std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
            return std::tie(a.x, a.y, a.z, a.index) < std::tie(b.x, b.y, b.z, b.index);
        });
        std::vector<std::uint32_t> first_of(positions.size());
        for (std::size_t b = 0; b < keys.size();) {
            const Vec3& p = positions[keys[b].index];
            const bool unequal = std::isnan(p[0]) || std::isnan(p[1]) || std::isnan(p[2]);
            std::size_t e = b + 1u;
            while (!unequal && e < keys.size() && keys[e].x == keys[b].x && keys[e].y == keys[b].y &&
                   keys[e].z == keys[b].z) {
                ++e;
            }
            for (std::size_t k = b; k < e; ++k) first_of[keys[k].index] = keys[b].index;
            b = e;
        }
        places.reserve(positions.size());
        for (std::uint32_t i = 0; i < positions.size(); ++i) {
            if (first_of[i] == i) {
                place[i] = static_cast<std::uint32_t>(places.size());
                places.push_back(widen(positions[i]));
            } else {
                place[i] = place[first_of[i]];
            }
        }
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
    std::vector<D3> unit_normals(normals.size());
    for (std::size_t i = 0; i < normals.size(); ++i) unit_normals[i] = normalized(widen(normals[i]));

    // Corners at each place; edges; each directed edge's corner normals (crease detection, non-degenerate triangles)
    // and normal indices (every triangle); zero-area triangles' collinear corners as junctions.
    struct PlaceCorner {
        std::uint32_t place;
        At at;
    };
    struct EdgeEnd {
        std::uint64_t key;
        D3 normal;
    };
    std::vector<PlaceCorner> place_corners;
    std::vector<std::uint64_t> edges;
    std::vector<std::pair<std::uint64_t, std::uint16_t>> edge_index;
    std::vector<EdgeEnd> edge_ends;
    std::vector<std::array<std::uint32_t, 3>> junctions;
    place_corners.reserve(corners.size());
    edges.reserve(corners.size());
    edge_index.reserve(2u * corners.size());
    edge_ends.reserve(2u * corners.size());
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
            n[k] = unit_normals[c.normal];
        }
        if (!valid || p[0] == p[1] || p[1] == p[2] || p[0] == p[2]) continue;
        for (unsigned k = 0; k < 3u; ++k) {
            const std::uint32_t a = p[k];
            const std::uint32_t b = p[(k + 1u) % 3u];
            edges.push_back(pair_key(std::min(a, b), std::max(a, b)));
            for (const unsigned j : {(k + 1u) % 3u, (k + 2u) % 3u}) {
                edge_index.push_back({pair_key(p[k], p[j]), corners[t + k].normal});
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
            place_corners.push_back({p[k], {n[k], corners[t + k].normal, face, angle, {o0, o1}}});
            edge_ends.push_back({pair_key(p[k], o0), n[k]});
            edge_ends.push_back({pair_key(p[k], o1), n[k]});
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    std::sort(edge_index.begin(), edge_index.end());
    edge_index.erase(std::unique(edge_index.begin(), edge_index.end()), edge_index.end());

    // Each place's corners in triangle order.
    std::vector<std::uint32_t> corner_first(place_count + 1u, 0u);
    for (const PlaceCorner& pc : place_corners) ++corner_first[pc.place + 1u];
    for (std::uint32_t q = 0; q < place_count; ++q) corner_first[q + 1u] += corner_first[q];
    std::vector<At> at(place_corners.size());
    {
        std::vector<std::uint32_t> next(corner_first.begin(), corner_first.end() - 1);
        for (const PlaceCorner& pc : place_corners) at[next[pc.place]++] = pc.at;
    }

    // Crease edges: their triangles disagree on the normal at the near end (against the first, in triangle order), or
    // one triangle holds them. Sorted by key, so each place's run lists its crease neighbours in order.
    std::stable_sort(edge_ends.begin(), edge_ends.end(),
                     [](const EdgeEnd& a, const EdgeEnd& b) { return a.key < b.key; });
    std::vector<std::uint64_t> crease_keys;
    for (std::size_t b = 0; b < edge_ends.size();) {
        std::size_t e = b + 1u;
        bool agree = parallel(edge_ends[b].normal, edge_ends[b].normal);
        while (e < edge_ends.size() && edge_ends[e].key == edge_ends[b].key) {
            agree = agree && parallel(edge_ends[b].normal, edge_ends[e].normal);
            ++e;
        }
        if (e - b == 1u || !agree) crease_keys.push_back(edge_ends[b].key);
        b = e;
    }
    std::vector<std::uint32_t> crease_first(place_count + 1u, 0u);
    for (const std::uint64_t key : crease_keys) ++crease_first[(key >> 32u) + 1u];
    for (std::uint32_t q = 0; q < place_count; ++q) crease_first[q + 1u] += crease_first[q];
    const auto is_crease = [&](std::uint32_t q, std::uint32_t j) {
        return std::binary_search(crease_keys.begin() + crease_first[q], crease_keys.begin() + crease_first[q + 1u],
                                  pair_key(q, j));
    };

    // Each place's groups, their bends and the crease's.
    std::vector<std::pair<std::uint16_t, D3>> bends;  // by place, then by normal index
    std::vector<std::uint32_t> bend_first(place_count + 1u, 0u);
    std::vector<std::uint16_t> flats;                 // the normal indices of flat solids' corners, by place
    std::vector<std::uint32_t> flat_first(place_count + 1u, 0u);
    std::vector<D3> crease_normal(places.size(), D3{0.0, 0.0, 0.0});
    struct SetBend {
        bool any = false;
        bool agree = true;
        D3 value{};
    };
    std::vector<std::uint32_t> corner_sets;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> through;
    std::vector<std::uint32_t> roots;
    std::vector<std::uint32_t> group_of;
    std::vector<std::uint32_t> group_first;
    std::vector<std::uint32_t> group_next;
    std::vector<std::uint32_t> grouped_index;
    std::vector<const At*> grouped;
    std::vector<std::uint16_t> slot_index;
    std::vector<std::uint32_t> slot_of;
    std::vector<std::uint32_t> slot_sets;
    std::vector<SetBend> set_bends;
    const auto root = [](std::vector<std::uint32_t>& sets, std::uint32_t x) {
        while (sets[x] != x) {
            sets[x] = sets[sets[x]];
            x = sets[x];
        }
        return x;
    };
    const auto join = [&root](std::vector<std::uint32_t>& sets, std::uint32_t a, std::uint32_t b) {
        sets[root(sets, a)] = root(sets, b);
    };
    for (std::uint32_t q = 0; q < place_count; ++q) {
        bend_first[q] = static_cast<std::uint32_t>(bends.size());
        flat_first[q] = static_cast<std::uint32_t>(flats.size());
        const At* const cs = at.data() + corner_first[q];
        const std::uint32_t count = corner_first[q + 1u] - corner_first[q];
        if (count == 0u) continue;
        // Groups: corners joined through their triangles' shared non-crease edges at q; each group's corners in
        // triangle order.
        corner_sets.resize(count);
        std::iota(corner_sets.begin(), corner_sets.end(), 0u);
        through.clear();
        for (std::uint32_t i = 0; i < count; ++i) {
            for (const std::uint32_t j : cs[i].other) {
                if (is_crease(q, j)) continue;
                const auto it = std::find_if(through.begin(), through.end(),
                                             [j](const std::pair<std::uint32_t, std::uint32_t>& e) { return e.first == j; });
                if (it == through.end()) {
                    through.push_back({j, i});
                } else {
                    join(corner_sets, i, it->second);
                }
            }
        }
        roots.clear();
        group_of.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t r = root(corner_sets, i);
            const auto it = std::find(roots.begin(), roots.end(), r);
            group_of[i] = static_cast<std::uint32_t>(it - roots.begin());
            if (it == roots.end()) roots.push_back(r);
        }
        const auto groups = static_cast<std::uint32_t>(roots.size());
        group_first.assign(groups + 1u, 0u);
        for (std::uint32_t i = 0; i < count; ++i) ++group_first[group_of[i] + 1u];
        for (std::uint32_t g = 0; g < groups; ++g) group_first[g + 1u] += group_first[g];
        grouped_index.resize(count);
        grouped.resize(count);
        group_next.assign(group_first.begin(), group_first.end() - 1);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t slot = group_next[group_of[i]]++;
            grouped_index[slot] = i;
            grouped[slot] = &cs[i];
        }
        const auto group = [&](std::uint32_t g) {
            return std::span<const At* const>(grouped.data() + group_first[g], group_first[g + 1u] - group_first[g]);
        };
        const bool multi = groups > 1u;
        // A group that is no sampled surface but a flat solid's corner shaded soft: its normal indices.
        const auto note_flat = [&](std::span<const At* const> members) {
            if (flat_solid(members[0]->normal, members)) {
                for (const At* c : members) flats.push_back(c->normal_index);
            }
        };
        const std::uint32_t crease_count = crease_first[q + 1u] - crease_first[q];
        const auto sort_here = [&] {
            std::sort(bends.begin() + bend_first[q], bends.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
        };
        if (multi && crease_count != 2u) {
            // A corner: nothing bends.
            for (std::uint32_t i = 0; i < count; ++i) bends.push_back({cs[i].normal_index, D3{0.0, 0.0, 0.0}});
            for (std::uint32_t g = 0; g < groups; ++g) note_flat(group(g));
            sort_here();
            continue;
        }
        D3 crease_plane{0.0, 0.0, 0.0};
        if (multi) {
            const auto c0 = static_cast<std::uint32_t>(crease_keys[crease_first[q]] & 0xFFFFFFFFu);
            const auto c1 = static_cast<std::uint32_t>(crease_keys[crease_first[q] + 1u] & 0xFFFFFFFFu);
            out.crease_places[q] = {static_cast<std::uint16_t>(c0), static_cast<std::uint16_t>(c1)};
            const D3& a = places[c0];
            const D3& b = places[c1];
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
        slot_index.clear();
        slot_of.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto it = std::find(slot_index.begin(), slot_index.end(), cs[i].normal_index);
            slot_of[i] = static_cast<std::uint32_t>(it - slot_index.begin());
            if (it == slot_index.end()) slot_index.push_back(cs[i].normal_index);
        }
        slot_sets.resize(slot_index.size());
        std::iota(slot_sets.begin(), slot_sets.end(), 0u);
        for (std::uint32_t g = 0; g < groups; ++g) {
            const std::uint32_t lead = slot_of[grouped_index[group_first[g]]];
            for (std::uint32_t k = group_first[g]; k < group_first[g + 1u]; ++k) join(slot_sets, slot_of[grouped_index[k]], lead);
        }
        set_bends.assign(slot_index.size(), SetBend{});
        bool backed = false;
        for (std::uint32_t g = 0; g < groups; ++g) {
            const std::span<const At* const> members = group(g);
            const D3& n0 = members[0]->normal;
            const bool curved = sampled(q, n0, members, places);
            if (!curved) note_flat(members);
            const D3 value = curved ? n0 : D3{0.0, 0.0, 0.0};
            SetBend& set = set_bends[root(slot_sets, slot_of[grouped_index[group_first[g]]])];
            if (!set.any) {
                set.any = true;
                set.value = value;
            }
            set.agree = set.agree && value[0] == set.value[0] && value[1] == set.value[1] && value[2] == set.value[2];
            if (curved && !is_zero(crease_normal[q])) {
                const D3 in_plane = sub(n0, mul(crease_plane, dot(n0, crease_plane)));
                const double l = length(in_plane);
                backed = backed || (l >= kInPlane && std::fabs(dot(in_plane, crease_normal[q])) >= kAgree * l);
            }
        }
        if (!backed) crease_normal[q] = {0.0, 0.0, 0.0};
        for (std::uint32_t slot = 0; slot < slot_index.size(); ++slot) {
            const SetBend& set = set_bends[root(slot_sets, slot)];
            bends.push_back({slot_index[slot], set.any && set.agree ? set.value : D3{0.0, 0.0, 0.0}});
        }
        sort_here();
    }
    bend_first[place_count] = static_cast<std::uint32_t>(bends.size());
    flat_first[place_count] = static_cast<std::uint32_t>(flats.size());

    // T-junctions (and zero-area triangles' corners): where any edge along the line would bend, the three places
    // stay put.
    find_junctions(places, edges, junctions);
    const auto bend_at = [&](std::uint32_t x, std::uint16_t normal_index, std::uint32_t y) -> D3 {
        const std::array<std::uint16_t, 2>& c = out.crease_places[x];
        if (c[0] == y || c[1] == y) return crease_normal[x];
        for (std::uint32_t k = bend_first[x]; k < bend_first[x + 1u]; ++k) {
            if (bends[k].first == normal_index) return bends[k].second;
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
                const std::uint64_t key = pair_key(x, y);
                auto it = std::lower_bound(edge_index.begin(), edge_index.end(), std::pair<std::uint64_t, std::uint16_t>{key, 0u});
                for (; it != edge_index.end() && it->first == key; ++it) {
                    bows = bows || std::fabs(dot(bend_at(x, it->second, y), line)) > kBow;
                }
            }
        }
        if (bows) {
            for (const std::uint32_t x : j) fixed[x] = true;
        }
    }

    out.bends.reserve(bends.size());
    for (std::uint32_t q = 0; q < place_count; ++q) {
        out.first[q] = static_cast<std::uint32_t>(out.bends.size());
        const auto flat_begin = flats.begin() + flat_first[q];
        const auto flat_end = flats.begin() + flat_first[q + 1u];
        for (std::uint32_t k = bend_first[q]; k < bend_first[q + 1u]; ++k) {
            const auto& [index, bend] = bends[k];
            const bool flat = std::find(flat_begin, flat_end, index) != flat_end;
            out.bends.push_back({index, fixed[q] ? Vec3{0.0f, 0.0f, 0.0f} : narrow(bend), flat});
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

bool Model::flat_of(std::uint32_t position, std::uint32_t normal) const noexcept {
    if (position + 1u >= first.size()) return false;
    for (std::uint32_t i = first[position]; i < first[position + 1u]; ++i) {
        if (bend_normal[i] == normal) return bend_flat[i] != 0u;
    }
    return false;
}

void register_model(std::uint64_t key, const Surface& surface) {
    Registry& r = registry();
    std::uint64_t generation;
    {
        std::lock_guard guard(r.lock);
        Entry& entry = r.models[key];
        entry = Entry{nullptr, ++r.generation, true};
        generation = entry.generation;
    }
    publish(r, key, generation, make_model(surface));
}

void register_model_later(std::uint64_t key, std::vector<Vec3> positions, std::vector<Vec3> normals,
                          std::vector<Corner> corners) {
    Registry& r = registry();
    Job job{key, 0u, std::move(positions), std::move(normals), std::move(corners)};
    {
        std::lock_guard guard(r.lock);
        Entry& entry = r.models[key];
        entry = Entry{nullptr, ++r.generation, true};
        job.generation = entry.generation;
        r.jobs.push_back(std::move(job));
        if (!r.worker) {
            r.worker = true;
            std::thread(analysis_worker).detach();
        }
    }
    r.work.notify_one();
}

std::shared_ptr<const Model> find_model(std::uint64_t key) {
    Registry& r = registry();
    std::unique_lock guard(r.lock);
    auto it = r.models.find(key);
    if (it == r.models.end()) return nullptr;
    // The model's first draw waits for its analysis rather than drawing plain and then rounding.
    const std::uint64_t generation = it->second.generation;
    while (it != r.models.end() && it->second.pending && it->second.generation == generation) {
        r.done.wait(guard);
        it = r.models.find(key);
    }
    return it != r.models.end() && !it->second.pending ? it->second.model : nullptr;
}

void clear_models() {
    Registry& r = registry();
    {
        std::lock_guard guard(r.lock);
        r.models.clear();
    }
    r.done.notify_all();
}

}  // namespace galaxy::gx::curved
