#include "galaxy/achievements.h"

#include "galaxy/atomic_file.h"
#include "galaxy/runtime_settings.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace galaxy::achievements {

namespace {
// The settings page's English; the game's pop-up takes each name from NebulaText (localization/NebulaText.tsv)
// in the played language.
constexpr Info kCatalogue[] = {
    {"first_star", Group::Story, "First Star", "GET A POWER STAR"},
    {"gateway", Group::Story, "Welcome Aboard", "WIN THE GRAND STAR IN THE GATEWAY GALAXY"},
    {"robot_reactor", Group::Story, "Bowser Jr.'s Robot Reactor", "WIN THE GRAND STAR IN BOWSER JR.'S ROBOT REACTOR"},
    {"star_reactor", Group::Story, "Bowser's Star Reactor", "WIN THE GRAND STAR IN BOWSER'S STAR REACTOR"},
    {"airship_armada", Group::Story, "Bowser Jr.'s Airship Armada",
     "WIN THE GRAND STAR IN BOWSER JR.'S AIRSHIP ARMADA"},
    {"dark_matter", Group::Story, "Bowser's Dark Matter Plant", "WIN THE GRAND STAR IN BOWSER'S DARK MATTER PLANT"},
    {"lava_reactor", Group::Story, "Bowser Jr.'s Lava Reactor", "WIN THE GRAND STAR IN BOWSER JR.'S LAVA REACTOR"},
    {"luigi_found", Group::Story, "Luigi Found", "RESCUE LUIGI IN THE GHOSTLY GALAXY"},
    {"halfway", Group::Story, "Halfway There", "COLLECT 60 POWER STARS"},
    {"ending", Group::Story, "The Universe Reborn", "DEFEAT BOWSER IN BOWSER'S GALAXY REACTOR"},
    {"storybook", Group::Story, "Bedtime Story", "READ EVERY CHAPTER OF ROSALINA'S STORYBOOK"},

    {"stars120", Group::Challenge, "Every Power Star", "COLLECT ALL 120 POWER STARS"},
    {"luigi120", Group::Challenge, "Luigi's Turn", "COLLECT ALL 120 POWER STARS AS LUIGI"},
    {"grandfinale", Group::Challenge, "Grand Finale", "GET THE POWER STAR IN THE GRAND FINALE GALAXY"},
    {"comets", Group::Challenge, "Comet Chaser", "CLEAR EVERY PRANKSTER COMET"},
    {"daredevil", Group::Challenge, "Daredevil", "CLEAR EVERY DAREDEVIL COMET"},
    {"cosmic", Group::Challenge, "Cosmic Rival", "WIN EVERY COSMIC COMET RACE"},
    {"speedy", Group::Challenge, "Beat the Clock", "CLEAR EVERY SPEEDY COMET"},
    {"purplecoins", Group::Challenge, "Purple Coin Master", "GET EVERY PURPLE COIN STAR"},
    {"hidden", Group::Challenge, "Hidden Treasures", "FIND EVERY HIDDEN STAR"},
    {"greenstars", Group::Challenge, "Green Stars", "FIND ALL THREE GREEN STARS"},
    {"trials", Group::Challenge, "Trial Run", "CLEAR THE ROLLING GIZMO, LOOPDEELOOP AND BUBBLE BLAST GALAXIES"},
    {"lumas", Group::Challenge, "Well Fed", "FEED EVERY HUNGRY LUMA THAT BECOMES A GALAXY"},
    {"bank", Group::Challenge, "Star Bit Bank", "HOLD 9,999 STAR BITS"},
    {"lives", Group::Challenge, "Extra Lives", "HAVE 99 LIVES"},

    {"first_comet", Group::Extra, "Comet Sighting", "CLEAR A PRANKSTER COMET"},
    {"red_star", Group::Extra, "Taking Flight", "FIND THE RED STAR"},
    {"first_luma", Group::Extra, "Snack Time", "FEED A HUNGRY LUMA UNTIL IT BECOMES A GALAXY"},
    {"luigi_play", Group::Extra, "Enter Luigi", "GET A POWER STAR AS LUIGI"},
    {"bits1000", Group::Extra, "Pocketful of Star Bits", "HOLD 1,000 STAR BITS"},
};

std::mutex s_mutex;
std::filesystem::path s_path;
bool s_loaded = false;
std::map<std::string, std::string> s_unlocked;  // id -> when (UTC, ISO 8601)

std::filesystem::path file_path() {
    if (!s_path.empty()) return s_path;
    const std::filesystem::path folder = runtime_settings_folder();
    return folder.empty() ? folder : folder / "achievements.ini";
}

void load_locked() {
    if (s_loaded) return;
    s_loaded = true;
    std::ifstream in(file_path());
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0u) continue;
        s_unlocked[line.substr(0, eq)] = line.substr(eq + 1u);
    }
}

}  // namespace

std::span<const Info> list() noexcept {
    return kCatalogue;
}

const Info* find(std::string_view id) noexcept {
    for (const Info& info : kCatalogue) {
        if (id == info.id) return &info;
    }
    return nullptr;
}

bool unlock(std::string_view id) {
    if (find(id) == nullptr) return false;
    const std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    if (s_unlocked.count(std::string(id)) != 0u) return false;
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char when[32];
    std::strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ", &utc);
    s_unlocked[std::string(id)] = when;
    std::ostringstream contents;
    for (const auto& [key, value] : s_unlocked) contents << key << '=' << value << '\n';
    const std::filesystem::path path = file_path();
    if (!path.empty()) (void)io::write_text_file_atomically(path, contents.str());
    return true;
}

bool unlocked(std::string_view id) {
    const std::lock_guard<std::mutex> lock(s_mutex);
    load_locked();
    return s_unlocked.count(std::string(id)) != 0u;
}

void set_path_for_tests(const std::filesystem::path& path) {
    const std::lock_guard<std::mutex> lock(s_mutex);
    s_path = path;
    s_loaded = false;
    s_unlocked.clear();
}
}  // namespace galaxy::achievements
