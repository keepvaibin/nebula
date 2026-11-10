// SPDX-License-Identifier: GPL-3.0-only
// Mods: folders in the mods folder (next to the settings), each any of
//   riivolution/*.xml  a Riivolution mod as on an SD card (its file, folder, savegame and memory patches; options),
//   files/             files in the game's own layout, replacing or adding to the game's (/StageData/..., /MovieData/...);
//                      the game's own folders in the mod (or in its one subfolder) count as files/,
//   loose files        files beside nothing else (Mario.arc): each replaces every game file of its name, as
//                      Riivolution's paths without a '/' do,
//   textures/          a texture pack (the community's tex1_ names); tex1_ files anywhere in a
//                      mod with nothing else make it a pack,
//   movies/            HD videos for the game's movies (movie_playback.h).
// A mod whose text covers some of the game's languages but not the one played shows its own text in that language
// (the same language of another region first, else English) unless its TEXT choice keeps the game's.
// The mods are one list, as layers: where two change the same file or texture, the one higher in the list wins (order 1
// is the top). A mod new to the list goes on top. Texture packs and movies apply at once; changes to the game's files at the next
// start. The order, on/off state and choices are kept in mods.ini beside the settings.
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

// Whether one part of a path a mod names stays one plain name inside its folder: no drive or stream (':'), separator,
// name of only dots and spaces, or device name.
[[nodiscard]] bool safe_path_part(std::string_view part);

// A Riivolution option (its XML's options): its choices; `selected` 0 is off, else a choice (1-based).
struct ModOption {
    std::string key;      // the option's id, else its section and name (Riivolution's config key)
    std::string section;
    std::string name;
    std::vector<std::string> choices;
    std::vector<bool> other_region;  // by choice: named for another region than this release's
    unsigned selected = 0;
    // A .luma's option: its choices' ids, and whether it has no Off.
    std::vector<std::string> choice_ids;
    bool must_choose = false;
};

// Where two enabled mods change the same things: `files` game files, `textures` textures, and `settings` what their rows
// and merges set (tunables, table cells, lines, constants...: luma::setting_keys).
struct ModOverlap {
    std::string other;       // the other mod's id
    bool other_above = false;  // it is higher in the list (it wins these)
    unsigned files = 0;
    unsigned textures = 0;
    unsigned settings = 0;
};

// What a mod changes, for its overlaps with the others: disc paths ("name:x.arc" for a file name alone), texture names,
// and what its rows and merges set (luma::setting_keys, luma::merge_keys).
struct ModChanges {
    std::set<std::string> paths;
    std::set<std::string> textures;
    std::set<std::string> settings;
};

// A texture of a pack: its name hash (galaxy::textures::pack_file_key) and what its folder's words say
// (kCategoryCount: nothing).
struct PackTexture {
    std::uint64_t hash = 0;
    std::uint8_t folder_category = galaxy::textures::kCategoryCount;
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
    bool loose = false;
    bool package = false;  // a .luma: luma.json, and layers of the mod's files
    std::shared_ptr<const galaxy::luma::Manifest> luma;
    bool luma_changes_game = false;  // it, or its layers in use, set anything the game takes as it starts (luma::manifest_sets_game)
    std::vector<std::filesystem::path> luma_textures;  // its layers' textures/ and movies/ in use, in order
    std::vector<std::filesystem::path> luma_movies;
    //                       // loose files placed by name
    std::vector<std::string> languages;       // the language folders it changes (UsEnglish, EuFrench, ...)
    bool own_text = true;                     // its text stands in for a language it lacks
    unsigned code_patches = 0;                // patches over the game's original code: never applied (problem says so)
    std::vector<std::string> code_patch_notes;  // one per such patch: where it writes and what replaces it, for the log
    unsigned changed_files = 0;               // game files it changes (its choices as they stand)
    unsigned changed_textures = 0;            // textures its packs replace
    std::vector<ModOverlap> overlaps;         // enabled mods changing the same files or textures
    std::vector<ModOption> options;
    std::string code_token;    // a .luma's libraries for this device, as they are: empty if it has no code
    bool code_allowed = false;  // the player has allowed them as they are
    // A texture pack, sorted: its textures, how many of each category and of this game's textures, and the categories
    // turned off (category_bit). A pack's manifest.json (a resource pack's) names it.
    std::shared_ptr<const std::vector<PackTexture>> pack_textures;
    galaxy::textures::CategoryCounts category_textures{};
    unsigned game_textures = 0;
    bool categories_final = false;  // the game's textures were known when they were counted
    std::uint32_t categories_off = 0;
    std::string pack_title;
    std::string pack_author;
    std::string pack_description;
    std::string problem;  // why it does nothing for this game (for another game, unreadable XML), or what the list makes of it
    std::string own_problem;  // its own problem, before what the other mods on the list make of it (settle_mods)
    std::shared_ptr<const ModChanges> changes;
    bool unreadable = false;  // a folder of the mod could not be read in full (problem says so when nothing else does)
};

// What kinds of things a mod changes, as the MODS page names them ("FILES, SETTINGS, TEXTURES"): Riivolution choices,
// files, settings (tunables, rows, lines, merges), textures, movies and code. Empty when nothing.
[[nodiscard]] std::string change_kinds(const ModInfo& mod);
// Whether a mod changes the game's files (its files, code patches, saves): such changes take a restart.
[[nodiscard]] bool changes_game_files(const ModInfo& mod) noexcept;
// The enabled mods' textures/ and movies/ folders, lowest in the list first (the last wins), for the live renderer.
[[nodiscard]] std::vector<std::filesystem::path> texture_folders(std::span<const ModInfo> mods);
// The categories off in each of texture_folders' folders, in its order.
[[nodiscard]] std::vector<std::uint32_t> texture_folder_categories_off(std::span<const ModInfo> mods);
// A pack's textures counted by category again, once the game's textures are sorted; true when anything changed.
bool update_pack_categories(ModInfo& mod);
// The store the game's textures are sorted from (game.pak); the sorting starts when a mod has a pack.
void set_mods_store_file(const std::filesystem::path& store);
[[nodiscard]] std::vector<std::filesystem::path> movie_folders(std::span<const ModInfo> mods);

// The mods folder, the file of their choices and the game they are for (boot.bin's six characters).
void set_mods_folder(const std::filesystem::path& folder, const std::filesystem::path& settings_file,
                     const std::string& game_id);
[[nodiscard]] std::filesystem::path mods_folder();
// This run left mods' code off after a crash: the Mods page says so.
void set_code_off_after_crash() noexcept;
// The game set_mods_folder named (boot.bin's six characters; empty before).
[[nodiscard]] std::string mods_game_id();
// The mods found and their saved choices (Riivolution defaults where none is saved): the enabled ones in load order
// (the first wins), then the disabled ones by name; for the game set_mods_folder named when `game_id` is empty.
[[nodiscard]] std::vector<ModInfo> scan_mods(const std::string& game_id = {});
// Saves the mods' order, on/off state and choices (for the next start); false when the file could not be written.
bool save_mod_settings(std::span<const ModInfo> mods);
// The list's relations again, after the player turned mods on or off or moved them: places, the same mod twice, needs,
// saves and conflicts (each mod's problem from its own), and the overlaps. scan_mods ends with it.
void settle_mods(std::vector<ModInfo>& mods);
// A mod of a scan_mods list turned on (to the top of the enabled ones) or off (among the disabled ones by name); the
// orders renumbered.
void set_mod_enabled(std::vector<ModInfo>& mods, std::size_t index, bool enabled);

// The game's folder for a language it plays in (UsEnglish, EuFrench, JpJapanese, ...), from the game's region
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
// What the enabled mods change at this start, the one higher in the list winning a file: each game path and the file
// that holds it. Records the session. `language` is the folder of the language played, `store` the game's files.
struct ModPlan {
    struct File {
        std::string disc;
        std::filesystem::path host;
    };
    std::vector<File> files;
    // What the mods add to files many share (a layer's merge/ folder), bottom of the list first: `disc` is the file's
    // place in the store, with ".json" for a table's rows.
    std::vector<File> merges;
    // Materials with a shader written for them: `disc` is the material's name.
    std::vector<File> materials;
    // The .luma mods in use, bottom of the list first: their shaders.
    std::vector<std::filesystem::path> luma_folders;
    std::vector<PluginFile> plugins;  // the .luma mods' code, bottom of the list first
    ModRows rows;
    std::vector<std::string> notes;  // what could not apply, for the log
    unsigned code_patches = 0;
    std::filesystem::path save_folder;
    bool save_clone = true;
    std::filesystem::path save_start;  // files its saves begin from
};
// The language played, for a .luma's translated text: as language_folder takes it.
void set_played_language(unsigned language) noexcept;
// The plan over the list scan_mods(game_id) gave at the start (the played language set): the start scans once.
[[nodiscard]] ModPlan plan_mod_changes(const galaxy::assets::AssetStore& store, const std::string& game_id,
                                         const std::string& language, std::vector<ModInfo> scanned);
// The plan as the cooker reads it (its first line the game id), and what names this plan's result: the files' sizes
// and times with it, so that a changed mod is cooked again.
[[nodiscard]] std::string plan_text(const ModPlan& plan, const std::string& game_id);
[[nodiscard]] std::string plan_signature(const ModPlan& plan, const std::string& game_id);
// Whether `mods` (the settings' current choices) differ from what this session started with (a restart applies them).
[[nodiscard]] bool mods_differ_from_session(std::span<const ModInfo> mods);

}  // namespace galaxy::mods
