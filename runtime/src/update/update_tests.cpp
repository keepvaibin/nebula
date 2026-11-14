// SPDX-License-Identifier: GPL-3.0-only
// The update core's rules (update.h) with a stand-in transport and platform: versions and channels, listing selection
// and malformed answers, notes, SHA-256 and RSA against known vectors, the release key's copy, the check's timing
// (a fresh answer within the budget, a late answer at the next start, offline without waiting, no request within the
// hour, a future-dated answer checked again), Skip this version, and the install's signed-manifest rules (the package
// installed, a replayed older manifest refused, a corrupt download refused). Usage: galaxy_update_tests [key.xml]
// (installer/release-public-key.xml: the built-in key must be its modulus).
#include "update_internal.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("%s: %s\n", ok ? "PASS" : "FAILED", what.c_str());
}

using namespace galaxy::update;
using Clock = std::chrono::steady_clock;

long long ms_since(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

constexpr std::string_view kTestModulus =
    "821RhhEolsNN7/9RB0ukYjEsDwzpJ7Lzy2QJut9jY98ZVIPm8oep6jn5Q8dMAerkQ1Y5ZWVcSJnzpGfv9X9uvhSZYnVhdz3I95UvOGA1w1XrrH9nUyTFP59NOV"
    "CMmnCmC1j8vap3C1x1jQDEas73Tfix8tJ6e+4sIsRajE67SFPFAd0UA18HCne2KIPYu1shHY/WuMc1DA8rRhMVyBReiRysBxyCq2ZF48L3I3AekOUclSehfYru1"
    "Dzw8jmXFpjOcfgI2V+V3wFR3IE271zHRXUsh9oDLAQXe+IJOS3vCrqYukBBG1O9R+fSisdanxa5ma+9WCESrtheYT2Fxn8S2Q==";
// RSACryptoServiceProvider.SignData(SHA256) with the test key over kSignedText.
constexpr std::string_view kSignedText = "version=9.9.9-beta.1\nsha256=x\nsize=1\n";
constexpr std::string_view kSignedTextSignature =
    "gn1fApWMKI4NB/CjhV2NYUyXLtdHIIM+aWjvRvmz1jEUsEEdiE8xh3xjppWHf06Wl9akEc0W5HIAMbUOmGRsXArPWubRVjX+l22gnsw5Gzx8H3VJ4aCasyhT8D"
    "u+/8/fcYtPRRn0eSDGrNyx2lPutBb1XmqAP3IsiCNR5M361+9eUnY6FJ5oYeNIbXvkHADjhqhHWbYLMwuFxDB1u6+H4K/c9E0aggLLG/LdDAN9Io7zKSzzrMeF"
    "4/Si79mSkFmF4tXcjckBxV12rM+qKzZhVOT9kRmJKaesVGZq9790Zd0ZuqSYQ4hk/twTezP/NCQXglLAlmJt13TWIh9z19GKdg==";
constexpr std::string_view kPackage = "nebula package 9.9.9-beta.1";
constexpr std::string_view kPackageSha = "06a6d2b2f0b8ec784d400a288149a8e496516e0bff6e144415723d0714830419";
// The same signing over "version=9.9.9-beta.1\nsha256=<kPackageSha>\nsize=27\n", a manifest that names no asset.
constexpr std::string_view kManifestNoAssetSignature =
    "7Dd/s+Xh8rS2GjX7YlX7PLLMlQxJqMZeJQeeIwfUyHjM02nNGVDNKPFoKx4zvxGm+inm+u7mUy+F7c3kEksJ6lbhPmuXgdHSp5uiwD67dFuvja+bCmjWLjJKEG"
    "6d49tM6z7SKDvJLAGm1JV2DBDWyXhTSMnGhiMine9ph6hlfNI0LBXSsd6N/2RXMPEhb6PiVbg62lSFwQGckM0HAsP8mjrfwbmK7qCHKRu+rzFPrlUezO7QsVP"
    "c1p8IDMZ4205PQSsgqo1bOcPG1/hAwYu/wEaDhA35wbWnqR9SdHD6KY8Fp8YC3SNJTTIm7+wWBWMIdl74G1OVxxUR5I+RZsVS7A==";
// Over "version=9.9.9-beta.1\nsha256=<kPackageSha>\nsize=27\nasset=Nebula-Beta-Test.pkg\n".
constexpr std::string_view kManifestSignature =
    "g59Hoaw7lp7cGCKt1Mu/cEB5vpERTbwM6LroFoauqTUyNM6HCQoI1o6HEQ53/qqHplY2Eo0tB3R1fa/IF1zGB19tB2GFz/jwG8GDnonMv8XGljmCFgBSA7Dw"
    "dUiMeNMD5rb5MCz3570tqZeGu3oeu6MFgbijceHVgn81oLOEl3UIhTend4J6swp7ka4EuBCQFELKnl5aWg16S0wbL1s3h5Es1RBQFTra2CuZ36MLyh4AcP5c"
    "BwTlsfRYXHTHb/S4TcsA7zTIDroEs4VIi/fbUk8NP8UANQ2B4oN23HU1iFe09cm0D7ynfsSMfGVQlz64kllwFrvZLt9xhn0QJJgicA==";
// Over "version=2.0.1-beta.1\nsha256=<kPackageSha>\nsize=27\nasset=Nebula-Beta-Test.pkg\n": an older release's manifest.
constexpr std::string_view kReplayedSignature =
    "heBgoFtzkdzQ/vIaNjp0hi1JLERjoBhdg/cPZNz9h9wKVoqb61NvNHNQBzCvU+l84GjNUlXJzN3az07tRvUJ68NcaYy7NZaT2FZwx/r9SGP8o0UgygTsXDNH"
    "9Sy5HR2H/hK2ruRO4XC3gJCZgFOp8zVqY3wO6t+2cS9m5Jf0BhJZcvNo2z/mZL0QQbPJPLjVsQpCHsXbV1bjEysM9FQRoeRS5HY0po66WmUzqxL1HlvQtUGq"
    "DHz4p/cbg2N7IELusDnMNsos7ikcqBKSTu5BGHgLVLIDtjpZvurk39zKVhNl7rjBISmOh+625rCyaU3YNdzttujVPPuaASPXL+f41g==";

constexpr std::string_view kAsset = "Nebula-Beta-Test.pkg";

std::string listing(const std::string& version, const std::string& notes) {
    const std::string base = "https://example.org/" + version + "/";
    return "[{\"tag_name\": \"v" + version + "\", \"draft\": false, \"prerelease\": true, \"body\": \"" + notes +
           "\", \"assets\": [{\"name\": \"Nebula-Beta-Test.pkg\", \"size\": " + std::to_string(kPackage.size()) +
           ", \"browser_download_url\": \"" + base + "pkg\"}, {\"name\": \"Nebula-Beta-Test.pkg.manifest\", \"browser_download_url\": \"" +
           base + "manifest\"}, {\"name\": \"Nebula-Beta-Test.pkg.manifest.sig\", \"browser_download_url\": \"" + base + "sig\"}]}]";
}

// What the stand-in server answers, set per case.
class FakeTransport final : public detail::Transport {
public:
    std::mutex mutex;
    std::string releases = "[]";
    int delay_ms = 0;
    bool offline = false;
    int requests = 0;
    std::string manifest, signature, package;

    int get(const std::string& url, std::string& body, std::size_t max_bytes, int) override {
        std::string answer;
        int delay = 0;
        {
            std::lock_guard lock(mutex);
            if (offline) return -1;
            if (url.find("api.github.com") != std::string::npos) {
                ++requests;
                answer = releases;
                delay = delay_ms;
            } else if (url.ends_with("/manifest")) {
                answer = manifest;
            } else if (url.ends_with("/sig")) {
                answer = signature;
            } else {
                return 404;
            }
        }
        if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if (answer.size() > max_bytes) return -2;
        body = answer;
        return 200;
    }
    int download(const std::string&, const std::filesystem::path& file, std::uint64_t max_bytes, const std::atomic_bool&,
                 const std::function<void(std::uint64_t)>& progress) override {
        std::string bytes;
        {
            std::lock_guard lock(mutex);
            bytes = package;
        }
        if (bytes.size() > max_bytes) return -2;
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << bytes;
        out.close();
        progress(bytes.size());
        return 200;
    }
    bool unreachable(const std::string&) override {
        std::lock_guard lock(mutex);
        return offline;
    }
};

FakeTransport g_transport;
std::filesystem::path g_installed_package;

void write_cache(const std::filesystem::path& data, const std::string& text) {
    std::ofstream out(data / "update-check.json", std::ios::binary | std::ios::trunc);
    out << text;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

// The stand-in platform.
namespace galaxy::update::detail {
Transport& platform_transport() { return g_transport; }
std::string platform_asset(bool beta) { return beta ? std::string(kAsset) : std::string("Nebula-Test.pkg"); }
bool platform_can_install() noexcept { return true; }
bool platform_install(const std::filesystem::path& package, std::string&) {
    g_installed_package = package;
    return true;
}
}  // namespace galaxy::update::detail

int main(int argc, char** argv) {
    // Versions and channels (Updater.CompareVersions' order).
    check(compare_versions("2.0.10", "2.0.9") > 0 && compare_versions("2.1.0", "2.1.0-beta.9") > 0 &&
              compare_versions("2.1.0-beta.10", "2.1.0-beta.9") > 0 && compare_versions("2.0.1", "2.0.1") == 0 &&
              compare_versions("", "0.0.0") < 0,
          "versions: numbers, a release after its betas, beta numbers");
    check(is_beta("2.1.0-beta.3") && is_beta("2.1.0-BETA.3") && !is_beta("2.1.0"), "beta channel by its -beta. label");

    // Listing selection.
    bool is_list = false;
    auto found = select_newer(listing("9.9.9-beta.1", "## News\\n\\n* One **fix**, [notes](https://example.org/n)."), "2.0.8-beta.1", kAsset, is_list);
    check(is_list && found && found->version == "9.9.9-beta.1" && found->package_bytes == kPackage.size() &&
              found->manifest_url.ends_with("/manifest") && found->manifest_signature_url.ends_with("/sig"),
          "a newer beta with its package, manifest and signature is selected");
    check(found && found->notes == "News\n\n- One fix, notes (https://example.org/n).", "its notes as plain text: " + (found ? found->notes : ""));
    check(!select_newer(listing("9.9.9", "x"), "2.0.8-beta.1", kAsset, is_list) && is_list, "a release is not offered to a beta install");
    check(!select_newer(listing("2.0.7-beta.1", "x"), "2.0.8-beta.1", kAsset, is_list), "an older beta is not offered");
    check(!select_newer("{\"message\": \"API rate limit exceeded\"}", "2.0.8-beta.1", kAsset, is_list) && !is_list,
          "a rate limit's message is no listing");
    check(!select_newer("<html>Sign in to the network</html>", "2.0.8-beta.1", kAsset, is_list) && !is_list, "a captive portal's page is no listing");
    check(!select_newer("[1, \"text\", null, {\"tag_name\": 5, \"assets\": [7, null]}]", "2.0.8-beta.1", kAsset, is_list) && is_list,
          "items that are not releases are passed over");
    std::string plain = listing("9.9.9-beta.1", "x");
    for (std::size_t at; (at = plain.find("https://example.org")) != std::string::npos;) plain.replace(at, 5, "http");
    check(!select_newer(plain, "2.0.8-beta.1", kAsset, is_list), "a release not served over HTTPS is not offered");
    check(plain_notes(std::string(3000, 'a'), 1500).size() <= 1500u + 4u, "notes are cut at their limit");

    // Crypto.
    {
        detail::Sha256 sha;
        sha.update("abc", 3);
        const auto d = sha.finish();
        check(d[0] == 0xbau && d[1] == 0x78u && d[31] == 0xadu, "SHA-256 of abc (FIPS 180-4 vector)");
        detail::Sha256 empty;
        const auto e = empty.finish();
        check(e[0] == 0xe3u && e[1] == 0xb0u && e[31] == 0x55u, "SHA-256 of nothing");
    }
    detail::set_release_key_for_tests(kTestModulus);
    check(verify_signature(kSignedText, kSignedTextSignature), "an RSA SHA-256 signature made by .NET verifies");
    check(!verify_signature(std::string(kSignedText) + " ", kSignedTextSignature), "a changed text does not");
    std::string bad(kSignedTextSignature);
    bad[10] = bad[10] == 'A' ? 'B' : 'A';
    check(!verify_signature(kSignedText, bad) && !verify_signature(kSignedText, "not base64!"), "a changed or malformed signature does not");
    if (argc > 1) {
        const std::string xml = read_file(argv[1]);
        const std::size_t a = xml.find("<Modulus>"), b = xml.find("</Modulus>");
        check(a != std::string::npos && b != std::string::npos && xml.substr(a + 9, b - a - 9) == detail::release_key_modulus(),
              "the built-in release key is installer/release-public-key.xml's");
    }

    // The service.
    const std::filesystem::path data = std::filesystem::temp_directory_path() / ("nebula-update-tests-" + std::to_string(std::rand()));
    std::filesystem::create_directories(data);
    detail::set_data_directory_for_tests(data);
    detail::set_transport_for_tests(&g_transport);
    const std::string installed = "2.0.8-beta.1";

    g_transport.releases = listing("9.9.9-beta.1", "fresh");
    galaxy::update::begin(installed);
    auto start = Clock::now();
    auto offer_now = offer(kFreshBudgetMs);
    long long waited = ms_since(start);
    check(offer_now && offer_now->notes == "fresh" && waited < kFreshBudgetMs,
          "a fresh answer inside the budget is offered at this start (" + std::to_string(waited) + " ms)");
    finish();
    check(g_transport.requests == 1, "one start, one request");
    detail::reset_for_tests();
    galaxy::update::begin(installed);
    start = Clock::now();
    offer_now = offer(kFreshBudgetMs);
    waited = ms_since(start);
    check(g_transport.requests == 1 && offer_now && offer_now->notes == "fresh" && waited < 100,
          "within the hour after an answer no request is made; the kept answer at once (" + std::to_string(waited) + " ms)");
    detail::reset_for_tests();

    write_cache(data, "{\"checkedUtc\": \"2020-01-01T00:00:00.0000000Z\"}");
    g_transport.releases = listing("9.9.9-beta.1", "late");
    g_transport.delay_ms = 1200;
    galaxy::update::begin(installed);
    start = Clock::now();
    offer_now = offer(kFreshBudgetMs);
    waited = ms_since(start);
    check(!offer_now && waited >= kFreshBudgetMs - 20 && waited < kFreshBudgetMs + 250,
          "an answer after the budget is not waited for (" + std::to_string(waited) + " ms)");
    finish();
    g_transport.delay_ms = 0;
    detail::reset_for_tests();
    const int before_late = g_transport.requests;
    galaxy::update::begin(installed);
    offer_now = poll();
    check(offer_now && offer_now->notes == "late" && g_transport.requests == before_late,
          "the late answer is offered at the next start, with no request");
    detail::reset_for_tests();

    std::string kept = read_file(data / "update-check.json");
    const std::size_t stamp = kept.find("\"checkedUtc\": \"") + 15;
    kept.replace(stamp, 20, "2020-01-01T00:00:00Z");
    write_cache(data, kept);
    g_transport.offline = true;
    galaxy::update::begin(installed);
    start = Clock::now();
    offer_now = offer(kFreshBudgetMs);
    waited = ms_since(start);
    check(offer_now && offer_now->notes == "late" && waited < kFreshBudgetMs,
          "offline the wait ends at once and the kept answer is offered (" + std::to_string(waited) + " ms)");
    finish();
    check(read_file(data / "update-check.json").find("2020-01-01T00:00:00Z") != std::string::npos,
          "a failed check keeps the last answer and its time");
    g_transport.offline = false;
    detail::reset_for_tests();

    kept = read_file(data / "update-check.json");
    kept.replace(kept.find("2020-01-01T00:00:00Z"), 20, "2099-01-01T00:00:00Z");
    write_cache(data, kept);
    const int before_future = g_transport.requests;
    galaxy::update::begin(installed);
    finish();
    check(g_transport.requests == before_future + 1, "an answer dated after now is checked again");

    skip("9.9.9-beta.1");
    check(!poll(), "a skipped version is not offered");
    detail::reset_for_tests();
    galaxy::update::begin(installed);
    check(!poll(), "nor at the next start");
    detail::reset_for_tests();
    write_cache(data, "{\"checkedUtc\": \"2099-01-01T00:00:00Z\", \"update_check\": false}");
    galaxy::update::begin(installed);
    check(!poll() && g_transport.requests == before_future + 1, "the check turned off offers nothing and asks nothing");
    detail::reset_for_tests();

    // The signed manifest's rules without a signature (detail::manifest_takes): the asset ties it to this build.
    {
        const std::string sha(kPackageSha);
        const std::string ok_text = "version=9.9.9-beta.1\r\nsha256=" + sha + "\r\nsize=27\r\nasset=Nebula-Beta-Test.pkg\r\n";
        const detail::Manifest ok = detail::parse_manifest(ok_text);
        check(ok.version == "9.9.9-beta.1" && ok.sha256 == sha && ok.size == 27u && ok.asset == kAsset,
              "a manifest's version, SHA-256, size and asset are read (CR LF lines)");
        check(detail::manifest_takes(ok, "9.9.9-beta.1", installed, kAsset), "a manifest of this release and this build's asset is taken");
        check(!detail::manifest_takes(ok, "9.9.9-beta.1", installed, "Nebula-Test.pkg"),
              "a manifest signed for another asset (platform or channel) is refused");
        check(!detail::manifest_takes(detail::parse_manifest("version=9.9.9-beta.1\nsha256=" + sha + "\nsize=27\n"), "9.9.9-beta.1", installed, kAsset),
              "a manifest without an asset is refused");
        check(!detail::manifest_takes(ok, "9.9.9-beta.1", installed, ""), "no asset for this build takes no manifest");
        check(!detail::manifest_takes(ok, "9.9.9-beta.2", installed, kAsset) && !detail::manifest_takes(ok, "9.9.9-beta.1", "9.9.9-beta.1", kAsset),
              "a manifest of another or a not newer version is refused");
    }

    // The install: the signed manifest decides.
    write_cache(data, "{\"checkedUtc\": \"2020-01-01T00:00:00Z\"}");
    g_transport.releases = listing("9.9.9-beta.1", "notes");
    galaxy::update::begin(installed);
    const auto release = offer(kFreshBudgetMs);
    const auto wait_install = [] {
        for (int i = 0; i < 500; ++i) {
            const InstallState state = install_progress().state;
            if (state == InstallState::Restart || state == InstallState::Failed || state == InstallState::Canceled) return state;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return InstallState::Idle;
    };
    const std::string asset_line = "asset=" + std::string(kAsset) + "\n";
    g_transport.manifest = "version=9.9.9-beta.1\nsha256=" + std::string(kPackageSha) + "\nsize=" + std::to_string(kPackage.size()) + "\n";
    g_transport.signature = std::string(kManifestNoAssetSignature) + "\n";
    g_transport.package = std::string(kPackage);
    check(release && start_install(*release) && wait_install() == InstallState::Failed && g_installed_package.empty(),
          "a validly signed manifest that names no asset is refused");
    g_transport.manifest += asset_line;
    g_transport.signature = std::string(kManifestSignature) + "\n";
    check(release && start_install(*release) && wait_install() == InstallState::Restart && read_file(g_installed_package) == kPackage,
          "a package matching its signed manifest is installed");
    g_installed_package.clear();
    g_transport.manifest =
        "version=2.0.1-beta.1\nsha256=" + std::string(kPackageSha) + "\nsize=" + std::to_string(kPackage.size()) + "\n" + asset_line;
    g_transport.signature = std::string(kReplayedSignature);
    check(release && start_install(*release) && wait_install() == InstallState::Failed && g_installed_package.empty(),
          "a signed manifest of an older version under this release is refused (replay, downgrade)");
    g_transport.manifest = "version=9.9.9-beta.1\nsha256=" + std::string(kPackageSha) + "\nsize=" + std::to_string(kPackage.size()) + "\n" + asset_line;
    g_transport.signature = std::string(kManifestSignature);
    g_transport.package = "nebula package 9.9.9-beta.X";
    check(release && start_install(*release) && wait_install() == InstallState::Failed && g_installed_package.empty(),
          "a package that is not the signed bytes is refused");
    g_transport.manifest += "extra=1\n";
    check(release && start_install(*release) && wait_install() == InstallState::Failed, "a manifest changed after signing is refused");
    detail::reset_for_tests();
    detail::set_release_key_for_tests({});
    check(!verify_signature(kSignedText, kSignedTextSignature), "the release key does not take the test key's signature");

    std::error_code ignored;
    std::filesystem::remove_all(data, ignored);
    std::printf("%s: %d update core checks, %d failed\n", g_failures == 0 ? "PASS" : "FAILED", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
