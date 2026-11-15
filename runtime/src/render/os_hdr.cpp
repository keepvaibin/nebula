// SPDX-License-Identifier: GPL-3.0-only
// Windows HDR for the game's display (os_hdr.h).
#include "galaxy/render/os_hdr.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#if defined(_WIN32)
#include "galaxy/platform_paths.h"
#include "galaxy/render/display_modes.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <system_error>
#include <thread>
#endif

namespace galaxy::render::os_hdr {

namespace plan {
namespace {

bool has(const std::vector<std::string>& list, const std::string& path) {
    return std::find(list.begin(), list.end(), path) != list.end();
}

void add(std::vector<std::string>& list, const std::string& path) {
    if (!has(list, path)) list.push_back(path);
}

const Target* find(const std::vector<Target>& now, const std::string& path) {
    const auto it = std::find_if(now.begin(), now.end(), [&](const Target& target) { return target.path == path; });
    return it != now.end() ? &*it : nullptr;
}

}  // namespace

Steps next(const std::vector<Target>& now, const std::vector<std::string>& wished, bool on, bool off_due, Memory& memory) {
    Steps steps;
    // An owned display with HDR off was turned off outside the game: no longer the game's to turn off, and, while the game
    // wishes it on, the player's choice.
    for (auto it = memory.owned.begin(); it != memory.owned.end();) {
        const Target* target = find(now, *it);
        if (target != nullptr && !target->on) {
            if (on && has(wished, *it)) add(memory.declined, *it);
            it = memory.owned.erase(it);
        } else {
            ++it;
        }
    }
    // The same for a wished display that had HDR on before the game wished it (the player's own).
    for (const std::string& path : wished) {
        const Target* target = find(now, path);
        if (on && target != nullptr && !target->on && has(memory.seen_on, path)) add(memory.declined, path);
    }
    memory.seen_on.clear();
    if (on) {
        for (const std::string& path : wished) {
            const Target* target = find(now, path);
            if (target != nullptr && target->on) memory.seen_on.push_back(path);
        }
    }
    if (off_due) {
        for (const std::string& path : memory.owned) {
            if (on && has(wished, path)) continue;
            const Target* target = find(now, path);
            if (target != nullptr && target->on) steps.off.push_back(path);
        }
    }
    if (on) {
        for (const std::string& path : wished) {
            const Target* target = find(now, path);
            if (target == nullptr || target->on || !target->supported) continue;
            if (target->blocked || has(memory.declined, path)) {
                steps.refused = true;
                continue;
            }
            steps.on.push_back(path);
        }
    }
    return steps;
}

std::string list_text(const List& list) {
    std::string text = "process " + std::to_string(list.process) + "\n";
    for (const std::string& path : list.paths) {
        text += path;
        text += '\n';
    }
    return text;
}

List read_list(const std::string& text) {
    List list;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        const std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line.erase(0, start);
        if (line.rfind("process ", 0) == 0) {
            list.process = std::strtoul(line.c_str() + 8, nullptr, 10);
        } else {
            add(list.paths, line);
        }
    }
    return list;
}

}  // namespace plan

#if defined(_WIN32)
namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

// DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 and DISPLAYCONFIG_SET_HDR_STATE (Windows 11 24H2), declared here as wingdi.h
// declares them: only for NTDDI_WIN11_GA and later, above this build's target.
constexpr auto kGetColorInfo2 = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(15);
constexpr auto kSetHdrState = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(16);
struct ColorInfo2 {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value;  // bit 3 advancedColorLimitedByPolicy, bit 4 highDynamicRangeSupported
    UINT32 colour_encoding;
    UINT32 bits_per_channel;
    UINT32 active_mode;  // DISPLAYCONFIG_ADVANCED_COLOR_MODE: 0 SDR, 1 WCG, 2 HDR
};
struct SetHdrState {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value;  // bit 0 enableHdr
};
static_assert(sizeof(ColorInfo2) == sizeof(DISPLAYCONFIG_DEVICE_INFO_HEADER) + 16u);
static_assert(sizeof(SetHdrState) == sizeof(DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE));
constexpr UINT32 kModeHdr = 2u;

// One active path: the source's GDI name (\\.\DISPLAY1, the window's monitor) and the target, the display itself.
struct Path {
    std::wstring source;
    LUID adapter{};
    UINT32 id = 0;
    std::string device;  // monitorDevicePath: the display's own name, the same across starts
};

std::vector<Path> active_paths() {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    for (int attempt = 0; attempt < 4; ++attempt) {
        UINT32 path_count = 0;
        UINT32 mode_count = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) return {};
        paths.resize(path_count);
        modes.resize(mode_count);
        const LONG result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER) continue;  // the displays changed between the two calls
        if (result != ERROR_SUCCESS) return {};
        paths.resize(path_count);
        break;
    }
    std::vector<Path> out;
    for (const DISPLAYCONFIG_PATH_INFO& info : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), info.sourceInfo.adapterId, info.sourceInfo.id};
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target), info.targetInfo.adapterId, info.targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS ||
            target.monitorDevicePath[0] == L'\0') {
            continue;
        }
        out.push_back({source.viewGdiDeviceName, info.targetInfo.adapterId, info.targetInfo.id, narrow_utf8(target.monitorDevicePath)});
    }
    return out;
}

// The display's HDR as Windows has it, and whether it knows the 24H2 calls.
Display state_of(const Path& path, bool* hdr_state_call = nullptr) {
    Display display;
    ColorInfo2 info2{};
    info2.header = {kGetColorInfo2, sizeof(info2), path.adapter, path.id};
    if (DisplayConfigGetDeviceInfo(&info2.header) == ERROR_SUCCESS) {
        if (hdr_state_call != nullptr) *hdr_state_call = true;
        display.known = true;
        display.supported = ((info2.value >> 4u) & 1u) != 0u;
        display.on = info2.active_mode == kModeHdr;
        display.blocked = !display.on && ((info2.value >> 3u) & 1u) != 0u;
        return display;
    }
    if (hdr_state_call != nullptr) *hdr_state_call = false;
    // Before 24H2: advanced colour is HDR on an HDR display; wide colour enforced is an SDR display's colour management.
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info{};
    info.header = {DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, sizeof(info), path.adapter, path.id};
    if (DisplayConfigGetDeviceInfo(&info.header) == ERROR_SUCCESS) {
        display.known = true;
        display.supported = info.advancedColorSupported != 0u && info.wideColorEnforced == 0u;
        display.on = display.supported && info.advancedColorEnabled != 0u;
        display.blocked = info.advancedColorForceDisabled != 0u;
    }
    return display;
}

bool set_state(const Path& path, bool on) {
    bool hdr_state_call = false;
    (void)state_of(path, &hdr_state_call);
    if (hdr_state_call) {
        SetHdrState set{};
        set.header = {kSetHdrState, sizeof(set), path.adapter, path.id};
        set.value = on ? 1u : 0u;
        return DisplayConfigSetDeviceInfo(&set.header) == ERROR_SUCCESS;
    }
    DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE set{};
    set.header = {DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE, sizeof(set), path.adapter, path.id};
    set.enableAdvancedColor = on ? 1u : 0u;
    return DisplayConfigSetDeviceInfo(&set.header) == ERROR_SUCCESS;
}

const Path* path_of(const std::vector<Path>& paths, const std::string& device) {
    const auto it = std::find_if(paths.begin(), paths.end(), [&](const Path& path) { return path.device == device; });
    return it != paths.end() ? &*it : nullptr;
}

std::wstring device_of(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    return monitor != nullptr && GetMonitorInfoW(monitor, &info) ? std::wstring(info.szDevice) : std::wstring();
}

std::filesystem::path handover_path() {
    const std::filesystem::path data = nebula_data_directory();
    return data.empty() ? std::filesystem::path() : data / "hdr-handover.txt";
}

plan::List read_list_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return plan::read_list(text.str());
}

// Written beside and moved over: a reader never sees half a list.
void write_list_file(const std::filesystem::path& file, const plan::List& list) {
    if (file.empty()) return;
    std::error_code error;
    if (list.paths.empty()) {
        std::filesystem::remove(file, error);
        return;
    }
    std::filesystem::create_directories(file.parent_path(), error);
    std::filesystem::path part = file;
    part += L".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        out << plan::list_text(list);
        if (!out) return;
    }
    if (!MoveFileExW(part.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(part, error);
    }
}

bool process_running(unsigned long id) {
    if (id == 0u || id == GetCurrentProcessId()) return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (process == nullptr) return false;
    DWORD code = 0;
    const bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return running;
}

std::string name_of(const Path& path) {
    return narrow_utf8(path.source);
}

// Turns off what a list names (the game that owned it has ended, or this one ends).
void turn_off(const std::vector<std::string>& devices, const char* why) {
    if (devices.empty()) return;
    const std::vector<Path> paths = active_paths();
    for (const std::string& device : devices) {
        const Path* path = path_of(paths, device);
        if (path == nullptr || !state_of(*path).on) continue;
        const bool done = set_state(*path, false);
        std::cerr << "[display] Windows HDR " << (done ? "turned off again" : "could not be turned off again") << " for "
                  << name_of(*path) << " (" << why << ")\n";
    }
}

constexpr auto kOffDelay = 1s;    // a wish held this long turns displays off: a passing change does not switch them twice
constexpr auto kAdoptWait = 10s;  // an adopted display not wished on within this is turned off

std::mutex g_mutex;
std::condition_variable g_wake;
std::thread* g_thread = nullptr;  // never destroyed: the process ends without static destructors joining it
bool g_stop = false;
bool g_dirty = false;
bool g_wished = false;
std::wstring g_device;  // the window's display (its GDI name)
bool g_on = false;
unsigned g_wish = 0;  // counts the wish's changes: a pass's declined list is kept only if the wish held through it
Clock::time_point g_changed{};
plan::Memory g_memory;
std::atomic<bool> g_refused{false};

void save_owned_locked() {
    write_list_file(list_path(), {GetCurrentProcessId(), g_memory.owned});
}

void run() {
    std::unique_lock<std::mutex> lock(g_mutex);
    bool waiting_off = false;
    for (;;) {
        if (waiting_off) {
            g_wake.wait_until(lock, g_changed + kOffDelay, [] { return g_stop || g_dirty; });
        } else {
            g_wake.wait(lock, [] { return g_stop || g_dirty; });
        }
        if (g_stop) return;
        g_dirty = false;
        const std::wstring device = g_device;
        const bool on = g_wished && g_on;
        const unsigned wish = g_wish;
        const bool off_due = Clock::now() >= g_changed + kOffDelay;
        plan::Memory memory = g_memory;
        lock.unlock();

        const std::vector<Path> paths = active_paths();
        std::vector<plan::Target> now;
        std::vector<std::string> wished;
        for (const Path& path : paths) {
            const Display display = state_of(path);
            if (!display.known) continue;
            now.push_back({path.device, display.supported, display.on, display.blocked});
            if (path.source == device) wished.push_back(path.device);
        }
        const std::vector<std::string> owned_before = memory.owned;
        const plan::Steps steps = plan::next(now, wished, on, off_due, memory);
        bool refused = steps.refused;
        for (const std::string& target : steps.off) {
            const Path* path = path_of(paths, target);
            if (path == nullptr) continue;
            const bool done = set_state(*path, false);
            std::cerr << "[display] Windows HDR " << (done ? "turned off again" : "could not be turned off again") << " for "
                      << name_of(*path) << '\n';
            if (done) memory.owned.erase(std::remove(memory.owned.begin(), memory.owned.end(), target), memory.owned.end());
        }
        for (const std::string& target : steps.on) {
            const Path* path = path_of(paths, target);
            if (path == nullptr) continue;
            // Listed before the switch: a game that ends during it still has the watcher turn the display off.
            memory.owned.push_back(target);
            {
                const std::lock_guard<std::mutex> relock(g_mutex);
                g_memory.owned = memory.owned;
                save_owned_locked();
            }
            const bool done = set_state(*path, true) && state_of(*path).on;
            std::cerr << "[display] Windows HDR " << (done ? "turned on" : "not turned on: Windows refused") << " for " << name_of(*path)
                      << (done ? " while the game shows HDR" : "") << '\n';
            if (done) {
                memory.seen_on.push_back(target);
            } else {
                memory.owned.erase(std::remove(memory.owned.begin(), memory.owned.end(), target), memory.owned.end());
                memory.declined.push_back(target);
                refused = true;
            }
        }
        waiting_off = false;
        if (!off_due) {
            for (const std::string& target : memory.owned) {
                if (!(on && std::find(wished.begin(), wished.end(), target) != wished.end())) waiting_off = true;
            }
        }

        lock.lock();
        const bool same_wish = wish == g_wish;
        g_memory.owned = memory.owned;
        g_memory.seen_on = memory.seen_on;
        if (same_wish) {
            g_memory.declined = memory.declined;
            g_refused.store(refused, std::memory_order_release);
        }
        if (g_memory.owned != owned_before) save_owned_locked();
    }
}

void start_locked() {
    if (g_thread == nullptr) g_thread = new std::thread(run);
}

}  // namespace

Display read(HMONITOR monitor) {
    const std::wstring device = device_of(monitor);
    if (device.empty()) return {};
    for (const Path& path : active_paths()) {
        if (path.source == device) return state_of(path);
    }
    return {};
}

void want(HMONITOR monitor, bool on) {
    std::wstring device = device_of(monitor);
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (g_wished && device == g_device && on == g_on) return;
    g_wished = true;
    g_device = std::move(device);
    g_on = on;
    ++g_wish;
    g_changed = Clock::now();
    g_memory.declined.clear();
    g_refused.store(false, std::memory_order_release);
    if (!on && g_memory.owned.empty() && g_thread == nullptr) return;  // nothing to do: no thread for it
    g_dirty = true;
    start_locked();
    g_wake.notify_one();
}

void display_changed() {
    const std::lock_guard<std::mutex> lock(g_mutex);
    if (g_thread == nullptr) return;
    g_dirty = true;
    g_wake.notify_one();
}

bool refused() noexcept {
    return g_refused.load(std::memory_order_acquire);
}

std::filesystem::path list_path() {
    const std::filesystem::path data = nebula_data_directory();
    return data.empty() ? std::filesystem::path() : data / "hdr-restore.txt";
}

void adopt() {
    const std::filesystem::path list = list_path();
    if (list.empty()) return;
    std::vector<std::string> devices;
    // A restart's hand-over, and a list whose game is not running (an end the watcher missed). A list of a game still
    // running is that game's.
    const std::filesystem::path handover = handover_path();
    for (const std::string& device : read_list_file(handover).paths) devices.push_back(device);
    const plan::List left = read_list_file(list);
    if (!process_running(left.process)) {
        for (const std::string& device : left.paths) {
            if (std::find(devices.begin(), devices.end(), device) == devices.end()) devices.push_back(device);
        }
    }
    std::error_code error;
    std::filesystem::remove(handover, error);
    if (devices.empty()) return;
    const std::lock_guard<std::mutex> lock(g_mutex);
    for (const std::string& device : devices) {
        if (std::find(g_memory.owned.begin(), g_memory.owned.end(), device) == g_memory.owned.end()) g_memory.owned.push_back(device);
    }
    save_owned_locked();
    std::cerr << "[display] Windows HDR: " << devices.size() << " display(s) turned on by the last run, kept while the settings ask\n";
    g_changed = Clock::now() + kAdoptWait - kOffDelay;
    g_dirty = true;
    start_locked();
    g_wake.notify_one();
}

void finish(bool hand_over) {
    std::vector<std::string> owned;
    std::thread* thread = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        g_stop = true;
        thread = g_thread;
        g_wake.notify_one();
    }
    if (thread != nullptr && thread->joinable()) thread->join();
    {
        const std::lock_guard<std::mutex> lock(g_mutex);
        owned = g_memory.owned;
        g_memory.owned.clear();
    }
    if (owned.empty()) return;
    if (hand_over) {
        write_list_file(handover_path(), {GetCurrentProcessId(), owned});
        std::error_code error;
        std::filesystem::remove(list_path(), error);
        return;
    }
    turn_off(owned, "the game ended");
    std::error_code error;
    std::filesystem::remove(list_path(), error);
}

void restore_listed(const std::filesystem::path& list, unsigned long game) {
    if (list.empty()) return;
    const plan::List listed = read_list_file(list);
    if (listed.paths.empty() || listed.process != game) return;
    turn_off(listed.paths, "the game ended without turning it off");
    std::error_code error;
    std::filesystem::remove(list, error);
}
#endif

}  // namespace galaxy::render::os_hdr
