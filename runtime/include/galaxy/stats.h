#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

// Nebula's all-time counters (jumps, spins, coins, lives lost, play time...): the game counts them and the achievement
// board shows them. Kept across save files in stats.ini next to the settings, written by a background thread so a
// count never waits on the disk.
namespace galaxy::stats {

// A counter's name: 1 to 32 of a-z, 0-9 and '_'. Others are ignored.
[[nodiscard]] bool valid_name(std::string_view name) noexcept;
void add(std::string_view name, std::uint64_t delta);
[[nodiscard]] std::uint64_t get(std::string_view name);
// Writes what is waiting now (the host's exit).
void flush();

// Tests: sets the file (empty: stats.ini in the settings folder), drops the cache and stops the writer.
void set_path_for_tests(const std::filesystem::path& path);
}  // namespace galaxy::stats
