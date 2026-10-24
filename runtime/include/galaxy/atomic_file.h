// Crash-safe replacement of small text files (settings, achievements).
#pragma once

#include <filesystem>
#include <string_view>

namespace galaxy::io {

// Writes `contents` to a uniquely named sibling, then replaces the destination with it once the write,
// flush and close succeed. On failure the destination is untouched and the sibling is deleted; a
// preexisting file that holds the sibling's name is never removed.
[[nodiscard]] bool write_text_file_atomically(
    const std::filesystem::path& destination,
    std::string_view contents) noexcept;

namespace detail {

// Tests: `on_collision` runs each time a sibling name is already taken.
[[nodiscard]] bool write_text_file_atomically_with_collision_hook(
    const std::filesystem::path& destination,
    std::string_view contents,
    void (*on_collision)(void*),
    void* user) noexcept;

}  // namespace detail

}  // namespace galaxy::io
