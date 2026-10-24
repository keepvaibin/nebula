// SPDX-License-Identifier: GPL-3.0-only
// Mii import: Miis from any console's Mii files (mii_formats.h) into the console's own database (the NAND's
// RFL_DB.dat), which the game's Mii icon picker shows.
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace galaxy::host {

// One Mii as the console stores it (RFLiCharData, big-endian): 74 bytes.
inline constexpr std::size_t kMiiRecordSize = 0x4A;
inline constexpr std::size_t kMiiDatabaseSlots = 100;
// The Wii's limit of favourite Miis (its Mii Channel; the game's icon picker holds that many).
inline constexpr unsigned kMiiFavoriteLimit = 10;
using MiiRecord = std::array<std::byte, kMiiRecordSize>;
// The game's checks of a Mii for its database, run after fixing what the console would refuse (a birthday it does
// not take, a special Mii not kept local, no name). Empty: only a stored, non-temporary create ID is required.
using MiiPreparer = std::function<bool(MiiRecord&)>;

// The Miis a file holds, its empty slots left out (any console's: mii_formats.h). `error` says why there are none.
[[nodiscard]] std::vector<MiiRecord> read_miis(std::span<const std::byte> bytes, std::string* error);
struct MiiMergeResult {
    unsigned added = 0;        // into free slots
    unsigned updated = 0;      // a Mii the database holds (its create ID), changed since
    unsigned duplicates = 0;   // the database already holds it (the same Mii, whatever its create ID)
    unsigned invalid = 0;      // the game refused it
    unsigned no_room = 0;      // every slot taken
    unsigned unfavorited = 0;  // past the ten favourites the Wii allows: added as an ordinary Mii
    unsigned converted = 0;    // from a newer console's Mii (its extra parts and colours: the Wii's nearest)
};

// Adds Miis to a database image (RFL_DB.dat): new ones into its first free slots (a fresh create ID for one without
// a console's, or one another Mii holds), changed ones over their old selves, the same ones skipped; renews its CRC.
// `placed`: each written Mii and its slot.
MiiMergeResult merge_miis(std::vector<std::byte>& database, std::span<const MiiRecord> miis, const MiiPreparer& prepare,
                          std::vector<std::pair<unsigned, MiiRecord>>* placed);
// The Miis a database image holds.
[[nodiscard]] unsigned count_miis(std::span<const std::byte> database);

// The console's Mii library: its database file, the folder whose files are imported at each start, the game's checks.
void set_mii_library(const std::filesystem::path& database, const std::filesystem::path& import_folder,
                     MiiPreparer prepare);
[[nodiscard]] std::filesystem::path mii_import_folder();
// Imports files into the library (the database written atomically); the Miis written go to the running game too
// (take_mii_installs). `message`: what happened, for the settings.
MiiMergeResult import_mii_files(std::span<const std::filesystem::path> files, std::string* message);
// Imports every file of the import folder.
MiiMergeResult import_mii_folder(std::string* message);
// The Miis in the library; 0 without one.
[[nodiscard]] unsigned mii_library_count();
// The game thread: the Miis written since the last call, by slot, for its loaded database.
[[nodiscard]] std::vector<std::pair<unsigned, MiiRecord>> take_mii_installs();

}  // namespace galaxy::host
