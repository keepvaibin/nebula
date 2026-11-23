// SPDX-License-Identifier: GPL-3.0-only
// Two game modules and the choice at start: smg_game carries every LumaCore row;
// smg_game_plain, beside it, is the game without them. The host loads the plain one unless a mod's code needs rows.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace galaxy::game_module {

// The plain module's file beside the rows module's: <stem>_plain<ext> in the same folder.
[[nodiscard]] std::filesystem::path plain_beside(const std::filesystem::path& rows_module);

// The module to load, before it loads, and `why` (for the log line "game module: ..."):
// - rows_module when an enabled mod has code for this device the player allowed, other than the mod the last run's
//   crash named and unless that crash named none with code on (galaxy::mods::take_safe_start, called first), or when
//   `need_rows` (--luma-describe reads the type rows);
//   in a safe start (`safe`: every mod off for this start, galaxy::mods::take_safe_start) only the mod `kept` counts
//   (the harness's own game half, which stays on; empty: none);
// - its plain sibling otherwise, when that file exists;
// - rows_module when the plain one is missing (a development build), said in `why`.
// mods_folder, mods_file and game_id are what galaxy::mods::set_mods_folder takes; empty mods_folder: no mods.
// The mods are scanned here once (galaxy::mods::scan_mods), read only.
[[nodiscard]] std::filesystem::path choose(const std::filesystem::path& rows_module, const std::filesystem::path& mods_folder,
                                           const std::filesystem::path& mods_file, const std::string& game_id, bool need_rows,
                                           bool safe, const std::string& kept, std::string* why);

// The same with the rule for mod support given (mod_support.h): without it nothing is scanned and the plain module
// is the answer whether or not its file is there (a build with no mod support never loads the rows module); `why` says so.
[[nodiscard]] std::filesystem::path choose_with(bool with_mods, const std::filesystem::path& rows_module,
                                                const std::filesystem::path& mods_folder, const std::filesystem::path& mods_file,
                                                const std::string& game_id, bool need_rows, bool safe, const std::string& kept,
                                                std::string* why);

// The module a Mac or iOS app names when it is not told (--module): its rows module in the app's own folder
// `frameworks` (Contents/Frameworks), or the framework form of it. An app with no mod support has only the plain module
// there (build_macos.sh -n): the rows module's name is given all the same, in that folder, since the plain module is the
// file beside it (plain_beside), and a name in the program's folder would send the start to a folder with no module.
// Without a Frameworks folder (a build folder): the rows module's name beside the program, `program_folder`.
[[nodiscard]] std::filesystem::path module_in_app(const std::filesystem::path& frameworks,
                                                  const std::filesystem::path& program_folder);

// The line the host stops with when `module` did not load (the words "cannot load the game module" first: the crash
// report's NEB-S03 starts from them). `system_text` is the system's own reason: dlerror(), or the text of the
// Windows error. It says why where it can be told:
//   "cannot load the game module <file>: the file is not there (<folder>)"      the file is missing
//   "cannot load the game module <file>: the system refused it: <system_text>"  the file is there and did not load
// The folder is its last two names (Contents/Frameworks): no user's name is in a report.
[[nodiscard]] std::string load_failure(const std::filesystem::path& module, std::string_view system_text);

// What the start chose: true when the module loaded is the plain one (no mod's code may start with it).
void set_plain_loaded(bool plain) noexcept;
[[nodiscard]] bool plain_loaded() noexcept;

}  // namespace galaxy::game_module
