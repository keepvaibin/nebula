// The game's save files (GameData.bin) in the player's save folder, by name: whole-file reads, atomic whole-file
// writes and deletes. The results are the native game's save request results (NEBULA_SAVE_*).
#pragma once

#include <cstdint>
#include <filesystem>
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

[[nodiscard]] Result read(const std::filesystem::path& folder, std::string_view name, void* buffer,
                          std::uint32_t capacity, std::uint32_t* length) noexcept;
// Replaces the whole file: a reader sees the old contents or the new, never a part (galaxy/atomic_file.h).
[[nodiscard]] Result write(const std::filesystem::path& folder, std::string_view name, const void* data,
                           std::uint32_t size) noexcept;
[[nodiscard]] Result remove(const std::filesystem::path& folder, std::string_view name) noexcept;

}  // namespace galaxy::saves
