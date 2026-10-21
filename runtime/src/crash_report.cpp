// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/crash_report.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace galaxy::crash {
namespace {

thread_local Activity t_activity = Activity::None;
thread_local char t_activity_file[512];

// The last mod file read: written by the disc thread, read by the crash handler (a sequence count: odd while written).
std::atomic<std::uint32_t> s_mod_read_sequence{0u};
std::atomic<std::int64_t> s_mod_read_ms{-1};
char s_mod_read[640];

char s_session_mods[2048];
std::atomic<bool> s_session_change_game{false};

std::int64_t now_ms() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void copy(char* out, std::size_t size, std::string_view text) noexcept {
    if (size == 0u) return;
    const std::size_t n = std::min(size - 1u, text.size());
    std::memcpy(out, text.data(), n);
    out[n] = '\0';
}

// snprintf into a field, cut where it ends.
void put(char* out, std::size_t size, const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    std::vsnprintf(out, size, format, args);
    va_end(args);
}

bool has(std::string_view text, std::string_view part) noexcept { return text.find(part) != std::string_view::npos; }

bool gpu_lost(std::string_view text) noexcept {
    // DXGI_ERROR_DEVICE_REMOVED, _HUNG, _RESET, _DRIVER_INTERNAL_ERROR; Vulkan's device loss.
    return has(text, "887A0005") || has(text, "887A0006") || has(text, "887A0007") || has(text, "887A0020") ||
           has(text, "DEVICE REMOVED") || has(text, "DEVICE_REMOVED") || has(text, "DEVICE_HUNG") ||
           has(text, "DEVICE_LOST") || has(text, "device lost");
}

bool renderer_error(std::string_view text) noexcept {
    return text.starts_with("renderer:") || has(text, "[Renderer") || has(text, "Renderer]") || has(text, "[GX]");
}

bool out_of_memory(std::string_view text) noexcept {
    return has(text, "bad_alloc") || has(text, "out of memory") || has(text, "E_OUTOFMEMORY") ||
           has(text, "8007000E") || has(text, "OUT_OF_MEMORY");
}

void set(Report& r, const char* code, const char* title, const char* advice) noexcept {
    copy(r.code, sizeof(r.code), code);
    copy(r.title, sizeof(r.title), title);
    copy(r.advice, sizeof(r.advice), advice);
}

constexpr const char* kReportIt =
    "Start the game again. If it happens again, report it to the Nebula team with the report file.";

}  // namespace

ScopedActivity::ScopedActivity(Activity activity, const std::filesystem::path& file) noexcept : previous_(t_activity) {
    try {
        const std::u8string name = file.u8string();
        copy(t_activity_file, sizeof(t_activity_file),
             std::string_view(reinterpret_cast<const char*>(name.data()), name.size()));
    } catch (...) {
        t_activity_file[0] = '\0';
    }
    t_activity = activity;
}

ScopedActivity::~ScopedActivity() {
    t_activity = previous_;
    if (previous_ == Activity::None) t_activity_file[0] = '\0';
}

Activity current_activity(char* file, std::size_t size) noexcept {
    copy(file, size, t_activity == Activity::None ? std::string_view() : std::string_view(t_activity_file));
    return t_activity;
}

void note_mod_read(std::string_view mod, std::string_view disc_path) noexcept {
    s_mod_read_sequence.fetch_add(1u, std::memory_order_acq_rel);
    char line[sizeof(s_mod_read)];
    std::snprintf(line, sizeof(line), "%.*s: %.*s", static_cast<int>(std::min<std::size_t>(mod.size(), 200u)),
                  mod.data(), static_cast<int>(std::min<std::size_t>(disc_path.size(), 400u)), disc_path.data());
    std::memcpy(s_mod_read, line, sizeof(line));
    s_mod_read_ms.store(now_ms(), std::memory_order_relaxed);
    s_mod_read_sequence.fetch_add(1u, std::memory_order_acq_rel);
}

bool last_mod_read(char* out, std::size_t size, double* seconds_ago) noexcept {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const std::uint32_t before = s_mod_read_sequence.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;
        const std::int64_t at = s_mod_read_ms.load(std::memory_order_relaxed);
        if (at < 0) return false;
        copy(out, size, std::string_view(s_mod_read, strnlen(s_mod_read, sizeof(s_mod_read))));
        if (s_mod_read_sequence.load(std::memory_order_acquire) != before) continue;
        if (seconds_ago != nullptr) *seconds_ago = static_cast<double>(now_ms() - at) / 1000.0;
        return true;
    }
    return false;
}

void set_session_mods(std::string_view lines, bool change_game) noexcept {
    copy(s_session_mods, sizeof(s_session_mods), lines);
    s_session_change_game.store(change_game, std::memory_order_release);
}

std::string_view session_mods() noexcept { return std::string_view(s_session_mods, strnlen(s_session_mods, sizeof(s_session_mods))); }

bool session_mods_change_game() noexcept { return s_session_change_game.load(std::memory_order_acquire); }

Report classify(const Facts& facts) noexcept {
    Report r;
    const std::string_view text = facts.text;
    const int text_len = static_cast<int>(std::min<std::size_t>(text.size(), 400u));
    const bool running = facts.kind != Facts::Kind::Startup || text.starts_with("renderer:");

    // A thread loading a texture pack image or decoding an HD movie: that file.
    if (running && facts.activity == Activity::TexturePack) {
        set(r, "NEB-T01", "A texture pack image stopped Nebula",
            "Remove this image from its texture pack or save it again as a PNG, then start the game again.");
        put(r.detail, sizeof(r.detail), "Nebula stopped while it read %.*s.", static_cast<int>(facts.activity_file.size()),
            facts.activity_file.data());
        return r;
    }
    if (running && facts.activity == Activity::Movie) {
        set(r, "NEB-V01", "A replacement movie stopped Nebula",
            "Remove this video, or convert it to an H.264 MP4, then start the game again.");
        put(r.detail, sizeof(r.detail), "Nebula stopped while it played %.*s.", static_cast<int>(facts.activity_file.size()),
            facts.activity_file.data());
        return r;
    }
    if (running && gpu_lost(text)) {
        set(r, "NEB-G01", "The graphics card stopped responding",
            "Update the graphics driver. If it happens again, turn off ray tracing and frame generation or lower the "
            "internal resolution.");
        put(r.detail, sizeof(r.detail), "The graphics driver reset the graphics card while the game was running.");
        return r;
    }
    if (out_of_memory(text)) {
        set(r, "NEB-R01", "Nebula ran out of memory",
            "Close other programs, set texture packs to ON instead of PRELOAD, or lower the internal resolution.");
        put(r.detail, sizeof(r.detail), "Nebula asked for memory the computer could not give.");
        return r;
    }
    if (running && (facts.kind == Facts::Kind::Exception || facts.kind == Facts::Kind::Startup) && renderer_error(text)) {
        set(r, "NEB-G02", "The graphics renderer stopped on an error",
            "Update the graphics driver and start the game again. If it happens again, report it with the report "
            "file.");
        put(r.detail, sizeof(r.detail), "The renderer reported: %.*s", text_len, text.data());
        return r;
    }

    // The game's own code: the mods that change it come first.
    const bool game = (facts.kind == Facts::Kind::Fault && facts.in_game) || facts.kind == Facts::Kind::GameFatal;
    if (game) {
        const bool fatal = facts.kind == Facts::Kind::GameFatal;
        const char* what = fatal ? "stopped with an error" : facts.stack_overflow ? "ran out of stack space" : "crashed";
        // The game's own words, where it said any (a fault's location is in the technical details).
        char said[480] = "";
        if (fatal && !text.empty()) put(said, sizeof(said), " The game reported: %.*s", text_len, text.data());
        if (!facts.last_mod_read.empty() && facts.last_mod_seconds >= 0.0 && facts.last_mod_seconds <= kRecentModRead) {
            set(r, "NEB-M02", "A mod's file crashed the game",
                "Turn this mod off in Settings > Mods or in the launcher's Mods tab, or check that it is made for this "
                "game's region and version.");
            put(r.detail, sizeof(r.detail), "The game %s %.0f s after it read a file from a mod (%.*s).%s", what,
                facts.last_mod_seconds, static_cast<int>(facts.last_mod_read.size()), facts.last_mod_read.data(), said);
            return r;
        }
        if (facts.mods_change_game) {
            set(r, "NEB-M01", "The game crashed with mods that change it",
                "Turn the mods off in Settings > Mods or in the launcher's Mods tab and play again. If it no longer "
                "happens, turn them back on one at a time to find the one.");
            put(r.detail, sizeof(r.detail), "The game %s while mods changed its files or code.%s", what, said);
            return r;
        }
        if (fatal) {
            set(r, "NEB-C02", "The game stopped with an error", kReportIt);
        } else if (facts.stack_overflow) {
            set(r, "NEB-C03", "The game ran out of stack space", kReportIt);
        } else {
            set(r, "NEB-C01", "The game crashed", kReportIt);
        }
        put(r.detail, sizeof(r.detail), "The game %s.%s", what, said);
        return r;
    }

    switch (facts.kind) {
    case Facts::Kind::Fault:
        set(r, "NEB-N01", "Nebula crashed", kReportIt);
        put(r.detail, sizeof(r.detail), "Nebula's own code failed, not the game's.");
        return r;
    case Facts::Kind::Exception:
        set(r, "NEB-N02", "Nebula stopped on an unexpected error", kReportIt);
        break;
    case Facts::Kind::Abort:
        set(r, "NEB-N03", "Nebula stopped itself", kReportIt);
        break;
    case Facts::Kind::Startup:
        if (text.starts_with("cannot load the game module") || text.starts_with("the game module")) {
            set(r, "NEB-S03", "Nebula's files do not match",
                "Install Nebula again, or update it, so that all of its files come from one version.");
        } else if (text.starts_with("cannot open") || text.starts_with("cannot map")) {
            set(r, "NEB-S01", "The game content was not found",
                "Run Nebula Setup again, or choose the game content in Nebula settings > Advanced.");
        } else if (has(text, "too short for a game partition") || has(text, "points past the partition") ||
                   has(text, "runs past the partition")) {
            set(r, "NEB-S02", "The game content is damaged", "Run Nebula Setup again to rebuild it from your disc image.");
        } else if (text.starts_with("the renderer did not start")) {
            set(r, "NEB-S04", "The graphics card could not start the game",
                "Update the graphics driver. On a computer with two graphics cards, choose the other one in Nebula "
                "settings > Display.");
        } else if (text.starts_with("the settings did not load")) {
            set(r, "NEB-S05", "Nebula's settings could not be read",
                "Rename config.ini in Nebula's settings folder to start with the default settings.");
        } else if (text.starts_with("usage:") || text.starts_with("unknown option")) {
            set(r, "NEB-S06", "Nebula was started the wrong way", "Start Nebula from its launcher or shortcut.");
        } else {
            set(r, "NEB-N04", "Nebula stopped on an internal error", kReportIt);
        }
        break;
    case Facts::Kind::GameFatal:
        break;
    }
    put(r.detail, sizeof(r.detail), "%.*s", text_len, text.data());
    return r;
}

std::size_t format_report(const Report& report, std::string_view technical, char* out, std::size_t size) noexcept {
    if (size == 0u) return 0u;
    const std::string_view mods = session_mods();
    const int n = std::snprintf(out, size, "%s: %s\n\n%s\n\n%s\n%s%.*s\nTechnical details:\n%.*s\n", report.code,
                                report.title, report.detail, report.advice, mods.empty() ? "" : "\nMods this session:\n",
                                static_cast<int>(mods.size()), mods.data(), static_cast<int>(technical.size()),
                                technical.data());
    if (n < 0) {
        out[0] = '\0';
        return 0u;
    }
    return std::min(static_cast<std::size_t>(n), size - 1u);
}

}  // namespace galaxy::crash
