// SPDX-License-Identifier: GPL-3.0-only
// Mods are folders in the mods folder (next to the settings). A mod can hold any of:
//   riivolution/*.xml  a Riivolution mod as it sits on an SD card (file, folder, savegame and memory patches; options)
//   files/             files laid out like the game's (/StageData/..., /MovieData/...). The game's own folder names at
//                      the top of the mod (or of its only subfolder) count as files/ too.
//   loose files        files with nothing else beside them (Mario.arc). Each replaces every game file with that name,
//                      like a Riivolution path without a '/'.
//   textures/          a texture pack (tex1_ names). A mod holding only tex1_ files is a pack.
//   movies/            HD videos for the game's movies (movie_playback.h).
// If a mod's text lacks the language being played, its text in a near language is shown (the same language from
// another region, else English), unless its TEXT choice keeps the game's text.
// The mods form one ordered list. When two change the same file or texture, the higher one wins (order 1 is the top).
// A newly added mod goes on top. Texture packs and movies apply at once; file changes apply at the next start. The
// order, on/off state and choices are saved in mods.ini next to the settings.
#pragma once


#include "galaxy/mod_plugins.h"
#include "galaxy/luma.h"
#include "galaxy/texture_categories.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::assets {
class AssetStore;
}

namespace galaxy::mods {

// True when one part of a path from a mod is a plain name that stays inside its folder: no drive or stream (':'), no
// separator, not only dots and spaces, and not a device name.
[[nodiscard]] bool safe_path_part(std::string_view part);

// A Riivolution option and its choices. `selected` is 0 for off, else the 1-based choice.
struct ModOption {
    std::string key;      // the option's id, else its section and name (Riivolution's config key)
    std::string section;
    std::string name;
    std::vector<std::string> choices;
    std::vector<bool> other_region;  // per choice: made for another region's release
    unsigned selected = 0;
    // For a .luma option: the choice ids, and whether the option has no Off.
    std::vector<std::string> choice_ids;
    bool must_choose = false;
};

// What two enabled mods both change: game files, textures, and settings (tunables, table cells, lines, constants;
// see luma::setting_keys).
struct ModOverlap {
    std::string other;         // the other mod's id
    bool other_above = false;  // the other mod is higher in the list, so it wins
    unsigned files = 0;
    unsigned textures = 0;
    unsigned settings = 0;
};

// Everything a mod changes, used to find overlaps: game paths ("name:x.arc" for a bare file name), texture names, and
// settings (luma::setting_keys, luma::merge_keys).
struct ModChanges {
    std::set<std::string> paths;
    std::set<std::string> textures;
    std::set<std::string> settings;
};

// One texture in a pack: its name hash (galaxy::textures::pack_file_key) and the category its folder name implies
// (kCategoryCount when none).
struct PackTexture {
    std::uint64_t hash = 0;
    std::uint8_t folder_category = galaxy::textures::kCategoryCount;
};

struct ModInfo {
    std::string id;  // the folder name
    std::filesystem::path path;
    bool enabled = true;
    unsigned order = 0;  // place in the list from the top (1 wins over 2)
    bool riivolution = false;
    bool files = false;
    bool textures = false;
    bool movies = false;
    bool loose = false;
    bool package = false;  // a .luma: luma.json plus layers of files
    std::shared_ptr<const galaxy::luma::Manifest> luma;
    bool luma_changes_game = false;  // it or its active layers set something read at game start (luma::manifest_sets_game)
    std::vector<std::filesystem::path> luma_textures;  // textures/ and movies/ of its active layers, in order
    std::vector<std::filesystem::path> luma_movies;
    std::vector<std::string> languages;       // language folders it changes (UsEnglish, EuFrench, ...)
    bool own_text = true;                     // its text stands in for a language it lacks
    unsigned code_patches = 0;                // patches to the original Wii code; never applied (`problem` says so)
    std::vector<std::string> code_patch_notes;  // one per such patch, for the log: where it writes and what replaces it
    unsigned changed_files = 0;               // game files it changes with its current choices
    unsigned changed_textures = 0;            // textures its packs replace
    std::vector<ModOverlap> overlaps;         // enabled mods changing the same files or textures
    std::vector<ModOption> options;
    std::string code_token;     // identifies this device's libraries of a .luma; empty when it has no code
    bool code_allowed = false;  // the player allowed exactly these libraries
    // Texture pack details: its textures, counts per category and of this game's textures, and the categories turned
    // off (category_bit). A pack's manifest.json gives its title, author and description.
    std::shared_ptr<const std::vector<PackTexture>> pack_textures;
    galaxy::textures::CategoryCounts category_textures{};
    unsigned game_textures = 0;
    bool categories_final = false;  // the game's textures were known when these were counted
    std::uint32_t categories_off = 0;
    std::string pack_title;
    std::string pack_author;
    std::string pack_description;
    std::string problem;      // why it does nothing (another game, unreadable XML), or a conflict with the list
    std::string own_problem;  // its own problem, before conflicts with other mods (settle_mods)
    std::shared_ptr<const ModChanges> changes;
    bool unreadable = false;  // a folder in the mod could not be fully read (`problem` says so when nothing else does)
};

// The kinds of change a mod makes, as the Mods page lists them ("FILES, SETTINGS, TEXTURES"). Empty when none.
[[nodiscard]] std::string change_kinds(const ModInfo& mod);
// True when a mod changes game files, code patches or saves. Those need a restart.
[[nodiscard]] bool changes_game_files(const ModInfo& mod) noexcept;
// The textures/ and movies/ folders of the enabled mods, lowest in the list first (the last one wins).
[[nodiscard]] std::vector<std::filesystem::path> texture_folders(std::span<const ModInfo> mods);
// The categories turned off for each folder of texture_folders, in the same order.
[[nodiscard]] std::vector<std::uint32_t> texture_folder_categories_off(std::span<const ModInfo> mods);
// Recounts a pack's textures by category once the game's textures are sorted. Returns true when anything changed.
bool update_pack_categories(ModInfo& mod);
// The game store (game.pak) the game's textures are sorted from. Sorting starts once a mod has a pack.
void set_mods_store_file(const std::filesystem::path& store);
[[nodiscard]] std::vector<std::filesystem::path> movie_folders(std::span<const ModInfo> mods);

// Sets the mods folder, the file holding the player's choices, and the game id (boot.bin's six characters).
void set_mods_folder(const std::filesystem::path& folder, const std::filesystem::path& settings_file,
                     const std::string& game_id);
[[nodiscard]] std::filesystem::path mods_folder();
// Mod code was turned off after a crash in this run; the Mods page says so.
void set_code_off_after_crash() noexcept;
// The game id given to set_mods_folder (empty before).
[[nodiscard]] std::string mods_game_id();
// The mods found, with their saved choices (Riivolution defaults when nothing is saved): enabled mods in load order
// (first wins), then disabled mods by name. An empty `game_id` means the one given to set_mods_folder.
[[nodiscard]] std::vector<ModInfo> scan_mods(const std::string& game_id = {});
// Saves order, on/off state and choices for the next start. Returns false when the file could not be written.
bool save_mod_settings(std::span<const ModInfo> mods);
// Recomputes how the mods relate after a change to the list: order, duplicates, requirements, saves, conflicts (each
// mod's problem) and overlaps. scan_mods calls this last.
void settle_mods(std::vector<ModInfo>& mods);
// Turns a mod on (moved to the top of the enabled mods) or off (placed among the disabled mods by name), then
// renumbers the order.
void set_mod_enabled(std::vector<ModInfo>& mods, std::size_t index, bool enabled);

// The game's folder for a language (UsEnglish, EuFrench, JpJapanese, ...), from the region (game_id[3]) and the
// language (1 English, 2 German, 3 French, 4 Spanish, 5 Italian, 6 Dutch; Japan and Korea have one each). Empty when
// unknown.
[[nodiscard]] std::string language_folder(const std::string& game_id, unsigned language);
// The mods this run started with, as recorded by apply_mods, and what they changed.
struct SessionMods {
    std::vector<ModInfo> started_with;
    std::vector<std::string> active;
    unsigned files_changed = 0;
    unsigned unsupported = 0;
};
[[nodiscard]] SessionMods session_mods();
// What the enabled mods change at this start: each game path and the file that replaces it (the higher mod wins).
// Recording it makes it this run's mods. `language` is the folder of the language played, `store` the game's files.
struct ModPlan {
    struct File {
        std::string disc;
        std::filesystem::path host;
    };
    std::vector<File> files;
    // Additions to shared files (a layer's merge/ folder), bottom of the list first. `disc` is the file's path in the
    // store, with ".json" added for table rows.
    std::vector<File> merges;
    // Materials that have a shader written for them. `disc` is the material name.
    std::vector<File> materials;
    // The .luma mods in use, bottom of the list first, for their shaders.
    std::vector<std::filesystem::path> luma_folders;
    std::vector<PluginFile> plugins;  // code of the .luma mods, top of the list first (the order they bind in)
    ModRows rows;
    std::vector<std::string> notes;  // what could not be applied, for the log
    unsigned code_patches = 0;
    std::filesystem::path save_folder;
    bool save_clone = true;
    std::filesystem::path save_start;  // files the mod's saves start from
};
// The language played, used to pick a .luma's translated text (same numbering as language_folder).
void set_played_language(unsigned language) noexcept;
// Builds the plan from the list scan_mods(game_id) returned at start, so the start scans only once. Call
// set_played_language first.
[[nodiscard]] ModPlan plan_mod_changes(const galaxy::assets::AssetStore& store, const std::string& game_id,
                                         const std::string& language, std::vector<ModInfo> scanned);
// plan_text is the plan as the cooker reads it (first line: the game id). plan_signature adds the files' sizes and
// times, so a changed mod is cooked again.
[[nodiscard]] std::string plan_text(const ModPlan& plan, const std::string& game_id);
[[nodiscard]] std::string plan_signature(const ModPlan& plan, const std::string& game_id);
// True when `mods` (the current choices) differ from what this run started with; a restart applies them.
[[nodiscard]] bool mods_differ_from_session(std::span<const ModInfo> mods);

}  // namespace galaxy::mods
