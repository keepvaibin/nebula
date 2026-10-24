// SPDX-License-Identifier: GPL-3.0-only
// Importing a mod into the mods folder (mods.h): a folder, or an archive libarchive reads (zip, 7z, RAR 4 and 5,
// tar and its compressions). The files go to a hidden staging folder inside the mods folder first and take the mod's
// name only when complete, so a failed or cancelled import leaves nothing half there. An archive's single top folder
// (with only notes beside it) is the mod's folder. A mod imported under a name already present replaces that mod and
// keeps its settings (an update). Entries that would leave the folder (absolute or `..` paths) and links are refused.
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
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::string kinds;          // what it holds, as the MODS page lists it ("RIIVOLUTION, TEXTURES")
    std::string message;        // shown to the player: what was imported, or why it failed
};

// Whether `path` is an archive the importer reads (by its signature; a file named .zip that is not one is refused).
[[nodiscard]] bool is_mod_archive(const std::filesystem::path& path);

// Imports `source` (a folder or an archive) into `mods_folder`. Runs on the calling thread; `progress` (optional) is
// called as files are written; setting `cancel` stops it (the staging folder is removed).
[[nodiscard]] ImportResult import_mod(const std::filesystem::path& source, const std::filesystem::path& mods_folder,
                                      const std::function<void(const ImportProgress&)>& progress = {},
                                      const std::atomic<bool>* cancel = nullptr);

// Removes a mod's folder (to the system's recycle bin where there is one); its settings stay until saved again.
bool remove_mod(const std::filesystem::path& mods_folder, const std::string& id, std::string* message);

}  // namespace galaxy::mods
