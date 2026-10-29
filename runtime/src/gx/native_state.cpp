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
    bp(s, 0xEFu, b_m);
    bp(s, 0xF0u, b_expn);
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
            xf(s, static_cast<std::uint16_t>(0x100Cu + (a[0] & 1u)), rgba(a + 1));
            break;
        case kAmbientColor:
            xf(s, static_cast<std::uint16_t>(0x100Au + (a[0] & 1u)), rgba(a + 1));
            break;
        case kTexCoordGen:
            xf(s, static_cast<std::uint16_t>(0x1040u + a[0]), material::tex_gen_word(a[1], a[2]));
            xf(s, static_cast<std::uint16_t>(0x1050u + a[0]),
               static_cast<std::uint32_t>(static_cast<std::int32_t>(a[4]) - 64) | static_cast<std::uint32_t>(a[3]) << 8u);
            break;
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
    } out(state);
    encode(ops, out);
}

}  // namespace galaxy::gx::state
