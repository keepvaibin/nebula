// SPDX-License-Identifier: GPL-3.0-only
// Importing a mod into the mods folder (mods.h): a folder, or an archive libarchive reads (zip, 7z, RAR 4 and 5,
// tar and its compressions). The files go to a hidden staging folder inside the mods folder first and take the mod's
// name only when complete, so a failed or cancelled import leaves nothing half there. An archive's single top folder
// (with only notes beside it) is the mod's folder. A mod whose files are exactly those of a mod already there, under
// any name, is not added again. A mod imported under a name already present replaces that mod and keeps its settings
// (an update); one whose folder can't be moved (Windows: another program has a file of it open) waits beside it as
// .update-<name> and takes its place at the next start (apply_pending_updates). Entries that would leave the folder
// (absolute, drive or `..` paths), device names and
// links are refused, and so is an archive that unpacks past the importer's limits.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace galaxy::mods {

struct ImportProgress {
    std::uint64_t done = 0;   // bytes written
    std::uint64_t total = 0;  // the archive's or folder's size (0 while unknown)
    std::string current;      // the file being written
};

struct ImportResult {
    bool ok = false;
    std::string id;             // the mod's folder name
    bool replaced = false;      // a mod of that name was there (updated)
    bool pending = false;       // replaced at the next start: the mod's folder is in use (Windows: a file of it open)
    bool already = false;       // a mod with exactly these files was there (id names it): nothing was added
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::string kinds;          // what it holds, as the MODS page lists it ("RIIVOLUTION, TEXTURES")
    std::string title;          // its name as the MODS page shows it (its own, else its folder's)
    std::string old_version;    // a .luma replaced: the version that was there, and the one now
    std::string new_version;
    std::string message;        // shown to the player: what was imported, or why it failed
    // A Dolphin texture pack converted to the game's names: its tex1_ files, the textures they now name, and the files
    // naming none of them (another game or release).
    std::uint64_t dolphin_files = 0;
    std::uint64_t dolphin_textures = 0;
    std::uint64_t dolphin_unmatched = 0;
};

// Whether `path` is an archive the importer reads (by its signature; a file named .zip that is not one is refused).
[[nodiscard]] bool is_mod_archive(const std::filesystem::path& path);

// Imports `source` (a folder or an archive) into `mods_folder`. Runs on the calling thread; `progress` (optional) is
// called as files are written; setting `cancel` stops it (the staging folder is removed).
[[nodiscard]] ImportResult import_mod(const std::filesystem::path& source, const std::filesystem::path& mods_folder,
                                      const std::function<void(const ImportProgress&)>& progress = {},
                                      const std::atomic<bool>* cancel = nullptr);

// Removes a mod's folder (to the system's recycle bin where there is one); its settings stay until saved again. A
// folder in use is left whole (IN USE); it is never half removed.
bool remove_mod(const std::filesystem::path& mods_folder, const std::string& id, std::string* message);

// At the start, before any mod is read: each update that waited (.update-<name>) takes its mod's place. One still in
// use (another Nebula running) waits for the next start. What an import, update or removal cut off by a run that ended
// left is tidied: a mod moved aside for an update that never took its place goes back; staging folders go.
void apply_pending_updates(const std::filesystem::path& mods_folder);

}  // namespace galaxy::mods
