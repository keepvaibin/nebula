// SPDX-License-Identifier: GPL-3.0-only
// The test runs' host extension (nebula_host_extension.h): run.sh loads it with --extension. It reads the run's
// GALAXY_* environment once, when attached (docs/DIAGNOSTICS.md lists the names), and gives the host the scripted
// controls, the pictures and sound to keep, menu keys, window sizes, device losses and the fixed-step clock.
#include <nebula_host_extension.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include "data_guard.h"
#include "galaxy/hidden_start.h"
#define NEBULA_EXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
#include <time.h>
#define NEBULA_EXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

const NebulaHostForExtension* g_host = nullptr;

// What the host offers, when it has it.
#define HOST_HAS(name) \
    (g_host != nullptr && g_host->size >= offsetof(NebulaHostForExtension, name) + sizeof(g_host->name) && g_host->name != nullptr)

void log(const std::string& line) {
    if (HOST_HAS(log)) {
        g_host->log(g_host->host, line.c_str());
    } else {
        std::fprintf(stderr, "%s\n", line.c_str());
    }
}

// The clock the latency lines are on (the host's: QueryPerformanceCounter, CLOCK_MONOTONIC in nanoseconds).
long long clock_ticks() {
#if defined(_WIN32)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
#else
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long long>(now.tv_sec) * 1000000000ll + now.tv_nsec;
#endif
}

std::optional<std::string> env(const char* name) {
#if defined(_WIN32)
    std::string value(256u, '\0');
    DWORD length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    if (length >= value.size()) {
        value.resize(length);  // the size it asks for, its terminator included
        length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    }
    if (length == 0u || length >= value.size()) {
        return std::nullopt;
    }
    value.resize(length);
    return value;
#else
    const char* const value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

#if defined(_WIN32)
std::wstring wide_env(const wchar_t* name) {
    std::wstring value(256u, L'\0');
    DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    if (length >= value.size()) {
        value.resize(length);
        length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    }
    if (length == 0u || length >= value.size()) {
        return {};
    }
    value.resize(length);
    return value;
}

// The work-folder policy (data_guard.h) over this process's arguments; null when the run may go on.
const char* guard() {
    int count = 0;
    LPWSTR* const args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args == nullptr) {
        return "cannot read the command line";
    }
    const char* const refusal = test_guard::error(count, args, wide_env, galaxy::host::player_data_folder().wstring());
    LocalFree(args);
    return refusal;
}
#endif

// Set and not starting with 0, f or n.
bool env_flag(const char* name) {
    const std::optional<std::string> value = env(name);
    return value.has_value() && (*value)[0] != '0' && (*value)[0] != 'f' && (*value)[0] != 'F' && (*value)[0] != 'n' &&
           (*value)[0] != 'N';
}

std::uint64_t env_u64(const char* name, std::uint64_t fallback) {
    const std::optional<std::string> value = env(name);
    return value.has_value() ? std::strtoull(value->c_str(), nullptr, 10) : fallback;
}

[[noreturn]] void bad(const std::string& what) {
    log("test extension: " + what);
    std::fflush(stderr);
    std::abort();
}

bool parse_u64(std::string_view text, std::uint64_t& value) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t parsed = 0u;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10u) {
            return false;
        }
        parsed = parsed * 10u + digit;
    }
    value = parsed;
    return true;
}

std::uint64_t script_u64(const std::string& field, const char* name) {
    std::uint64_t value = 0u;
    if (!parse_u64(field, value)) {
        bad(std::string("invalid number in ") + name + ": '" + field + "'");
    }
    return value;
}

float script_float(const std::string& field, const char* name) {
    char* end = nullptr;
    const float value = field.empty() ? 0.0f : std::strtof(field.c_str(), &end);
    if (field.empty() || end == field.c_str() || *end != '\0' || !std::isfinite(value)) {
        bad(std::string("invalid number in ") + name + ": '" + field + "'");
    }
    return std::clamp(value, -1.0f, 1.0f);
}

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> fields;
    std::stringstream stream(text);
    std::string field;
    while (std::getline(stream, field, delimiter)) {
        fields.push_back(field);
    }
    if (!text.empty() && text.back() == delimiter) {
        fields.emplace_back();
    }
    return fields;
}

bool in_window(std::uint64_t frame, std::uint64_t start, std::uint64_t duration) {
    return duration != 0u && frame >= start && frame - start < duration;
}

// --- The scripted controls -----------------------------------------------------------------------------------------

struct ButtonStep {
    std::uint64_t start, duration;
    std::uint32_t buttons;
    bool shake;
    std::string text;  // for the latency line
};
struct StickStep {
    std::uint64_t start, duration;
    float x, y;
};
struct PointerAt {
    bool given = false;
    float x = 0.0f, y = 0.0f;
    std::uint64_t from = 0u;  // the second position's first frame
    std::uint64_t duration = 0u;  // 0: until the run ends
};

// GALAXY_TEST_SCRIPT_CLOCK: the frames the scripts count. game: the game's frames; frames: the frames of 3D scenes;
// stop: scene frames since the tour's current stop began, nothing before the first stop. The scene and stop counts come
// from the test plugin in the game (nebula_test_scene_frame).
enum class ScriptClock { Game, Scene, Stop };

struct Script {
    std::vector<ButtonStep> buttons;
    std::vector<StickStep> sticks;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> shakes;
    PointerAt pointer, pointer2;
    ScriptClock clock = ScriptClock::Game;
} g_script;

std::string g_latency_logged;  // the game thread's: the last step logged
constexpr std::uint64_t kNoStop = ~std::uint64_t{0};
std::atomic<std::uint64_t> g_game_frames{0};
std::atomic<std::uint64_t> g_scene_frames{0};
std::atomic<std::uint64_t> g_stop_frames{kNoStop};

std::uint32_t button_bit(const std::string& token, const char* name, bool& shake) {
    std::string upper = token;
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    static constexpr std::pair<std::string_view, std::uint32_t> kButtons[] = {
        {"A", NEBULA_EXT_BUTTON_A},         {"B", NEBULA_EXT_BUTTON_B},          {"PLUS", NEBULA_EXT_BUTTON_PLUS},
        {"START", NEBULA_EXT_BUTTON_PLUS},  {"MINUS", NEBULA_EXT_BUTTON_MINUS},  {"SELECT", NEBULA_EXT_BUTTON_MINUS},
        {"BACK", NEBULA_EXT_BUTTON_MINUS},  {"ONE", NEBULA_EXT_BUTTON_ONE},      {"1", NEBULA_EXT_BUTTON_ONE},
        {"TWO", NEBULA_EXT_BUTTON_TWO},     {"2", NEBULA_EXT_BUTTON_TWO},        {"HOME", NEBULA_EXT_BUTTON_HOME},
        {"LEFT", NEBULA_EXT_BUTTON_LEFT},   {"RIGHT", NEBULA_EXT_BUTTON_RIGHT},  {"UP", NEBULA_EXT_BUTTON_UP},
        {"DOWN", NEBULA_EXT_BUTTON_DOWN},   {"C", NEBULA_EXT_BUTTON_C},          {"Z", NEBULA_EXT_BUTTON_Z},
    };
    if (upper == "SHAKE") {
        shake = true;
        return 0u;
    }
    for (const auto& [button, bit] : kButtons) {
        if (upper == button) {
            return bit;
        }
    }
    bad(std::string("unknown button '") + token + "' in " + name);
}

void read_script() {
    if (const std::optional<std::string> clock = env("GALAXY_TEST_SCRIPT_CLOCK")) {
        g_script.clock = *clock == "frames" ? ScriptClock::Scene : *clock == "stop" ? ScriptClock::Stop : ScriptClock::Game;
    }
    // GALAXY_INPUT_AUTOPRESS_SCRIPT=<start>:<frames>:<button>[+<button>...];...
    constexpr const char* kPress = "GALAXY_INPUT_AUTOPRESS_SCRIPT";
    if (const std::optional<std::string> text = env(kPress)) {
        for (const std::string& segment : split(*text, ';')) {
            if (segment.empty()) {
                continue;
            }
            const std::vector<std::string> fields = split(segment, ':');
            if (fields.size() != 3u || fields[2].empty()) {
                bad(std::string("invalid segment in ") + kPress + ": '" + segment + "'");
            }
            ButtonStep step{script_u64(fields[0], kPress), script_u64(fields[1], kPress), 0u, false, fields[0] + ':' + fields[2]};
            for (const std::string& token : split(fields[2], '+')) {
                if (token.empty()) {
                    bad(std::string("empty button in ") + kPress);
                }
                step.buttons |= button_bit(token, kPress, step.shake);
            }
            g_script.buttons.push_back(std::move(step));
        }
    }
    // GALAXY_INPUT_STICK_SCRIPT=<start>:<frames>:<x>:<y>;...
    constexpr const char* kStick = "GALAXY_INPUT_STICK_SCRIPT";
    if (const std::optional<std::string> text = env(kStick)) {
        for (const std::string& segment : split(*text, ';')) {
            if (segment.empty()) {
                continue;
            }
            const std::vector<std::string> fields = split(segment, ':');
            if (fields.size() != 4u) {
                bad(std::string("invalid segment in ") + kStick + ": '" + segment + "'");
            }
            g_script.sticks.push_back({script_u64(fields[0], kStick), script_u64(fields[1], kStick),
                                       script_float(fields[2], kStick), script_float(fields[3], kStick)});
        }
    }
    // GALAXY_INPUT_SHAKE_SCRIPT=<start>:<frames>;...
    constexpr const char* kShake = "GALAXY_INPUT_SHAKE_SCRIPT";
    if (const std::optional<std::string> text = env(kShake)) {
        for (const std::string& segment : split(*text, ';')) {
            if (segment.empty()) {
                continue;
            }
            const std::vector<std::string> fields = split(segment, ':');
            if (fields.size() != 2u) {
                bad(std::string("invalid segment in ") + kShake + ": '" + segment + "'");
            }
            g_script.shakes.emplace_back(script_u64(fields[0], kShake), script_u64(fields[1], kShake));
        }
    }
    // GALAXY_INPUT_POINTER_X/Y: the pointer held there; GALAXY_INPUT_POINTER2_X/Y from GALAXY_INPUT_POINTER2_FRAME, for
    // GALAXY_INPUT_POINTER2_DURATION frames when given.
    const auto pointer = [](const char* x_name, const char* y_name, const char* frame_name, const char* duration_name) {
        PointerAt at;
        const std::optional<std::string> x = env(x_name);
        const std::optional<std::string> y = env(y_name);
        if (x.has_value() != y.has_value()) {
            bad(std::string("only one of ") + x_name + " and " + y_name);
        }
        if (!x.has_value()) {
            return at;
        }
        at.given = true;
        at.x = script_float(*x, x_name);
        at.y = script_float(*y, y_name);
        if (frame_name != nullptr) {
            const std::optional<std::string> from = env(frame_name);
            if (!from.has_value()) {
                bad(std::string(x_name) + " without " + frame_name);
            }
            at.from = script_u64(*from, frame_name);
            if (const std::optional<std::string> duration = env(duration_name)) {
                at.duration = script_u64(*duration, duration_name);
            }
        }
        return at;
    };
    g_script.pointer = pointer("GALAXY_INPUT_POINTER_X", "GALAXY_INPUT_POINTER_Y", nullptr, nullptr);
    g_script.pointer2 = pointer("GALAXY_INPUT_POINTER2_X", "GALAXY_INPUT_POINTER2_Y", "GALAXY_INPUT_POINTER2_FRAME",
                                "GALAXY_INPUT_POINTER2_DURATION");
}

void controls(void*, const NebulaExtensionFrame* frame, NebulaExtensionControls* out) {
    std::uint64_t now = frame->game_frames;
    if (g_script.clock == ScriptClock::Scene) {
        now = g_scene_frames.load(std::memory_order_relaxed);
    } else if (g_script.clock == ScriptClock::Stop) {
        now = g_stop_frames.load(std::memory_order_relaxed);
        if (now == kNoStop) {
            return;  // nothing before the first stop
        }
    }
    for (const ButtonStep& step : g_script.buttons) {
        if (!in_window(now, step.start, step.duration)) {
            continue;
        }
        out->buttons |= step.buttons;
        out->shake = static_cast<std::uint8_t>(out->shake | (step.shake ? 1u : 0u));
        // A step's first sample is logged with its time, for input-to-present measurements.
        if (now == step.start && g_latency_logged != step.text) {
            g_latency_logged = step.text;
            log("[latency] buttons " + step.text.substr(step.text.find(':') + 1u) + " frame=" + std::to_string(now) +
                " qpc=" + std::to_string(clock_ticks()));
        }
    }
    for (const StickStep& step : g_script.sticks) {
        if (in_window(now, step.start, step.duration)) {
            out->has_stick = 1u;
            out->stick_x = step.x;
            out->stick_y = step.y;
        }
    }
    for (const auto& [start, duration] : g_script.shakes) {
        if (in_window(now, start, duration)) {
            out->shake = 1u;
        }
    }
    for (const PointerAt* at : {&g_script.pointer, &g_script.pointer2}) {
        if (at->given && now >= at->from && (at->duration == 0u || in_window(now, at->from, at->duration))) {
            out->has_pointer = 1u;
            out->pointer_x = at->x;
            out->pointer_y = at->y;
        }
    }
}

// --- Menus, window sizes, device losses, at game frames ------------------------------------------------------------

struct FrameKey {
    std::uint64_t at;
    std::uint32_t key;
};
struct Settings {
    std::optional<std::pair<std::uint32_t, std::uint64_t>> open;  // GALAXY_TEST_SETTINGS_OPEN=<page>[:<frame>]
    std::vector<FrameKey> keys;                                     // GALAXY_TEST_SETTINGS_KEYS=<frame>:<key>,...
    std::size_t next_key = 0u;
    // GALAXY_TEST_HOME_ROW=<row>:<frames>: once the HOME menu has been open that many frames, the row a player would
    // pick with the keys, and yes when it asks (reset, leave).
    std::optional<std::pair<std::uint32_t, std::uint64_t>> home_row;
    std::uint64_t home_frames = 0u;
    // GALAXY_TEST_DEVICE_LOST=<frame> (or GALAXY_TEST_GPU_LOST_FRAME), GALAXY_TEST_GPU_LOST_TIMES (1): the graphics
    // device lost at that frame and every 300 after.
    std::uint64_t lose_at = 0u, lose_times = 0u, lost = 0u;
} g_menus;

void read_menus() {
    if (const std::optional<std::string> open = env("GALAXY_TEST_SETTINGS_OPEN")) {
        const std::vector<std::string> fields = split(*open, ':');
        g_menus.open = std::make_pair(static_cast<std::uint32_t>(script_u64(fields[0], "GALAXY_TEST_SETTINGS_OPEN")),
                                      fields.size() > 1u ? script_u64(fields[1], "GALAXY_TEST_SETTINGS_OPEN") : 0u);
    }
    if (const std::optional<std::string> keys = env("GALAXY_TEST_SETTINGS_KEYS")) {
        static constexpr std::pair<std::string_view, std::uint32_t> kNames[] = {
            {"up", NEBULA_EXT_KEY_UP},        {"down", NEBULA_EXT_KEY_DOWN},         {"left", NEBULA_EXT_KEY_LEFT},
            {"right", NEBULA_EXT_KEY_RIGHT},  {"enter", NEBULA_EXT_KEY_ENTER},       {"esc", NEBULA_EXT_KEY_ESCAPE},
            {"tab", NEBULA_EXT_KEY_TAB},      {"backspace", NEBULA_EXT_KEY_BACKSPACE}, {"space", NEBULA_EXT_KEY_SPACE},
            {"f1", NEBULA_EXT_KEY_MENU}};
        for (const std::string& item : split(*keys, ',')) {
            const std::size_t colon = item.find(':');
            if (colon == std::string::npos) {
                bad("invalid GALAXY_TEST_SETTINGS_KEYS item '" + item + "'");
            }
            const std::string name = item.substr(colon + 1u);
            const auto known = std::find_if(std::begin(kNames), std::end(kNames), [&](const auto& k) { return k.first == name; });
            if (known == std::end(kNames)) {
                bad("unknown key '" + name + "' in GALAXY_TEST_SETTINGS_KEYS");
            }
            g_menus.keys.push_back({script_u64(item.substr(0, colon), "GALAXY_TEST_SETTINGS_KEYS"), known->second});
        }
    }
    if (const std::optional<std::string> row = env("GALAXY_TEST_HOME_ROW")) {
        const std::vector<std::string> fields = split(*row, ':');
        if (fields.size() != 2u) {
            bad("invalid GALAXY_TEST_HOME_ROW '" + *row + "'");
        }
        g_menus.home_row = std::make_pair(static_cast<std::uint32_t>(script_u64(fields[0], "GALAXY_TEST_HOME_ROW")),
                                          script_u64(fields[1], "GALAXY_TEST_HOME_ROW"));
    }
    g_menus.lose_at = env_u64("GALAXY_TEST_DEVICE_LOST", env_u64("GALAXY_TEST_GPU_LOST_FRAME", 0u));
    g_menus.lose_times = env_u64("GALAXY_TEST_GPU_LOST_TIMES", 1u);
}

void frame(void*, const NebulaExtensionFrame* frame) {
    const std::uint64_t now = frame->game_frames;
    g_game_frames.store(now, std::memory_order_relaxed);
    if (g_menus.open.has_value() && now >= g_menus.open->second && HOST_HAS(open_settings)) {
        g_host->open_settings(g_host->host, g_menus.open->first);
        g_menus.open.reset();
    }
    while (g_menus.next_key < g_menus.keys.size() && now >= g_menus.keys[g_menus.next_key].at && HOST_HAS(menu_key)) {
        g_host->menu_key(g_host->host, g_menus.keys[g_menus.next_key++].key);
    }
    if (g_menus.home_row.has_value() && HOST_HAS(menu_open) && HOST_HAS(menu_key) &&
        g_host->menu_open(g_host->host) == NEBULA_EXT_MENU_HOME && ++g_menus.home_frames == g_menus.home_row->second) {
        for (std::uint32_t i = 0; i < g_menus.home_row->first; ++i) {
            g_host->menu_key(g_host->host, NEBULA_EXT_KEY_DOWN);
        }
        g_host->menu_key(g_host->host, NEBULA_EXT_KEY_ENTER);
        if (g_menus.home_row->first != 0u) {
            g_host->menu_key(g_host->host, NEBULA_EXT_KEY_RIGHT);
            g_host->menu_key(g_host->host, NEBULA_EXT_KEY_ENTER);
        }
    }
    if (g_menus.lose_at != 0u && g_menus.lost < g_menus.lose_times && now >= g_menus.lose_at + g_menus.lost * 300u &&
        HOST_HAS(lose_gpu_device)) {
        ++g_menus.lost;
        g_host->lose_gpu_device(g_host->host);
    }
}

// --- Pictures ------------------------------------------------------------------------------------------------------

struct Pictures {
    std::string folder;               // GALAXY_PRESENT_SNAPSHOT_DIR
    std::uint64_t every = 0u;         // GALAXY_PRESENT_SNAPSHOT_EVERY: every Nth present
    std::uint64_t first = 0u;         // GALAXY_PRESENT_SNAPSHOT_FIRST..LAST: the presents kept
    std::uint64_t last = std::numeric_limits<std::uint64_t>::max();
    bool game_only = false;           // GALAXY_PRESENT_SNAPSHOT_GAME_ONLY: a generated present neither kept nor counted
    bool by_stop = false;             // GALAXY_PRESENT_SNAPSHOT_BY_STOP: numbered by the tour's stop and scene frame
    struct Size {
        std::uint64_t at;
        std::uint32_t width, height;
    };
    std::vector<Size> sizes;          // GALAXY_TEST_WINDOW_SIZES=<present>:<width>x<height>,...
    std::size_t next_size = 0u;
    std::uint64_t presents = 0u;      // the render thread's
    std::uint64_t numbered_frame = 0u;
    bool held = false;
    std::uint64_t left_out = 0u;
    std::string path;
} g_pictures;

// By stop: the test plugin names each scene frame's number (a stop's thousand plus its scene frame) or a hold (0, a
// load after a stop's change) for the game frame that makes it; the render thread finds the one for the newest list it
// drew. A ring the game thread writes and the render thread reads.
struct Numbered {
    std::atomic<std::uint64_t> game_frame{~std::uint64_t{0}};
    std::atomic<std::uint64_t> number{0};
};
Numbered g_numbers[256];

void read_pictures() {
    if (const std::optional<std::string> folder = env("GALAXY_PRESENT_SNAPSHOT_DIR")) {
        g_pictures.folder = *folder;
    }
    g_pictures.every = env_u64("GALAXY_PRESENT_SNAPSHOT_EVERY", 0u);
    g_pictures.first = env_u64("GALAXY_PRESENT_SNAPSHOT_FIRST", 0u);
    g_pictures.last = env_u64("GALAXY_PRESENT_SNAPSHOT_LAST", std::numeric_limits<std::uint64_t>::max());
    g_pictures.game_only = env_flag("GALAXY_PRESENT_SNAPSHOT_GAME_ONLY");
    g_pictures.by_stop = env("GALAXY_PRESENT_SNAPSHOT_BY_STOP").has_value();
    if (const std::optional<std::string> sizes = env("GALAXY_TEST_WINDOW_SIZES")) {
        for (const std::string& item : split(*sizes, ',')) {
            const std::string_view text = item;
            const std::size_t colon = text.find(':');
            const std::size_t by = colon == std::string_view::npos ? colon : text.find('x', colon);
            std::uint64_t at = 0u, width = 0u, height = 0u;
            if (by == std::string_view::npos || !parse_u64(text.substr(0, colon), at) ||
                !parse_u64(text.substr(colon + 1u, by - colon - 1u), width) || !parse_u64(text.substr(by + 1u), height) ||
                width == 0u || height == 0u || width > 65535u || height > 65535u) {
                bad("invalid GALAXY_TEST_WINDOW_SIZES item '" + item + "'");
            }
            g_pictures.sizes.push_back({at, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});
        }
    }
}

const char* picture_path(void*, std::uint64_t game_frame, std::uint32_t kind) {
    Pictures& p = g_pictures;
    if (p.game_only && kind == NEBULA_EXT_PICTURE_GENERATED) {
        if (++p.left_out % 300u == 0u) {
            log("[snapshot] " + std::to_string(p.left_out) + " generated presents left out");
        }
        return nullptr;
    }
    ++p.presents;
    while (p.next_size < p.sizes.size() && p.presents >= p.sizes[p.next_size].at && HOST_HAS(resize_window)) {
        g_host->resize_window(g_host->host, p.sizes[p.next_size].width, p.sizes[p.next_size].height);
        ++p.next_size;
    }
    if (p.by_stop && game_frame != p.numbered_frame) {
        const Numbered& entry = g_numbers[game_frame % std::size(g_numbers)];
        if (entry.game_frame.load(std::memory_order_acquire) == game_frame) {
            p.numbered_frame = game_frame;
            const std::uint64_t number = entry.number.load(std::memory_order_relaxed);
            p.held = number == 0u;
            if (number != 0u) {
                p.presents = number;
            }
        }
    }
    if (p.every == 0u || p.folder.empty() || p.presents < p.first || p.presents > p.last || p.presents % p.every != 0u ||
        (p.by_stop && (p.held || p.presents % 1000u == 0u))) {
        return nullptr;
    }
    p.path = p.folder + "/present_" + std::to_string(p.presents) + ".ppm";
    return p.path.c_str();
}

// --- Sound ---------------------------------------------------------------------------------------------------------

// GALAXY_AUDIO_WAV_DUMP=<file>: the mix as a 16-bit stereo WAV file, 15 s or GALAXY_AUDIO_WAV_DUMP_SECONDS (up to an
// hour). A rate change starts a new file beside it (<name>.rate-<hz>.part-<n>.wav).
class WavDump {
public:
    void start(std::filesystem::path base, std::uint32_t seconds) {
        base_ = std::move(base);
        seconds_ = seconds;
    }
    [[nodiscard]] bool on() const { return !base_.empty() && !failed_; }

    void add(const float* samples, std::uint32_t frames, std::uint32_t channels, std::uint32_t rate) {
        if (!on() || channels != 2u) {
            return;
        }
        if (out_.is_open() && rate != rate_) {
            finish();
            ++part_;
        }
        if (!out_.is_open() && !open(rate)) {
            return;
        }
        const std::uint64_t room = std::uint64_t{rate_} * 4u * seconds_ - bytes_;
        const std::uint64_t take = std::min<std::uint64_t>(std::uint64_t{frames} * 4u, room) / 4u;
        block_.resize(take * 4u);
        for (std::uint64_t i = 0; i < take * 2u; ++i) {
            const float value = std::clamp(samples[i] * 32768.0f, -32768.0f, 32767.0f);
            const auto bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lrint(value)));
            block_[i * 2u] = static_cast<char>(bits & 0xFFu);
            block_[i * 2u + 1u] = static_cast<char>(bits >> 8u);
        }
        out_.write(block_.data(), static_cast<std::streamsize>(block_.size()));
        bytes_ += static_cast<std::uint32_t>(block_.size());
        if (!out_) {
            log("[audio] WAV dump write failed: " + path_.string());
            failed_ = true;
        }
    }

    void finish() {
        if (!out_.is_open()) {
            return;
        }
        out_.seekp(4, std::ios::beg);
        u32(36u + bytes_);
        out_.seekp(40, std::ios::beg);
        u32(bytes_);
        out_.close();
        log("[audio] WAV dump wrote " + std::to_string(bytes_) + " bytes at " + std::to_string(rate_) + " Hz: " + path_.string());
        bytes_ = 0u;
    }

private:
    bool open(std::uint32_t rate) {
        path_ = base_;
        if (part_ != 0u) {
            path_ = base_.parent_path() / (base_.stem().string() + ".rate-" + std::to_string(rate) + ".part-" +
                                           std::to_string(part_) + base_.extension().string());
        }
        std::error_code error;
        if (!path_.parent_path().empty()) {
            std::filesystem::create_directories(path_.parent_path(), error);
        }
        out_.open(path_, std::ios::binary | std::ios::trunc);
        if (!out_) {
            log("[audio] WAV dump open failed: " + path_.string());
            failed_ = true;
            return false;
        }
        rate_ = rate;
        out_.write("RIFF", 4);
        u32(36u);
        out_.write("WAVEfmt ", 8);
        u32(16u);
        u16(1u);
        u16(2u);
        u32(rate);
        u32(rate * 4u);
        u16(4u);
        u16(16u);
        out_.write("data", 4);
        u32(0u);
        // The first sample's time, to line a recording's sound up with its pictures ("[snapshot] ... qpc=").
        log("[audio] WAV dump enabled: " + path_.string() + " qpc=" + std::to_string(clock_ticks()));
        return true;
    }
    void u16(std::uint16_t value) {
        const char bytes[2] = {static_cast<char>(value & 0xFFu), static_cast<char>(value >> 8u)};
        out_.write(bytes, 2);
    }
    void u32(std::uint32_t value) {
        const char bytes[4] = {static_cast<char>(value & 0xFFu), static_cast<char>((value >> 8u) & 0xFFu),
                               static_cast<char>((value >> 16u) & 0xFFu), static_cast<char>(value >> 24u)};
        out_.write(bytes, 4);
    }

    std::filesystem::path base_, path_;
    std::ofstream out_;
    std::vector<char> block_;
    std::uint32_t seconds_ = 15u, rate_ = 0u, bytes_ = 0u, part_ = 0u;
    bool failed_ = false;
} g_wav;

void sound(void*, const float* samples, std::uint32_t frames, std::uint32_t channels, std::uint32_t rate) {
    g_wav.add(samples, frames, channels, rate);
}

bool g_mods_changed = false;
std::uint32_t mods_changed(void*) {
    return g_mods_changed ? 1u : 0u;
}

NebulaHostExtension g_extension{};

}  // namespace

// The test plugin in the game (the same library, loaded again by the mods): each scene frame and stop, and the picture
// number for the game frame it is in (0: none until the next).
NEBULA_EXTENSION_EXPORT void nebula_test_scene_frame(std::uint64_t scene_frames, std::uint64_t stop_frames,
                                                     std::uint64_t picture_number) {
    g_scene_frames.store(scene_frames, std::memory_order_relaxed);
    g_stop_frames.store(stop_frames, std::memory_order_relaxed);
    if (g_pictures.by_stop) {
        const std::uint64_t game_frame = g_game_frames.load(std::memory_order_relaxed);
        Numbered& entry = g_numbers[game_frame % std::size(g_numbers)];
        entry.number.store(picture_number, std::memory_order_relaxed);
        entry.game_frame.store(game_frame, std::memory_order_release);
    }
}

// The tour's end: the run ends as closing the window ends it.
NEBULA_EXTENSION_EXPORT void nebula_test_done() {
    g_wav.finish();
    if (HOST_HAS(quit)) {
        g_host->quit(g_host->host);
    }
}

NEBULA_EXTENSION_EXPORT const NebulaHostExtension* nebula_extension_attach(const NebulaHostForExtension* host) {
    if (host == nullptr || host->version != NEBULA_HOST_EXTENSION_VERSION) {
        return nullptr;
    }
    g_host = host;
#if defined(_WIN32)
    // Before anything is read or written: a test run keeps to its work folder, or the host stops.
    if (const char* const refusal = guard()) {
        log(std::string("test extension: ") + refusal);
        return nullptr;
    }
#endif
    read_script();
    read_menus();
    read_pictures();
    if (const std::optional<std::string> wav = env("GALAXY_AUDIO_WAV_DUMP")) {
        g_wav.start(*wav, static_cast<std::uint32_t>(std::clamp<std::uint64_t>(env_u64("GALAXY_AUDIO_WAV_DUMP_SECONDS", 15u), 1u, 3600u)));
    }
    g_mods_changed = env("GALAXY_TEST_MODS_CHANGED").has_value();
    NebulaHostExtension& e = g_extension;
    e.version = NEBULA_HOST_EXTENSION_VERSION;
    e.size = sizeof(e);
    e.flags = (env_flag("GALAXY_TEST_DETERMINISTIC") ? NEBULA_EXT_FIXED_STEPS : 0u) |
              (env_flag("GALAXY_AUDIO_WAV_DUMP_ONLY") ? NEBULA_EXT_NO_SOUND_DEVICE : 0u) |
              (env("GALAXY_TEST_MODS_SCREEN").has_value() ? NEBULA_EXT_MODS_SCREEN : 0u);
    e.controls = controls;
    e.frame = frame;
    e.picture_path = picture_path;
    e.sound = g_wav.on() ? sound : nullptr;
    e.mods_changed = mods_changed;
    e.vram_budget_mb = static_cast<std::uint32_t>(env_u64("GALAXY_TEST_VRAM_BUDGET_MB", 0u));
    e.disc_read_delay_us = static_cast<std::uint32_t>(env_u64("GALAXY_TEST_DISC_DELAY_US", 0u));
    log("test extension attached");
    return &e;
}
