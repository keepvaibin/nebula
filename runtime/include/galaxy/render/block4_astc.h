// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The record's BLOCK4 blocks (BC1's layout with CMPR's colours, nebula_cooked_texture.h) as ASTC 4x4 blocks, for a GPU
// that samples ASTC and not BC1: 16 bytes a block instead of 64 as RGBA8. A block keeps its two colours exactly (LDR RGB
// direct endpoints at 8 bits) and its sixteen indices. A four-colour block's mixes, 5/8 + 3/8 rounded down in the record,
// take weights 25/64 and 39/64: within |c1 - c0| / 64 + 7/8 of a 255th of the record's value (4.61 at most). A
// three-colour block's mix takes 32/64: within half a 255th. The record's blocks are opaque (texture.rs
// blocks_opaque: no index 3 in a three-colour block), so RGB endpoints serve. Blocks stay in the record's order; to_block4 gives the
// record's block back.

#include <array>
#include <cstdint>
#include <cstring>

namespace galaxy::render::block4_astc {

inline constexpr std::uint32_t kBlockBytes = 16u;

namespace detail {

// The five trits an 8-bit code of the integer sequence encoding holds (ASTC's decoding of trit blocks).
constexpr std::array<std::uint8_t, 5> decode_trits(unsigned t) {
    const auto bit = [t](unsigned n) { return (t >> n) & 1u; };
    unsigned c = 0u;
    std::array<std::uint8_t, 5> v{};
    if (((t >> 2) & 7u) == 7u) {
        c = ((t >> 5) & 7u) << 2 | (t & 3u);
        v[4] = 2u;
        v[3] = 2u;
    } else {
        c = t & 31u;
        if (((t >> 5) & 3u) == 3u) {
            v[4] = 2u;
            v[3] = static_cast<std::uint8_t>(bit(7));
        } else {
            v[4] = static_cast<std::uint8_t>(bit(7));
            v[3] = static_cast<std::uint8_t>((t >> 5) & 3u);
        }
    }
    const auto cb = [c](unsigned n) { return (c >> n) & 1u; };
    if ((c & 3u) == 3u) {
        v[2] = 2u;
        v[1] = static_cast<std::uint8_t>(cb(4));
        v[0] = static_cast<std::uint8_t>(cb(3) << 1 | (cb(2) & ~cb(3) & 1u));
    } else if (((c >> 2) & 3u) == 3u) {
        v[2] = 2u;
        v[1] = 2u;
        v[0] = static_cast<std::uint8_t>(c & 3u);
    } else {
        v[2] = static_cast<std::uint8_t>(cb(4));
        v[1] = static_cast<std::uint8_t>((c >> 2) & 3u);
        v[0] = static_cast<std::uint8_t>(cb(1) << 1 | (cb(0) & ~cb(1) & 1u));
    }
    return v;
}

// The code of each five trits (t0 + 3 t1 + 9 t2 + 27 t3 + 81 t4): the smallest code that decodes to them, so trits
// that are all zero after the first take a code whose bits past the first two are zero (a partial group's).
constexpr std::array<std::uint8_t, 243> make_trit_codes() {
    std::array<std::uint8_t, 243> code{};
    std::array<bool, 243> seen{};
    for (unsigned t = 0; t < 256u; ++t) {
        const auto v = decode_trits(t);
        const unsigned index = v[0] + 3u * (v[1] + 3u * (v[2] + 3u * (v[3] + 3u * v[4])));
        if (!seen[index]) {
            seen[index] = true;
            code[index] = static_cast<std::uint8_t>(t);
        }
    }
    return code;
}

inline constexpr std::array<std::uint8_t, 243> kTritCodes = make_trit_codes();

// Block modes: a 4x4 weight grid, one plane, weights of six levels (0, 64, 12, 52, 25, 39 by value) or of three
// (0, 32, 64).
inline constexpr unsigned kModeSix = 0x043u;
inline constexpr unsigned kModeThree = 0x051u;

inline unsigned take(const std::uint8_t* bytes, unsigned at, unsigned count) {
    unsigned value = 0u;
    for (unsigned i = 0; i < count; ++i) {
        value |= ((bytes[(at + i) >> 3] >> ((at + i) & 7u)) & 1u) << i;
    }
    return value;
}

// The sixteen weights' integer sequence: n plain bits and one trit each, five to a group (the last holds one).
// Positions: m0, T[1:0], m1, T[3:2], m2, T[4], m3, T[6:5], m4, T[7]. At most 42 bits.
inline std::uint64_t weight_sequence(const std::uint8_t* q, unsigned n) {
    static constexpr unsigned kTritBits[5][2] = {{0u, 2u}, {2u, 2u}, {4u, 1u}, {5u, 2u}, {7u, 1u}};
    std::uint64_t sequence = 0u;
    unsigned at = 0u;
    for (unsigned first = 0; first < 16u; first += 5u) {
        const unsigned count = first + 5u <= 16u ? 5u : 16u - first;
        unsigned trits = 0u;
        for (unsigned k = count; k-- > 0u;) {
            trits = trits * 3u + (q[first + k] >> n);
        }
        const unsigned t = kTritCodes[trits];
        for (unsigned k = 0; k < count; ++k) {
            sequence |= std::uint64_t{q[first + k] & ((1u << n) - 1u)} << at;
            at += n;
            // A partial group of one carries T[1:0] only.
            sequence |= std::uint64_t{(t >> kTritBits[k][0]) & ((1u << kTritBits[k][1]) - 1u)} << at;
            at += kTritBits[k][1];
        }
    }
    return sequence;
}

inline std::uint64_t reverse_bits(std::uint64_t v) {
    v = (v >> 1 & 0x5555555555555555u) | (v & 0x5555555555555555u) << 1;
    v = (v >> 2 & 0x3333333333333333u) | (v & 0x3333333333333333u) << 2;
    v = (v >> 4 & 0x0F0F0F0F0F0F0F0Fu) | (v & 0x0F0F0F0F0F0F0F0Fu) << 4;
    v = (v >> 8 & 0x00FF00FF00FF00FFu) | (v & 0x00FF00FF00FF00FFu) << 8;
    v = (v >> 16 & 0x0000FFFF0000FFFFu) | (v & 0x0000FFFF0000FFFFu) << 16;
    return v >> 32 | v << 32;
}

inline void take_weights(const std::uint8_t* stream, std::uint8_t* q, unsigned n) {
    unsigned at = 0u;
    for (unsigned first = 0; first < 16u; first += 5u) {
        const unsigned count = first + 5u <= 16u ? 5u : 16u - first;
        static constexpr unsigned kTritBits[5][2] = {{0u, 2u}, {2u, 2u}, {4u, 1u}, {5u, 2u}, {7u, 1u}};
        unsigned t = 0u;
        unsigned low[5] = {};
        for (unsigned k = 0; k < count; ++k) {
            low[k] = take(stream, at, n);
            at += n;
            t |= take(stream, at, kTritBits[k][1]) << kTritBits[k][0];
            at += kTritBits[k][1];
        }
        const auto trits = decode_trits(t);
        for (unsigned k = 0; k < count; ++k) {
            q[first + k] = static_cast<std::uint8_t>(trits[k] << n | low[k]);
        }
    }
}

struct Rgb {
    unsigned r, g, b;
};

inline Rgb widen(unsigned c) {
    const unsigned r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return {r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2};
}

inline unsigned narrow(const Rgb& c) {
    return (c.r >> 3) << 11 | (c.g >> 2) << 5 | (c.b >> 3);
}

}  // namespace detail

// One BLOCK4 block (8 bytes) as one ASTC 4x4 block (16 bytes), each byte of `out` written once and none read (it may be
// write-combined upload memory).
inline void from_block4(const std::uint8_t* in, std::uint8_t* out) {
    using namespace detail;
    const unsigned c0 = in[0] | in[1] << 8;
    const unsigned c1 = in[2] | in[3] << 8;
    const std::uint32_t indices = static_cast<std::uint32_t>(in[4]) | in[5] << 8 | in[6] << 16 |
                                  static_cast<std::uint32_t>(in[7]) << 24;
    const Rgb a = widen(c0);
    const Rgb b = widen(c1);
    const bool four = c0 > c1;
    // LDR RGB direct swaps its endpoints (and contracts blue) when the second's sum is the smaller: the larger sum goes
    // second, the weights turned with it.
    const bool turned = b.r + b.g + b.b < a.r + a.g + a.b;
    const Rgb& e0 = turned ? b : a;
    const Rgb& e1 = turned ? a : b;
    // A palette index's weight level: c0 at 0, c1 at 64, the mixes at 25 and 39 (four colours) or both at 32.
    static constexpr std::uint8_t kSix[2][4] = {{0u, 1u, 4u, 5u}, {1u, 0u, 5u, 4u}};
    static constexpr std::uint8_t kThree[2][4] = {{0u, 2u, 1u, 1u}, {2u, 0u, 1u, 1u}};
    const std::uint8_t* const level = four ? kSix[turned ? 1 : 0] : kThree[turned ? 1 : 0];
    std::uint8_t q[16];
    for (unsigned i = 0; i < 16u; ++i) {
        q[i] = level[(indices >> (2u * i)) & 3u];
    }
    // Bits 0-10 the block mode, 11-12 one partition (0), 13-16 colour endpoint mode 8, 17-64 the endpoints r0 r1 g0 g1
    // b0 b1; the weights' sequence runs down from bit 127.
    const std::uint64_t low = std::uint64_t{four ? kModeSix : kModeThree} | std::uint64_t{8u} << 13 |
                              std::uint64_t{e0.r} << 17 | std::uint64_t{e1.r} << 25 | std::uint64_t{e0.g} << 33 |
                              std::uint64_t{e1.g} << 41 | std::uint64_t{e0.b} << 49 | std::uint64_t{e1.b} << 57;
    const std::uint64_t high = std::uint64_t{e1.b >> 7} | reverse_bits(weight_sequence(q, four ? 1u : 0u));
    for (unsigned i = 0; i < 8u; ++i) {
        out[i] = static_cast<std::uint8_t>(low >> (8u * i));
        out[8u + i] = static_cast<std::uint8_t>(high >> (8u * i));
    }
}

// The BLOCK4 block one from_block4 block holds.
inline void to_block4(const std::uint8_t* in, std::uint8_t* out) {
    using namespace detail;
    const bool four = take(in, 0u, 11u) == kModeSix;
    const Rgb e0{take(in, 17u, 8u), take(in, 33u, 8u), take(in, 49u, 8u)};
    const Rgb e1{take(in, 25u, 8u), take(in, 41u, 8u), take(in, 57u, 8u)};
    std::uint8_t stream[kBlockBytes] = {};
    for (unsigned i = 0; i < 128u; ++i) {
        if (((in[(127u - i) >> 3] >> ((127u - i) & 7u)) & 1u) != 0u) {
            stream[i >> 3] = static_cast<std::uint8_t>(stream[i >> 3] | 1u << (i & 7u));
        }
    }
    std::uint8_t q[16];
    take_weights(stream, q, four ? 1u : 0u);
    const unsigned n0 = narrow(e0), n1 = narrow(e1);
    const bool turned = four ? n0 < n1 : n0 > n1;
    const unsigned c0 = turned ? n1 : n0;
    const unsigned c1 = turned ? n0 : n1;
    static constexpr std::uint8_t kSix[2][6] = {{0u, 1u, 0u, 0u, 2u, 3u}, {1u, 0u, 0u, 0u, 3u, 2u}};
    static constexpr std::uint8_t kThree[2][3] = {{0u, 2u, 1u}, {1u, 2u, 0u}};
    std::uint32_t indices = 0u;
    for (unsigned i = 0; i < 16u; ++i) {
        const unsigned index = four ? kSix[turned ? 1 : 0][q[i]] : kThree[turned ? 1 : 0][q[i]];
        indices |= static_cast<std::uint32_t>(index) << (2u * i);
    }
    out[0] = static_cast<std::uint8_t>(c0);
    out[1] = static_cast<std::uint8_t>(c0 >> 8);
    out[2] = static_cast<std::uint8_t>(c1);
    out[3] = static_cast<std::uint8_t>(c1 >> 8);
    std::memcpy(out + 4, &indices, 4);
}

}  // namespace galaxy::render::block4_astc
