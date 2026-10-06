#pragma once

#include <filesystem>
#include <string_view>

namespace galaxy::io {

// Writes a complete UTF-8/ASCII text payload to a uniquely named sibling and
// atomically replaces the destination after the write, flush and close calls
// succeed. On failure the old destination is left untouched and cleanup of
// this call's own sibling is attempted. Cleanup/power-loss durability still
// depends on the filesystem; a collided preexisting sibling is never removed.
[[nodiscard]] bool write_text_file_atomically(
    const std::filesystem::path& destination,
    std::string_view contents) noexcept;

namespace detail {

// Fault-injection seam for the ownership boundary after CREATE_NEW refuses a
// preexisting sibling. Production callers use the ordinary writer above.
[[nodiscard]] bool write_text_file_atomically_with_collision_hook(
    const std::filesystem::path& destination,
    std::string_view contents,
    void (*on_collision)(void*),
    void* user) noexcept;

}  // namespace detail

}  // namespace galaxy::io
