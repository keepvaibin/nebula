// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/native_material.h"

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_state.h"

#include <bit>

namespace galaxy::gx::material {

namespace {

// GX's hardware ids (gx-packing-reference: J3DGD.cpp's tables).
constexpr std::uint8_t kTexMode0[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};
constexpr std::uint8_t kTexMode1[8] = {0x84, 0x85, 0x86, 0x87, 0xA4, 0xA5, 0xA6, 0xA7};
constexpr std::uint8_t kTexImage0[8] = {0x88, 0x89, 0x8A, 0x8B, 0xA8, 0xA9, 0xAA, 0xAB};
constexpr std::uint8_t kTexImage3[8] = {0x94, 0x95, 0x96, 0x97, 0xB4, 0xB5, 0xB6, 0xB7};
constexpr std::uint8_t kTexTlut[8] = {0x98, 0x99, 0x9A, 0x9B, 0xB8, 0xB9, 0xBA, 0xBB};
constexpr std::uint8_t kFilter[6] = {0x00, 0x04, 0x01, 0x05, 0x02, 0x06};
constexpr std::uint8_t kChannelToRas[16] = {0, 1, 0, 1, 0, 1, 7, 5, 6, 0, 0, 0, 0, 0, 0, 7};
constexpr std::uint8_t kCull[4] = {0, 2, 1, 3};
constexpr std::uint8_t kNullMap = 0xFF;
constexpr std::uint8_t kTexDisable = 0x00;  // map bit 0x100 cannot be set in a byte map

struct Writer {
    GxState& state;
    FifoSink* actions;
    void bp(std::uint32_t word) {
        state.load_bp(word);
        if (actions != nullptr && word >> 24u == 0x65u) {
            actions->on_tlut_load();  // the palette copy the TLUT destination write starts
        }
    }
    void bp(std::uint8_t id, std::uint32_t value) { bp(static_cast<std::uint32_t>(id) << 24u | (value & 0xFFFFFFu)); }
    void xf(std::uint16_t address, std::uint32_t value) { state.load_xf(address, &value, 1u); }
    void xf(std::uint16_t address, const std::uint32_t* values, std::uint16_t count) {
        state.load_xf(address, values, count);
    }
};

std::uint32_t rgba(const std::uint8_t* c) {
    return static_cast<std::uint32_t>(c[0]) << 24u | static_cast<std::uint32_t>(c[1]) << 16u |
           static_cast<std::uint32_t>(c[2]) << 8u | c[3];
}

// One TEV operation as J3DTevStage::load packs it (compare operations put their kind in the scale field, bias 3).
std::uint32_t tev_op(std::uint8_t op, std::uint8_t bias, std::uint8_t scale, std::uint8_t clamp, std::uint8_t reg) {
    const bool compare = op > 1u;
    return (compare ? 3u : bias) << 16u | static_cast<std::uint32_t>(op & 1u) << 18u |
           static_cast<std::uint32_t>(clamp) << 19u | (compare ? (op >> 1u) & 3u : scale) << 20u |
           static_cast<std::uint32_t>(reg) << 22u;
}

void texture(Writer& w, unsigned map, const Texture& t) {
    w.bp(kTexImage3[map], static_cast<std::uint32_t>(t.image >> 5u));
    w.bp(kTexImage0[map], static_cast<std::uint32_t>(t.width - 1u) | static_cast<std::uint32_t>(t.height - 1u) << 10u |
                              static_cast<std::uint32_t>(t.format) << 20u);
    w.bp(kTexMode0[map], static_cast<std::uint32_t>(t.wrap_s) | static_cast<std::uint32_t>(t.wrap_t) << 2u |
                             static_cast<std::uint32_t>(t.mag_filter == 1u) << 4u |
                             static_cast<std::uint32_t>(kFilter[t.min_filter % 6u]) << 5u |
                             static_cast<std::uint32_t>(!t.edge_lod) << 8u |
                             static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int32_t>(32.0f * t.lod_bias))) << 9u |
                             static_cast<std::uint32_t>(t.anisotropy) << 19u | static_cast<std::uint32_t>(t.bias_clamp) << 21u);
    w.bp(kTexMode1[map], static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int32_t>(16.0f * t.min_lod))) |
                             static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int32_t>(16.0f * t.max_lod))) << 8u);
    if (t.palette != 0u) {
        const std::uint32_t tmem = (map << 13u) + 0xF0000u;
        const std::uint32_t size = t.palette_entries > 16u ? 256u : 16u;  // GXTlutSize: entries / 16
        w.bp(0xFEFFFF00u);
        w.bp(0x0F000000u);
        w.bp(0x64u, static_cast<std::uint32_t>(t.palette >> 5u));
        w.bp(0x65u, ((tmem - 0x80000u) >> 9u) | (size / 16u) << 10u);
        w.bp(0xFEFFFF00u);
        w.bp(0x0F000000u);
        w.bp(kTexTlut[map], ((tmem - 0x80000u) >> 9u) | static_cast<std::uint32_t>(t.palette_format) << 10u);
    }
}

void coord_scale(Writer& w, const Material& m, unsigned coord) {
    const auto& s = m.tex_coord_scale[coord & 7u];
    w.bp(0xFE03FFFFu);
    w.bp(static_cast<std::uint8_t>(0x30u + 2u * (coord & 7u)),
         static_cast<std::uint32_t>(s.scale_s - 1u) | static_cast<std::uint32_t>(s.bias_s) << 16u);
    w.bp(static_cast<std::uint8_t>(0x31u + 2u * (coord & 7u)),
         static_cast<std::uint32_t>(s.scale_t - 1u) | static_cast<std::uint32_t>(s.bias_t) << 16u);
}

std::uint32_t order_half(const TevStage& s) {
    const std::uint32_t coord = s.tex_coord >= 8u ? 0u : s.tex_coord;
    const bool enable = s.tex_map != kNullMap;
    return static_cast<std::uint32_t>(s.tex_map & 7u) | coord << 3u | static_cast<std::uint32_t>(enable) << 6u |
           static_cast<std::uint32_t>(kChannelToRas[s.channel & 15u]) << 7u;
}

// TEV colours, then konst colours (J3DTevBlock*::load and diffTevReg).
void tev_registers(Writer& w, const Material& m) {
    if ((m.loads & kLoadsTevColor) != 0u) {
        // The material's three colours are registers 1-3 (register 0 is the previous stage's output).
        for (unsigned r = 0; r < 3u; ++r) {
            const auto c = [&](unsigned k) { return static_cast<std::uint32_t>(m.tev_color[r][k]) & 0x7FFu; };
            w.bp(static_cast<std::uint8_t>(0xE2u + 2u * r), c(0) | c(3) << 12u);
            for (unsigned n = 0; n < 3u; ++n) {
                w.bp(static_cast<std::uint8_t>(0xE3u + 2u * r), c(2) | c(1) << 12u);
            }
        }
    }
    if ((m.loads & kLoadsKonstColor) != 0u) {
        for (unsigned r = 0; r < 4u; ++r) {
            const std::uint8_t* c = m.konst_color[r];
            w.bp(static_cast<std::uint8_t>(0xE0u + 2u * r), c[0] | static_cast<std::uint32_t>(c[3]) << 12u | 1u << 23u);
            w.bp(static_cast<std::uint8_t>(0xE1u + 2u * r), c[2] | static_cast<std::uint32_t>(c[1]) << 12u | 1u << 23u);
        }
    }
}

void tev(Writer& w, const Material& m) {
    for (unsigned map = 0; map < 8u; ++map) {
        if (m.texture[map].image != 0u) {
            texture(w, map, m.texture[map]);
        }
    }
    for (unsigned i = 0; i < m.tev_stages; i += 2u) {
        w.bp(static_cast<std::uint8_t>(0x28u + i / 2u), order_half(m.tev[i]) | order_half(m.tev[i + 1u]) << 12u);
        coord_scale(w, m, m.tev[i].tex_coord);
        if (i + 1u < m.tev_stages || m.tev[i + 1u].tex_map != kNullMap) {
            coord_scale(w, m, m.tev[i + 1u].tex_coord);
        }
    }
    tev_registers(w, m);
    for (unsigned i = 0; i < m.tev_stages; ++i) {
        const TevStage& s = m.tev[i];
        w.bp(static_cast<std::uint8_t>(0xC0u + 2u * i),
             tev_op(s.color_op, s.color_bias, s.color_scale, s.color_clamp, s.color_reg) | s.color_in[3] |
                 static_cast<std::uint32_t>(s.color_in[2]) << 4u | static_cast<std::uint32_t>(s.color_in[1]) << 8u |
                 static_cast<std::uint32_t>(s.color_in[0]) << 12u);
        w.bp(static_cast<std::uint8_t>(0xC1u + 2u * i),
             tev_op(s.alpha_op, s.alpha_bias, s.alpha_scale, s.alpha_clamp, s.alpha_reg) | s.ras_swap |
                 static_cast<std::uint32_t>(s.tex_swap) << 2u | static_cast<std::uint32_t>(s.alpha_in[3]) << 4u |
                 static_cast<std::uint32_t>(s.alpha_in[2]) << 7u | static_cast<std::uint32_t>(s.alpha_in[1]) << 10u |
                 static_cast<std::uint32_t>(s.alpha_in[0]) << 13u);
        w.bp(static_cast<std::uint8_t>(0x10u + i),
             static_cast<std::uint32_t>(s.ind_stage) | static_cast<std::uint32_t>(s.ind_format) << 2u |
                 static_cast<std::uint32_t>(s.ind_bias) << 4u | static_cast<std::uint32_t>(s.ind_alpha) << 7u |
                 static_cast<std::uint32_t>(s.ind_matrix) << 9u | static_cast<std::uint32_t>(s.ind_wrap_s) << 13u |
                 static_cast<std::uint32_t>(s.ind_wrap_t) << 16u | static_cast<std::uint32_t>(s.ind_lod) << 19u |
                 static_cast<std::uint32_t>(s.ind_prev) << 20u);
    }
    if ((m.loads & kLoadsKonstSelect) != 0u) {
        for (unsigned i = 0; i < 16u; i += 4u) {
            const std::uint8_t* swap = m.swap_table[i / 4u];
            for (unsigned half = 0; half < 2u; ++half) {
                const TevStage& a = m.tev[i + 2u * half];
                const TevStage& b = m.tev[i + 2u * half + 1u];
                w.bp(static_cast<std::uint8_t>(0xF6u + (i + 2u * half) / 2u),
                     static_cast<std::uint32_t>(swap[2u * half]) | static_cast<std::uint32_t>(swap[2u * half + 1u]) << 2u |
                         static_cast<std::uint32_t>(a.konst_color) << 4u | static_cast<std::uint32_t>(a.konst_alpha) << 9u |
                         static_cast<std::uint32_t>(b.konst_color) << 14u | static_cast<std::uint32_t>(b.konst_alpha) << 19u);
            }
        }
    }
}

void indirect(Writer& w, const Material& m) {
    for (unsigned i = 0; i < m.ind_stages && i < 3u; ++i) {
        std::int32_t v[6];
        const auto& mt = m.ind_matrix[i];
        v[0] = static_cast<std::int32_t>(mt.m[0][0] * 1024.0f) & 0x7FF;
        v[1] = static_cast<std::int32_t>(mt.m[1][0] * 1024.0f) & 0x7FF;
        v[2] = static_cast<std::int32_t>(mt.m[0][1] * 1024.0f) & 0x7FF;
        v[3] = static_cast<std::int32_t>(mt.m[1][1] * 1024.0f) & 0x7FF;
        v[4] = static_cast<std::int32_t>(mt.m[0][2] * 1024.0f) & 0x7FF;
        v[5] = static_cast<std::int32_t>(mt.m[1][2] * 1024.0f) & 0x7FF;
        const auto scale = static_cast<std::int8_t>(mt.scale + 17);
        for (unsigned r = 0; r < 3u; ++r) {
            w.bp(static_cast<std::uint8_t>(6u + 3u * i + r), static_cast<std::uint32_t>(v[2u * r]) |
                                                                 static_cast<std::uint32_t>(v[2u * r + 1u]) << 11u |
                                                                 static_cast<std::uint32_t>((scale >> (2u * r)) & 3) << 22u);
        }
    }
    for (unsigned i = 0; i < m.ind_stages; i += 2u) {
        w.bp(static_cast<std::uint8_t>(0x25u + i / 2u),
             static_cast<std::uint32_t>(m.ind_order[i].scale_s) | static_cast<std::uint32_t>(m.ind_order[i].scale_t) << 4u |
                 static_cast<std::uint32_t>(m.ind_order[i + 1u].scale_s) << 8u |
                 static_cast<std::uint32_t>(m.ind_order[i + 1u].scale_t) << 12u);
    }
    for (unsigned i = 0; i < 4u; ++i) {
        coord_scale(w, m, m.ind_order[i].coord);
    }
    std::uint32_t order = 0;
    std::uint32_t mask = 0;
    for (unsigned i = 0; i < 4u; ++i) {
        order |= (static_cast<std::uint32_t>(m.ind_order[i].map & 7u) | static_cast<std::uint32_t>(m.ind_order[i].coord & 7u) << 3u)
                 << (6u * i);
        if (i < m.ind_stages) {
            mask |= 1u << (m.ind_order[i].map & 7u);
        }
    }
    w.bp(0x27u, order);
    w.bp(0x0Fu, mask);
}

void fog(Writer& w, const Material& m) {
    const auto& f = m.fog;
    float a, b, c;
    if (f.far_z == f.near_z || f.end == f.start) {
        a = 0.0f;
        b = 0.5f;
        c = 0.0f;
    } else {
        a = (f.far_z * f.near_z) / ((f.far_z - f.near_z) * (f.end - f.start));
        b = f.far_z / (f.far_z - f.near_z);
        c = f.start / (f.end - f.start);
    }
    float mant = b;
    std::uint32_t expn = 1;
    while (mant > 1.0) {
        mant *= 0.5f;
        ++expn;
    }
    while (mant > 0.0f && mant < 0.5) {
        mant *= 2.0f;
        --expn;
    }
    const float a_f = a / static_cast<float>(1u << expn);
    w.bp(0xEEu, std::bit_cast<std::uint32_t>(a_f) >> 12u);
    w.bp(0xEFu, static_cast<std::uint32_t>(8388638.0f * mant));
    w.bp(0xF0u, expn);
    w.bp(0xF1u, std::bit_cast<std::uint32_t>(c) >> 12u | static_cast<std::uint32_t>(f.type) << 21u);
    w.bp(0xF2u, f.color[2] | static_cast<std::uint32_t>(f.color[1]) << 8u | static_cast<std::uint32_t>(f.color[0]) << 16u);
    if (f.adjust_enable) {
        for (unsigned i = 0; i < 10u; i += 2u) {
            w.bp(static_cast<std::uint8_t>(0xE9u + i / 2u), f.adjust[i] | static_cast<std::uint32_t>(f.adjust[i + 1u]) << 12u);
        }
    }
    w.bp(0xE8u, static_cast<std::uint32_t>(f.adjust_center + 342u) | static_cast<std::uint32_t>(f.adjust_enable) << 10u);
}

void blend_and_z(Writer& w, const Material& m);

void pixel(Writer& w, const Material& m) {
    if ((m.loads & kLoadsFog) != 0u) {
        fog(w, m);
    }
    const auto& ac = m.alpha_compare;
    w.bp(0xF3u, ac.ref0 | static_cast<std::uint32_t>(ac.ref1) << 8u | static_cast<std::uint32_t>(ac.comp0) << 16u |
                    static_cast<std::uint32_t>(ac.comp1) << 19u | static_cast<std::uint32_t>(ac.op) << 22u);
    blend_and_z(w, m);
    w.bp(0xFE000040u);
    w.bp(0x43u, static_cast<std::uint32_t>(m.z_compare_location) << 6u);
}

// Blend (with the dither when the material sets it), then Z mode (J3DBlend::load, J3DZMode::load; diffBlend).
void blend_and_z(Writer& w, const Material& m) {
    const auto& bl = m.blend;
    const bool dither = (m.loads & kLoadsDither) != 0u;
    w.bp(dither ? 0xFE00FFE7u : 0xFE00FFE3u);
    // GXBlendMode: NONE 0, BLEND 1, LOGIC 2, SUBTRACT 3.
    w.bp(0x41u, static_cast<std::uint32_t>(bl.type == 1u || bl.type == 3u) | static_cast<std::uint32_t>(bl.type == 2u) << 1u |
                    (dither ? static_cast<std::uint32_t>(m.dither) << 2u : 0u) | static_cast<std::uint32_t>(bl.destination) << 5u |
                    static_cast<std::uint32_t>(bl.source) << 8u | static_cast<std::uint32_t>(bl.type == 3u) << 11u |
                    static_cast<std::uint32_t>(bl.logic_op) << 12u);
    w.bp(0x40u, static_cast<std::uint32_t>(m.z_mode.enable) | static_cast<std::uint32_t>(m.z_mode.func) << 1u |
                    static_cast<std::uint32_t>(m.z_mode.update) << 4u);
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

namespace {

std::uint32_t channel(const Material& m, unsigned i) {
    const auto& c = m.channel[i];
    // GXAttnFn: SPEC 0, SPOT 1, NONE 2.
    return static_cast<std::uint32_t>(c.material_source) | static_cast<std::uint32_t>(c.enable) << 1u |
           static_cast<std::uint32_t>(c.light_mask & 0xFu) << 2u | static_cast<std::uint32_t>(c.ambient_source) << 6u |
           static_cast<std::uint32_t>(c.attenuation == 0u ? 0u : c.diffuse) << 7u |
           static_cast<std::uint32_t>(c.attenuation != 2u) << 9u | static_cast<std::uint32_t>(c.attenuation != 0u) << 10u |
           static_cast<std::uint32_t>(c.light_mask >> 4u) << 11u;
}

void channels_and_lights(Writer& w, const Material& m);

void colour(Writer& w, const Material& m) {
    const std::uint32_t material[2] = {rgba(m.material_color[0]), rgba(m.material_color[1])};
    w.xf(0x100Cu, material, 2u);
    if ((m.loads & kLoadsAmbient) != 0u) {
        const std::uint32_t ambient[2] = {rgba(m.ambient_color[0]), rgba(m.ambient_color[1])};
        w.xf(0x100Au, ambient, 2u);
    }
    channels_and_lights(w, m);
}

// Channel controls (colour 0, alpha 0, colour 1, alpha 1 as J3D orders them) and lights (diffLight).
void channels_and_lights(Writer& w, const Material& m) {
    const std::uint32_t chans[4] = {channel(m, 0), channel(m, 2), channel(m, 1), channel(m, 3)};
    w.xf(0x100Eu, chans, 4u);
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
        w.xf(static_cast<std::uint16_t>(base + 0x0Au), pos, 3u);
        w.xf(static_cast<std::uint16_t>(base + 0x04u), attn, 6u);
        w.xf(static_cast<std::uint16_t>(base + 0x03u), rgba(l.color));
        w.xf(static_cast<std::uint16_t>(base + 0x0Du), dir, 3u);
    }
}

// Texture matrices (J3DTexMtx::load): the listed ones for a whole material, every present one for a differed list.
void texture_matrices(Writer& w, const Material& m, bool differed_list) {
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
        w.xf(base, words, static_cast<std::uint16_t>(count));
    }
}

// Texgens and their post-transform matrices (loadTexCoordGens).
void tex_gens(Writer& w, const Material& m) {
    if (m.tex_gens == 0u) {
        return;
    }
    std::uint32_t gens[8];
    std::uint32_t post[8];
    for (unsigned i = 0; i < m.tex_gens && i < 8u; ++i) {
        gens[i] = tex_gen_word(m.tex_gen[i].type, m.tex_gen[i].source);
        post[i] = m.tex_gen[i].post_matrix;
    }
    w.xf(0x1040u, gens, m.tex_gens);
    w.xf(0x1050u, post, m.tex_gens);
}

// J3DMaterial::diff's writes for an instance's differed list (J3DMatBlock.cpp diff bodies), in its order.
void differed(Writer& w, const Material& m) {
    const std::uint32_t f = m.diff_flags;
    if (((f >> 16u) & 15u) != 0u) {  // TexNoNum: diffTexNo
        for (unsigned map = 0; map < 8u; ++map) {
            if (m.texture[map].image != 0u) {
                texture(w, map, m.texture[map]);
            }
        }
    }
    if ((f & 0x4000000u) != 0u) {  // TexCoordScale
        for (unsigned i = 0; i < m.tev_stages; i += 2u) {
            coord_scale(w, m, m.tev[i].tex_coord);
            coord_scale(w, m, m.tev[i + 1u].tex_coord);
        }
    }
    if (((f >> 20u) & 15u) != 0u) {  // TevStageNum: diffTevStage, then diffTevStageIndirect
        for (unsigned i = 0; i < m.tev_stages; ++i) {
            const TevStage& s = m.tev[i];
            w.bp(static_cast<std::uint8_t>(0xC0u + 2u * i),
                 tev_op(s.color_op, s.color_bias, s.color_scale, s.color_clamp, s.color_reg) | s.color_in[3] |
                     static_cast<std::uint32_t>(s.color_in[2]) << 4u | static_cast<std::uint32_t>(s.color_in[1]) << 8u |
                     static_cast<std::uint32_t>(s.color_in[0]) << 12u);
            w.bp(static_cast<std::uint8_t>(0xC1u + 2u * i),
                 tev_op(s.alpha_op, s.alpha_bias, s.alpha_scale, s.alpha_clamp, s.alpha_reg) | s.ras_swap |
                     static_cast<std::uint32_t>(s.tex_swap) << 2u | static_cast<std::uint32_t>(s.alpha_in[3]) << 4u |
                     static_cast<std::uint32_t>(s.alpha_in[2]) << 7u | static_cast<std::uint32_t>(s.alpha_in[1]) << 10u |
                     static_cast<std::uint32_t>(s.alpha_in[0]) << 13u);
        }
        if ((f & 0x8000000u) != 0u) {
            for (unsigned i = 0; i < m.tev_stages; ++i) {
                const TevStage& s = m.tev[i];
                w.bp(static_cast<std::uint8_t>(0x10u + i),
                     static_cast<std::uint32_t>(s.ind_stage) | static_cast<std::uint32_t>(s.ind_format) << 2u |
                         static_cast<std::uint32_t>(s.ind_bias) << 4u | static_cast<std::uint32_t>(s.ind_alpha) << 7u |
                         static_cast<std::uint32_t>(s.ind_matrix) << 9u | static_cast<std::uint32_t>(s.ind_wrap_s) << 13u |
                         static_cast<std::uint32_t>(s.ind_wrap_t) << 16u | static_cast<std::uint32_t>(s.ind_lod) << 19u |
                         static_cast<std::uint32_t>(s.ind_prev) << 20u);
            }
        }
    }
    if ((f & 0x1000000u) != 0u) {  // TevReg: diffTevReg (TEV colours, then konst colours)
        tev_registers(w, m);
    }
    if ((f & 0x10000000u) != 0u && (m.loads & kLoadsFog) != 0u) {  // Fog
        fog(w, m);
    }
    if ((f & 0x20000000u) != 0u) {  // Blend: diffBlend (blend, then Z mode)
        blend_and_z(w, m);
    }
    if ((f & 0x2000000u) != 0u) {  // KonstColor: GenMode_3Param and the texgen count
        w.bp(0xFE073C0Fu);
        w.bp(0x00u, static_cast<std::uint32_t>(m.tex_gens) | static_cast<std::uint32_t>(m.tev_stages - 1u) << 10u |
                        static_cast<std::uint32_t>(m.ind_stages) << 16u);
        w.xf(0x103Fu, m.tex_gens);
    }
    if (((f >> 8u) & 15u) != 0u) {  // TexGenNum: diffTexMtx, then (TexGen) diffTexGen
        texture_matrices(w, m, true);
        if ((f & 0x1000u) != 0u) {
            tex_gens(w, m);
        }
    }
    if ((f & 0x1u) != 0u) {  // MatColor
        const std::uint32_t material[2] = {rgba(m.material_color[0]), rgba(m.material_color[1])};
        w.xf(0x100Cu, material, 2u);
    }
    if ((f & 0x2u) != 0u || ((f >> 4u) & 15u) != 0u) {  // ColorChan or LightObjNum: diffLight
        channels_and_lights(w, m);
    }
}

}  // namespace

void apply(const Material& m, GxState& state, FifoSink* actions) {
    Writer w{state, actions};
    if (m.diff_flags != 0u) {
        differed(w, m);
        return;
    }
    if ((m.loads & kLoadsTev) != 0u) {
        tev(w, m);
    }
    if ((m.loads & kLoadsIndirect) != 0u) {
        indirect(w, m);
    }
    pixel(w, m);
    w.bp(0xFE07FC3Fu);
    w.bp(0x00u, static_cast<std::uint32_t>(m.tex_gens) | static_cast<std::uint32_t>(m.channels) << 4u |
                    static_cast<std::uint32_t>(m.tev_stages - 1u) << 10u | static_cast<std::uint32_t>(kCull[m.cull & 3u]) << 14u |
                    static_cast<std::uint32_t>(m.ind_stages) << 16u);
    texture_matrices(w, m, false);
    tex_gens(w, m);
    if ((m.loads & kLoadsChannels) != 0u) {
        colour(w, m);
    }
    w.xf(0x1009u, m.channels);
    w.xf(0x103Fu, m.tex_gens);
}

}  // namespace galaxy::gx::material
