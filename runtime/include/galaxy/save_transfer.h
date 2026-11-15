// SPDX-License-Identifier: GPL-3.0-only
// Save import and export in the game, on every platform: a save from anywhere read, checked and placed where the game
// reads it, and the save folder written to one zip that this import reads back.
//
// Sources: a Nebula saves archive (a zip with nebula-saves.json, saves/<path>, streams/<path>/<name>; folders and
// streams of 1.0.2's archives are read and left out); another zip or a folder holding a save folder (a Dolphin or NAND
// copy, or Nebula's own) or a GameData.bin or Nebula.sav; a loose GameData.bin (the Wii's, big-endian version 2; the
// Switch's, little-endian version 2; Nebula 1.0's, little-endian version 3); a Wii SD card save (data.bin, AES-128-CBC
// under the SD key: its GameData.bin and banner); a loose Nebula.sav. A Datel save (WIISAVE001) is recognised and
// refused: its contents are under a cipher of Datel's own.
//
// What is placed is what the game reads (SaveDataHandler.cpp): <code>/Nebula.sav as it is, or a GameData.bin in
// title/00010000/<code in hex>/data, which the game converts to Nebula.sav at its next load (the conversion is the game
// module's own, SaveDataHandler::readOld). GameData.bin is checked with the game's own check (SaveFileFormat.hpp).
//
// An import replaces a release's save whole: the files it replaces and the copies the game would read before it
// (Nebula.sav.1 and .2, GameData.bin.1 and .2; the release's Nebula.sav when a GameData.bin comes alone) move to
// <save root>-backup-<date> beside the save root, in the same layout. Nothing is written until the whole source has
// been read and checked; a failed import puts every file back. Paths are relative to the save root, '/'-separated.
//
// The per-file import places chosen user files of the source in chosen user files of the save: the game module reads
// the source's save as its start reads a save and gives it with the save in memory, both as Nebula.sav
// (nebula_save_import.h); merge_saves puts the chosen files together, and the result is placed as a whole import is.
//
// Every size read from a source is bounded; zip entries that leave the folder ("..", absolute, drive) refuse the
// whole source. No function throws.
#pragma once

#include <nebula_save_import.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::save_transfer {

enum class Error : std::int32_t {
    kNone = 0,
    kUnsupported,   // not a save, or a container that cannot be opened (a Datel save, an encrypted zip)
    kNewerVersion,  // a Nebula saves archive or Nebula.sav of a later version
    kDamaged,       // a known form whose checks fail: checksum, CRC, digest, sizes, a zip's structure or paths
    kWrongGame,     // another game's save
    kWrongRegion,   // an archive holding other releases' saves only, more than one of them (which is meant?)
    kTooLarge,      // a file, an entry or the source past its bound
    kNoSaves,       // a zip or folder without a Super Mario Galaxy save; export: no save to export
    kIo,            // the file system refused a read, write or move
};

// What a source is, as read.
enum class Kind : std::uint8_t {
    kUnknown,
    kNebulaArchive,  // zip (or folder) with nebula-saves.json
    kZip,            // another zip: a save folder or a save file inside
    kFolder,         // a folder: a save folder, or a folder holding a save file
    kGameData,       // a loose GameData.bin
    kWiiSdSave,      // a Wii SD card data.bin
    kNebulaSave,     // a loose Nebula.sav
    kDatelSave,      // WIISAVE001: recognised, not supported
};

// The GameData.bin layout of the save the import places (kNone: it places a Nebula.sav).
enum class GameDataFormat : std::uint8_t {
    kNone,
    kWii,     // big-endian, version 2 (Wii, Dolphin, a Wii SD save)
    kSwitch,  // little-endian, version 2 (Super Mario 3D All-Stars)
    kNative,  // little-endian, version 3 (Nebula 1.0)
};

// Bounds on what is read from a source.
inline constexpr std::size_t kMaxFileBytes = 16u << 20;    // any one file (a Nebula.sav with mods' values included)
inline constexpr std::size_t kMaxSourceBytes = 64u << 20;  // a whole source: a zip, or a folder's files together
inline constexpr std::size_t kMaxEntries = 4096;           // a zip's entries, an archive's listed items

struct SaveFile {
    std::string path;  // relative to the save root
    std::vector<std::byte> bytes;
};

// What reading a source found. On kNone, `files` is what the import writes.
struct ImportPlan {
    Error error = Error::kNone;
    Kind kind = Kind::kUnknown;
    GameDataFormat format = GameDataFormat::kNone;  // of the target release's GameData.bin, when one is placed
    std::string source_code;                        // the release a save came from ("RMGP"), empty when not known
    std::string target_code;                        // the release it is placed for
    std::vector<SaveFile> files;
    std::vector<std::string> codes;  // the releases whose save the import replaces
    bool banner_left_out = false;    // a banner.bin that failed its check
    bool miis_left_out = false;      // a Wii Mii database or nebula-miis.txt that failed its check
    bool mii_library = false;        // the import writes nebula-miis.txt (the Mii library is read again)
};

// The releases' codes ("RMGE", "RMGP", "RMGJ", "RMGK") and their save folders.
[[nodiscard]] bool is_release_code(std::string_view code) noexcept;
[[nodiscard]] std::string native_save_path(std::string_view code);  // <code>/Nebula.sav
[[nodiscard]] std::string game_data_path(std::string_view code);    // title/00010000/<hex>/data/GameData.bin

// Reads and checks a source (a file or folder, or a file's bytes) for the release `target_code` (the disc's game id's
// first four characters). A save of one release only is placed for the target whatever its release; an archive keeps
// each release's save as its own.
[[nodiscard]] ImportPlan plan_import(const std::filesystem::path& source, std::string_view target_code) noexcept;
[[nodiscard]] ImportPlan plan_import(std::span<const std::byte> bytes, std::string_view target_code) noexcept;

// Whether committing `plan` replaces a save the save root holds (ask the player first).
[[nodiscard]] bool replaces_saves(const ImportPlan& plan, const std::filesystem::path& save_root) noexcept;

// Whether `source`, read as `plan`, is a save, so that a file dropped on the window or opened with the game goes to the
// save import rather than the mod or Mii import: a file named GameData.bin or Nebula.sav, a file of a save's own form
// (a Nebula saves archive, a Nebula.sav, a Wii SD save, a Datel save) whatever its checks found, a GameData.bin that
// passes the game's check, and a zip or folder in which a save was found (of another game or release included).
[[nodiscard]] bool is_save_source(const std::filesystem::path& source, const ImportPlan& plan) noexcept;

struct ImportResult {
    Error error = Error::kNone;
    std::filesystem::path backup;  // the replaced files; empty when nothing was replaced
    std::size_t written = 0;
};

// Places `plan`'s files in the save root. Call it with no save request in flight; the game reads the save again after.
[[nodiscard]] ImportResult commit_import(const ImportPlan& plan, const std::filesystem::path& save_root) noexcept;

// The save root's saves as a Nebula saves archive: each release's save (the newest copy that passes the game's check,
// as Nebula.sav or GameData.bin), its banner, its mods' files (<code>/luma), the Mii library and the Wii Mii database.
// `nebula_version` goes in the manifest. kNoSaves when the root holds no save.
[[nodiscard]] Error make_export(const std::filesystem::path& save_root, std::string_view nebula_version,
                                std::vector<std::byte>* zip) noexcept;

struct ExportResult {
    Error error = Error::kNone;
    std::filesystem::path file;
};

// Writes Nebula-Saves-<yyyyMMdd-HHmmss>.zip (local time; -2, -3... when taken) into `folder`, read back before it is
// named.
[[nodiscard]] ExportResult export_saves(const std::filesystem::path& save_root, const std::filesystem::path& folder,
                                        std::string_view nebula_version) noexcept;

// The running game's save root and release ("RMGE"), for the settings' SAVES rows: the host sets them once the disc
// is open. Empty before, and in a run without a save folder.
void set_game_saves(const std::filesystem::path& save_root, std::string_view code);
[[nodiscard]] std::filesystem::path game_save_root();
[[nodiscard]] std::string game_code();

// ---- the per-file import ----

inline constexpr std::size_t kUserFiles = NEBULA_SAVE_IMPORT_FILES;  // a save's user files, 1 to 6

// The save of the plan's target release, which the per-file import has the game read: its Nebula.sav or GameData.bin.
// Null when the plan places none.
[[nodiscard]] const SaveFile* release_save(const ImportPlan& plan) noexcept;
// Whether the plan places more than that save and its banner (Miis, mods' files, another release's save): only its
// whole import brings them.
[[nodiscard]] bool places_more_than_save(const ImportPlan& plan) noexcept;

// `current` with imported user file i + 1 (its game data and its config) in user file targets[i] (1 to 6; 0 leaves it
// out), in Nebula.sav's form, rows in the game's order. Both are Nebula.sav files as the game makes them
// (nebula_save_import.h). kDamaged: either is not one; kUnsupported: a target past 6, or one given twice; kNoSaves:
// none chosen.
[[nodiscard]] Error merge_saves(std::span<const std::byte> current, std::span<const std::byte> imported,
                                std::span<const std::uint8_t, kUserFiles> targets, std::vector<std::byte>* merged) noexcept;
// A plan placing `save` (a Nebula.sav) as release `code`'s save: commit_import moves that release's save and copies to
// the backup first, as a whole import does. kDamaged when `save` is not a Nebula.sav.
[[nodiscard]] ImportPlan plan_native_save(std::vector<std::byte> save, std::string_view code) noexcept;

// The game module's nebula_save_import_read, given by the host once the module is loaded; null before, and with a
// module without it (the import is then whole only).
void set_game_reader(NebulaSaveImportRead read) noexcept;
[[nodiscard]] NebulaSaveImportRead game_reader() noexcept;

namespace detail {

// AES-128-CBC decryption (no padding; `bytes` a multiple of 16), in place.
void aes128_cbc_decrypt(const std::uint8_t key[16], const std::uint8_t iv[16], std::span<std::uint8_t> bytes) noexcept;

struct ZipEntry {
    std::string name;
    std::vector<std::byte> bytes;
};
// A zip's files (directories left out), stored or deflated. kDamaged for a bad structure, CRC, duplicate name or a
// name that leaves the folder; kUnsupported for encryption, ZIP64 or another method.
[[nodiscard]] Error read_zip(std::span<const std::byte> zip, std::vector<ZipEntry>* entries) noexcept;
// A zip of stored entries (names UTF-8), in this order.
[[nodiscard]] bool write_zip(std::span<const ZipEntry> entries, std::vector<std::byte>* zip) noexcept;
// A zip entry name inside its folder: relative, no "." or ".." part, no drive, no empty part.
[[nodiscard]] bool is_safe_entry_name(std::string_view name) noexcept;

// Tests: commit_import fails at its `step`th move (0: never), and puts back what it moved.
void set_commit_fault(int step) noexcept;

}  // namespace detail

}  // namespace galaxy::save_transfer
