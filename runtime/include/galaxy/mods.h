// SPDX-License-Identifier: GPL-3.0-only
// Mods: folders in the mods folder (next to the settings), each any of
//   riivolution/*.xml  a Riivolution mod as on an SD card (its file, folder, savegame and memory patches; options),
//   files/             files in the disc's layout, replacing or adding to the game's (/StageData/..., /MovieData/...);
//                      the disc's own folders in the mod (or in its one subfolder) count as files/,
//   loose files        files beside nothing else (Mario.arc): each replaces every disc file of its name, as
//                      Riivolution's disc paths without a '/' do,
//   textures/          a texture pack (the community's tex1_ names); tex1_ files anywhere in a
//                      mod with nothing else make it a pack,
//   movies/            HD videos for the game's movies (movie_replacement.h).
// A mod whose text covers some of the disc's languages but not the one played shows its own text in that language
// (the same language of another region first, else English) unless its TEXT choice keeps the game's.
// The mods are one list, as layers: where two change the same file or texture, the one higher in the list wins (order 1
// is the top). A mod new to the list goes on top. Texture packs and movies apply at once; disc changes at the next
// start. The order, on/off state and choices are kept in mods.ini beside the settings.
#pragma once


#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace galaxy::mods {

// A Riivolution option (its XML's options): its choices; `selected` 0 is off, else a choice (1-based).
struct ModOption {
    std::string key;      // the option's id, else its section and name (Riivolution's config key)
    std::string section;
    std::string name;
    std::vector<std::string> choices;
    std::vector<bool> other_region;  // by choice: named for another region than this disc's
    unsigned selected = 0;
};

// Where two enabled mods change the same things: `files` disc files and `textures` textures.
struct ModOverlap {
    std::string other;       // the other mod's id
    bool other_above = false;  // it is higher in the list (it wins these)
    unsigned files = 0;
    unsigned textures = 0;
};

struct ModInfo {
    std::string id;  // its folder's name
    std::filesystem::path path;
    bool enabled = true;
    unsigned order = 0;  // its place in the list from the top (1 wins over 2)
    bool riivolution = false;
    bool files = false;
    bool textures = false;
    bool movies = false;
    bool loose = false;                       // loose files placed by name
    std::vector<std::string> languages;       // the language folders it changes (UsEnglish, EuFrench, ...)
    bool own_text = true;                     // its text stands in for a language it lacks
    unsigned code_patches = 0;                // patches over the game's original code: the mod stays off (problem says so)
    unsigned changed_files = 0;               // disc files it changes (its choices as they stand)
    unsigned changed_textures = 0;            // textures its packs replace
    std::vector<ModOverlap> overlaps;         // enabled mods changing the same files or textures
    std::vector<ModOption> options;
    std::string problem;  // why it does nothing for this game (for another game, unreadable XML)
};

// Whether a mod changes the game's disc (its files, main.dol, saves): such changes take a restart.
[[nodiscard]] bool changes_disc(const ModInfo& mod) noexcept;
// The enabled mods' textures/ and movies/ folders, lowest in the list first (the last wins), for the live renderer.
[[nodiscard]] std::vector<std::filesystem::path> texture_folders(std::span<const ModInfo> mods);
[[nodiscard]] std::vector<std::filesystem::path> movie_folders(std::span<const ModInfo> mods);

// The mods folder, the file of their choices and the game they are for (boot.bin's six characters).
void set_mods_folder(const std::filesystem::path& folder, const std::filesystem::path& settings_file,
                     const std::string& game_id);
[[nodiscard]] std::filesystem::path mods_folder();
// The game set_mods_folder named (boot.bin's six characters; empty before).
[[nodiscard]] std::string mods_game_id();
// The mods found and their saved choices (Riivolution defaults where none is saved): the enabled ones in load order
// (the first wins), then the disabled ones by name; for the game set_mods_folder named when `game_id` is empty.
[[nodiscard]] std::vector<ModInfo> scan_mods(const std::string& game_id = {});
// Saves the mods' order, on/off state and choices (for the next start); false when the file could not be written.
bool save_mod_settings(std::span<const ModInfo> mods);
// A mod of a scan_mods list turned on (to the top of the enabled ones) or off (among the disabled ones by name); the
// orders renumbered.
void set_mod_enabled(std::vector<ModInfo>& mods, std::size_t index, bool enabled);

// The disc's folder for a language the game plays in (UsEnglish, EuFrench, JpJapanese, ...), from the disc's region
// (game_id[3]) and the language (1 English, 2 German, 3 French, 4 Spanish, 5 Italian, 6 Dutch; Japan and Korea have
// one each). Empty when unknown.
[[nodiscard]] std::string language_folder(const std::string& game_id, unsigned language);
// This session's mods (apply_mods records them): the mods and choices it started with, what they changed.
struct SessionMods {
    std::vector<ModInfo> started_with;
    std::vector<std::string> active;
    unsigned files_changed = 0;
    unsigned unsupported = 0;
};
[[nodiscard]] SessionMods session_mods();
// Whether `mods` (the settings' current choices) differ from what this session started with (a restart applies them).
[[nodiscard]] bool mods_differ_from_session(std::span<const ModInfo> mods);

}  // namespace galaxy::mods
