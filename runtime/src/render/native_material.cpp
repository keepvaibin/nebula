// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/native_material.h"

#include "galaxy/render/draw_matrices.h"

#include <bit>

namespace galaxy::render::material {

namespace {

// J3D's texture matrices (J3DTexMtx::load): the listed ones for a whole material, every present one for a change
// list.
void texture_matrices(const Program& m, const Values& v, DrawMatrices& matrices, bool differed_list) {
    for (unsigned i = 0; i < 8u; ++i) {
        const auto& t = m.texture_matrix[i];
        if (!t.present || (!differed_list && !t.listed)) {
            continue;
        }
        matrices.set_material_texture(t.id, v.texture_matrix[i], t.rows == 2u ? 2u : 3u);
    }
}

// J3D's lights (diffLight): the lights the material's mask names.
void lights(const Program& m, const Values& v, DrawMatrices& matrices) {
    for (unsigned i = 0; (m.loads & kLoadsLights) != 0u && i < 8u; ++i) {
        if ((v.light_mask & (1u << i)) != 0u) {
            matrices.set_light(i, v.light[i]);
        }
    }
}

}  // namespace

std::uint32_t tex_gen_word(std::uint8_t type, std::uint8_t source) {
    std::uint32_t form = 0, proj = 0, row = 5, emboss_row = 5, emboss_lit = 0, tg = 0;
    switch (source) {
    case 0: row = 0; form = 1; break;   // POS
    case 1: row = 1; form = 1; break;   // NRM
    case 2: row = 3; form = 1; break;   // BINRM
    case 3: row = 4; form = 1; break;   // TANGENT
    case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: row = 5u + (source - 4u); break;  // TEX0-7
    case 12: case 13: case 14: case 15: case 16: case 17: case 18: emboss_row = source - 12u; break;    // TEXCOORD0-6
    case 19: case 20: row = 2; break;   // COLOR0, COLOR1
    default: break;
    }
    switch (type) {
    case 0: tg = 0; proj = 1; break;    // MTX3x4
    case 1: tg = 0; break;              // MTX2x4
    case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: tg = 1; emboss_lit = type - 2u; break;  // BUMP0-7
    case 10: tg = source == 19 ? 2u : 3u; break;  // SRTG
    default: break;
    }
    return proj << 1u | form << 2u | tg << 4u | row << 7u | emboss_row << 12u | emboss_lit << 15u;
}

void load_matrices(const Program& m, const Values& v, DrawMatrices& memory) {
    const std::uint32_t f = m.diff_flags;
    if (f != 0u) {
        if (((f >> 8u) & 15u) != 0u) {  // TexGenNum: diffTexMtx
            texture_matrices(m, v, memory, true);
        }
        if ((f & 0x2u) != 0u || ((f >> 4u) & 15u) != 0u) {  // ColorChan or LightObjNum: diffLight
            lights(m, v, memory);
        }
        return;
    }
    texture_matrices(m, v, memory, false);
    if ((m.loads & kLoadsChannels) != 0u) {
        lights(m, v, memory);
    }
}

}  // namespace galaxy::render::material
