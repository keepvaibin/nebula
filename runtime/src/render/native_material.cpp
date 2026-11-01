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

// The record's encoding (nebula_game_api.h NebulaMaterialProgram::tex_gen, J3DNativeTexGen).
TexGen tex_gen(std::uint8_t kind, std::uint8_t source) {
    return TexGen{static_cast<TexGenType>(kind & 3u), static_cast<std::uint8_t>(source & 15u),
                  static_cast<std::uint8_t>((source >> 4u) & 7u), static_cast<std::uint8_t>((kind >> 4u) & 7u)};
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
