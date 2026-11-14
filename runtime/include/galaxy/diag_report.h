// SPDX-License-Identifier: GPL-3.0-only
// The benchmark's diagnostics report, sealed to the developer's public key and sent once on the player's press
// (docs/DIAGNOSTICS_UPLOAD.md). Sealing is HPKE base mode (RFC 9180: DHKEM(X25519, HKDF-SHA256), HKDF-SHA256,
// ChaCha20-Poly1305); the client holds no secret. The host modes (--send-report, --save-sealed, --report-enabled,
// --open-report, --report-keygen) run before the renderer, the game module or the pak are touched.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::diag {

using Bytes = std::vector<std::uint8_t>;
using Key = std::array<std::uint8_t, 32>;

// ---- primitives (diag_seal.cpp) ----
std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size);
std::array<std::uint8_t, 32> hmac_sha256(const std::uint8_t* key, std::size_t key_size, const std::uint8_t* data,
                                         std::size_t size);
Bytes hkdf_expand(const std::uint8_t* prk, const std::uint8_t* info, std::size_t info_size, std::size_t length);
Key x25519(const Key& scalar, const Key& point);
Key x25519_public(const Key& secret);
// RFC 8439 AEAD; open returns false when the tag does not match (out is then empty).
Bytes chacha20_poly1305_seal(const Key& key, const std::uint8_t* nonce12, const std::uint8_t* aad, std::size_t aad_size,
                             const std::uint8_t* plain, std::size_t plain_size);
bool chacha20_poly1305_open(const Key& key, const std::uint8_t* nonce12, const std::uint8_t* aad, std::size_t aad_size,
                            const std::uint8_t* sealed, std::size_t sealed_size, Bytes& out);
// False when the system's random source fails.
bool random_bytes(std::uint8_t* out, std::size_t size);

// ---- HPKE base mode, the one suite ----
struct HpkeContext {
    Key key{};
    std::array<std::uint8_t, 12> base_nonce{};
};
// DeriveKeyPair (RFC 9180 7.1.3).
Key hpke_derive_secret(const std::uint8_t* ikm, std::size_t ikm_size);
// SetupBaseS with the ephemeral secret given (the tests' fixed ikmE); false for an all-zero shared secret.
bool hpke_setup_sender(const Key& recipient_public, const Key& ephemeral_secret, std::string_view info, Key& enc,
                       HpkeContext& context);
bool hpke_setup_recipient(const Key& recipient_secret, const Key& enc, std::string_view info, HpkeContext& context);

// ---- the envelope (version 1) ----
inline constexpr std::size_t kEnvelopeHeader = 48;
inline constexpr std::size_t kEnvelopeMax = 256u * 1024u;
inline constexpr std::size_t kReportMax = 1024u * 1024u;  // decompressed
inline constexpr std::string_view kHpkeInfo = "Nebula diagnostics report v1";
std::array<std::uint8_t, 8> key_id(const Key& public_key);
// Compresses and seals; empty on failure (random source, size).
Bytes seal_report(std::string_view report, const Key& recipient_public);
enum class OpenResult { Ok, NotEnvelope, OtherKey, Tampered, BadContent };
OpenResult open_report(const std::uint8_t* envelope, std::size_t size, const Key& recipient_secret, std::string& report);

// ---- the report (diag_report.cpp) ----
// Empty when the document holds a field, type or size the benchmark's schema 1 does not have; else the reason.
std::string schema_error(std::string_view benchmark_json);
// The plaintext a report seals: {"report":1,"benchmark":...,"log":[...]}; schema_error applies to the whole.
std::string report_error(std::string_view report_json);
std::string make_report(std::string_view benchmark_json, const std::vector<std::string>& log_lines, bool with_log);
// One log line with every path, e-mail address, user name and computer name replaced (docs/DIAGNOSTICS_UPLOAD.md).
std::string strip_log_line(std::string_view line, const std::vector<std::string>& names);
// The last lines of a log, stripped: at most 200 lines and 32 KiB.
std::vector<std::string> log_tail(const std::string& text, const std::vector<std::string>& names);

bool key_from_hex(std::string_view hex, Key& key);
std::string hex(const std::uint8_t* data, std::size_t size);

// True when this build names an endpoint and a valid public key (diag_endpoint.h); apps hide the choice otherwise.
bool uploads_enabled();

enum Exit : int {
    kOk = 0, kUsage = 1, kDisabled = 2, kInvalid = 3, kNetwork = 4, kRefused = 5, kWrite = 6, kNotOpened = 7, kAlreadySent = 8
};
struct SendResult {
    int exit = kUsage;
    std::string line;  // the one result line, without the "[diag] " prefix
};
// Exactly the text --send-report would seal (the "Show the full report" view); exit kInvalid when refused.
SendResult report_preview(const std::filesystem::path& file, bool with_log, std::string& report);
// --send-report's work, for an app that calls it in its own process (on a worker thread: it blocks up to 10 s).
// Once per result: a sent report leaves <file>.sent beside it, and a second send answers kAlreadySent.
SendResult send_report_file(const std::filesystem::path& file, bool with_log);
// One HTTPS POST of an envelope (10 s, no redirects, no retry). allow_loopback_http is for the unit tests only:
// http://127.0.0.1 is then accepted.
SendResult post_envelope(std::string_view url, const Bytes& body, bool allow_loopback_http = false);

// The host modes; -1 when argv[1] names none of them.
int run_command_line(int argc, char** argv);

}  // namespace galaxy::diag
