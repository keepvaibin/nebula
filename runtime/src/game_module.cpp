// SPDX-License-Identifier: GPL-3.0-only
// The choice between the two game modules at start (game_module.h).
#include "galaxy/game_module.h"

#include "galaxy/mod_support.h"
#include "galaxy/mods.h"

#include <atomic>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace galaxy::game_module {
namespace {
std::atomic<bool> g_plain{false};
}  // namespace

std::filesystem::path plain_beside(const std::filesystem::path& rows_module) {
    std::filesystem::path plain = rows_module;
    plain.replace_filename(rows_module.stem().native() + std::filesystem::path("_plain").native() + rows_module.extension().native());
    return plain;
}

std::filesystem::path module_in_app(const std::filesystem::path& frameworks, const std::filesystem::path& program_folder) {
    std::error_code error;
    if (!frameworks.empty()) {
        for (const std::filesystem::path& candidate :
             {frameworks / "libsmg_game.dylib", frameworks / "smg_game.framework" / "smg_game"}) {
            if (std::filesystem::exists(candidate, error)) return candidate;
        }
        // No mod support: only the plain module is in the app. The name of the rows module beside it stands for both.
        if (std::filesystem::exists(frameworks / "libsmg_game_plain.dylib", error)) return frameworks / "libsmg_game.dylib";
    }
    return program_folder.empty() ? std::filesystem::path{} : program_folder / "libsmg_game.dylib";
}

std::string load_failure(const std::filesystem::path& module, std::string_view system_text) {
    const auto utf8 = [](const std::filesystem::path& path) {
        const std::u8string text = path.u8string();
        return std::string(reinterpret_cast<const char*>(text.data()), text.size());
    };
    std::string text = "cannot load the game module " + utf8(module.filename());
    std::error_code error;
    if (!std::filesystem::exists(module, error)) {
        const std::filesystem::path folder = std::filesystem::absolute(module, error).parent_path();
        text += ": the file is not there (" + (folder.has_parent_path() ? utf8(folder.parent_path().filename()) + "/" : std::string{}) +
                utf8(folder.filename()) + ")";
        return text;
    }
    text += ": the system refused it: ";
    text += system_text.empty() ? std::string_view("it gave no reason") : system_text;
    return text;
}

std::filesystem::path choose(const std::filesystem::path& rows_module, const std::filesystem::path& mods_folder,
                             const std::filesystem::path& mods_file, const std::string& game_id, bool need_rows,
                             bool safe, const std::string& kept, std::string* why) {
    return choose_with(mod_support(), rows_module, mods_folder, mods_file, game_id, need_rows, safe, kept, why);
}

std::filesystem::path choose_with(bool with_mods, const std::filesystem::path& rows_module, const std::filesystem::path& mods_folder,
                                  const std::filesystem::path& mods_file, const std::string& game_id, bool need_rows,
                                  bool safe, const std::string& kept, std::string* why) {
    if (!with_mods) {
        if (why != nullptr) *why = "plain (this build has no mod support)";
        return plain_beside(rows_module);
    }
    std::string reason;
    if (need_rows) {
        reason = "--luma-describe reads the rows";
    } else if (!mods_folder.empty()) {
        // What the last run's crash left (galaxy::mods::take_safe_start, taken before this choice).
        const bool crashed = galaxy::mods::code_off_after_crash();
        const std::string crashed_mod = galaxy::mods::crashed_mod().id;
        galaxy::mods::set_mods_folder(mods_folder, mods_file, game_id);
        const std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods(game_id);
        // A library no mod that is on needs is not loaded (galaxy::mods::libraries_of): its code needs no rows.
        const std::set<std::string> idle = galaxy::mods::libraries_of(mods).idle;
        for (const galaxy::mods::ModInfo& mod : mods) {
            const bool dev = galaxy::mods::dev_mod(mod.id);
            if (!mod.enabled || mod.code_token.empty() || (!mod.code_allowed && !dev)) continue;
            if (mod.luma && idle.count(mod.luma->id) != 0u) continue;
            // The last run crashed with mods' code on: it stays off for this run (host_main.cpp svc_mods_start), so
            // the code needs no rows; a dev mod's stays on.
            if (crashed && !dev && !galaxy::mods::harness_half(mod.id)) continue;
            // The mod the last run's crash named is off for this run (host_main.cpp).
            if (mod.id == crashed_mod) continue;
            // A safe start turns every mod off for this start (host_main.cpp), all but `kept`.
            if (safe && mod.id != kept) continue;
            reason = safe ? "safe start: only mod " + mod.id + "'s code starts" : "mod " + mod.id + " has code";
            break;
        }
    }
    const std::filesystem::path plain = plain_beside(rows_module);
    if (reason.empty()) {
        std::error_code error;
        if (std::filesystem::is_regular_file(plain, error)) {
            if (why != nullptr) *why = safe ? "plain (safe start: no mod's code starts)" : "plain (no mod with code on)";
            return plain;
        }
        if (why != nullptr) *why = "rows (no mod with code on, but " + plain.filename().string() + " is not there)";
        return rows_module;
    }
    if (why != nullptr) *why = "rows (" + reason + ")";
    return rows_module;
}

void set_plain_loaded(bool plain) noexcept {
    g_plain.store(plain, std::memory_order_relaxed);
}

bool plain_loaded() noexcept {
    return g_plain.load(std::memory_order_relaxed);
}

}  // namespace galaxy::game_module
