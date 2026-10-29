// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/native_state.h"

#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/native_material.h"

#include <bit>

namespace galaxy::gx::state {

namespace {

constexpr std::uint8_t kCullToHw[4] = {0, 2, 1, 3};
constexpr std::uint8_t kChannelToRas[16] = {0, 1, 0, 1, 0, 1, 7, 5, 6, 0, 0, 0, 0, 0, 0, 7};
constexpr std::uint8_t kFilter[6] = {0x00, 0x04, 0x01, 0x05, 0x02, 0x06};
constexpr std::uint8_t kTexMode0[8] = {0x80, 0x81, 0x82, 0x83, 0xA0, 0xA1, 0xA2, 0xA3};
constexpr std::uint8_t kTexMode1[8] = {0x84, 0x85, 0x86, 0x87, 0xA4, 0xA5, 0xA6, 0xA7};
constexpr std::uint8_t kTexImage0[8] = {0x88, 0x89, 0x8A, 0x8B, 0xA8, 0xA9, 0xAA, 0xAB};
constexpr std::uint8_t kTexImage3[8] = {0x94, 0x95, 0x96, 0x97, 0xB4, 0xB5, 0xB6, 0xB7};
constexpr std::uint8_t kTexTlutReg[8] = {0x98, 0x99, 0x9A, 0x9B, 0xB8, 0xB9, 0xBA, 0xBB};

// GXInit's palette places in TMEM (in 512-byte units from its high half): 16 of 256 entries, then 4 of 1024.
std::uint32_t tlut_offset(std::uint8_t name) {
    return name < 16u ? 0x200u + 0x10u * name : 0x300u + 0x40u * ((name - 16u) & 3u);
}

void bp(Writes& s, std::uint8_t id, std::uint32_t value) {
    s.bp(static_cast<std::uint32_t>(id) << 24u | (value & 0xFFFFFFu));
}

void mask(Writes& s, std::uint32_t bits) {
    s.bp(0xFE000000u | (bits & 0xFFFFFFu));
}

void xf(Writes& s, std::uint16_t address, std::uint32_t value) {
    s.xf(address, value);
}

// A TEV operation's fields as GX packs them (compare operations: bias 3, their kind in the scale field).
std::uint32_t tev_op(std::uint8_t op, std::uint8_t bias, std::uint8_t scale, std::uint8_t clamp, std::uint8_t out) {
    const bool compare = op > 1u;
    return (compare ? 3u : bias) << 16u | static_cast<std::uint32_t>(op & 1u) << 18u |
           static_cast<std::uint32_t>(clamp) << 19u | (compare ? (op >> 1u) & 3u : scale) << 20u |
           static_cast<std::uint32_t>(out) << 22u;
}

std::uint32_t gen_mode(std::uint32_t texgens, std::uint32_t chans, std::uint32_t tevs, std::uint32_t cull,
                       std::uint32_t inds) {
    return texgens | chans << 4u | (tevs - 1u) << 10u | cull << 14u | inds << 16u;
}

std::uint32_t rgba(const std::uint8_t* c) {
    return static_cast<std::uint32_t>(c[0]) << 24u | static_cast<std::uint32_t>(c[1]) << 16u |
           static_cast<std::uint32_t>(c[2]) << 8u | c[3];
}

std::uint32_t order_half(std::uint8_t coord, std::uint8_t map, std::uint8_t chan) {
    const bool enable = map != 0xFFu;
    return static_cast<std::uint32_t>(map & 7u) | static_cast<std::uint32_t>(coord & 7u) << 3u |
           static_cast<std::uint32_t>(enable) << 6u | static_cast<std::uint32_t>(kChannelToRas[chan & 15u]) << 7u;
}

std::uint32_t blend_word(const std::uint8_t* a, bool updates) {
    // GXBlendMode: NONE 0, BLEND 1, LOGIC 2, SUBTRACT 3.
    const std::uint32_t type = a[0];
    std::uint32_t w = static_cast<std::uint32_t>(type == 1u || type == 3u) | static_cast<std::uint32_t>(type == 2u) << 1u |
                      static_cast<std::uint32_t>(a[2]) << 5u | static_cast<std::uint32_t>(a[1]) << 8u |
                      static_cast<std::uint32_t>(type == 3u) << 11u | static_cast<std::uint32_t>(a[3]) << 12u;
    if (updates) {
        w |= static_cast<std::uint32_t>(a[6]) << 2u | static_cast<std::uint32_t>(a[4]) << 3u |
             static_cast<std::uint32_t>(a[5]) << 4u;
    }
    return w;
}

// GXSetFog's registers (GDSetFog's arithmetic: a projected fog takes the orthographic form).
// GXSetTexCopyDst's tiles: the format's tile shifts (row, column) and whether it takes two tiles a block.
void copy_tiles(std::uint8_t format, unsigned& row_shift, unsigned& column_shift) {
    switch (format) {
    case 0x00: case 0x08: case 0x0E: case 0x20: case 0x30:  // I4, C4, CMPR, R4, Z4
        row_shift = 3;
        column_shift = 3;
        break;
    case 0x01: case 0x02: case 0x09: case 0x11: case 0x22: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x39:
    case 0x3A:  // I8, IA4, C8, Z8, RA4, A8, R8, G8, B8, Z8M, Z8L
        row_shift = 3;
        column_shift = 2;
        break;
    case 0x03: case 0x04: case 0x05: case 0x06: case 0x0A: case 0x13: case 0x16: case 0x23: case 0x2B: case 0x2C:
    case 0x3C:  // IA8, RGB565, RGB5A3, RGBA8, C14X2, Z16, Z24X8, RA8, RG8, GB8, Z16L
        row_shift = 2;
        column_shift = 2;
        break;
    default:
        row_shift = 0;
        column_shift = 0;
        break;
    }
}

// GXCopyDisp / GXCopyTex with the live state where they read the SDK's shadows: a clear runs with z always passing and
// blending off, the depth test moves after texturing when the copy needs it, then the state comes back.
void copy(Writes& s, const Op& op) {
    const std::uint8_t* a = op.a;
    const bool display = a[0] != 0u, clear = a[1] != 0u;
    const std::uint32_t zmode = s.bp_read(0x40u), cmode0 = s.bp_read(0x41u), pe_control = s.bp_read(0x43u);
    if (clear) {
        bp(s, 0x40u, (zmode & ~0xFu) | 1u | 7u << 1u);
        bp(s, 0x41u, cmode0 & ~3u);
    }
    std::uint32_t temp = pe_control;
    bool change = false;
    if (!display && (a[2] & 0x10u) != 0u && (temp & 7u) != 3u) {
        change = true;
        temp = (temp & ~7u) | 3u;
    }
    if ((clear || (temp & 7u) == 3u) && ((temp >> 6u) & 1u) == 1u) {
        change = true;
        temp &= ~(1u << 6u);
    }
    if (change) {
        bp(s, 0x43u, temp);
    }
    const auto left = static_cast<std::uint32_t>(op.f[0]), top = static_cast<std::uint32_t>(op.f[1]);
    const auto width = static_cast<std::uint32_t>(op.f[2]), height = static_cast<std::uint32_t>(op.f[3]);
    bp(s, 0x49u, (left & 0x3FFu) | (top & 0x3FFu) << 10u);
    bp(s, 0x4Au, ((width - 1u) & 0x3FFu) | ((height - 1u) & 0x3FFu) << 10u);
    std::uint32_t command = static_cast<std::uint32_t>(a[4] & 1u) | static_cast<std::uint32_t>(a[5] & 1u) << 1u |
                            static_cast<std::uint32_t>(clear) << 11u | static_cast<std::uint32_t>(a[6] & 3u) << 12u;
    if (display) {
        bp(s, 0x4Du, ((op.size[0] * 2u) >> 5u) & 0x3FFu);
        command |= static_cast<std::uint32_t>(a[8] & 3u) << 7u | static_cast<std::uint32_t>(a[7] & 1u) << 10u | 1u << 14u;
    } else {
        const std::uint8_t format = a[2];
        unsigned row_shift = 0, column_shift = 0;
        copy_tiles(format, row_shift, column_shift);
        const std::uint32_t w = op.size[0] != 0u ? op.size[0] : 1u;
        const std::uint32_t row_tiles = (w + (1u << row_shift) - 1u) >> row_shift;
        const std::uint32_t blocks = format == 0x06u || format == 0x16u ? 2u : 1u;
        bp(s, 0x4Du, (row_tiles * blocks) & 0x3FFu);
        const std::uint32_t pe_format = format == 0x13u ? 0xBu : format & 0xFu;
        const bool intensity = format == 0x00u || format == 0x01u || format == 0x02u || format == 0x03u || format == 0x26u;
        command |= ((pe_format >> 3u) & 1u) << 3u | (pe_format & 7u) << 4u | static_cast<std::uint32_t>(a[3] & 1u) << 9u |
                   (intensity ? 3u : 2u) << 15u;
    }
    bp(s, 0x4Bu, static_cast<std::uint32_t>(op.image >> 5u) & 0xFFFFFFu);
    bp(s, 0x52u, command);
    if (clear) {
        bp(s, 0x40u, zmode);
        bp(s, 0x41u, cmode0);
    }
    if (change) {
        bp(s, 0x43u, pe_control);
    }
}

void fog(Writes& s, const Op& op) {
    const std::uint32_t type = op.a[0];
    const float start = op.f[0], end = op.f[1], near_z = op.f[2], far_z = op.f[3];
    const std::uint32_t proj = (type >> 3u) & 1u;
    float a_f, c;
    std::uint32_t b_expn = 0u, b_m = 0u;
    if (proj != 0u) {
        if (far_z == near_z || end == start) {
            a_f = 0.0f;
            c = 0.0f;
        } else {
            const float a = 1.0f / (end - start);
            a_f = (far_z - near_z) * a;
            c = (start - near_z) * a;
        }
    } else {
        float a, b;
        if (far_z == near_z || end == start) {
            a = 0.0f;
            b = 0.5f;
            c = 0.0f;
        } else {
            a = (far_z * near_z) / ((far_z - near_z) * (end - start));
            b = far_z / (far_z - near_z);
            c = start / (end - start);
        }
        float mant = b;
        b_expn = 1u;
        while (mant > 1.0) {
            mant /= 2;
            ++b_expn;
        }
        while (mant > 0 && mant < 0.5) {
            mant *= 2;
            --b_expn;
        }
        a_f = a / static_cast<float>(1u << b_expn);
        b_m = static_cast<std::uint32_t>(mant * 8388638);
    }
    bp(s, 0xEEu, std::bit_cast<std::uint32_t>(a_f) >> 12u);
    if (proj != 0u && op.a[5] != 0u) {
        // GXSetFog leaves these two words without their register id for a projected fog: register 0 takes them.
        s.bp(0u);
        s.bp(0u);
    } else {
        bp(s, 0xEFu, b_m);
        bp(s, 0xF0u, b_expn);
    }
    bp(s, 0xF1u, std::bit_cast<std::uint32_t>(c) >> 12u | proj << 20u | (type & 7u) << 21u);
    bp(s, 0xF2u, op.a[3] | static_cast<std::uint32_t>(op.a[2]) << 8u | static_cast<std::uint32_t>(op.a[1]) << 16u);
}

}  // namespace

void encode(std::span<const Op> ops, Writes& s) {
    for (const Op& op : ops) {
        const std::uint8_t* a = op.a;
        switch (op.kind) {
        case kBlendEtc:
            bp(s, 0x41u, blend_word(a, true));
            break;
        case kBlend:
            mask(s, 0xFFE3u);
            bp(s, 0x41u, blend_word(a, false));
            break;
        case kZMode:
            bp(s, 0x40u, static_cast<std::uint32_t>(a[0]) | static_cast<std::uint32_t>(a[1]) << 1u |
                             static_cast<std::uint32_t>(a[2]) << 4u);
            break;
        case kAlphaCompare:
            bp(s, 0xF3u, static_cast<std::uint32_t>(a[1]) | static_cast<std::uint32_t>(a[4]) << 8u |
                             static_cast<std::uint32_t>(a[0]) << 16u | static_cast<std::uint32_t>(a[3]) << 19u |
                             static_cast<std::uint32_t>(a[2]) << 22u);
            break;
        case kDstAlpha:
            bp(s, 0x42u, static_cast<std::uint32_t>(a[1]) | static_cast<std::uint32_t>(a[0]) << 8u);
            break;
        case kCull:
            mask(s, 0x3u << 14u);
            bp(s, 0x00u, static_cast<std::uint32_t>(kCullToHw[a[0] & 3u]) << 14u);
            break;
        case kGenMode:
            mask(s, 0xFu | 0x3u << 4u | 0xFu << 10u);
            bp(s, 0x00u, gen_mode(a[0], a[1], a[2], 0u, 0u));
            xf(s, 0x1009u, a[1]);
            xf(s, 0x103Fu, a[0]);
            break;
        case kGenMode2:
            mask(s, 0xFu | 0x3u << 4u | 0xFu << 10u | 0x3u << 14u | 0x7u << 16u);
            bp(s, 0x00u, gen_mode(a[0], a[1], a[2], kCullToHw[a[4] & 3u], a[3]));
            xf(s, 0x1009u, a[1]);
            xf(s, 0x103Fu, a[0]);
            break;
        case kChannel: {
            // GXAttnFn: SPEC 0, SPOT 1, NONE 2.
            const std::uint32_t attn = a[6];
            const std::uint32_t word = static_cast<std::uint32_t>(a[3]) | static_cast<std::uint32_t>(a[1]) << 1u |
                                       static_cast<std::uint32_t>(a[4] & 15u) << 2u | static_cast<std::uint32_t>(a[2]) << 6u |
                                       (attn == 0u ? 0u : static_cast<std::uint32_t>(a[5])) << 7u |
                                       static_cast<std::uint32_t>(attn != 2u) << 9u | static_cast<std::uint32_t>(attn != 0u) << 10u |
                                       static_cast<std::uint32_t>(a[4] >> 4u) << 11u;
            xf(s, static_cast<std::uint16_t>(0x100Eu + (a[0] & 3u)), word);
            if (a[0] == 4u || a[0] == 5u) {  // GX_COLOR0A0, GX_COLOR1A1: the alpha channel too
                xf(s, static_cast<std::uint16_t>(0x100Eu + (a[0] - 2u)), word);
            }
            break;
        }
        case kTevColorCalc:
            bp(s, static_cast<std::uint8_t>(0xC0u + 2u * a[0]),
               tev_op(a[5], a[6], a[7], a[8], a[9]) | a[4] | static_cast<std::uint32_t>(a[3]) << 4u |
                   static_cast<std::uint32_t>(a[2]) << 8u | static_cast<std::uint32_t>(a[1]) << 12u);
            break;
        case kTevAlphaCalc:
            bp(s, static_cast<std::uint8_t>(0xC1u + 2u * a[0]),
               tev_op(a[5], a[6], a[7], a[8], a[9]) | a[10] | static_cast<std::uint32_t>(a[11]) << 2u |
                   static_cast<std::uint32_t>(a[4]) << 4u | static_cast<std::uint32_t>(a[3]) << 7u |
                   static_cast<std::uint32_t>(a[2]) << 10u | static_cast<std::uint32_t>(a[1]) << 13u);
            break;
        case kTevDirect:
            bp(s, static_cast<std::uint8_t>(0x10u + a[0]), 0u);
            break;
        case kTevOrder:
            bp(s, static_cast<std::uint8_t>(0x28u + a[0] / 2u), order_half(a[1], a[2], a[3]) | order_half(a[4], a[5], a[6]) << 12u);
            break;
        case kTevColor: {
            const std::uint32_t ra = a[1] | static_cast<std::uint32_t>(a[4]) << 12u;
            const std::uint32_t bg = a[3] | static_cast<std::uint32_t>(a[2]) << 12u;
            bp(s, static_cast<std::uint8_t>(0xE0u + 2u * a[0]), ra);
            for (unsigned n = 0; n < 3u; ++n) {
                bp(s, static_cast<std::uint8_t>(0xE1u + 2u * a[0]), bg);
            }
            break;
        }
        case kMaterialColor:
        case kAmbientColor: {
            const auto address = static_cast<std::uint16_t>((op.kind == kMaterialColor ? 0x100Cu : 0x100Au) + (a[0] & 1u));
            std::uint32_t value = rgba(a + 1);
            if (a[5] != 0u && a[0] < 4u) {
                // GXSetChan*Color: GX_COLOR0/1 keep the alpha, GX_ALPHA0/1 keep the colour.
                const std::uint32_t own = a[0] < 2u ? 0xFFFFFF00u : 0x000000FFu;
                value = (value & own) | (s.xf_read(address) & ~own);
            }
            xf(s, address, value);
            break;
        }
        case kTexCoordGen: {
            std::uint32_t gen = material::tex_gen_word(a[1], a[2]);
            if (a[5] != 0u && (a[1] < 2u || a[1] > 9u)) {
                gen &= ~(0x7u << 12u);  // GXSetTexCoordGen2 sets the bump source only for a bump texgen.
            }
            xf(s, static_cast<std::uint16_t>(0x1040u + a[0]), gen);
            xf(s, static_cast<std::uint16_t>(0x1050u + a[0]),
               static_cast<std::uint32_t>(static_cast<std::int32_t>(a[4]) - 64) | static_cast<std::uint32_t>(a[3]) << 8u);
            if (a[5] != 0u) {
                // GXSetTexCoordGen2 also sets the coordinate's matrix index: six bits of matrix index A (coordinates
                // 0-3, from bit 6) or B (4-7), in CP 0x30/0x40 and their XF copies 0x1018/0x1019.
                const bool b = a[0] >= 4u;
                const unsigned shift = b ? 6u * (a[0] - 4u) : 6u + 6u * a[0];
                const std::uint32_t field = 0x3Fu << shift, value = (static_cast<std::uint32_t>(a[6]) << shift) & field;
                const std::uint8_t reg = b ? 0x40u : 0x30u;
                const auto address = static_cast<std::uint16_t>(b ? 0x1019u : 0x1018u);
                s.cp(reg, (s.cp_read(reg) & ~field) | value);
                xf(s, address, (s.xf_read(address) & ~field) | value);
            }
            break;
        }
        case kTexLookup: {
            const unsigned map = a[0] & 7u;
            bp(s, kTexMode0[map],
               static_cast<std::uint32_t>(a[1]) | static_cast<std::uint32_t>(a[2]) << 2u |
                   static_cast<std::uint32_t>(a[4] == 1u) << 4u | static_cast<std::uint32_t>(kFilter[a[3] % 6u]) << 5u |
                   static_cast<std::uint32_t>(!a[6]) << 8u |
                   static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int32_t>(32.0f * op.f[2]))) << 9u |
                   static_cast<std::uint32_t>(a[7]) << 19u | static_cast<std::uint32_t>(a[5]) << 21u);
            bp(s, kTexMode1[map],
               static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int32_t>(16.0f * op.f[0]))) |
                   static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int32_t>(16.0f * op.f[1]))) << 8u);
            break;
        }
        case kTexImageAttr:
            bp(s, kTexImage0[a[0] & 7u], static_cast<std::uint32_t>(op.size[0] - 1u) |
                                             static_cast<std::uint32_t>(op.size[1] - 1u) << 10u |
                                             static_cast<std::uint32_t>(a[1]) << 20u);
            break;
        case kTexImage:
            bp(s, kTexImage3[a[0] & 7u], static_cast<std::uint32_t>(op.image >> 5u));
            break;
        case kFog:
            fog(s, op);
            break;
        // Partial setters: a write mask, then the fields (the register's other fields stay).
        case kTevColorIn:
            mask(s, 0x00FFFFu);
            bp(s, static_cast<std::uint8_t>(0xC0u + 2u * a[0]),
               a[4] | static_cast<std::uint32_t>(a[3]) << 4u | static_cast<std::uint32_t>(a[2]) << 8u |
                   static_cast<std::uint32_t>(a[1]) << 12u);
            break;
        case kTevAlphaIn:
            mask(s, 0x00FFF0u);
            bp(s, static_cast<std::uint8_t>(0xC1u + 2u * a[0]),
               static_cast<std::uint32_t>(a[4]) << 4u | static_cast<std::uint32_t>(a[3]) << 7u |
                   static_cast<std::uint32_t>(a[2]) << 10u | static_cast<std::uint32_t>(a[1]) << 13u);
            break;
        case kTevColorOp:
        case kTevAlphaOp:
            mask(s, 0xFF0000u);
            bp(s, static_cast<std::uint8_t>((op.kind == kTevColorOp ? 0xC0u : 0xC1u) + 2u * a[0]),
               tev_op(a[1], a[2], a[3], a[4], a[5]));
            break;
        case kTevKColorSel:
            mask(s, (a[0] & 1u) != 0u ? 0x07C000u : 0x0001F0u);
            bp(s, static_cast<std::uint8_t>(0xF6u + a[0] / 2u),
               static_cast<std::uint32_t>(a[1]) << ((a[0] & 1u) != 0u ? 14u : 4u));
            break;
        case kTevKAlphaSel:
            mask(s, (a[0] & 1u) != 0u ? 0xF80000u : 0x003E00u);
            bp(s, static_cast<std::uint8_t>(0xF6u + a[0] / 2u),
               static_cast<std::uint32_t>(a[1]) << ((a[0] & 1u) != 0u ? 19u : 9u));
            break;
        case kNumTexGens:
            mask(s, 0x00000Fu);
            bp(s, 0x00u, a[0]);
            xf(s, 0x103Fu, a[0]);
            break;
        case kNumChans:
            mask(s, 0x000070u);
            bp(s, 0x00u, static_cast<std::uint32_t>(a[0]) << 4u);
            xf(s, 0x1009u, a[0]);
            break;
        case kNumTevStages:
            mask(s, 0x003C00u);
            bp(s, 0x00u, static_cast<std::uint32_t>(a[0] - 1u) << 10u);
            break;
        case kNumIndStages:
            mask(s, 0x070000u);
            bp(s, 0x00u, static_cast<std::uint32_t>(a[0]) << 16u);
            break;
        case kCoPlanar:
            mask(s, 0x080000u);
            bp(s, 0x00u, static_cast<std::uint32_t>(a[0]) << 19u);
            break;
        case kZCompLoc:
            mask(s, 0x000040u);
            bp(s, 0x43u, static_cast<std::uint32_t>(a[0]) << 6u);
            break;
        case kCurrentMatrix: {
            // The position/normal matrix index: the low six bits of matrix index A (CP 0x30, its XF copy 0x1018).
            const std::uint32_t value = (s.cp_read(0x30u) & ~0x3Fu) | (a[0] & 0x3Fu);
            s.cp(0x30u, value);
            xf(s, 0x1018u, (s.xf_read(0x1018u) & ~0x3Fu) | (a[0] & 0x3Fu));
            break;
        }
        case kTevIndirect:
            bp(s, static_cast<std::uint8_t>(0x10u + a[0]),
               static_cast<std::uint32_t>(a[1] & 3u) | static_cast<std::uint32_t>(a[2] & 3u) << 2u |
                   static_cast<std::uint32_t>(a[3] & 7u) << 4u | static_cast<std::uint32_t>(a[9] & 3u) << 7u |
                   static_cast<std::uint32_t>(a[4] & 15u) << 9u | static_cast<std::uint32_t>(a[5] & 7u) << 13u |
                   static_cast<std::uint32_t>(a[6] & 7u) << 16u | static_cast<std::uint32_t>(a[8] & 1u) << 19u |
                   static_cast<std::uint32_t>(a[7] & 1u) << 20u);
            break;
        case kIndTexScale: {
            // Two stages per register (0x25: stages 0-1, 0x26: 2-3), s and t four bits each.
            const unsigned shift = (a[0] & 1u) * 8u;
            mask(s, 0xFFu << shift);
            bp(s, static_cast<std::uint8_t>(0x25u + (a[0] >> 1u)),
               (static_cast<std::uint32_t>(a[1] & 15u) | static_cast<std::uint32_t>(a[2] & 15u) << 4u) << shift);
            break;
        }
        case kLineWidth:
            mask(s, 0x0700FFu);
            bp(s, 0x22u, static_cast<std::uint32_t>(a[0]) | static_cast<std::uint32_t>(a[1] & 7u) << 16u);
            break;
        case kPointSize:
            mask(s, 0x38FF00u);
            bp(s, 0x22u, static_cast<std::uint32_t>(a[0]) << 8u | static_cast<std::uint32_t>(a[1] & 7u) << 19u);
            break;
        case kLightAttenuation: {
            // XF light memory: 16 words a light from 0x600; three reserved, the colour, a0-a2, k0-k2.
            const auto base = static_cast<std::uint16_t>(0x600u + 16u * (a[0] & 7u));
            for (unsigned i = 0; i < 3u; ++i) {
                xf(s, static_cast<std::uint16_t>(base + i), 0u);
            }
            xf(s, static_cast<std::uint16_t>(base + 3u), rgba(a + 1));
            for (unsigned i = 0; i < 6u; ++i) {
                xf(s, static_cast<std::uint16_t>(base + 4u + i), std::bit_cast<std::uint32_t>(op.f[i]));
            }
            break;
        }
        case kLightPosition: {
            const auto base = static_cast<std::uint16_t>(0x600u + 16u * (a[0] & 7u));
            for (unsigned i = 0; i < 6u; ++i) {
                xf(s, static_cast<std::uint16_t>(base + 10u + i), std::bit_cast<std::uint32_t>(op.f[i]));
            }
            break;
        }
        case kPixelFormat:
            if (a[0] != 0u) {
                mask(s, 0x00003Fu);
                bp(s, 0x43u, static_cast<std::uint32_t>(a[1] & 7u) | static_cast<std::uint32_t>(a[2] & 7u) << 3u);
                mask(s, 0x000200u);
                bp(s, 0x00u, static_cast<std::uint32_t>(a[3] & 1u) << 9u);
            }
            if (a[4] != 0u) {
                mask(s, 0x000600u);
                bp(s, 0x42u, static_cast<std::uint32_t>(a[5] & 3u) << 9u);
            }
            break;
        case kInvalidateTextures:
            // GXInvalidateTexAll's two tag invalidations (TMEM 0 and 256, eight lines each).
            bp(s, 0x66u, 8u << 9u);
            bp(s, 0x66u, 256u | 8u << 9u);
            break;
        case kZTexture: {
            // GXSetZTexture: the format as its type (Z8 0, Z16 1, otherwise 2).
            const std::uint32_t type = a[1] == 0x11u ? 0u : a[1] == 0x13u ? 1u : 2u;
            bp(s, 0xF4u, static_cast<std::uint32_t>(a[2]) | static_cast<std::uint32_t>(a[3]) << 8u |
                             static_cast<std::uint32_t>(a[4]) << 16u);
            bp(s, 0xF5u, type | static_cast<std::uint32_t>(a[0] & 3u) << 2u);
            break;
        }
        case kCopyClear:
            bp(s, 0x4Fu, static_cast<std::uint32_t>(a[0]) | static_cast<std::uint32_t>(a[3]) << 8u);
            bp(s, 0x50u, static_cast<std::uint32_t>(a[2]) | static_cast<std::uint32_t>(a[1]) << 8u);
            bp(s, 0x51u, static_cast<std::uint32_t>(a[4]) | static_cast<std::uint32_t>(a[5]) << 8u |
                             static_cast<std::uint32_t>(a[6]) << 16u);
            break;
        case kCopySamples:
            // GXSetCopyFilter's sample locations (BP 0x01-0x04, three points each); 6 everywhere without antialiasing.
            for (unsigned r = 0; r < 4u; ++r) {
                std::uint32_t word = 0;
                for (unsigned p = 0; p < 3u; ++p) {
                    const unsigned i = 3u * r + p;
                    const std::uint32_t x = a[0] != 0u ? a[1u + i] & 15u : 6u;
                    const std::uint32_t y = a[0] != 0u ? a[1u + i] >> 4u : 6u;
                    word |= (x | y << 4u) << (8u * p);
                }
                bp(s, static_cast<std::uint8_t>(1u + r), word);
            }
            break;
        case kCopyVfilter: {
            // Its coefficients, or GXSetCopyFilter's pass-through ones (0 0 21 22 21 0 0) without the filter.
            static constexpr std::uint8_t kOff[7] = {0, 0, 21, 22, 21, 0, 0};
            const std::uint8_t* c = a[0] != 0u ? a + 1 : kOff;
            bp(s, 0x53u, static_cast<std::uint32_t>(c[0] & 63u) | static_cast<std::uint32_t>(c[1] & 63u) << 6u |
                             static_cast<std::uint32_t>(c[2] & 63u) << 12u | static_cast<std::uint32_t>(c[3] & 63u) << 18u);
            bp(s, 0x54u, static_cast<std::uint32_t>(c[4] & 63u) | static_cast<std::uint32_t>(c[5] & 63u) << 6u |
                             static_cast<std::uint32_t>(c[6] & 63u) << 12u);
            break;
        }
        case kCopyYScale:
            bp(s, 0x4Eu, op.size[0] & 0x1FFu);
            break;
        case kCopy:
            copy(s, op);
            break;
        case kFogRangeTable:
            bp(s, static_cast<std::uint8_t>(0xE9u + (a[0] % 5u)),
               (op.size[0] & 0xFFFu) | static_cast<std::uint32_t>(op.size[1] & 0xFFFu) << 12u);
            break;
        case kFogRange:
            // GXSetFogRangeAdj: the centre offset by 342, the enable above it.
            bp(s, 0xE8u, ((op.size[0] + 342u) & 0x3FFu) | static_cast<std::uint32_t>(a[0] & 1u) << 10u);
            break;
        case kTexTlut:
            bp(s, kTexTlutReg[a[0] & 7u], tlut_offset(a[2]) | static_cast<std::uint32_t>(a[1] & 3u) << 10u);
            break;
        case kTlutLoad:
            // GXLoadTlut: the palette's address, then the place and its size (which starts the load).
            bp(s, 0x64u, static_cast<std::uint32_t>(op.image >> 5u) & 0xFFFFFFu);
            bp(s, 0x65u, tlut_offset(a[0]) | (a[0] < 16u ? 16u : 64u) << 10u);
            break;
        case kIndTexOrder: {
            // GXSetIndTexOrder: a null map or coordinate reads as 0; six bits a stage.
            const std::uint32_t map = a[2] >= 8u ? 0u : a[2], coord = a[1] >= 8u ? 0u : a[1];
            const unsigned shift = 6u * (a[0] & 3u);
            mask(s, 0x3Fu << shift);
            bp(s, 0x27u, (map | coord << 3u) << shift);
            break;
        }
        case kTexOffsets:
            mask(s, 3u << 18u);
            bp(s, static_cast<std::uint8_t>(0x30u + 2u * (a[0] & 7u)),
               static_cast<std::uint32_t>(a[1] & 1u) << 18u | static_cast<std::uint32_t>(a[2] & 1u) << 19u);
            break;
        case kTevSwapTable:
            // The low four bits of the table's two konst-select registers: red, green / blue, alpha.
            mask(s, 0x00000Fu);
            bp(s, static_cast<std::uint8_t>(0xF6u + 2u * a[0]), a[1] | static_cast<std::uint32_t>(a[2]) << 2u);
            mask(s, 0x00000Fu);
            bp(s, static_cast<std::uint8_t>(0xF7u + 2u * a[0]), a[3] | static_cast<std::uint32_t>(a[4]) << 2u);
            break;
        case kTevSwapMode:
            mask(s, 0x00000Fu);
            bp(s, static_cast<std::uint8_t>(0xC1u + 2u * a[0]), a[1] | static_cast<std::uint32_t>(a[2]) << 2u);
            break;
        case kTevStageOrder: {
            // GXSetTevOrder's mapping (not GD's): a null coordinate or map reads as 0, the map enabled only when real.
            const std::uint32_t coord = a[1] >= 8u ? 0u : a[1];
            const std::uint32_t map = a[2] >= 8u ? 0u : a[2];
            const std::uint32_t half = map | coord << 3u | static_cast<std::uint32_t>(a[2] != 0xFFu && a[4] == 0u) << 6u |
                                       static_cast<std::uint32_t>(a[3] == 0xFFu ? 7u : kChannelToRas[a[3] & 15u]) << 7u;
            mask(s, (a[0] & 1u) != 0u ? 0x3FF000u : 0x0003FFu);
            bp(s, static_cast<std::uint8_t>(0x28u + a[0] / 2u), (a[0] & 1u) != 0u ? half << 12u : half);
            break;
        }
        case kColorUpdate:
            mask(s, 0x000008u);
            bp(s, 0x41u, static_cast<std::uint32_t>(a[0] != 0u) << 3u);
            break;
        case kAlphaUpdate:
            mask(s, 0x000010u);
            bp(s, 0x41u, static_cast<std::uint32_t>(a[0] != 0u) << 4u);
            break;
        case kDither:
            mask(s, 0x000004u);
            bp(s, 0x41u, static_cast<std::uint32_t>(a[0] != 0u) << 2u);
            break;
        case kProjection:
            // XF 0x1020-0x1025: the six terms, 0x1026: the type.
            for (unsigned i = 0; i < 6u; ++i) {
                xf(s, static_cast<std::uint16_t>(0x1020u + i), std::bit_cast<std::uint32_t>(op.f[i]));
            }
            xf(s, 0x1026u, a[0]);
            break;
        case kViewport:
            for (unsigned i = 0; i < 6u; ++i) {
                xf(s, static_cast<std::uint16_t>(0x101Au + i), std::bit_cast<std::uint32_t>(op.f[i]));
            }
            break;
        case kScissor: {
            // GXSetScissor: the box in GX's coordinates (342 added), corners inclusive, 11 bits each.
            const auto left = static_cast<std::uint32_t>(op.f[0]);
            const auto top = static_cast<std::uint32_t>(op.f[1]);
            const std::uint32_t tp = top + 342u, lf = left + 342u;
            const std::uint32_t bm = tp + static_cast<std::uint32_t>(op.f[3]) - 1u;
            const std::uint32_t rt = lf + static_cast<std::uint32_t>(op.f[2]) - 1u;
            bp(s, 0x20u, (tp & 0x7FFu) | (lf & 0x7FFu) << 12u);
            bp(s, 0x21u, (bm & 0x7FFu) | (rt & 0x7FFu) << 12u);
            break;
        }
        case kTevKColor:
            // GXSetTevKColor: the konst bank (bit 23), each half written once.
            bp(s, static_cast<std::uint8_t>(0xE0u + 2u * a[0]),
               a[1] | static_cast<std::uint32_t>(a[4]) << 12u | 0x800000u);
            bp(s, static_cast<std::uint8_t>(0xE1u + 2u * a[0]),
               a[3] | static_cast<std::uint32_t>(a[2]) << 12u | 0x800000u);
            break;
        case kTevColorS10: {
            // GXSetTevColorS10: 11-bit fields; the second half written three times as GXSetTevColor does.
            const auto r = static_cast<std::uint32_t>(static_cast<std::int16_t>(op.size[0])) & 0x7FFu;
            const auto g = static_cast<std::uint32_t>(static_cast<std::int16_t>(op.size[1])) & 0x7FFu;
            const auto b = static_cast<std::uint32_t>(static_cast<std::int32_t>(op.f[0])) & 0x7FFu;
            const auto al = static_cast<std::uint32_t>(static_cast<std::int32_t>(op.f[1])) & 0x7FFu;
            bp(s, static_cast<std::uint8_t>(0xE0u + 2u * a[0]), r | al << 12u);
            for (unsigned n = 0; n < 3u; ++n) {
                bp(s, static_cast<std::uint8_t>(0xE1u + 2u * a[0]), b | g << 12u);
            }
            break;
        }
        case kClipMode:
            xf(s, 0x1005u, a[0]);
            break;
        case kIndTexMatrix: {
            // GXSetIndTexMtx: 11-bit 1.10 terms, the scale exponent + 17 two bits per word.
            const std::uint32_t id = a[0] >= 9u ? a[0] - 9u : a[0] >= 5u ? a[0] - 5u : a[0] >= 1u ? a[0] - 1u : 0u;
            const auto scale = static_cast<std::int8_t>(static_cast<std::int8_t>(a[1]) + 0x11);
            auto term = [&](unsigned i) {
                return static_cast<std::uint32_t>(static_cast<std::int32_t>(op.f[i] * static_cast<float>(1 << 10))) & 0x7FFu;
            };
            for (unsigned w = 0; w < 3u; ++w) {
                const std::uint32_t bits = static_cast<std::uint32_t>((scale >> (2 * w)) & 0x3);
                bp(s, static_cast<std::uint8_t>(0x06u + w + id * 3u), term(2u * w) | term(2u * w + 1u) << 11u | bits << 22u);
            }
            break;
        }
        case kScissorOffset: {
            const std::uint32_t hx = static_cast<std::uint32_t>(static_cast<std::int32_t>(op.f[0]) + 342) >> 1u;
            const std::uint32_t hy = static_cast<std::uint32_t>(static_cast<std::int32_t>(op.f[1]) + 342) >> 1u;
            bp(s, 0x59u, (hx & 0x3FFu) | (hy & 0x3FFu) << 10u);
            break;
        }
        default:
            break;
        }
    }
}

void apply(std::span<const Op> ops, GxState& state) {
    struct ToState final : Writes {
        GxState& state;
        explicit ToState(GxState& s) : state(s) {}
        void bp(std::uint32_t word) override { state.load_bp(word); }
        void xf(std::uint16_t address, std::uint32_t value) override { state.load_xf(address, &value, 1u); }
        void cp(std::uint8_t reg, std::uint32_t value) override { state.load_cp(reg, value); }
        std::uint32_t bp_read(std::uint8_t reg) override { return state.bp(reg); }
        std::uint32_t xf_read(std::uint16_t address) override { return state.xf(address); }
        std::uint32_t cp_read(std::uint8_t reg) override { return state.cp(reg); }
    } out(state);
    encode(ops, out);
}

}  // namespace galaxy::gx::state
