// SPDX-License-Identifier: GPL-3.0-only
// The consent store (mod_consent.h, contracts/consent.md 1.5): the digest, the facts, grant, decline, a changed hosts
// list (asked again), a reworded `why` (not asked again), a removed permission (its line dropped), code Not now, and
// the answers' round trip through the mod settings file.

#include "galaxy/luma.h"
#include "galaxy/mod_consent.h"
#include "galaxy/mods.h"
#include "galaxy/test_temp.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    return std::string(R"({"format":1,"id":"asker","name":"Asker","version":"1.0.0","game":"RMG","layers":[{"path":"layer"}],)") +
           R"("plugins":[{"api":3,"binaries":{")" + std::string(galaxy::luma::plugin_target()) + R"(":"code/asker.bin"}}],)" +
           R"("permissions":)" + std::string(permissions) + "}";
}

galaxy::mods::ModInfo* find(std::vector<galaxy::mods::ModInfo>& mods) {
    const auto found = std::find_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == "asker"; });
    return found != mods.end() ? &*found : nullptr;
}

bool digest() {
    bool ok = true;
    // Independent: FNV-1a 64 over "net.internet\0api.example.org\0b.example.org\0" (Python, consent 1.2).
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
    mod.enabled = false;  // Not now turns it off (consent 2.3)
    ok &= expect(galaxy::mods::consent_facts(mod).code_answer == Answer::Declined && !galaxy::mods::consent_needed(mod),
                 "code Not now: declined, off, not asked again");
    mod.enabled = true;
    ok &= expect(galaxy::mods::consent_needed(mod), "code Not now, turned on again: asked (6402)");

    galaxy::mods::ModInfo content;
    content.id = "content";
    content.luma = mod.luma;
    ok &= expect(!galaxy::mods::consent_needed(content) && !galaxy::mods::consent_facts(content).code,
                 "no code: never asked (the validator's L218 says it)");
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
    write(folder / "code" / "asker.bin", "code");
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
    mod->enabled = false;  // Not now turns it off (consent 2.3)
    ok &= expect(galaxy::mods::save_mod_settings(mods) && read_all(root / "mods.ini").find("code_no=" + mod->code_token + "\n") != std::string::npos,
                 "code_no stored");
    mods = galaxy::mods::scan_mods();
    mod = find(mods);
    ok &= expect(mod != nullptr && mod->code_declined && !galaxy::mods::consent_needed(*mod), "Not now is not asked at the next start");
    write(folder / "code" / "asker.bin", "other code");
    mods = galaxy::mods::scan_mods();
    mod = find(mods);
    ok &= expect(mod != nullptr && !mod->code_declined && mod->code_changed && galaxy::mods::consent_needed(*mod),
                 "other libraries: asked again, marked changed");
    fs::remove_all(root, ec);
    return ok;
}

}  // namespace

int main() {
    // Every test runs, whichever fail.
    const bool results[] = {digest(), facts(), store()};
    bool ok = true;
    for (const bool passed : results) ok = ok && passed;
    std::cout << (ok ? "mod consent tests passed" : "mod consent tests FAILED") << '\n';
    return ok ? 0 : 1;
}
