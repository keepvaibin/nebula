// SPDX-License-Identifier: GPL-3.0-only
// Nebula's update check and install on the hosts without a launcher app of their own (Linux, macOS), with the rules of
// the Windows launcher's UpdateService (installer/src/Common/UpdateService.cs): the check starts as the process starts,
// a start takes its answer within kFreshBudgetMs or else the last kept answer, offline ends the wait at once, within an
// hour of an answer no request is made, a skipped version is offered again only when a newer one comes, a beta install
// takes betas only and a release releases only, and a package is installed only when its signed manifest (the Windows
// setup's release key) names this release's version, newer than the running one, and the package's size and SHA-256.
//
// Threading: every function may be called from any thread. begin, poll, skip, can_install, install_progress and
// cancel_install never block; offer blocks at most its budget; finish at most the check's timeout (15 s).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace galaxy::update {

// A healthy GitHub release listing answers in about 150 to 350 ms (TLS set-up and the listing): 400 ms takes it and
// costs a slow network at most that.
inline constexpr int kFreshBudgetMs = 400;

struct Release {
    std::string version;  // "2.1.0" or "2.1.0-beta.3"
    std::string notes;    // plain text, UTF-8, at most 1500 bytes
    std::string tag;
    std::string package_url;
    std::string manifest_url;
    std::string manifest_signature_url;
    std::uint64_t package_bytes = 0;
};

// Starts the check behind the caller unless the last answer is under an hour old (and not dated after now). Does
// nothing when the check is off: NEBULA_NO_UPDATE_CHECK=1 in the environment, "update_check": false in
// update-check.json, or a platform without a package of its own (Android, iOS: their stores).
void begin(std::string_view installed_version);

// The release to offer now: the check's answer if it comes within budget_ms (or the server proves unreachable first),
// else the last kept answer; nothing when it is not newer than this build, of the other channel, or no newer than the
// skipped version.
[[nodiscard]] std::optional<Release> offer(int budget_ms);

// As offer(0): never waits.
[[nodiscard]] std::optional<Release> poll();

// Offers nothing up to and including `version` again.
void skip(std::string_view version);

// False where the system updates Nebula (a store, a distribution's package, Flatpak, a build that is not an AppImage).
[[nodiscard]] bool can_install() noexcept;

enum class InstallState : unsigned { Idle, Downloading, Verifying, Installing, Restart, Failed, Canceled };
struct InstallProgress {
    InstallState state = InstallState::Idle;
    std::uint64_t bytes = 0;
    std::uint64_t total = 0;
    std::string error;  // English, for the log; the menu shows its own text per state
};

// Downloads, checks and installs `release` on a thread of its own; Restart when the new version is in place. False when
// an install runs already or this build cannot install.
bool start_install(const Release& release);
[[nodiscard]] InstallProgress install_progress();
void cancel_install();

// Waits for a running check so its answer is kept for the next start; at once when none runs.
void finish();

// The rules, shared with the platform files and the tests.
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b) noexcept;
[[nodiscard]] bool is_beta(std::string_view version) noexcept;
// The newest release in a GitHub release listing newer than `installed`, of its channel, with `asset`, its manifest and
// the manifest's signature served over HTTPS (HTTP to 127.0.0.1 only). is_list false: the answer is no release list
// (a rate limit's message, a captive portal's page).
[[nodiscard]] std::optional<Release> select_newer(std::string_view listing_json, std::string_view installed, std::string_view asset,
                                                  bool& is_list);
// RSA PKCS#1 v1.5 with SHA-256 over `data`, against the Nebula release key.
[[nodiscard]] bool verify_signature(std::string_view data, std::string_view signature_base64) noexcept;
// Release notes as GitHub holds them (Markdown) as plain text, cut at a line end before `limit` bytes.
[[nodiscard]] std::string plain_notes(std::string_view markdown, std::size_t limit);

}  // namespace galaxy::update
