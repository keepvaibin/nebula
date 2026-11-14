// SPDX-License-Identifier: GPL-3.0-only
// SHA-256, base64 and RSA PKCS#1 v1.5 signature verification in portable C++, so the update check needs no crypto
// library on any platform. Verification only: the public exponent is small and nothing here handles a secret, so the
// code is written for clarity, not for constant time.
#include "update_internal.h"

#include <algorithm>
#include <cstring>

namespace galaxy::update {

namespace {

// installer/release-public-key.xml's modulus (the key the Windows setups are signed with; update_tests checks the copy).
constexpr std::string_view kReleaseModulus =
    "rUk5e121s2AdrxlsfX2k8jjcuGmiMrgQ2vqzUq3k0ed5/VjFko4leTGUpcCtDSjlR+UToFHSdmubvhB4yPFMRptscbBzEyNyKYHO6EBp+RY7iBldBN0piup"
    "FpxTWiXHPklRiBIZHDjERcWFY/PrwyYS5SMnJ7eWL7AvFCVSU97+xJBJgQ9G3ZEaxqQyHD6OBSpwGyHuI8t9m8yx63fsmA53JdDCFPIf1PuDp6BBACfsMBq47W1"
    "TMH61UxjLC4m9ToLgsCe1qVfwLAt3KZS8SCGTI0txejW2kydWdo5dsRhqP5TJpSy289S3pjQYjIB1wHvNyafifyikS6D8BN2z1crP9lGykOD2vBy+e5vfR5BYu/"
    "ziXlCVKBfA95833NxFfGoWyZZhZGNV3gzkX5gEdfJe4kb5bQnGYfAKcw5lhdMufv6QLjlKVD5RTgORobOa0ivKzt3JdIb0jpX/xVwgHe2aNVo+LlyjgMc1eOy4I"
    "JnMYfdtzDctbUVjQPiuZv5PN";
constexpr std::uint32_t kReleaseExponent = 65537u;  // AQAB
// Tests: a key of their own (set before any check runs).
std::string_view g_modulus = kReleaseModulus;

constexpr std::uint32_t kRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

// Numbers as 32-bit limbs, least significant first, all of the modulus's length.
using Limbs = std::vector<std::uint32_t>;

Limbs from_bytes(std::span<const std::uint8_t> big_endian, std::size_t limbs) {
    Limbs out(limbs, 0u);
    for (std::size_t i = 0; i < big_endian.size() && i / 4u < limbs; ++i) {
        out[i / 4u] |= static_cast<std::uint32_t>(big_endian[big_endian.size() - 1u - i]) << (8u * (i % 4u));
    }
    return out;
}

bool at_least(const Limbs& a, const Limbs& b) {
    for (std::size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return true;
}

void subtract(Limbs& a, const Limbs& b) {
    std::uint64_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const std::uint64_t d = static_cast<std::uint64_t>(a[i]) - b[i] - borrow;
        a[i] = static_cast<std::uint32_t>(d);
        borrow = (d >> 63) & 1u;
    }
}

// Montgomery multiplication (CIOS): a * b / R mod n, R = 2^(32 * limbs).
Limbs mont_mul(const Limbs& a, const Limbs& b, const Limbs& n, std::uint32_t n0inv) {
    const std::size_t s = n.size();
    std::vector<std::uint32_t> t(s + 2u, 0u);
    for (std::size_t i = 0; i < s; ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < s; ++j) {
            const std::uint64_t v = static_cast<std::uint64_t>(t[j]) + static_cast<std::uint64_t>(a[j]) * b[i] + carry;
            t[j] = static_cast<std::uint32_t>(v);
            carry = v >> 32;
        }
        std::uint64_t v = static_cast<std::uint64_t>(t[s]) + carry;
        t[s] = static_cast<std::uint32_t>(v);
        t[s + 1u] = static_cast<std::uint32_t>(v >> 32);
        const std::uint32_t m = t[0] * n0inv;
        v = static_cast<std::uint64_t>(t[0]) + static_cast<std::uint64_t>(m) * n[0];
        carry = v >> 32;
        for (std::size_t j = 1; j < s; ++j) {
            v = static_cast<std::uint64_t>(t[j]) + static_cast<std::uint64_t>(m) * n[j] + carry;
            t[j - 1u] = static_cast<std::uint32_t>(v);
            carry = v >> 32;
        }
        v = static_cast<std::uint64_t>(t[s]) + carry;
        t[s - 1u] = static_cast<std::uint32_t>(v);
        t[s] = t[s + 1u] + static_cast<std::uint32_t>(v >> 32);
    }
    Limbs out(t.begin(), t.begin() + static_cast<std::ptrdiff_t>(s));
    if (t[s] != 0u || at_least(out, n)) subtract(out, n);
    return out;
}

}  // namespace

namespace detail {

Sha256::Sha256()
    : state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u} {}

void Sha256::block(const std::uint8_t* data) {
    std::uint32_t w[64];
    for (unsigned i = 0; i < 16u; ++i) {
        w[i] = static_cast<std::uint32_t>(data[i * 4u]) << 24 | static_cast<std::uint32_t>(data[i * 4u + 1u]) << 16 |
               static_cast<std::uint32_t>(data[i * 4u + 2u]) << 8 | data[i * 4u + 3u];
    }
    for (unsigned i = 16; i < 64u; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15u], 7) ^ rotr(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2u], 17) ^ rotr(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (unsigned i = 0; i < 64u; ++i) {
        const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kRound[i] + w[i];
        const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t bytes) {
    const auto* in = static_cast<const std::uint8_t*>(data);
    length_ += bytes;
    while (bytes > 0u) {
        const std::size_t take = std::min(bytes, buffer_.size() - buffered_);
        std::memcpy(buffer_.data() + buffered_, in, take);
        buffered_ += take;
        in += take;
        bytes -= take;
        if (buffered_ == buffer_.size()) {
            block(buffer_.data());
            buffered_ = 0;
        }
    }
}

std::array<std::uint8_t, 32> Sha256::finish() {
    const std::uint64_t bits = length_ * 8u;
    const std::uint8_t one = 0x80u;
    update(&one, 1u);
    const std::uint8_t zero = 0u;
    while (buffered_ != 56u) update(&zero, 1u);
    std::uint8_t tail[8];
    for (unsigned i = 0; i < 8u; ++i) tail[i] = static_cast<std::uint8_t>(bits >> (56u - 8u * i));
    update(tail, 8u);
    std::array<std::uint8_t, 32> out{};
    for (unsigned i = 0; i < 8u; ++i) {
        for (unsigned j = 0; j < 4u; ++j) out[i * 4u + j] = static_cast<std::uint8_t>(state_[i] >> (24u - 8u * j));
    }
    return out;
}

std::vector<std::uint8_t> base64_decode(std::string_view text, bool& ok) {
    std::vector<std::uint8_t> out;
    std::uint32_t bits = 0;
    unsigned count = 0, padding = 0;
    ok = true;
    for (const char c : text) {
        unsigned value;
        if (c >= 'A' && c <= 'Z') value = static_cast<unsigned>(c - 'A');
        else if (c >= 'a' && c <= 'z') value = static_cast<unsigned>(c - 'a') + 26u;
        else if (c >= '0' && c <= '9') value = static_cast<unsigned>(c - '0') + 52u;
        else if (c == '+') value = 62u;
        else if (c == '/') value = 63u;
        else if (c == '=') { ++padding; continue; }
        else if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        else { ok = false; return {}; }
        if (padding != 0u) { ok = false; return {}; }  // data after padding
        bits = bits << 6 | value;
        if (++count == 4u) {
            out.push_back(static_cast<std::uint8_t>(bits >> 16));
            out.push_back(static_cast<std::uint8_t>(bits >> 8));
            out.push_back(static_cast<std::uint8_t>(bits));
            bits = 0;
            count = 0;
        }
    }
    if (count == 2u) out.push_back(static_cast<std::uint8_t>(bits >> 4));
    else if (count == 3u) {
        out.push_back(static_cast<std::uint8_t>(bits >> 10));
        out.push_back(static_cast<std::uint8_t>(bits >> 2));
    } else if (count == 1u) ok = false;
    return out;
}

bool verify_rsa_sha256(std::span<const std::uint8_t> modulus, std::uint32_t exponent, std::span<const std::uint8_t> digest,
                       std::span<const std::uint8_t> signature) noexcept {
    try {
        while (!modulus.empty() && modulus.front() == 0u) modulus = modulus.subspan(1);
        const std::size_t k = modulus.size();
        if (k < 128u || digest.size() != 32u || signature.size() != k || (modulus.back() & 1u) == 0u || exponent == 0u) return false;
        const std::size_t s = (k + 3u) / 4u;
        const Limbs n = from_bytes(modulus, s);
        const Limbs sig = from_bytes(signature, s);
        if (at_least(sig, n)) return false;
        // -n^-1 mod 2^32 (Newton's iteration on the odd low limb).
        std::uint32_t inv = 1u;
        for (int i = 0; i < 5; ++i) inv *= 2u - n[0] * inv;
        const std::uint32_t n0inv = 0u - inv;
        // R^2 mod n by doubling 1 2 * 32 * s times.
        Limbs r2(s, 0u);
        r2[0] = 1u;
        for (std::size_t i = 0; i < 64u * s; ++i) {
            std::uint32_t carry = 0;
            for (std::size_t j = 0; j < s; ++j) {
                const std::uint32_t next = r2[j] >> 31;
                r2[j] = r2[j] << 1 | carry;
                carry = next;
            }
            if (carry != 0u || at_least(r2, n)) subtract(r2, n);
        }
        const Limbs base = mont_mul(sig, r2, n, n0inv);
        Limbs x = base;
        int top = 31;
        while (((exponent >> top) & 1u) == 0u) --top;
        for (int bit = top - 1; bit >= 0; --bit) {
            x = mont_mul(x, x, n, n0inv);
            if ((exponent >> bit) & 1u) x = mont_mul(x, base, n, n0inv);
        }
        Limbs one(s, 0u);
        one[0] = 1u;
        x = mont_mul(x, one, n, n0inv);
        // EM = 00 01 FF..FF 00 DigestInfo(SHA-256) digest, compared as a whole.
        static constexpr std::uint8_t kDigestInfo[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                                       0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
        std::vector<std::uint8_t> expected(k, 0xFFu);
        expected[0] = 0x00u;
        expected[1] = 0x01u;
        const std::size_t tail = sizeof(kDigestInfo) + 32u;
        expected[k - tail - 1u] = 0x00u;
        std::memcpy(expected.data() + (k - tail), kDigestInfo, sizeof(kDigestInfo));
        std::memcpy(expected.data() + (k - 32u), digest.data(), 32u);
        unsigned differ = 0;
        for (std::size_t i = 0; i < k; ++i) {
            const std::uint8_t byte = static_cast<std::uint8_t>(x[(k - 1u - i) / 4u] >> (8u * ((k - 1u - i) % 4u)));
            differ |= static_cast<unsigned>(byte ^ expected[i]);
        }
        return differ == 0u;
    } catch (...) {
        return false;
    }
}

void set_release_key_for_tests(std::string_view modulus_base64) {
    g_modulus = modulus_base64.empty() ? kReleaseModulus : modulus_base64;
}

std::string_view release_key_modulus() { return kReleaseModulus; }

}  // namespace detail

bool verify_signature(std::string_view data, std::string_view signature_base64) noexcept {
    try {
        bool ok = false;
        const std::vector<std::uint8_t> modulus = detail::base64_decode(g_modulus, ok);
        if (!ok) return false;
        const std::vector<std::uint8_t> signature = detail::base64_decode(signature_base64, ok);
        if (!ok) return false;
        detail::Sha256 sha;
        sha.update(data.data(), data.size());
        const auto digest = sha.finish();
        return detail::verify_rsa_sha256(modulus, kReleaseExponent, digest, signature);
    } catch (...) {
        return false;
    }
}

}  // namespace galaxy::update
