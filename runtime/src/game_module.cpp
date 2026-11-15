// SPDX-License-Identifier: GPL-3.0-only
// The choice between the two game modules at start (game_module.h).
#include "galaxy/game_module.h"

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

std::filesystem::path choose(const std::filesystem::path& rows_module, const std::filesystem::path& mods_folder,
                             const std::filesystem::path& mods_file, const std::string& game_id, bool need_rows,
                             bool safe, const std::string& kept, std::string* why) {
    std::string reason;
    if (need_rows) {
        reason = "--luma-describe reads the rows";
    } else if (!mods_folder.empty()) {
        std::error_code error;
        const bool crashed = std::filesystem::exists(mods_folder / ".code_crashed", error);
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
            if (crashed && !dev) continue;
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
