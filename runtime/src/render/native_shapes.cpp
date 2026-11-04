// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/native_shapes.h"

#include "galaxy/render/curved_surfaces.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace galaxy::render::shapes {

namespace {

// `count` floats of entry `index`, from float `first` of each entry; null when the array is not of `kind` or too short.
const float* entry(const Array* array, ArrayKind kind, std::uint32_t index, std::uint32_t floats, std::uint32_t first) {
    if (array == nullptr || array->kind != kind || index >= array->count) return nullptr;
    const std::size_t at = static_cast<std::size_t>(index) * floats + first;
    return at + (floats - first) <= array->floats.size() ? array->floats.data() + at : nullptr;
}

}  // namespace

std::uint32_t widened_count(std::uint8_t primitive, std::uint32_t count) noexcept {
    switch (primitive) {
    case kLines: return count / 2u * 4u;
    case kLineStrip: return count > 1u ? (count - 1u) * 4u : 0u;
    case kPoints: return count * 4u;
    default: return 0u;
    }
}

void widen_lines_points(std::uint8_t primitive, std::uint32_t count, const RenderVertex* in, RenderVertex* out) noexcept {
    const auto corner = [&out](const RenderVertex& self, const RenderVertex* other, std::uint32_t flags) {
        *out = self;
        out->mtx_indices[2] |= flags;
        if (other != nullptr) {
            std::memcpy(out->tangent, other->position, sizeof(out->tangent));
            out->curve[0] = other->mtx_indices[0] & 0xFFu;
        }
        ++out;
    };
    const auto line = [&corner](const RenderVertex& a, const RenderVertex& b) {
        corner(a, &b, kVertexWidenLine);
        corner(a, &b, kVertexWidenLine | kVertexWidenRight);
        corner(b, &a, kVertexWidenLine);
        corner(b, &a, kVertexWidenLine | kVertexWidenRight);
    };
    if (primitive == kPoints) {
        for (std::uint32_t i = 0; i < count; ++i) {
            corner(in[i], nullptr, kVertexWidenPoint);
            corner(in[i], nullptr, kVertexWidenPoint | kVertexWidenRight);
            corner(in[i], nullptr, kVertexWidenPoint | kVertexWidenBottom);
            corner(in[i], nullptr, kVertexWidenPoint | kVertexWidenRight | kVertexWidenBottom);
        }
    } else if (primitive == kLines) {
        for (std::uint32_t i = 0; i + 1u < count; i += 2u) {
            line(in[i], in[i + 1u]);
        }
    } else if (primitive == kLineStrip) {
        for (std::uint32_t i = 0; i + 1u < count; ++i) {
            line(in[i], in[i + 1u]);
        }
    }
}

void primitive_indices(std::uint8_t primitive, std::uint32_t count, std::vector<std::uint16_t>& out) {
    out.clear();
    auto add = [&](std::uint32_t i) { out.push_back(static_cast<std::uint16_t>(i)); };
    switch (primitive) {
    case kQuads:
        for (std::uint32_t i = 0; i + 3u < count; i += 4u) {
            add(i), add(i + 1u), add(i + 2u);
            add(i), add(i + 2u), add(i + 3u);
        }
        break;
    case kWideQuads:
        // Each corner four is a strip: the diagonal joins its second and third.
        for (std::uint32_t i = 0; i + 3u < count; i += 4u) {
            add(i), add(i + 1u), add(i + 2u);
            add(i + 2u), add(i + 1u), add(i + 3u);
        }
        break;
    case kTriangles:
        for (std::uint32_t i = 0; i < count / 3u * 3u; ++i) add(i);
        break;
    case kTriangleStrip:
        for (std::uint32_t i = 0; i + 2u < count; ++i) {
            if ((i & 1u) == 0u) {
                add(i), add(i + 1u), add(i + 2u);
            } else {
                add(i + 1u), add(i), add(i + 2u);
            }
        }
        break;
    case kTriangleFan:
        for (std::uint32_t i = 1u; i + 1u < count; ++i) add(0u), add(i), add(i + 1u);
        break;
    default:
        break;
    }
}

namespace {

constexpr unsigned kRefineMostLevels = 3u;  // eight segments an edge
constexpr float kRefineOfExtent = 1.0f / 800.0f;

std::uint32_t matrix_of(const RenderVertex& v) {
    return v.mtx_indices[0] & 0xFFu;
}

// The normal corner `a` bends the edge to corner `b` by (Phong tessellation's edge).
curved::Vec3 refine_edge_normal(const RenderVertex& a, const RenderVertex& b) {
    const std::uint32_t place = b.curve[2] & 0xFFFFu;
    const bool crease = place != 0xFFFFu && (place == (a.curve[2] >> 16u) || place == (a.mtx_indices[2] >> 16u));
    return curved::unpack_normal(crease ? a.curve[1] : a.curve[0]);
}

// How many times the edge halves: the arc of a chord of length L turning by angle t stands L t / 8 off it, and n segments
// divide that by n squared.
unsigned refine_level(const RenderVertex& a, const RenderVertex& b, float tolerance) {
    if (matrix_of(a) != matrix_of(b)) {
        return 0u;
    }
    const float e[3] = {b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]};
    const float length = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
    if (!(length > 0.0f)) {
        return 0u;
    }
    const curved::Vec3 na = refine_edge_normal(a, b);
    const curved::Vec3 nb = refine_edge_normal(b, a);
    const float tilt = std::max(std::abs(e[0] * na[0] + e[1] * na[1] + e[2] * na[2]),
                                std::abs(e[0] * nb[0] + e[1] * nb[1] + e[2] * nb[2])) / length;
    if (!(tilt > 0.0f)) {
        return 0u;
    }
    const float off = length * 2.0f * std::asin(std::min(tilt, 1.0f)) / (8.0f * tolerance);
    unsigned level = 0u;
    for (float n2 = 1.0f; n2 < off && level < kRefineMostLevels; n2 *= 4.0f) {
        ++level;
    }
    return level;
}

std::uint32_t mix_colour(std::uint32_t a, std::uint32_t b, std::uint32_t c, const float w[3]) {
    std::uint32_t out = 0u;
    for (unsigned shift = 0u; shift < 32u; shift += 8u) {
        const float v = static_cast<float>((a >> shift) & 0xFFu) * w[0] + static_cast<float>((b >> shift) & 0xFFu) * w[1] +
                        static_cast<float>((c >> shift) & 0xFFu) * w[2];
        out |= static_cast<std::uint32_t>(v + 0.5f) << shift;
    }
    return out;
}

// The point at barycentrics `w` of a triangle (on an edge, one weight zero: it takes the matrices of a corner of that edge).
RenderVertex refine_point(const RenderVertex* const t[3], const float w[3]) {
    RenderVertex v = *t[w[0] > 0.0f ? 0u : 1u];
    const auto mix = [&](const float* a, const float* b, const float* c, float* out, unsigned count) {
        for (unsigned i = 0u; i < count; ++i) {
            out[i] = a[i] * w[0] + b[i] * w[1] + c[i] * w[2];
        }
    };
    mix(t[0]->position, t[1]->position, t[2]->position, v.position, 3u);
    mix(t[0]->normal, t[1]->normal, t[2]->normal, v.normal, 3u);
    mix(t[0]->tangent, t[1]->tangent, t[2]->tangent, v.tangent, 3u);
    mix(t[0]->binormal, t[1]->binormal, t[2]->binormal, v.binormal, 3u);
    mix(&t[0]->uv[0][0], &t[1]->uv[0][0], &t[2]->uv[0][0], &v.uv[0][0], 16u);
    const float length2 = v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2];
    if (length2 > 0.0f) {
        const float scale = 1.0f / std::sqrt(length2);
        v.normal[0] *= scale;
        v.normal[1] *= scale;
        v.normal[2] *= scale;
    }
    v.color0 = mix_colour(t[0]->color0, t[1]->color0, t[2]->color0, w);
    v.color1 = mix_colour(t[0]->color1, t[1]->color1, t[2]->color1, w);
    float move[3] = {0.0f, 0.0f, 0.0f};
    for (unsigned i = 0u; i < 3u; ++i) {
        const unsigned k = (i + 1u) % 3u;
        const float both = w[i] * w[k];
        if (both == 0.0f) {
            continue;
        }
        const float e[3] = {t[k]->position[0] - t[i]->position[0], t[k]->position[1] - t[i]->position[1],
                            t[k]->position[2] - t[i]->position[2]};
        const curved::Vec3 ni = refine_edge_normal(*t[i], *t[k]);
        const curved::Vec3 nk = refine_edge_normal(*t[k], *t[i]);
        const float di = e[0] * ni[0] + e[1] * ni[1] + e[2] * ni[2];
        const float dk = e[0] * nk[0] + e[1] * nk[1] + e[2] * nk[2];
        for (unsigned a = 0u; a < 3u; ++a) {
            move[a] -= both * (di * ni[a] - dk * nk[a]);
        }
    }
    for (unsigned a = 0u; a < 3u; ++a) {
        v.position[a] += 0.5f * move[a];
    }
    return v;
}

struct RefineCorner {
    std::uint32_t id = 0u;
    float w[3]{};
};

struct Refiner {
    std::vector<RenderVertex>& vertices;
    std::vector<std::uint16_t>& indices;
    std::unordered_map<std::uint64_t, std::uint32_t> points;
    const RenderVertex* triangle[3]{};

    // The point half way along (a, b): the same one for every triangle holding that edge.
    RefineCorner middle(const RefineCorner& a, const RefineCorner& b) {
        RefineCorner m;
        for (unsigned i = 0u; i < 3u; ++i) {
            m.w[i] = 0.5f * (a.w[i] + b.w[i]);
        }
        const std::uint64_t key = static_cast<std::uint64_t>(std::min(a.id, b.id)) << 32u | std::max(a.id, b.id);
        const auto found = points.find(key);
        if (found != points.end()) {
            m.id = found->second;
            return m;
        }
        m.id = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(refine_point(triangle, m.w));
        points.emplace(key, m.id);
        return m;
    }

    void split(const RefineCorner& a, const RefineCorner& b, const RefineCorner& c, unsigned ab, unsigned bc, unsigned ca, bool inside) {
        const unsigned most = std::max({ab, bc, ca});
        if (most == 0u) {
            indices.push_back(static_cast<std::uint16_t>(a.id));
            indices.push_back(static_cast<std::uint16_t>(b.id));
            indices.push_back(static_cast<std::uint16_t>(c.id));
            return;
        }
        if (ab != 0u && bc != 0u && ca != 0u) {
            const RefineCorner mab = middle(a, b);
            const RefineCorner mbc = middle(b, c);
            const RefineCorner mca = middle(c, a);
            const unsigned in = most - 1u;
            split(a, mab, mca, ab - 1u, in, ca - 1u, inside);
            split(mab, b, mbc, ab - 1u, bc - 1u, in, inside);
            split(mca, mbc, c, in, bc - 1u, ca - 1u, inside);
            split(mab, mbc, mca, in, in, in, inside);
            return;
        }
        // Halve the most bent edge, across from the corner it does not touch.
        const unsigned in = inside ? most - 1u : 0u;
        if (ab == most) {
            const RefineCorner m = middle(a, b);
            split(a, m, c, ab - 1u, in, ca, inside);
            split(m, b, c, ab - 1u, bc, in, inside);
        } else if (bc == most) {
            const RefineCorner m = middle(b, c);
            split(a, b, m, ab, bc - 1u, in, inside);
            split(a, m, c, in, bc - 1u, ca, inside);
        } else {
            const RefineCorner m = middle(c, a);
            split(a, b, m, ab, in, ca - 1u, inside);
            split(m, b, c, in, bc, ca - 1u, inside);
        }
    }
};

}  // namespace

bool refine_curved(std::span<const RenderVertex> vertices, std::span<const std::uint16_t> indices, float tolerance,
                   std::vector<RenderVertex>& out_vertices, std::vector<std::uint16_t>& out_indices) {
    // A group the size of the stage is seen from afar or from on it, where its arc's depth does not show: its tolerance
    // grows with its largest extent, and a sky or a planet costs what it shows.
    float low[3] = {1e30f, 1e30f, 1e30f};
    float high[3] = {-1e30f, -1e30f, -1e30f};
    for (const RenderVertex& v : vertices) {
        for (unsigned a = 0u; a < 3u; ++a) {
            low[a] = std::min(low[a], v.position[a]);
            high[a] = std::max(high[a], v.position[a]);
        }
    }
    tolerance = std::max(tolerance, kRefineOfExtent * std::max({high[0] - low[0], high[1] - low[1], high[2] - low[2]}));
    out_vertices.assign(vertices.begin(), vertices.end());
    out_indices.clear();
    out_indices.reserve(indices.size());
    Refiner refiner{out_vertices, out_indices, {}, {}};
    refiner.points.reserve(indices.size());
    const std::size_t count = indices.size() / 3u * 3u;
    for (std::size_t n = 0u; n < count; n += 3u) {
        const std::uint32_t id[3] = {indices[n], indices[n + 1u], indices[n + 2u]};
        // The output's own storage moves as it grows: the corners stay in `vertices`.
        const RenderVertex* const t[3] = {&vertices[id[0]], &vertices[id[1]], &vertices[id[2]]};
        refiner.triangle[0] = t[0];
        refiner.triangle[1] = t[1];
        refiner.triangle[2] = t[2];
        const bool one_matrix = matrix_of(*t[0]) == matrix_of(*t[1]) && matrix_of(*t[1]) == matrix_of(*t[2]);
        RefineCorner a{id[0], {1.0f, 0.0f, 0.0f}};
        RefineCorner b{id[1], {0.0f, 1.0f, 0.0f}};
        RefineCorner c{id[2], {0.0f, 0.0f, 1.0f}};
        refiner.split(a, b, c, refine_level(*t[0], *t[1], tolerance), refine_level(*t[1], *t[2], tolerance),
                      refine_level(*t[2], *t[0], tolerance), one_matrix);
        if (out_vertices.size() > 0x10000u) {
            return false;
        }
    }
    return true;
}

bool expand(const Geometry& geometry, const DrawArrays& arrays, std::uint8_t position_matrix,
            const curved::Model* curved, std::vector<RenderVertex>& out) {
    const std::uint32_t a = geometry.attributes;
    const bool nbt = (a & kNbt) != 0u;
    // Curve data belongs to a model's plain normals (bump-mapped shapes carry tangents where it would go).
    if ((a & kNormal) == 0u || nbt) curved = nullptr;
    out.resize(geometry.corners.size());
    for (std::size_t i = 0; i < geometry.corners.size(); ++i) {
        const Corner& c = geometry.corners[i];
        RenderVertex v;
        std::memset(&v, 0, sizeof(v));
        // Places as matrix rows (three a matrix, texture matrices from row 30).
        const std::uint8_t pn =
            static_cast<std::uint8_t>(3u * ((a & kPositionMatrix) != 0u ? c.position_matrix : position_matrix));
        std::uint8_t tex[8]{};
        for (unsigned t = 0; t < 8u; ++t) {
            if ((a & (kTextureMatrix0 << t)) != 0u) {
                tex[t] = c.texture_matrix[t] == kCornerPositionMatrix ? pn : static_cast<std::uint8_t>(30u + 3u * c.texture_matrix[t]);
            }
        }
        v.mtx_indices[0] = pn | static_cast<std::uint32_t>(tex[0]) << 8u | static_cast<std::uint32_t>(tex[1]) << 16u |
                           static_cast<std::uint32_t>(tex[2]) << 24u;
        v.mtx_indices[1] = tex[3] | static_cast<std::uint32_t>(tex[4]) << 8u |
                           static_cast<std::uint32_t>(tex[5]) << 16u | static_cast<std::uint32_t>(tex[6]) << 24u;
        v.mtx_indices[2] = tex[7];

        const float* p = entry(arrays[0], ArrayKind::Position, c.position, 3u, 0u);
        if (p == nullptr) return false;
        std::memcpy(v.position, p, sizeof(v.position));
        if ((a & kNormal) != 0u) {
            if (nbt) {
                const float* n = entry(arrays[1], ArrayKind::Nbt, c.normal, 9u, 0u);
                const float* t = entry(arrays[1], ArrayKind::Nbt, c.tangent, 9u, 3u);
                const float* b = entry(arrays[1], ArrayKind::Nbt, c.binormal, 9u, 6u);
                if (n == nullptr || t == nullptr || b == nullptr) return false;
                std::memcpy(v.normal, n, sizeof(v.normal));
                std::memcpy(v.tangent, t, sizeof(v.tangent));
                std::memcpy(v.binormal, b, sizeof(v.binormal));
            } else {
                const float* n = entry(arrays[1], ArrayKind::Normal, c.normal, 3u, 0u);
                if (n == nullptr) return false;
                std::memcpy(v.normal, n, sizeof(v.normal));
            }
        }
        // A registered model's curve data (curved_surfaces.h): the corner's displacement normal by its normal index,
        // its place's crease normal, its place and crease neighbours.
        if (curved != nullptr && c.position < curved->places.size()) {
            v.curve[0] = curved->bend_of(c.position, c.normal);
            v.curve[1] = curved->crease[c.position];
            v.curve[2] = curved->places[c.position];
            v.mtx_indices[2] |= static_cast<std::uint32_t>(curved->second[c.position]) << 16u;
            // Bit 15: a flat solid's corner shaded soft (Model::flat_of; ray_tracing.hlsl GeometryCS shades its face).
            v.mtx_indices[2] |= curved->flat_of(c.position, c.normal) ? kVertexFlatCorner : 0u;
        } else if (curved != nullptr) {
            // Outside the registered arrays: no place, no neighbours.
            v.curve[2] = 0xFFFFFFFFu;
            v.mtx_indices[2] |= 0xFFFFu << 16u;
        }
        for (unsigned k = 0; k < 2u; ++k) {
            if ((a & (kColor0 << k)) == 0u) continue;
            const Array* colors = arrays[2u + k];
            if (colors == nullptr || colors->kind != ArrayKind::Color || c.color[k] >= colors->colors.size()) return false;
            (k == 0u ? v.color0 : v.color1) = colors->colors[c.color[k]];
        }
        for (unsigned t = 0; t < 8u; ++t) {
            if ((a & (kTexCoord0 << t)) == 0u) continue;
            const float* uv = entry(arrays[4u + t], ArrayKind::TexCoord, c.texcoord[t], 2u, 0u);
            if (uv == nullptr) return false;
            v.uv[t][0] = uv[0];
            v.uv[t][1] = uv[1];
        }
        out[i] = v;
    }
    return true;
}

}  // namespace galaxy::render::shapes
