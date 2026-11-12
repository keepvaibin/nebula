// The game's save files (GameData.bin) in the player's save folder, by name: whole-file reads, atomic whole-file
// writes and deletes. The results are the native game's save request results (NEBULA_SAVE_*).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace galaxy::saves {

enum Result : std::int32_t {
    kDone = 0,
    kNoFile = -1,    // read or delete of a file that is not there
    kTooLarge = -2,  // read: the file is longer than the buffer (its length is still reported)
    kFailed = -3,    // a name that is not a plain file name, or the file system refused
};

// A plain file name: not empty, no separators, not "." or "..".
[[nodiscard]] bool is_plain_name(std::string_view name) noexcept;

// Mods' global files (contract services-save 7) go by the game's save requests as "luma/<plain name>": in the save
// folder's `luma` folder, written whole without the backups the game's own file keeps (a forgotten mod's file goes
// whole). The plain name of such a request, or empty for any other name.
inline constexpr std::string_view kModFolder = "luma";
[[nodiscard]] std::string_view mod_file_name(std::string_view request_name) noexcept;

// Whether a write keeps the two files it replaced last beside it (<name>.1, <name>.2).
enum class Backups { kKeep, kNone };

// Where a game's saves are under the save root, as paths from it. `current` is the game's four-character code
// ("/RMGE": Nebula.sav and its backups); `earlier` is where the versions before Nebula.sav kept GameData.bin
// ("/title/00010000/<the code in hex>/data"), which the game reads once and leaves as it was.
struct SaveFolders {
    std::string current;
    std::string earlier;
};
[[nodiscard]] SaveFolders save_folders(std::string_view code);

[[nodiscard]] Result read(const std::filesystem::path& folder, std::string_view name, void* buffer,
                          std::uint32_t capacity, std::uint32_t* length) noexcept;
// Replaces the whole file: a reader sees the old contents or the new, never a part (galaxy/atomic_file.h). With
// Backups::kKeep the two files it replaced last stay beside it as <name>.1 and <name>.2.
[[nodiscard]] Result write(const std::filesystem::path& folder, std::string_view name, const void* data,
                           std::uint32_t size, Backups backups = Backups::kKeep) noexcept;
[[nodiscard]] Result remove(const std::filesystem::path& folder, std::string_view name) noexcept;

}  // namespace galaxy::saves
