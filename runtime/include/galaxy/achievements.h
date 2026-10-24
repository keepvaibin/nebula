#pragma once

#include <filesystem>
#include <span>
#include <string_view>

// Nebula's achievements: goals that take real play (every comet, every hidden star, Luigi's 120...), none that the
// story hands out. The game checks them against its save data (NebulaAchievements, at each stage start) and asks the
// host to unlock; they are the player's, kept across files in achievements.ini next to the settings.
namespace galaxy::achievements {

struct Info {
    const char* id;
    const char* name;  // as the game's banner and the settings page show it
    const char* hint;
};

// The catalogue, in the order the settings page lists it.
[[nodiscard]] std::span<const Info> list() noexcept;
// True when `id` is in the catalogue and was not unlocked yet; it is then saved as unlocked.
bool unlock(std::string_view id);
[[nodiscard]] bool unlocked(std::string_view id);

// Tests: the file kept (achievements.ini in the settings folder when empty) and the cache dropped.
void set_path_for_tests(const std::filesystem::path& path);
}  // namespace galaxy::achievements
