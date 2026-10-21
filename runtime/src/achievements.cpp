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
constexpr Info kCatalogue[] = {
    {"ending", "The Universe Reborn", "SEE THE ENDING"},
    {"stars120", "Every Power Star", "COLLECT ALL 120 POWER STARS"},
    {"luigi120", "Luigi's Turn", "COLLECT ALL 120 POWER STARS AS LUIGI"},
    {"grandfinale", "Grand Finale", "TAKE THE STAR IN THE GRAND FINALE GALAXY"},
    {"comets", "Comet Chaser", "EVERY PRANKSTER COMET STAR"},
    {"purplecoins", "Purple Haze", "EVERY PURPLE COIN COMET STAR"},
    {"hidden", "Hidden Treasures", "EVERY HIDDEN STAR"},
    {"lumas", "Well Fed", "FEED EVERY HUNGRY LUMA THAT MAKES A GALAXY"},
    {"storybook", "Bedtime Story", "READ EVERY CHAPTER OF THE STORYBOOK"},
    {"bank", "Star Bit Bank", "KEEP 9,999 STAR BITS"},
    {"lives", "Extra Lives", "HAVE 99 LIVES"},
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

bool known(std::string_view id) {
    for (const Info& info : kCatalogue) {
        if (id == info.id) return true;
    }
    return false;
}
}  // namespace

std::span<const Info> list() noexcept {
    return kCatalogue;
}

bool unlock(std::string_view id) {
    if (!known(id)) return false;
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
