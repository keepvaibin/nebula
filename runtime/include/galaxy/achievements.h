#pragma once

#include <filesystem>
#include <span>
#include <string_view>

// Nebula's achievements. The game checks them against its save data (NebulaAchievements, at each stage start) and
// asks the host to unlock them; they are kept across save files in achievements.ini next to the settings.
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

// Tests: sets the file (empty: achievements.ini in the settings folder) and drops the cache.
void set_path_for_tests(const std::filesystem::path& path);
}  // namespace galaxy::achievements
