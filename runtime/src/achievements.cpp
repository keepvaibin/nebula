#include "galaxy/achievements.h"

#include "galaxy/atomic_file.h"
#include "galaxy/runtime_settings.h"

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace galaxy::achievements {

namespace {
// Their English names (the host's log); the game shows each name and what earned it from NebulaText
// (localization/NebulaText.tsv) in the played language.
constexpr Info kCatalogue[] = {
    {"first_star", Group::Story, "A Star Is Born"},
    {"gateway", Group::Story, "Welcome Aboard"},
    {"robot_reactor", Group::Story, "Mega Takedown"},
    {"star_reactor", Group::Story, "Reactor Overload"},
    {"airship_armada", Group::Story, "Sink the Fleet"},
    {"dark_matter", Group::Story, "Anti-Matter? I Barely Know Her!"},
    {"lava_reactor", Group::Story, "Too Hot to Handle"},
    {"luigi_found", Group::Story, "Found You, Luigi!"},
    {"halfway", Group::Story, "Halfway There"},
    {"ending", Group::Story, "The Universe Reborn"},
    {"storybook", Group::Story, "The Mother of Stars"},
    {"stars120", Group::Challenge, "Super Mario Galaxy"},
    {"luigi120", Group::Challenge, "Super Luigi Galaxy"},
    {"grandfinale", Group::Challenge, "Grand Finale"},
    {"comets", Group::Challenge, "Comet Chaser"},
    {"daredevil", Group::Challenge, "One-Hit Wonder"},
    {"cosmic", Group::Challenge, "Light-Years Ahead"},
    {"speedy", Group::Challenge, "Beat the Clock"},
    {"purplecoins", Group::Challenge, "Purple Reign"},
    {"hidden", Group::Challenge, "Hidden Treasures"},
    {"wayfarer", Group::Challenge, "Galactic Wayfarer"},
    {"greenstars", Group::Challenge, "Going Green"},
    {"trials", Group::Challenge, "Trial Run"},
    {"lumas", Group::Challenge, "All You Can Eat"},
    {"bank", Group::Challenge, "Star Bit Tycoon"},
    {"lives", Group::Challenge, "1UP Hoarder"},
    {"no_miss_ending", Group::Challenge, "One-Life Wonder"},
    {"no_miss_120", Group::Challenge, "Cosmic Immortal"},
    {"first_comet", Group::Extra, "Wish Upon A Star"},
    {"red_star", Group::Extra, "All Aboard Air Mario!"},
    {"first_luma", Group::Extra, "Snack Time"},
    {"luigi_play", Group::Extra, "Green Means Go"},
    {"bits1000", Group::Extra, "Pocketful of Star Bits"},
    {"lives50", Group::Extra, "Life of the Party"},
    {"coins999", Group::Extra, "Coin-noisseur"},
};

// Unlocks arriving together (a save that already meets many goals) are written once.
constexpr std::chrono::milliseconds kWriteDelay{250};
// Between tries after a write failed.
constexpr std::chrono::seconds kRetryDelay{5};

// Never destroyed: the writer thread may still wait on it while the process ends.
struct State {
    std::mutex mutex;
    std::mutex io;  // one write at a time, in the order their contents were taken
    std::condition_variable wake;
    std::filesystem::path path;
    bool loaded = false;
    bool dirty = false;
    bool writer = false;
    std::map<std::string, std::string> unlocked;  // id -> when (UTC, ISO 8601)
};

State& state() {
    static State* s = new State;
    return *s;
}

std::filesystem::path file_path_locked(const State& s) {
    if (!s.path.empty()) return s.path;
    const std::filesystem::path folder = runtime_settings_folder();
    return folder.empty() ? folder : folder / "achievements.ini";
}

void load_locked(State& s) {
    if (s.loaded) return;
    s.loaded = true;
    std::ifstream in(file_path_locked(s));
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0u) continue;
        s.unlocked[line.substr(0, eq)] = line.substr(eq + 1u);
    }
}

// Writes what is waiting; false when the write failed (the unlocks stay waiting for the next try).
bool write_waiting() {
    State& s = state();
    const std::lock_guard<std::mutex> io(s.io);
    std::string contents;
    std::filesystem::path path;
    {
        const std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.dirty) return true;
        s.dirty = false;
        std::ostringstream out;
        for (const auto& [key, value] : s.unlocked) out << key << '=' << value << '\n';
        contents = out.str();
        path = file_path_locked(s);
    }
    if (path.empty() || io::write_file_atomically(path, contents)) return true;
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.dirty = true;
    return false;
}

void writer_main() {
    State& s = state();
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(s.mutex);
            s.wake.wait(lock, [&s] { return s.dirty; });
        }
        std::this_thread::sleep_for(kWriteDelay);
        // A failed write (the folder is locked for a moment) is tried again after a back-off, not in a spin.
        if (!write_waiting()) std::this_thread::sleep_for(kRetryDelay);
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

namespace {
bool unlock_key(std::string_view id) {
    State& s = state();
    {
        const std::lock_guard<std::mutex> lock(s.mutex);
        load_locked(s);
        if (s.unlocked.count(std::string(id)) != 0u) return false;
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        char when[32];
        std::strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ", &utc);
        s.unlocked[std::string(id)] = when;
        s.dirty = true;
        if (!s.writer) {
            s.writer = true;
            std::thread(writer_main).detach();
        }
    }
    s.wake.notify_all();
    return true;
}
}  // namespace

bool unlock(std::string_view id) {
    return find(id) != nullptr && unlock_key(id);
}

bool unlock_mod(std::string_view key) {
    // The file's lines are key=when: a key with '=' or a line break would read back as another.
    if (!key.starts_with("mod:") || key.size() <= 4u || key.find_first_of("=\r\n") != std::string_view::npos) return false;
    return unlock_key(key);
}

bool unlocked(std::string_view id) {
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    load_locked(s);
    return s.unlocked.count(std::string(id)) != 0u;
}

int unlocked_date(std::string_view id) {
    std::string when;
    {
        State& s = state();
        const std::lock_guard<std::mutex> lock(s.mutex);
        load_locked(s);
        const auto it = s.unlocked.find(std::string(id));
        if (it == s.unlocked.end()) return 0;
        when = it->second;
    }
    // Unlocked on a day that cannot be read (an edited file): 1, still unlocked.
    std::tm utc{};
    std::istringstream in(when);
    in >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    if (in.fail()) return 1;
#if defined(_WIN32)
    const std::time_t time = _mkgmtime(&utc);
#else
    const std::time_t time = timegm(&utc);
#endif
    std::tm local{};
#if defined(_WIN32)
    if (time == static_cast<std::time_t>(-1) || localtime_s(&local, &time) != 0) return 1;
#else
    if (time == static_cast<std::time_t>(-1) || localtime_r(&time, &local) == nullptr) return 1;
#endif
    return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}

void flush() {
    (void)write_waiting();
}

void set_path_for_tests(const std::filesystem::path& path) {
    (void)write_waiting();
    State& s = state();
    const std::lock_guard<std::mutex> io(s.io);
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.path = path;
    s.loaded = false;
    s.dirty = false;
    s.unlocked.clear();
}
}  // namespace galaxy::achievements
