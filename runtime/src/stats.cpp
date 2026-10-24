#include "galaxy/stats.h"

#include "galaxy/atomic_file.h"
#include "galaxy/runtime_settings.h"

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace galaxy::stats {
namespace {
constexpr std::size_t kMaxCounters = 64;
// Counts arriving together (the game hands them over every few seconds) are written once.
constexpr std::chrono::milliseconds kWriteDelay{1500};

// Never destroyed: the writer thread may still wait on it while the process ends.
struct State {
    std::mutex mutex;
    std::mutex io;  // one write at a time, in the order their contents were taken
    std::condition_variable wake;
    std::filesystem::path path;
    bool loaded = false;
    bool dirty = false;
    bool writer = false;
    std::map<std::string, std::uint64_t> counters;
};

State& state() {
    static State* s = new State;
    return *s;
}

std::filesystem::path file_path_locked(const State& s) {
    if (!s.path.empty()) return s.path;
    const std::filesystem::path folder = runtime_settings_folder();
    return folder.empty() ? folder : folder / "stats.ini";
}

void load_locked(State& s) {
    if (s.loaded) return;
    s.loaded = true;
    std::ifstream in(file_path_locked(s));
    std::string line;
    while (std::getline(in, line) && s.counters.size() < kMaxCounters) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || !valid_name(std::string_view(line).substr(0, eq))) continue;
        try {
            s.counters[line.substr(0, eq)] = std::stoull(line.substr(eq + 1u));
        } catch (...) {
        }
    }
}

void write_waiting() {
    State& s = state();
    const std::lock_guard<std::mutex> io(s.io);
    std::string contents;
    std::filesystem::path path;
    {
        const std::lock_guard<std::mutex> lock(s.mutex);
        if (!s.dirty) return;
        s.dirty = false;
        std::ostringstream out;
        for (const auto& [name, value] : s.counters) out << name << '=' << value << '\n';
        contents = out.str();
        path = file_path_locked(s);
    }
    if (!path.empty()) (void)io::write_text_file_atomically(path, contents);
}

void writer_main() {
    State& s = state();
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(s.mutex);
            s.wake.wait(lock, [&s] { return s.dirty; });
        }
        std::this_thread::sleep_for(kWriteDelay);
        write_waiting();
    }
}
}  // namespace

bool valid_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 32u) return false;
    for (const char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

void add(std::string_view name, std::uint64_t delta) {
    if (!valid_name(name) || delta == 0u) return;
    State& s = state();
    {
        const std::lock_guard<std::mutex> lock(s.mutex);
        load_locked(s);
        const auto it = s.counters.find(std::string(name));
        if (it == s.counters.end()) {
            if (s.counters.size() >= kMaxCounters) return;
            s.counters.emplace(std::string(name), delta);
        } else {
            it->second = delta > UINT64_MAX - it->second ? UINT64_MAX : it->second + delta;
        }
        s.dirty = true;
        if (!s.writer) {
            s.writer = true;
            std::thread(writer_main).detach();
        }
    }
    s.wake.notify_all();
}

std::uint64_t get(std::string_view name) {
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    load_locked(s);
    const auto it = s.counters.find(std::string(name));
    return it == s.counters.end() ? 0u : it->second;
}

void flush() {
    write_waiting();
}

void set_path_for_tests(const std::filesystem::path& path) {
    write_waiting();
    State& s = state();
    const std::lock_guard<std::mutex> io(s.io);
    const std::lock_guard<std::mutex> lock(s.mutex);
    s.path = path;
    s.loaded = false;
    s.dirty = false;
    s.counters.clear();
}
}  // namespace galaxy::stats
