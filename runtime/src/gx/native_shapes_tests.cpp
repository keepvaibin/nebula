// SPDX-License-Identifier: GPL-3.0-only
// Native shapes (native_shapes.h): a group's corners expanded into vertices from the arrays a draw names, matrix
// slots packed as the shaders read them, NBT entries split by their indices, curve data from the model's analysis,
// and groups whose arrays are missing or short refused.
#include "galaxy/gx/native_shapes.h"

#include "galaxy/gx/curved_surfaces.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace {

using namespace galaxy::gx;
using namespace galaxy::gx::shapes;

bool expect(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL %s\n", what);
    }
    return condition;
}

Array floats(ArrayKind kind, std::uint32_t per, std::uint32_t count) {
    Array a;
    a.kind = kind;
    a.count = count;
    for (std::uint32_t i = 0; i < count * per; ++i) a.floats.push_back(static_cast<float>(i) * 0.5f);
    return a;
}

}  // namespace

int main() {
    bool passed = true;
    const Array positions = floats(ArrayKind::Position, 3u, 4u);
    const Array normals = floats(ArrayKind::Normal, 3u, 4u);
    Array colors;
    colors.kind = ArrayKind::Color;
    colors.count = 2u;
    colors.colors = {0x11223344u, 0xAABBCCDDu};
    const Array uvs = floats(ArrayKind::TexCoord, 2u, 3u);

    Geometry g;
    g.attributes = kNormal | kColor0 | (kTexCoord0 << 1u) | kPositionMatrix | (kTextureMatrix0 << 1u);
    Corner c;
    c.position = 2u;
    c.normal = 1u;
    c.color[0] = 1u;
    c.texcoord[1] = 2u;
    c.position_matrix = 3u;
    c.texture_matrix[1] = 30u;
    g.corners = {c};
    g.indices = {0u, 0u, 0u};
    DrawArrays arrays{};
    arrays[0] = &positions;
    arrays[1] = &normals;
    arrays[2] = &colors;
    arrays[5] = &uvs;
    std::vector<GxVertexOut> out;
    passed &= expect(expand(g, arrays, 0u, nullptr, out) && out.size() == 1u, "a group expands");
    if (out.size() == 1u) {
        const GxVertexOut& v = out[0];
        passed &= expect(v.position[0] == 3.0f && v.position[1] == 3.5f && v.position[2] == 4.0f,
                         "positions come from the corner's entry");
        passed &= expect(v.normal[0] == 1.5f && v.tangent[0] == 0.0f, "normals from theirs, no tangent");
        passed &= expect(v.color0 == 0xAABBCCDDu && v.color1 == 0u, "colour 0 from its entry, colour 1 absent");
        passed &= expect(v.uv[1][0] == 2.0f && v.uv[1][1] == 2.5f && v.uv[0][0] == 0.0f, "texture coordinates");
        passed &= expect(v.mtx_indices[0] == (3u | 30u << 16u) && v.mtx_indices[1] == 0u && v.mtx_indices[2] == 0u,
                         "matrix slots as the shaders read them");
    }

    // Without per-corner position matrices, the draw's.
    Geometry plain = g;
    plain.attributes &= ~kPositionMatrix;
    passed &= expect(expand(plain, arrays, 9u, nullptr, out) && (out[0].mtx_indices[0] & 0xFFu) == 9u,
                     "corners without a position matrix take the draw's");

    // NBT: normal, tangent and binormal from three entries.
    const Array nbt = floats(ArrayKind::Nbt, 9u, 3u);
    Geometry bump = g;
    bump.attributes |= kNbt | kNbtThree;
    bump.corners[0].normal = 0u;
    bump.corners[0].tangent = 1u;
    bump.corners[0].binormal = 2u;
    DrawArrays bump_arrays = arrays;
    bump_arrays[1] = &nbt;
    passed &= expect(expand(bump, bump_arrays, 0u, nullptr, out) && out[0].normal[0] == 0.0f &&
                         out[0].tangent[0] == 6.0f && out[0].binormal[0] == 12.0f,
                     "NBT corners split by their three entries");

    // Missing or short arrays refuse the group.
    DrawArrays missing = arrays;
    missing[2] = nullptr;
    passed &= expect(!expand(g, missing, 0u, nullptr, out), "a missing array refuses the group");
    Geometry far = g;
    far.corners[0].position = 4u;
    passed &= expect(!expand(far, arrays, 0u, nullptr, out), "an entry past the array refuses the group");
    DrawArrays wrong = arrays;
    wrong[1] = &positions;
    passed &= expect(!expand(g, wrong, 0u, nullptr, out), "an array of another kind refuses the group");

    // Curve data: a corner's place from the model's analysis; past its positions, none.
    curved::Model model;
    model.places = {1u, 2u, 7u | 5u << 16u};
    model.second = {0u, 0u, 4u};
    model.crease = {0u, 0u, 0x12345u};
    model.first = {0u, 0u, 0u, 0u};
    passed &= expect(expand(g, arrays, 0u, &model, out) && out[0].curve[1] == 0x12345u &&
                         out[0].curve[2] == (7u | 5u << 16u) && (out[0].mtx_indices[2] >> 16u) == 4u,
                     "curve data from the model's analysis");
    Geometry outside = g;
    outside.corners[0].position = 3u;
    passed &= expect(expand(outside, arrays, 0u, &model, out) && out[0].curve[2] == 0xFFFFFFFFu &&
                         (out[0].mtx_indices[2] >> 16u) == 0xFFFFu,
                     "a corner past the analysis has no place");

    if (!passed) {
        return 1;
    }
    std::printf("native_shapes tests passed\n");
    return 0;
}
