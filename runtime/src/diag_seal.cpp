// SPDX-License-Identifier: GPL-3.0-only
// The diagnostics report's envelope (diag_report.h): the report compressed with zlib, then sealed with HPKE base mode
// (RFC 9180) for DHKEM(X25519, HKDF-SHA256), HKDF-SHA256 and ChaCha20-Poly1305, from BoringSSL.
#include "galaxy/diag_report.h"

#include <openssl/curve25519.h>
#include <openssl/hpke.h>
#include <openssl/rand.h>
#include <openssl/sha2.h>
#include <zlib.h>

#include <cstring>
#include <utility>

namespace galaxy::diag {
namespace {

constexpr std::uint8_t kMagic[4] = {'N', 'B', 'D', 'R'};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kCompressionZlib = 1;

const std::uint8_t* info_bytes() { return reinterpret_cast<const std::uint8_t*>(kHpkeInfo.data()); }

}  // namespace

bool random_bytes(std::uint8_t* out, std::size_t size) { return RAND_bytes(out, size) == 1; }

Key x25519_public(const Key& secret) {
    Key out{};
    X25519_public_from_private(out.data(), secret.data());
    return out;
}

std::array<std::uint8_t, 8> key_id(const Key& public_key) {
    std::uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(public_key.data(), public_key.size(), digest);
    std::array<std::uint8_t, 8> id{};
    std::memcpy(id.data(), digest, id.size());
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
    bssl::ScopedEVP_HPKE_CTX context;
    std::uint8_t enc[EVP_HPKE_MAX_ENC_LENGTH];
    std::size_t enc_size = 0;
    if (!EVP_HPKE_CTX_setup_sender(context.get(), enc, &enc_size, sizeof(enc), EVP_hpke_x25519_hkdf_sha256(),
                                   EVP_hpke_hkdf_sha256(), EVP_hpke_chacha20_poly1305(), recipient_public.data(),
                                   recipient_public.size(), info_bytes(), kHpkeInfo.size()) ||
        enc_size != 32u) {
        return {};
    }
    // The header is the seal's associated data: magic, version, compression, two zero bytes, the key id, enc.
    Bytes out(kEnvelopeHeader + packed.size() + EVP_HPKE_CTX_max_overhead(context.get()));
    std::memcpy(out.data(), kMagic, 4u);
    out[4] = kVersion;
    out[5] = kCompressionZlib;
    const auto id = key_id(recipient_public);
    std::memcpy(out.data() + 8, id.data(), 8u);
    std::memcpy(out.data() + 16, enc, 32u);
    std::size_t sealed_size = 0;
    if (!EVP_HPKE_CTX_seal(context.get(), out.data() + kEnvelopeHeader, &sealed_size, out.size() - kEnvelopeHeader,
                           packed.data(), packed.size(), out.data(), kEnvelopeHeader)) {
        return {};
    }
    out.resize(kEnvelopeHeader + sealed_size);
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
    bssl::ScopedEVP_HPKE_KEY key;
    bssl::ScopedEVP_HPKE_CTX context;
    Bytes packed(size - kEnvelopeHeader);
    std::size_t packed_size = 0;
    if (!EVP_HPKE_KEY_init(key.get(), EVP_hpke_x25519_hkdf_sha256(), recipient_secret.data(), recipient_secret.size()) ||
        !EVP_HPKE_CTX_setup_recipient(context.get(), key.get(), EVP_hpke_hkdf_sha256(), EVP_hpke_chacha20_poly1305(),
                                      envelope + 16, 32u, info_bytes(), kHpkeInfo.size()) ||
        !EVP_HPKE_CTX_open(context.get(), packed.data(), &packed_size, packed.size(), envelope + kEnvelopeHeader,
                           size - kEnvelopeHeader, envelope, kEnvelopeHeader)) {
        return OpenResult::Tampered;
    }
    packed.resize(packed_size);
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
