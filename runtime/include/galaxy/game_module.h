// SPDX-License-Identifier: GPL-3.0-only
// Two game modules and the choice at start (plan 1.7, contract modules): smg_game carries every LumaCore row;
// smg_game_plain, beside it, is the game without them. The host loads the plain one unless a mod's code needs rows.
#pragma once

#include <filesystem>
#include <string>

namespace galaxy::game_module {

// The plain module's file beside the rows module's: <stem>_plain<ext> in the same folder.
[[nodiscard]] std::filesystem::path plain_beside(const std::filesystem::path& rows_module);

// The module to load, before it loads, and `why` (for the log line "game module: ..."):
// - rows_module when an enabled mod has code for this device the player allowed and the last run did not crash with
//   mods' code on (mods_folder/.code_crashed absent), or when `need_rows` (--luma-describe reads the type rows);
//   in a safe start (`safe`: every mod off for this start, galaxy::mods::take_safe_start) only the mod `kept` counts
//   (the harness's own game half, which stays on; empty: none);
// - its plain sibling otherwise, when that file exists;
// - rows_module when the plain one is missing (a development build), said in `why`.
// mods_folder, mods_file and game_id are what galaxy::mods::set_mods_folder takes; empty mods_folder: no mods.
// The mods are scanned here once (galaxy::mods::scan_mods), read only.
[[nodiscard]] std::filesystem::path choose(const std::filesystem::path& rows_module, const std::filesystem::path& mods_folder,
                                           const std::filesystem::path& mods_file, const std::string& game_id, bool need_rows,
                                           bool safe, const std::string& kept, std::string* why);

// What the start chose: true when the module loaded is the plain one (no mod's code may start with it).
void set_plain_loaded(bool plain) noexcept;
[[nodiscard]] bool plain_loaded() noexcept;

}  // namespace galaxy::game_module
