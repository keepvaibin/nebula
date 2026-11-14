// SPDX-License-Identifier: GPL-3.0-only
// What update.cpp asks of a platform (update_transport_*.cpp, update_install_*.cpp) and what the tests replace.
#pragma once

#include "galaxy/update.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::update::detail {

// HTTP over the platform's own stack (its TLS, its proxy settings). Status codes as HTTP gives them; a negative value for
// a request that got no answer (no network, no name, refused, timed out, canceled).
class Transport {
public:
    virtual ~Transport() = default;
    // GET `url` into `body`, at most `max_bytes` (more: -2).
    virtual int get(const std::string& url, std::string& body, std::size_t max_bytes, int timeout_ms) = 0;
    // GET `url` appended to `file` from its current length (a Range request; a 200 answer starts the file again), at most
    // `max_bytes` in all. `progress` gets the file's length.
    virtual int download(const std::string& url, const std::filesystem::path& file, std::uint64_t max_bytes,
                         const std::atomic_bool& cancel, const std::function<void(std::uint64_t)>& progress) = 0;
    // True when the server of `url` cannot be reached directly: no network, its name not resolving, its port refusing.
    // False when it can, or when a proxy stands between (only the request can tell then).
    virtual bool unreachable(const std::string& url) = 0;
    // POST `body` to an https `url` once (no redirect followed, the answer's body dropped), within `timeout_ms` in all:
    // the diagnostics report's send (diag_report.h). The status, or a negative value without an answer.
    virtual int post(const std::string& url, std::string_view content_type, std::span<const std::uint8_t> body,
                     int timeout_ms) {
        (void)url;
        (void)content_type;
        (void)body;
        (void)timeout_ms;
        return -1;
    }
};

Transport& platform_transport();

// update_probe_posix.cpp: Transport::unreachable for Linux and Apple's systems, without a proxy.
bool posix_unreachable(const std::string& url, int connect_ms);

// The release asset this build installs ("" for none: the check is off), and the platform's install step.
std::string platform_asset(bool beta);
bool platform_can_install() noexcept;
// Puts the verified package in place of the running version, keeping the running one for rollback.
bool platform_install(const std::filesystem::path& package, std::string& error);

// The signed manifest (`<asset>.manifest`, key=value lines; a CR before the newline is dropped, a repeated key keeps the
// last): the release's version, the package's SHA-256 and size, and the asset it was signed for, `asset=` +
// platform_asset's name. One key signs every platform's manifest, so the name is what ties one to this build.
struct Manifest {
    std::string version;
    std::string sha256;
    std::string asset;
    std::uint64_t size = 0;
};
Manifest parse_manifest(std::string_view text);
// Whether an install takes `manifest` for the release `version` over `installed` with this build's `asset`: the same
// version, newer than the installed one (no replay, no downgrade), a 64-digit SHA-256, a size within the bound, and the
// same asset name (a manifest of another platform's or channel's package, or one without a name, is refused).
bool manifest_takes(const Manifest& manifest, std::string_view version, std::string_view installed, std::string_view asset);

// Tests: a transport, a data folder and a clock of theirs; reset forgets this process's check and install.
void set_transport_for_tests(Transport* transport);
void set_data_directory_for_tests(const std::filesystem::path& directory);
void reset_for_tests();
// Tests: verify_signature against this modulus (base64, exponent 65537); empty = the release key again.
void set_release_key_for_tests(std::string_view modulus_base64);
std::string_view release_key_modulus();

// SHA-256 (FIPS 180-4).
class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t bytes);
    std::array<std::uint8_t, 32> finish();

private:
    void block(const std::uint8_t* data);
    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;
};

std::vector<std::uint8_t> base64_decode(std::string_view text, bool& ok);

// RSA PKCS#1 v1.5 SHA-256 verification against a big-endian modulus and exponent (the release key's in verify_signature).
bool verify_rsa_sha256(std::span<const std::uint8_t> modulus, std::uint32_t exponent, std::span<const std::uint8_t> digest,
                       std::span<const std::uint8_t> signature) noexcept;

}  // namespace galaxy::update::detail
