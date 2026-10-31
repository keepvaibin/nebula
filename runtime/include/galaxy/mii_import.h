// SPDX-License-Identifier: GPL-3.0-only
// The Mii library: the Miis the game's icon picker shows, in a native file (one text line per Mii, MiiFields), filled
// by importing any console's Mii files (mii_formats.h). A console's RFL_DB.dat is read once, as the library's start,
// when the library file does not exist yet.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::host {

// One Mii as the Wii stores it (RFLiCharData, big-endian): 74 bytes. Only imports read it.
inline constexpr std::size_t kMiiRecordSize = 0x4A;
inline constexpr std::size_t kMiiLibrarySlots = 100;
// The Wii's limit of favourite Miis (its Mii Channel; the game's icon picker holds that many).
inline constexpr unsigned kMiiFavoriteLimit = 10;
using MiiRecord = std::array<std::byte, kMiiRecordSize>;

// A Mii's fields as numbers (the game's character info): every part, colour, scale and position, names UTF-16, the
// create ID as consoles store it (four bytes of type bits and time, four of the console).
struct MiiFields {
    std::uint8_t sex = 0, birth_month = 0, birth_day = 0, favorite_color = 0, favorite = 0, local_only = 0;
    std::uint8_t height = 0, build = 0;
    std::uint8_t face_type = 0, face_color = 0, face_texture = 0;
    std::uint8_t hair_type = 0, hair_color = 0, hair_flip = 0;
    std::uint8_t eye_type = 0, eye_color = 0, eye_scale = 0, eye_rotate = 0, eye_x = 0, eye_y = 0;
    std::uint8_t eyebrow_type = 0, eyebrow_color = 0, eyebrow_scale = 0, eyebrow_rotate = 0, eyebrow_x = 0,
                 eyebrow_y = 0;
    std::uint8_t nose_type = 0, nose_scale = 0, nose_y = 0;
    std::uint8_t mouth_type = 0, mouth_color = 0, mouth_scale = 0, mouth_y = 0;
    std::uint8_t mustache = 0, beard_type = 0, beard_color = 0, beard_scale = 0, beard_y = 0;
    std::uint8_t glass_type = 0, glass_color = 0, glass_scale = 0, glass_y = 0;
    std::uint8_t mole_type = 0, mole_scale = 0, mole_x = 0, mole_y = 0;
    std::array<std::uint16_t, 10> name{}, creator{};
    std::array<std::uint8_t, 8> create_id{};

    bool operator==(const MiiFields&) const = default;
};

// The game's checks of a Mii for the library, run on the imported record after fixing what the console would refuse
// (a birthday it does not take, a special Mii not kept local, no name). Empty: only a stored, non-temporary create ID
// is required.
using MiiPreparer = std::function<bool(MiiRecord&)>;

// The library's slots; an empty one holds nothing.
struct MiiLibrary {
    std::array<std::optional<MiiFields>, kMiiLibrarySlots> slots;
};

// The library file's text: a first line naming the format, then one line per Mii, "<slot> <name>=<value> ...", the
// numbers decimal, the names and create ID hexadecimal. Unknown names are skipped; a line without a slot or create ID
// is not a Mii. None when the first line is not the format's.
[[nodiscard]] std::string write_mii_library(const MiiLibrary& library);
[[nodiscard]] std::optional<MiiLibrary> read_mii_library(std::string_view text);

// The Miis a file holds, its empty slots left out (any console's: mii_formats.h). `error` says why there are none.
[[nodiscard]] std::vector<MiiRecord> read_miis(std::span<const std::byte> bytes, std::string* error);
struct MiiMergeResult {
    unsigned added = 0;        // into free slots
    unsigned updated = 0;      // a Mii the library holds (its create ID), changed since
    unsigned duplicates = 0;   // the library already holds it (the same Mii, whatever its create ID)
    unsigned invalid = 0;      // the game refused it
    unsigned no_room = 0;      // every slot taken
    unsigned unfavorited = 0;  // past the ten favourites the Wii allows: added as an ordinary Mii
    unsigned converted = 0;    // from a newer console's Mii (its extra parts and colours: the Wii's nearest)
};

// Adds Miis to the library: new ones into its first free slots (a fresh create ID for one without a console's, or one
// another Mii holds), changed ones over their old selves, the same ones skipped. `placed`: each written Mii and its
// slot.
MiiMergeResult merge_miis(MiiLibrary& library, std::span<const MiiRecord> miis, const MiiPreparer& prepare,
                          std::vector<std::pair<unsigned, MiiFields>>* placed);
[[nodiscard]] unsigned count_miis(const MiiLibrary& library);

// The library in use: its file, the console database it starts from when the file is missing, the folder whose files
// are imported at each start, the game's checks.
void set_mii_library(const std::filesystem::path& file, const std::filesystem::path& console_database,
                     const std::filesystem::path& import_folder, MiiPreparer prepare);
[[nodiscard]] std::filesystem::path mii_import_folder();
// Imports files into the library (its file written atomically); the Miis written go to the running game too
// (take_mii_installs). `message`: what happened, for the settings.
MiiMergeResult import_mii_files(std::span<const std::filesystem::path> files, std::string* message);
// Imports every file of the import folder.
MiiMergeResult import_mii_folder(std::string* message);
// The Miis in the library; 0 without one.
[[nodiscard]] unsigned mii_library_count();
// The Mii in a slot of the library (as the game reads it), none for an empty slot.
[[nodiscard]] std::optional<MiiFields> mii_library_slot(unsigned slot);
// The game thread: the Miis written since the last call, by slot.
[[nodiscard]] std::vector<std::pair<unsigned, MiiFields>> take_mii_installs();

}  // namespace galaxy::host
