// SPDX-License-Identifier: GPL-3.0-only
// HPKE base mode for diagnostics reports (diag_report.h): SHA-256 (FIPS 180-4), HMAC (RFC 2104), HKDF (RFC 5869),
// X25519 (RFC 7748), ChaCha20-Poly1305 (RFC 8439), composed as RFC 9180 specifies for DHKEM(X25519, HKDF-SHA256),
// HKDF-SHA256 and ChaCha20Poly1305. No branch or index depends on a secret.
#include "galaxy/diag_report.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <utility>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/random.h>
#endif
#endif

namespace galaxy::diag {
namespace {

std::uint32_t load32_be(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) << 24u | static_cast<std::uint32_t>(p[1]) << 16u |
           static_cast<std::uint32_t>(p[2]) << 8u | p[3];
}
std::uint32_t load32_le(const std::uint8_t* p) {
    return p[0] | static_cast<std::uint32_t>(p[1]) << 8u | static_cast<std::uint32_t>(p[2]) << 16u |
           static_cast<std::uint32_t>(p[3]) << 24u;
}
void store32_le(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
std::uint32_t rotl(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

// ---- SHA-256 ----

constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

struct Sha256 {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint8_t block[64]{};
    std::size_t used = 0;
    std::uint64_t total = 0;

    void compress(const std::uint8_t* p) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = load32_be(p + 4 * i);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3u);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10u);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kSha256K[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            k = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
    }
    void update(const std::uint8_t* data, std::size_t size) {
        total += size;
        while (size > 0u) {
            const std::size_t take = std::min<std::size_t>(64u - used, size);
            std::memcpy(block + used, data, take);
            used += take;
            data += take;
            size -= take;
            if (used == 64u) {
                compress(block);
                used = 0;
            }
        }
    }
    std::array<std::uint8_t, 32> finish() {
        const std::uint64_t bits = total * 8u;
        const std::uint8_t one = 0x80;
        update(&one, 1u);
        const std::uint8_t zero = 0;
        while (used != 56u) update(&zero, 1u);
        std::uint8_t length[8];
        for (int i = 0; i < 8; ++i) length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        update(length, 8u);
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<std::uint8_t>(h[i] >> (24 - 8 * j));
        }
        return out;
    }
};

// ---- X25519: field elements of 16 signed limbs of 16 bits ----

using Fe = std::int64_t[16];

void fe_carry(Fe o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += std::int64_t{1} << 16;
        const std::int64_t c = o[i] >> 16;  // arithmetic shift (C++20)
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}
void fe_swap(Fe p, Fe q, std::int64_t bit) {
    const std::int64_t mask = ~(bit - 1);
    for (int i = 0; i < 16; ++i) {
        const std::int64_t t = mask & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}
void fe_pack(std::uint8_t* out, const Fe n) {
    Fe t, m;
    for (int i = 0; i < 16; ++i) t[i] = n[i];
    fe_carry(t);
    fe_carry(t);
    fe_carry(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const std::int64_t borrow = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        fe_swap(t, m, 1 - borrow);
    }
    for (int i = 0; i < 16; ++i) {
        out[2 * i] = static_cast<std::uint8_t>(t[i] & 0xff);
        out[2 * i + 1] = static_cast<std::uint8_t>(t[i] >> 8);
    }
}
void fe_unpack(Fe o, const std::uint8_t* n) {
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + (static_cast<std::int64_t>(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}
void fe_add(Fe o, const Fe a, const Fe b) {
    for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i];
}
void fe_sub(Fe o, const Fe a, const Fe b) {
    for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i];
}
void fe_mul(Fe o, const Fe a, const Fe b) {
    std::int64_t t[31] = {};
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    }
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    fe_carry(o);
    fe_carry(o);
}
void fe_invert(Fe o, const Fe in) {
    Fe c;
    for (int i = 0; i < 16; ++i) c[i] = in[i];
    for (int a = 253; a >= 0; --a) {
        fe_mul(c, c, c);
        if (a != 2 && a != 4) fe_mul(c, c, in);
    }
    for (int i = 0; i < 16; ++i) o[i] = c[i];
}

// ---- ChaCha20 and Poly1305 ----

void chacha20_block(const Key& key, std::uint32_t counter, const std::uint8_t* nonce, std::uint8_t out[64]) {
    std::uint32_t s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574};
    for (int i = 0; i < 8; ++i) s[4 + i] = load32_le(key.data() + 4 * i);
    s[12] = counter;
    for (int i = 0; i < 3; ++i) s[13 + i] = load32_le(nonce + 4 * i);
    std::uint32_t x[16];
    std::memcpy(x, s, sizeof(x));
    const auto quarter = [&x](int a, int b, int c, int d) {
        x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 16);
        x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 12);
        x[a] += x[b]; x[d] = rotl(x[d] ^ x[a], 8);
        x[c] += x[d]; x[b] = rotl(x[b] ^ x[c], 7);
    };
    for (int round = 0; round < 10; ++round) {
        quarter(0, 4, 8, 12);
        quarter(1, 5, 9, 13);
        quarter(2, 6, 10, 14);
        quarter(3, 7, 11, 15);
        quarter(0, 5, 10, 15);
        quarter(1, 6, 11, 12);
        quarter(2, 7, 8, 13);
        quarter(3, 4, 9, 14);
    }
    for (int i = 0; i < 16; ++i) store32_le(out + 4 * i, x[i] + s[i]);
}

void chacha20_xor(const Key& key, std::uint32_t counter, const std::uint8_t* nonce, const std::uint8_t* in,
                  std::uint8_t* out, std::size_t size) {
    std::uint8_t stream[64];
    for (std::size_t done = 0; done < size; done += 64u, ++counter) {
        chacha20_block(key, counter, nonce, stream);
        const std::size_t n = std::min<std::size_t>(64u, size - done);
        for (std::size_t i = 0; i < n; ++i) out[done + i] = static_cast<std::uint8_t>(in[done + i] ^ stream[i]);
    }
}

// Poly1305 with 26-bit limbs (the message is whole: aad, pads, ciphertext and lengths, RFC 8439 2.8).
std::array<std::uint8_t, 16> poly1305(const std::uint8_t key[32], const std::uint8_t* m, std::size_t size) {
    const std::uint32_t r0 = load32_le(key + 0) & 0x3ffffff;
    const std::uint32_t r1 = (load32_le(key + 3) >> 2) & 0x3ffff03;
    const std::uint32_t r2 = (load32_le(key + 6) >> 4) & 0x3ffc0ff;
    const std::uint32_t r3 = (load32_le(key + 9) >> 6) & 0x3f03fff;
    const std::uint32_t r4 = (load32_le(key + 12) >> 8) & 0x00fffff;
    const std::uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    std::uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    std::uint8_t block[16];
    while (size > 0u) {
        std::uint32_t hibit = 1u << 24;
        const std::uint8_t* p = m;
        if (size < 16u) {
            std::memset(block, 0, sizeof(block));
            std::memcpy(block, m, size);
            block[size] = 1;
            hibit = 0;
            p = block;
        }
        h0 += load32_le(p + 0) & 0x3ffffff;
        h1 += (load32_le(p + 3) >> 2) & 0x3ffffff;
        h2 += (load32_le(p + 6) >> 4) & 0x3ffffff;
        h3 += (load32_le(p + 9) >> 6) & 0x3ffffff;
        h4 += (load32_le(p + 12) >> 8) | hibit;
        using U = std::uint64_t;
        const U d0 = U{h0} * r0 + U{h1} * s4 + U{h2} * s3 + U{h3} * s2 + U{h4} * s1;
        U d1 = U{h0} * r1 + U{h1} * r0 + U{h2} * s4 + U{h3} * s3 + U{h4} * s2;
        U d2 = U{h0} * r2 + U{h1} * r1 + U{h2} * r0 + U{h3} * s4 + U{h4} * s3;
        U d3 = U{h0} * r3 + U{h1} * r2 + U{h2} * r1 + U{h3} * r0 + U{h4} * s4;
        U d4 = U{h0} * r4 + U{h1} * r3 + U{h2} * r2 + U{h3} * r1 + U{h4} * r0;
        std::uint32_t c = static_cast<std::uint32_t>(d0 >> 26); h0 = static_cast<std::uint32_t>(d0) & 0x3ffffff;
        d1 += c; c = static_cast<std::uint32_t>(d1 >> 26); h1 = static_cast<std::uint32_t>(d1) & 0x3ffffff;
        d2 += c; c = static_cast<std::uint32_t>(d2 >> 26); h2 = static_cast<std::uint32_t>(d2) & 0x3ffffff;
        d3 += c; c = static_cast<std::uint32_t>(d3 >> 26); h3 = static_cast<std::uint32_t>(d3) & 0x3ffffff;
        d4 += c; c = static_cast<std::uint32_t>(d4 >> 26); h4 = static_cast<std::uint32_t>(d4) & 0x3ffffff;
        h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
        h1 += c;
        const std::size_t step = std::min<std::size_t>(16u, size);
        m += step;
        size -= step;
    }
    std::uint32_t c = h1 >> 26; h1 &= 0x3ffffff;
    h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += c;
    std::uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    std::uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    std::uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    std::uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    std::uint32_t g4 = h4 + c - (1u << 26);
    std::uint32_t mask = (g4 >> 31) - 1u;  // all ones when h >= p: take g
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;
    h0 = h0 | (h1 << 26);
    h1 = (h1 >> 6) | (h2 << 20);
    h2 = (h2 >> 12) | (h3 << 14);
    h3 = (h3 >> 18) | (h4 << 8);
    std::uint64_t f = std::uint64_t{h0} + load32_le(key + 16);
    std::array<std::uint8_t, 16> tag{};
    store32_le(tag.data() + 0, static_cast<std::uint32_t>(f));
    f = std::uint64_t{h1} + load32_le(key + 20) + (f >> 32);
    store32_le(tag.data() + 4, static_cast<std::uint32_t>(f));
    f = std::uint64_t{h2} + load32_le(key + 24) + (f >> 32);
    store32_le(tag.data() + 8, static_cast<std::uint32_t>(f));
    f = std::uint64_t{h3} + load32_le(key + 28) + (f >> 32);
    store32_le(tag.data() + 12, static_cast<std::uint32_t>(f));
    return tag;
}

std::array<std::uint8_t, 16> aead_tag(const Key& key, const std::uint8_t* nonce, const std::uint8_t* aad,
                                      std::size_t aad_size, const std::uint8_t* cipher, std::size_t cipher_size) {
    std::uint8_t block[64];
    chacha20_block(key, 0u, nonce, block);
    const auto padded = [](std::size_t n) { return (n + 15u) & ~std::size_t{15}; };
    Bytes mac(padded(aad_size) + padded(cipher_size) + 16u, 0u);
    if (aad_size != 0u) std::memcpy(mac.data(), aad, aad_size);
    if (cipher_size != 0u) std::memcpy(mac.data() + padded(aad_size), cipher, cipher_size);
    std::uint8_t* lengths = mac.data() + padded(aad_size) + padded(cipher_size);
    for (int i = 0; i < 8; ++i) {
        lengths[i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(aad_size) >> (8 * i));
        lengths[8 + i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(cipher_size) >> (8 * i));
    }
    const std::array<std::uint8_t, 16> tag = poly1305(block, mac.data(), mac.size());
    std::memset(block, 0, sizeof(block));
    return tag;
}

// ---- HPKE (RFC 9180 4 and 5) ----

constexpr std::uint8_t kKemSuite[] = {'K', 'E', 'M', 0x00, 0x20};
constexpr std::uint8_t kHpkeSuite[] = {'H', 'P', 'K', 'E', 0x00, 0x20, 0x00, 0x01, 0x00, 0x03};

Bytes concat(std::initializer_list<std::pair<const void*, std::size_t>> parts) {
    Bytes out;
    for (const auto& [data, size] : parts) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        out.insert(out.end(), p, p + size);
    }
    return out;
}

std::array<std::uint8_t, 32> labeled_extract(const std::uint8_t* suite, std::size_t suite_size, const std::uint8_t* salt,
                                             std::size_t salt_size, std::string_view label, const std::uint8_t* ikm,
                                             std::size_t ikm_size) {
    const Bytes labeled =
        concat({{"HPKE-v1", 7u}, {suite, suite_size}, {label.data(), label.size()}, {ikm, ikm_size}});
    const std::uint8_t zeros[32] = {};
    return salt_size == 0u ? hmac_sha256(zeros, sizeof(zeros), labeled.data(), labeled.size())
                           : hmac_sha256(salt, salt_size, labeled.data(), labeled.size());
}

Bytes labeled_expand(const std::uint8_t* suite, std::size_t suite_size, const std::uint8_t* prk, std::string_view label,
                     const std::uint8_t* info, std::size_t info_size, std::size_t length) {
    const std::uint8_t l[2] = {static_cast<std::uint8_t>(length >> 8u), static_cast<std::uint8_t>(length)};
    const Bytes labeled =
        concat({{l, 2u}, {"HPKE-v1", 7u}, {suite, suite_size}, {label.data(), label.size()}, {info, info_size}});
    return hkdf_expand(prk, labeled.data(), labeled.size(), length);
}

bool all_zero(const Key& k) {
    std::uint8_t any = 0;
    for (const std::uint8_t b : k) any |= b;
    return any == 0u;
}

// ExtractAndExpand of DHKEM (RFC 9180 4.1).
Key kem_shared_secret(const Key& dh, const Key& enc, const Key& recipient_public) {
    const auto prk = labeled_extract(kKemSuite, sizeof(kKemSuite), nullptr, 0u, "eae_prk", dh.data(), dh.size());
    const Bytes context = concat({{enc.data(), 32u}, {recipient_public.data(), 32u}});
    const Bytes secret =
        labeled_expand(kKemSuite, sizeof(kKemSuite), prk.data(), "shared_secret", context.data(), context.size(), 32u);
    Key out{};
    std::memcpy(out.data(), secret.data(), 32u);
    return out;
}

// KeySchedule, base mode (no psk), RFC 9180 5.1.
void key_schedule(const Key& shared_secret, std::string_view info, HpkeContext& context) {
    const auto psk_id_hash = labeled_extract(kHpkeSuite, sizeof(kHpkeSuite), nullptr, 0u, "psk_id_hash", nullptr, 0u);
    const auto info_hash = labeled_extract(kHpkeSuite, sizeof(kHpkeSuite), nullptr, 0u, "info_hash",
                                           reinterpret_cast<const std::uint8_t*>(info.data()), info.size());
    const std::uint8_t mode = 0;
    const Bytes ksc = concat({{&mode, 1u}, {psk_id_hash.data(), 32u}, {info_hash.data(), 32u}});
    const auto secret = labeled_extract(kHpkeSuite, sizeof(kHpkeSuite), shared_secret.data(), shared_secret.size(),
                                        "secret", nullptr, 0u);
    const Bytes key = labeled_expand(kHpkeSuite, sizeof(kHpkeSuite), secret.data(), "key", ksc.data(), ksc.size(), 32u);
    const Bytes nonce =
        labeled_expand(kHpkeSuite, sizeof(kHpkeSuite), secret.data(), "base_nonce", ksc.data(), ksc.size(), 12u);
    std::memcpy(context.key.data(), key.data(), 32u);
    std::memcpy(context.base_nonce.data(), nonce.data(), 12u);
}

constexpr std::uint8_t kMagic[4] = {'N', 'B', 'D', 'R'};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kCompressionZlib = 1;

}  // namespace

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size) {
    Sha256 s;
    s.update(data, size);
    return s.finish();
}

std::array<std::uint8_t, 32> hmac_sha256(const std::uint8_t* key, std::size_t key_size, const std::uint8_t* data,
                                         std::size_t size) {
    std::uint8_t k[64] = {};
    if (key_size > 64u) {
        const auto hashed = sha256(key, key_size);
        std::memcpy(k, hashed.data(), 32u);
    } else if (key_size != 0u) {
        std::memcpy(k, key, key_size);
    }
    std::uint8_t pad[64];
    for (int i = 0; i < 64; ++i) pad[i] = static_cast<std::uint8_t>(k[i] ^ 0x36);
    Sha256 inner;
    inner.update(pad, 64u);
    inner.update(data, size);
    const auto inner_hash = inner.finish();
    for (int i = 0; i < 64; ++i) pad[i] = static_cast<std::uint8_t>(k[i] ^ 0x5c);
    Sha256 outer;
    outer.update(pad, 64u);
    outer.update(inner_hash.data(), inner_hash.size());
    std::memset(k, 0, sizeof(k));
    return outer.finish();
}

Bytes hkdf_expand(const std::uint8_t* prk, const std::uint8_t* info, std::size_t info_size, std::size_t length) {
    Bytes out;
    std::array<std::uint8_t, 32> t{};
    std::size_t t_size = 0;
    for (std::uint8_t counter = 1; out.size() < length && counter != 0u; ++counter) {
        Bytes input(t.data(), t.data() + t_size);
        if (info_size != 0u) input.insert(input.end(), info, info + info_size);
        input.push_back(counter);
        t = hmac_sha256(prk, 32u, input.data(), input.size());
        t_size = 32u;
        out.insert(out.end(), t.begin(), t.begin() + std::min<std::size_t>(32u, length - out.size()));
    }
    return out;
}

Key x25519(const Key& scalar, const Key& point) {
    std::uint8_t z[32];
    std::memcpy(z, scalar.data(), 32u);
    z[31] = static_cast<std::uint8_t>((z[31] & 127) | 64);
    z[0] &= 248;
    Fe x, a = {}, b, c = {}, d = {}, e, f;
    const Fe k121665 = {0xDB41, 1};
    fe_unpack(x, point.data());
    for (int i = 0; i < 16; ++i) b[i] = x[i];
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        const std::int64_t bit = (z[i >> 3] >> (i & 7)) & 1;
        fe_swap(a, b, bit);
        fe_swap(c, d, bit);
        fe_add(e, a, c);
        fe_sub(a, a, c);
        fe_add(c, b, d);
        fe_sub(b, b, d);
        fe_mul(d, e, e);
        fe_mul(f, a, a);
        fe_mul(a, c, a);
        fe_mul(c, b, e);
        fe_add(e, a, c);
        fe_sub(a, a, c);
        fe_mul(b, a, a);
        fe_sub(c, d, f);
        fe_mul(a, c, k121665);
        fe_add(a, a, d);
        fe_mul(c, c, a);
        fe_mul(a, d, f);
        fe_mul(d, b, x);
        fe_mul(b, e, e);
        fe_swap(a, b, bit);
        fe_swap(c, d, bit);
    }
    fe_invert(c, c);
    fe_mul(a, a, c);
    Key out{};
    fe_pack(out.data(), a);
    std::memset(z, 0, sizeof(z));
    return out;
}

Key x25519_public(const Key& secret) {
    Key base{};
    base[0] = 9;
    return x25519(secret, base);
}

Bytes chacha20_poly1305_seal(const Key& key, const std::uint8_t* nonce12, const std::uint8_t* aad, std::size_t aad_size,
                             const std::uint8_t* plain, std::size_t plain_size) {
    Bytes out(plain_size + 16u);
    chacha20_xor(key, 1u, nonce12, plain, out.data(), plain_size);
    const auto tag = aead_tag(key, nonce12, aad, aad_size, out.data(), plain_size);
    std::memcpy(out.data() + plain_size, tag.data(), 16u);
    return out;
}

bool chacha20_poly1305_open(const Key& key, const std::uint8_t* nonce12, const std::uint8_t* aad, std::size_t aad_size,
                            const std::uint8_t* sealed, std::size_t sealed_size, Bytes& out) {
    out.clear();
    if (sealed_size < 16u) return false;
    const std::size_t size = sealed_size - 16u;
    const auto tag = aead_tag(key, nonce12, aad, aad_size, sealed, size);
    std::uint8_t diff = 0;
    for (int i = 0; i < 16; ++i) diff |= static_cast<std::uint8_t>(tag[i] ^ sealed[size + i]);
    if (diff != 0u) return false;
    out.resize(size);
    chacha20_xor(key, 1u, nonce12, sealed, out.data(), size);
    return true;
}

bool random_bytes(std::uint8_t* out, std::size_t size) {
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, out, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
#else
    for (std::size_t done = 0; done < size; done += 256u) {
        if (getentropy(out + done, std::min<std::size_t>(256u, size - done)) != 0) return false;
    }
    return true;
#endif
}

Key hpke_derive_secret(const std::uint8_t* ikm, std::size_t ikm_size) {
    const auto prk = labeled_extract(kKemSuite, sizeof(kKemSuite), nullptr, 0u, "dkp_prk", ikm, ikm_size);
    const Bytes sk = labeled_expand(kKemSuite, sizeof(kKemSuite), prk.data(), "sk", nullptr, 0u, 32u);
    Key out{};
    std::memcpy(out.data(), sk.data(), 32u);
    return out;
}

bool hpke_setup_sender(const Key& recipient_public, const Key& ephemeral_secret, std::string_view info, Key& enc,
                       HpkeContext& context) {
    const Key dh = x25519(ephemeral_secret, recipient_public);
    if (all_zero(dh)) return false;
    enc = x25519_public(ephemeral_secret);
    key_schedule(kem_shared_secret(dh, enc, recipient_public), info, context);
    return true;
}

bool hpke_setup_recipient(const Key& recipient_secret, const Key& enc, std::string_view info, HpkeContext& context) {
    const Key dh = x25519(recipient_secret, enc);
    if (all_zero(dh)) return false;
    key_schedule(kem_shared_secret(dh, enc, x25519_public(recipient_secret)), info, context);
    return true;
}

std::array<std::uint8_t, 8> key_id(const Key& public_key) {
    const auto digest = sha256(public_key.data(), public_key.size());
    std::array<std::uint8_t, 8> id{};
    std::memcpy(id.data(), digest.data(), 8u);
    return id;
}

Bytes seal_report(std::string_view report, const Key& recipient_public) {
    if (report.size() > kReportMax) return {};
    uLongf packed_size = compressBound(static_cast<uLong>(report.size()));
    Bytes packed(packed_size);
    if (compress2(packed.data(), &packed_size, reinterpret_cast<const Bytef*>(report.data()),
                  static_cast<uLong>(report.size()), Z_BEST_COMPRESSION) != Z_OK) {
        return {};
    }
    packed.resize(packed_size);
    if (kEnvelopeHeader + packed.size() + 16u > kEnvelopeMax) return {};
    Key ephemeral{};
    if (!random_bytes(ephemeral.data(), ephemeral.size())) return {};
    Key enc{};
    HpkeContext context;
    const bool ready = hpke_setup_sender(recipient_public, ephemeral, kHpkeInfo, enc, context);
    std::memset(ephemeral.data(), 0, ephemeral.size());
    if (!ready) return {};
    Bytes out(kEnvelopeHeader);
    std::memcpy(out.data(), kMagic, 4u);
    out[4] = kVersion;
    out[5] = kCompressionZlib;
    const auto id = key_id(recipient_public);
    std::memcpy(out.data() + 8, id.data(), 8u);
    std::memcpy(out.data() + 16, enc.data(), 32u);
    // Sequence 0: the nonce is the base nonce.
    const Bytes sealed = chacha20_poly1305_seal(context.key, context.base_nonce.data(), out.data(), kEnvelopeHeader,
                                                packed.data(), packed.size());
    out.insert(out.end(), sealed.begin(), sealed.end());
    return out;
}

OpenResult open_report(const std::uint8_t* envelope, std::size_t size, const Key& recipient_secret, std::string& report) {
    report.clear();
    if (size < kEnvelopeHeader + 16u || size > kEnvelopeMax || std::memcmp(envelope, kMagic, 4u) != 0 ||
        envelope[4] != kVersion || envelope[5] != kCompressionZlib || envelope[6] != 0u || envelope[7] != 0u) {
        return OpenResult::NotEnvelope;
    }
    const auto id = key_id(x25519_public(recipient_secret));
    if (std::memcmp(envelope + 8, id.data(), 8u) != 0) return OpenResult::OtherKey;
    Key enc{};
    std::memcpy(enc.data(), envelope + 16, 32u);
    HpkeContext context;
    Bytes packed;
    if (!hpke_setup_recipient(recipient_secret, enc, kHpkeInfo, context) ||
        !chacha20_poly1305_open(context.key, context.base_nonce.data(), envelope, kEnvelopeHeader,
                                envelope + kEnvelopeHeader, size - kEnvelopeHeader, packed)) {
        return OpenResult::Tampered;
    }
    // Inflate with a hard output limit: an authentic report is never larger, so more is refused, not truncated.
    z_stream z{};
    if (inflateInit(&z) != Z_OK) return OpenResult::BadContent;
    std::string out(kReportMax + 1u, '\0');
    z.next_in = packed.data();
    z.avail_in = static_cast<uInt>(packed.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = static_cast<uInt>(out.size());
    const int status = inflate(&z, Z_FINISH);
    const std::size_t produced = out.size() - z.avail_out;
    const bool whole = status == Z_STREAM_END && z.avail_in == 0u && produced <= kReportMax;
    inflateEnd(&z);
    if (!whole) return OpenResult::BadContent;
    out.resize(produced);
    report = std::move(out);
    return OpenResult::Ok;
}

}  // namespace galaxy::diag
