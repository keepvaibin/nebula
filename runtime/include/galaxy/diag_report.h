// SPDX-License-Identifier: GPL-3.0-only
// The benchmark's diagnostics report, sealed to the developer's public key and sent once on the player's press
// (the default answer is No). Sealing is HPKE base mode (RFC 9180: DHKEM(X25519, HKDF-SHA256), HKDF-SHA256,
// ChaCha20-Poly1305); the client holds no secret and the server stores only ciphertext. The host modes
// (--send-report, --save-sealed, --report-enabled, --open-report, --report-keygen) run before the renderer, the game
// module or the pak are touched. A build whose diag_endpoint.h is empty refuses --send-report (exit 2) and the apps
// hide the choice.
//
// The report: {"report":1,"benchmark":<the --benchmark 1 result, schema 1>,"log":[<lines>]}. "log" only when the
// player ticks "Include the last game log": the last 200 lines (32 KiB at most) of the last run's log, each stripped
// (strip_log_line). Never in a report: saves, Miis, settings files, mods, paths, user or computer names, serials.
//
// The envelope, version 1 (at most 256 KiB; an opened report at most 1 MiB, refused when larger):
//   offset 0, 4 bytes   magic "NBDR"
//   offset 4, 1 byte    version 1
//   offset 5, 1 byte    compression 1: a zlib stream (RFC 1950)
//   offset 6, 2 bytes   reserved, 0
//   offset 8, 8 bytes   key id: the first 8 bytes of SHA-256(public key)
//   offset 16, 32 bytes HPKE enc: the report's own ephemeral X25519 public key
//   offset 48           ChaCha20-Poly1305 ciphertext and tag of the compressed report
// Suite 0x0020 / 0x0001 / 0x0003, info kHpkeInfo, aad bytes 0 to 47 (a changed header fails the tag), one message.
// Sent as one POST, Content-Type application/x-nebula-report, HTTPS only, 10 seconds, no redirect, retry or cookie.
//
// Exit codes of --send-report: 0 sent (<file>.sent written beside it), 1 usage, 2 uploads off in this build, 3 the
// file is refused (unreadable, schema, size), 4 network (no connection, timeout, TLS, no HTTPS client), 5 the server
// refused (its status printed), 6 not written (--save-sealed, --report-keygen), 7 not a report, another key or
// changed (--open-report), 8 this result was sent before.
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

// ---- keys (diag_seal.cpp, BoringSSL) ----
// False when the system's random source fails.
bool random_bytes(std::uint8_t* out, std::size_t size);
Key x25519_public(const Key& secret);

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
// One log line with every path, email address, user name and computer name replaced: bytes outside printable ASCII
// become '?'; the user, home folder and computer names, in any case, become <user>; a token (a run between spaces,
// quotes, brackets, commas, semicolons, '=' and '|') becomes <path> when it holds a backslash, a slash and a letter,
// or starts with '~' or a drive letter, <email> when it holds '@' with a dot after it, <address> when it is four
// dot-separated numbers; the line is cut at 200 bytes.
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
