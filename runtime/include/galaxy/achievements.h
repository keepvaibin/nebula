#pragma once

#include <filesystem>
#include <span>
#include <string_view>

// Nebula's achievements. The game checks them against its save data (NebulaAchievements) and asks the host to unlock
// them; they are kept across save files in achievements.ini next to the settings.
namespace galaxy::achievements {

enum class Group { Story, Challenge, Extra };

struct Info {
    const char* id;
    Group group;
    const char* name;  // English, for the host's log
};

// The catalogue, grouped: story, challenges, extras.
[[nodiscard]] std::span<const Info> list() noexcept;
[[nodiscard]] const Info* find(std::string_view id) noexcept;
// True when `id` is in the catalogue and was not unlocked yet; it is then unlocked, and written to the file in the
// background (several unlocked together, once).
bool unlock(std::string_view id);
[[nodiscard]] bool unlocked(std::string_view id);
// The day it was unlocked, in the player's time zone: year * 10000 + month * 100 + day; 0 when locked; 1 when
// unlocked on a day that cannot be read.
[[nodiscard]] int unlocked_date(std::string_view id);

// Writes what is not written yet, now (before the process ends).
void flush();

// Tests: writes what is waiting, then sets the file (empty: achievements.ini in the settings folder) and drops the
// cache.
void set_path_for_tests(const std::filesystem::path& path);
}  // namespace galaxy::achievements
