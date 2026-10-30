// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/native_material.h"

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/matrix_memory.h"

#include <bit>

namespace galaxy::gx::material {

namespace {

// J3D's texture matrices (J3DTexMtx::load): the listed ones for a whole material, every present one for a change
// list.
void texture_matrices(const Material& m, MatrixMemory& memory, bool differed_list) {
    for (unsigned i = 0; i < 8u; ++i) {
        const auto& t = m.texture_matrix[i];
        if (!t.present || (!differed_list && !t.listed)) {
            continue;
        }
        std::uint32_t words[12];
        const unsigned count = t.rows == 2u ? 8u : 12u;
        for (unsigned k = 0; k < count; ++k) {
            words[k] = std::bit_cast<std::uint32_t>(t.m[k]);
        }
        const auto base = static_cast<std::uint16_t>(t.id >= 64u ? 0x500u + 4u * (t.id - 64u) : 4u * t.id);
        memory.load(base, words, static_cast<std::uint16_t>(count));
    }
}

// J3D's lights (diffLight): sixteen words a light from 0x600 (colour at 3, angle and distance terms at 4-9,
// position at 10-12, direction at 13-15).
void lights(const Material& m, MatrixMemory& memory) {
    for (unsigned i = 0; (m.loads & kLoadsLights) != 0u && i < 8u; ++i) {
        if ((m.light_mask & (1u << i)) == 0u) {
            continue;
        }
        const Light& l = m.light[i];
        const auto base = static_cast<std::uint16_t>(0x600u + 16u * i);
        const std::uint32_t pos[3] = {std::bit_cast<std::uint32_t>(l.position[0]), std::bit_cast<std::uint32_t>(l.position[1]),
                                      std::bit_cast<std::uint32_t>(l.position[2])};
        const std::uint32_t attn[6] = {std::bit_cast<std::uint32_t>(l.angle[0]), std::bit_cast<std::uint32_t>(l.angle[1]),
                                       std::bit_cast<std::uint32_t>(l.angle[2]), std::bit_cast<std::uint32_t>(l.distance[0]),
                                       std::bit_cast<std::uint32_t>(l.distance[1]), std::bit_cast<std::uint32_t>(l.distance[2])};
        const std::uint32_t dir[3] = {std::bit_cast<std::uint32_t>(l.direction[0]), std::bit_cast<std::uint32_t>(l.direction[1]),
                                      std::bit_cast<std::uint32_t>(l.direction[2])};
        const std::uint32_t colour = static_cast<std::uint32_t>(l.color[0]) << 24u |
                                     static_cast<std::uint32_t>(l.color[1]) << 16u |
                                     static_cast<std::uint32_t>(l.color[2]) << 8u | l.color[3];
        memory.load(static_cast<std::uint16_t>(base + 0x0Au), pos, 3u);
        memory.load(static_cast<std::uint16_t>(base + 0x04u), attn, 6u);
        memory.load(static_cast<std::uint16_t>(base + 0x03u), &colour, 1u);
        memory.load(static_cast<std::uint16_t>(base + 0x0Du), dir, 3u);
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

void load_matrices(const Material& m, MatrixMemory& memory) {
    const std::uint32_t f = m.diff_flags;
    if (f != 0u) {
        if (((f >> 8u) & 15u) != 0u) {  // TexGenNum: diffTexMtx
            texture_matrices(m, memory, true);
        }
        if ((f & 0x2u) != 0u || ((f >> 4u) & 15u) != 0u) {  // ColorChan or LightObjNum: diffLight
            lights(m, memory);
        }
        return;
    }
    texture_matrices(m, memory, false);
    if ((m.loads & kLoadsChannels) != 0u) {
        lights(m, memory);
    }
}

}  // namespace galaxy::gx::material
