// SPDX-License-Identifier: GPL-3.0-only
// Imports a mod into the mods folder (mods.h) from a folder or an archive (zip, 7z, RAR 4 and 5, tar and its
// compressions, read with libarchive).
// - Files are written to a hidden staging folder first and renamed into place only when complete, so a failed or
//   cancelled import leaves nothing behind.
// - An archive's single top folder (with only notes beside it) becomes the mod's folder.
// - A mod identical to one already installed, under any name, is not added again.
// - A mod with the same name as an installed one is an update: it replaces it and keeps its settings. If the old
//   folder can't be moved (on Windows, a program has one of its files open), the update waits as .update-<name> and is
//   applied at the next start (apply_pending_updates).
// - Absolute, drive and `..` paths, device names, links, and archives that unpack past the size limits are refused.
// - A package (a luma.json) passes the caller's check before it is placed (task 6.1: the validator, as at each start);
//   refused, nothing is added and the findings say why.
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
    std::uint64_t total = 0;  // size of the archive or folder (0 while unknown)
    std::string current;      // the file being written
};

struct ImportResult {
    bool ok = false;
    std::string id;             // the mod's folder name
    bool replaced = false;      // a mod with that name was updated
    bool pending = false;       // the update waits for the next start because the mod's folder is in use
    bool already = false;       // an identical mod was already installed (named by id); nothing was added
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    std::string kinds;          // what it holds, as the Mods page lists it ("RIIVOLUTION, TEXTURES")
    std::string title;          // its name on the Mods page (its own name, else the folder name)
    std::string old_version;    // for an updated .luma: the old and new versions
    std::string new_version;
    std::string message;        // shown to the player: what was imported, or why it failed
    // For a Dolphin texture pack converted to Nebula's names: its tex1_ files, how many textures they matched, and how
    // many matched nothing (made for another game or release).
    std::uint64_t dolphin_files = 0;
    std::uint64_t dolphin_textures = 0;
    std::uint64_t dolphin_unmatched = 0;
    // A package's check (ImportCheck): whether it ran, refused it, and what it found, in the played language.
    struct Finding {
        bool error = false;
        std::uint32_t code = 0;  // 509 is L509
        std::string file;
        std::string text;
        std::uint32_t line = 0;  // the place in `file` (0: none)
        std::uint32_t column = 0;
    };
    bool checked = false;
    bool refused = false;
    std::vector<Finding> findings;
};

// Checks a package staged for import (`staged`, laid out as a placed mod) under the folder name `id` it will take
// (an update's: the mod it replaces). Sets result.checked, refused and findings; true lets the import go on.
using ImportCheck = std::function<bool(const std::filesystem::path& staged, const std::string& id, ImportResult& result)>;

// True when `path` is an archive the importer can read. Checked by content, so a fake .zip is refused.
[[nodiscard]] bool is_mod_archive(const std::filesystem::path& path);

// Imports `source` (a folder or an archive) into `mods_folder` on the calling thread. `progress` (optional) is called
// as files are written. Setting `cancel` stops the import and removes the staging folder.
// `check` (optional) is asked about a package before it is placed.
[[nodiscard]] ImportResult import_mod(const std::filesystem::path& source, const std::filesystem::path& mods_folder,
                                      const std::function<void(const ImportProgress&)>& progress = {},
                                      const std::atomic<bool>* cancel = nullptr, const ImportCheck& check = {});

// The mod a folder or archive holds, read before importing it (services-mods 9: the install screen's UPDATE): its
// manifest's id and version, and the version of the installed mod of that id in `mods_folder` (empty: none installed,
// or no manifest). Reads the archive's luma.json only (at its top or in its one top folder, at most 1 MiB). Any thread.
struct ImportPeek {
    std::string id;
    std::string version;
    std::string installed;
};
[[nodiscard]] ImportPeek peek_import(const std::filesystem::path& source, const std::filesystem::path& mods_folder);

// Removes a mod's folder, to the recycle bin where there is one. Its settings stay until the next save. A folder in
// use is left untouched (IN USE); a mod is never half removed.
bool remove_mod(const std::filesystem::path& mods_folder, const std::string& id, std::string* message);

// Called at start before any mod is read. Applies waiting updates (.update-<name>); one still in use (another Nebula
// running) waits again. Cleans up after an import, update or removal that a closed game cut short: a mod moved aside
// for an update that never landed is put back, and staging folders are deleted.
void apply_pending_updates(const std::filesystem::path& mods_folder);

}  // namespace galaxy::mods
