// SPDX-License-Identifier: GPL-3.0-only
// The consent store (mod_consent.h): the digest, the facts, grant, decline, a changed hosts
// list (asked again), a reworded `why` (not asked again), a removed permission (its line dropped), code Not now, and
// the answers' round trip through the mod settings file.

#include "galaxy/luma.h"
#include "galaxy/mod_consent.h"
#include "galaxy/mod_import.h"
#include "galaxy/mods.h"
#include "galaxy/test_temp.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using galaxy::mods::Answer;

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

void write(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A .luma's folder with code for this platform and the given permissions (a JSON array).
std::string manifest(std::string_view permissions) {
    return std::string(R"({"format":"1.0","id":"asker","name":"Asker","version":"1.0.0","game":{"id":"RMG"},"layers":[{"name":"layer"}],)") +
           R"("code":{"platforms":[")" + std::string(galaxy::luma::plugin_target()) + R"("]},)" +
           R"("permissions":)" + std::string(permissions) + "}";
}

// The entry library of this platform in the mod's folder (luma.md 6).
fs::path entry_library(const fs::path& folder) {
    return folder / fs::path(galaxy::luma::code_entry(galaxy::luma::plugin_target()));
}

galaxy::mods::ModInfo* find(std::vector<galaxy::mods::ModInfo>& mods) {
    const auto found = std::find_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == "asker"; });
    return found != mods.end() ? &*found : nullptr;
}

bool digest() {
    bool ok = true;
    // Independent: FNV-1a 64 over "net.internet\0api.example.org\0b.example.org\0" (Python, consent.md 1.2).
    const galaxy::luma::Permission net{"net.internet", "Uploads times.", {"b.example.org", "API.example.org"}};
    ok &= expect(galaxy::mods::permission_digest(net) == "d84e3483f8dee40f", "digest: lowercased, sorted, 0 after each");
    const galaxy::luma::Permission reworded{"net.internet", "Another reason.", {"api.example.org", "B.EXAMPLE.ORG"}};
    ok &= expect(galaxy::mods::permission_digest(reworded) == "d84e3483f8dee40f", "digest: why and order and case are not in it");
    const galaxy::luma::Permission native{"native", "", {}};
    ok &= expect(galaxy::mods::permission_digest(native) == "485e8789e042913e", "digest: a name alone");
    return ok;
}

bool facts() {
    bool ok = true;
    const auto parsed = galaxy::luma::parse_manifest(
        manifest(R"([{"name":"net.internet","why":"Uploads times.","hosts":["api.example.org","*.example.net"]},)"
                 R"({"name":"native","why":"Its own library."}])"));
    ok &= expect(parsed.has_value() && parsed->permissions.size() == 2u, "the manifest's permissions are read");
    if (!parsed) return false;
    galaxy::mods::ModInfo mod;
    mod.id = "asker";
    mod.luma = std::make_shared<const galaxy::luma::Manifest>(*parsed);
    mod.code_token = "0123456789abcdef";
    const galaxy::mods::ModConsent none = galaxy::mods::consent_facts(mod);
    ok &= expect(none.code && none.code_answer == Answer::NotAsked && !none.code_changed, "code not asked");
    ok &= expect(none.permissions.size() == 2u && none.permissions[0].name == "native" && none.permissions[0].required &&
                     none.permissions[1].name == "net.internet" && !none.permissions[1].required,
                 "native first and required; the rest in the manifest's order");
    ok &= expect(none.permissions[1].row == "Permission.net.internet" && none.permissions[1].text_row == "Permission.net.internet.Text" &&
                     none.permissions[1].hosts == "api.example.org, *.example.net" && none.permissions[1].why == "Uploads times.",
                 "rows, hosts joined, why as written");
    ok &= expect(galaxy::mods::consent_needed(mod), "code not asked: asked");

    galaxy::mods::record_code_answer(mod, true);
    ok &= expect(galaxy::mods::consent_needed(mod), "code allowed, permissions not asked: asked");
    ok &= expect(galaxy::mods::record_answer(mod, "net.internet", true) && galaxy::mods::record_answer(mod, "native", false),
                 "answers recorded");
    ok &= expect(!galaxy::mods::consent_needed(mod), "everything answered: not asked");
    ok &= expect(!galaxy::mods::record_answer(mod, "audio.microphone", true), "an undeclared permission is not recorded");
    const std::vector<std::string> granted = galaxy::mods::granted_permissions(mod);
    ok &= expect(granted.size() == 1u && granted[0] == "net.internet", "only the granted, in the manifest's order");
    mod.required_permissions = {"net.internet"};
    ok &= expect(galaxy::mods::consent_facts(mod).permissions[1].required, "required from the kept report's list");

    galaxy::mods::record_code_answer(mod, false);
    mod.enabled = false;  // Not now turns it off (consent.md 2.3)
    ok &= expect(galaxy::mods::consent_facts(mod).code_answer == Answer::Declined && !galaxy::mods::consent_needed(mod),
                 "code Not now: declined, off, not asked again");
    mod.enabled = true;
    ok &= expect(galaxy::mods::consent_needed(mod), "code Not now, turned on again: asked (6402)");

    galaxy::mods::ModInfo content;
    content.id = "content";
    content.luma = mod.luma;
    ok &= expect(!galaxy::mods::consent_needed(content) && !galaxy::mods::consent_facts(content).code,
                 "no code: never asked (the validator's L218 says it)");

    // Dev mode (reload.md 4): a dev mod's code and every permission it declares count as allowed, unasked and unsaved.
    galaxy::mods::ModInfo dev;
    dev.id = "asker-dev";
    dev.luma = mod.luma;
    dev.code_token = "fedcba9876543210";
    dev.enabled = true;
    ok &= expect(galaxy::mods::consent_needed(dev), "not a dev mod: asked");
    galaxy::mods::set_dev_mods({"asker-dev"});
#if defined(__ANDROID__) || defined(__APPLE__) && TARGET_OS_IPHONE
    ok &= expect(!galaxy::mods::dev_mod("asker-dev") && galaxy::mods::consent_needed(dev), "no dev mode on a phone or tablet");
    galaxy::mods::set_dev_mods({});
    return ok;
#endif
    ok &= expect(galaxy::mods::dev_mod("asker-dev") && !galaxy::mods::dev_mod("asker"), "only the named mod is a dev mod");
    ok &= expect(!galaxy::mods::consent_needed(dev), "a dev mod: not asked");
    const std::vector<std::string> all = galaxy::mods::granted_permissions(dev);
    ok &= expect(all.size() == 2u && all[0] == "net.internet" && all[1] == "native", "a dev mod: every declared permission granted");
    ok &= expect(galaxy::mods::declined_permissions(dev).empty(), "a dev mod: none declined");
    ok &= expect(!dev.code_allowed && dev.permission_answers.empty(), "a dev mod's answers are not recorded");
    ok &= expect(galaxy::mods::consent_needed(mod), "another mod still asks in dev mode");
    galaxy::mods::set_dev_mods({});
    ok &= expect(galaxy::mods::consent_needed(dev), "dev mode over: asked again");
    return ok;
}

bool store() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mod_consent");
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path folder = root / "mods" / "asker";
    const std::string two_hosts = R"([{"name":"net.internet","why":"Uploads times.","hosts":["api.example.org"]},)"
                                  R"({"name":"system.links","why":"Opens its page."}])";
    write(folder / "luma.json", manifest(two_hosts));
    write(entry_library(folder), "code");
    write(folder / "layer" / "files" / "a.txt", "a");
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");

    std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods();
    galaxy::mods::ModInfo* mod = find(mods);
    ok &= expect(mod != nullptr && !mod->code_token.empty() && galaxy::mods::consent_needed(*mod), "found, with code, asked");
    if (mod == nullptr) return false;
    galaxy::mods::record_code_answer(*mod, true);
    galaxy::mods::record_answer(*mod, "net.internet", true);
    galaxy::mods::record_answer(*mod, "system.links", false);
    ok &= expect(galaxy::mods::save_mod_settings(mods), "saved");
    const std::string saved = read_all(root / "mods.ini");
    const std::string net_digest = galaxy::mods::permission_digest({"net.internet", "", {"api.example.org"}});
    ok &= expect(saved.find("permission.net.internet=yes " + net_digest + "\n") != std::string::npos &&
                     saved.find("permission.system.links=no ") != std::string::npos &&
                     saved.find("code_ok=" + mod->code_token + "\n") != std::string::npos,
                 "the lines of consent 1.2");

    // Read back: the same answers.
    mods = galaxy::mods::scan_mods();
    mod = find(mods);
    ok &= expect(mod != nullptr && mod->code_allowed && !galaxy::mods::consent_needed(*mod), "read back: nothing to ask");
    if (mod == nullptr) return false;
    const std::vector<std::string> granted = galaxy::mods::granted_permissions(*mod);
    ok &= expect(granted.size() == 1u && granted[0] == "net.internet", "read back: net.internet granted, system.links not");

    // A reworded why: not asked again.
    write(folder / "luma.json", manifest(R"([{"name":"net.internet","why":"Sends your best times.","hosts":["api.example.org"]},)"
                                         R"({"name":"system.links","why":"Opens its page."}])"));
    mods = galaxy::mods::scan_mods();
    mod = find(mods);
    ok &= expect(mod != nullptr && !galaxy::mods::consent_needed(*mod), "a reworded why is not asked again");

    // A host added: asked again, the old answer kept on disk until answered, granted no longer.
    write(folder / "luma.json", manifest(R"([{"name":"net.internet","why":"x","hosts":["api.example.org","cdn.example.org"]},)"
                                         R"({"name":"system.links","why":"Opens its page."}])"));
    mods = galaxy::mods::scan_mods();
    mod = find(mods);
    if (mod == nullptr) return false;
    const galaxy::mods::ModConsent changed = galaxy::mods::consent_facts(*mod);
    ok &= expect(galaxy::mods::consent_needed(*mod) && changed.permissions.size() == 2u &&
                     changed.permissions[0].answer == Answer::NotAsked && changed.permissions[0].changed &&
                     changed.permissions[1].answer == Answer::Declined && !changed.permissions[1].changed,
                 "a host added: that permission asked again, marked changed; the other's answer holds");
    ok &= expect(galaxy::mods::granted_permissions(*mod).empty(), "a changed declaration is not granted");

    // A permission removed: its line dropped at the next save.
    write(folder / "luma.json", manifest(R"([{"name":"system.links","why":"Opens its page."}])"));
    mods = galaxy::mods::scan_mods();
    ok &= expect(galaxy::mods::save_mod_settings(mods), "saved again");
    const std::string dropped = read_all(root / "mods.ini");
    ok &= expect(dropped.find("permission.net.internet") == std::string::npos && dropped.find("permission.system.links=no ") != std::string::npos,
                 "a permission no longer declared: its line dropped; the other kept");

    // Code Not now, then other libraries: asked again.
    mods = galaxy::mods::scan_mods();
    mod = find(mods);
    if (mod == nullptr) return false;
    galaxy::mods::record_code_answer(*mod, false);
    mod->enabled = false;  // Not now turns it off (consent.md 2.3)
    ok &= expect(galaxy::mods::save_mod_settings(mods) && read_all(root / "mods.ini").find("code_no=" + mod->code_token + "\n") != std::string::npos,
                 "code_no stored");
    mods = galaxy::mods::scan_mods();
    mod = find(mods);
    ok &= expect(mod != nullptr && mod->code_declined && !galaxy::mods::consent_needed(*mod), "Not now is not asked at the next start");
    write(entry_library(folder), "other code");
    mods = galaxy::mods::scan_mods();
    mod = find(mods);
    ok &= expect(mod != nullptr && !mod->code_declined && mod->code_changed && galaxy::mods::consent_needed(*mod),
                 "other libraries: asked again, marked changed");
    fs::remove_all(root, ec);
    return ok;
}

}  // namespace

// The mods that come with the game: installed with their code allowed, left alone while the player has that version or
// a later one, and brought up to date by a later one, its new code allowed too.
bool shipped() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mod_shipped");
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "shipped");
    fs::create_directories(root / "mods");
    const auto pack = [&](const char* version, const char* code, bool library) {
        archive* w = archive_write_new();
        archive_write_set_format(w, ARCHIVE_FORMAT_ZIP);
        archive_write_open_filename(w, (root / "shipped" / "kit.luma").string().c_str());
        const std::string json = std::string(R"({"format":"1.0","id":"kit","name":"Kit","version":")") + version +
                                 R"(","game":{"id":"RMG"},)" + (library ? R"("library":true,)" : "") +
                                 R"("layers":[{"name":"layer"}],"code":{"platforms":[")" +
                                 std::string(galaxy::luma::plugin_target()) + R"("]}})";
        const std::string entry = galaxy::luma::code_entry(galaxy::luma::plugin_target());
        for (const auto& [name, data] : std::vector<std::pair<std::string, std::string>>{
                 {"luma.json", json}, {entry, code}, {"layer/files/a.txt", "a"}}) {
            archive_entry* e = archive_entry_new();
            archive_entry_set_pathname(e, name.c_str());
            archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
            archive_entry_set_filetype(e, AE_IFREG);
            archive_entry_set_perm(e, 0644);
            archive_write_header(w, e);
            archive_write_data(w, data.data(), data.size());
            archive_entry_free(e);
        }
        archive_write_close(w);
        archive_write_free(w);
    };
    const auto kit = [](std::vector<galaxy::mods::ModInfo>& mods) -> galaxy::mods::ModInfo* {
        const auto found = std::find_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == "kit"; });
        return found != mods.end() ? &*found : nullptr;
    };
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    pack("1.0.0", "code one", false);
    std::vector<galaxy::mods::ShippedMod> done = galaxy::mods::install_shipped_mods(root / "shipped", root / "mods");
    ok &= expect(done.size() == 1u && done[0].ok && done[0].id == "kit" && done[0].version == "1.0.0" && done[0].file == "kit.luma",
                 "a shipped mod is installed");
    std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods();
    const galaxy::mods::ModInfo* mod = kit(mods);
    ok &= expect(mod != nullptr && !mod->code_token.empty() && mod->code_allowed && !galaxy::mods::consent_needed(*mod),
                 "its code is allowed: nothing to ask");
    ok &= expect(mod != nullptr && !galaxy::mods::built_in(*mod), "a shipped mod that is no library is listed like any other");
    const std::string first_token = mod != nullptr ? mod->code_token : std::string();
    ok &= expect(galaxy::mods::install_shipped_mods(root / "shipped", root / "mods").empty(), "the same version again: nothing done");
    pack("1.1.0", "code two", true);
    done = galaxy::mods::install_shipped_mods(root / "shipped", root / "mods");
    mods = galaxy::mods::scan_mods();
    mod = kit(mods);
    ok &= expect(done.size() == 1u && done[0].ok && mod != nullptr && mod->luma && mod->luma->version == "1.1.0" &&
                     mod->code_token != first_token && mod->code_allowed,
                 "a later shipped version updates it, its new code allowed");
    ok &= expect(mod != nullptr && galaxy::mods::built_in(*mod) && mod->enabled, "a shipped library is part of the game: built in and on");
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (mods[i].id == "kit") galaxy::mods::set_mod_enabled(mods, i, false);
    }
    (void)galaxy::mods::save_mod_settings(mods);
    mods = galaxy::mods::scan_mods();
    mod = kit(mods);
    ok &= expect(mod != nullptr && mod->enabled, "a shipped library stays on when the settings say off");
    pack("1.0.0", "code one", true);
    ok &= expect(galaxy::mods::install_shipped_mods(root / "shipped", root / "mods").empty(),
                 "an older shipped version leaves the player's later one");
    ok &= expect(galaxy::mods::install_shipped_mods(root / "none", root / "mods").empty(), "no shipped folder: nothing");
    fs::remove_all(root, ec);
    return ok;
}

int main() {
    // Every test runs, whichever fail.
    const bool results[] = {digest(), facts(), store(), shipped()};
    bool ok = true;
    for (const bool passed : results) ok = ok && passed;
    std::cout << (ok ? "mod consent tests passed" : "mod consent tests FAILED") << '\n';
    return ok ? 0 : 1;
}
