// SPDX-License-Identifier: GPL-3.0-only
// Mods: folders in the mods folder (next to the settings), each any of
//   riivolution/*.xml  a Riivolution mod as on an SD card (its file, folder, savegame and memory patches; options),
//   files/             files in the disc's layout, replacing or adding to the game's (/StageData/..., /MovieData/...),
//   textures/          a texture pack (the community's tex1_ names, as Dolphin loads them).
// Mods apply at the start, in load order (a later mod wins where two change a file); their order, on/off state and
// Riivolution choices are kept in mods.ini beside the settings.
#pragma once

#include "galaxy/virtual_disc.h"

#include <cstdint>
#include <filesystem>
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
    unsigned selected = 0;
};

struct ModInfo {
    std::string id;  // its folder's name
    std::filesystem::path path;
    bool enabled = true;
    unsigned order = 0;  // load order (later wins)
    bool riivolution = false;
    bool files = false;
    bool textures = false;
    std::vector<ModOption> options;
    std::string problem;  // why it does nothing for this game (for another game, unreadable XML)
};

// What the mods do to this session's game.
struct ActiveMods {
    VirtualDisc disc;
    std::vector<std::uint8_t> fst;        // the disc's file table with the mods' files
    std::vector<std::uint8_t> dol;        // main.dol with its patches (empty: unchanged)
    std::filesystem::path save_folder;    // Riivolution savegame: the game's saves there (empty: the NAND's)
    bool save_clone = false;              // copy the NAND's saves there when it is empty
    std::vector<std::filesystem::path> texture_folders;  // in load order
    std::vector<std::string> active;      // the mods applied
    unsigned files_changed = 0;
    unsigned unsupported = 0;             // patches the native game cannot take (code patches)
    std::string report;                   // what each mod did, for mods-report.txt and the log
};

// The mods folder, the file of their choices and the game they are for (boot.bin's six characters).
void set_mods_folder(const std::filesystem::path& folder, const std::filesystem::path& settings_file,
                     const std::string& game_id);
[[nodiscard]] std::filesystem::path mods_folder();
// The mods found and their saved choices (Riivolution defaults where none is saved), in load order; for the game
// set_mods_folder named when `game_id` is empty.
[[nodiscard]] std::vector<ModInfo> scan_mods(const std::string& game_id = {});
// Saves the mods' order, on/off state and choices (for the next start); false when the file could not be written.
bool save_mod_settings(std::span<const ModInfo> mods);

// The enabled mods applied to the game partition (its file table, main.dol) for game `game_id` (boot.bin's first six
// characters), revision and disc number. Null when no mod changes anything.
[[nodiscard]] std::unique_ptr<ActiveMods> apply_mods(std::span<const std::uint8_t> partition_fst,
                                                     std::span<const std::uint8_t> partition_dol,
                                                     std::uint64_t dol_offset, const std::uint8_t* partition,
                                                     std::uint64_t partition_size, const std::string& game_id,
                                                     int revision, int disc_number);

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

// Memory patches against main.dol's image (the native game reads only its data tables from it): the bytes at a
// console address within one of its sections, when `original` (if any) matches. False when the address is in none.
bool patch_dol(std::vector<std::uint8_t>& dol, std::uint32_t address, std::span<const std::uint8_t> value,
               std::span<const std::uint8_t> original);

}  // namespace galaxy::mods
