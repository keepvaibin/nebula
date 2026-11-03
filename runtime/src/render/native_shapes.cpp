// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/native_shapes.h"

#include "galaxy/render/curved_surfaces.h"

#include <cstring>

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
