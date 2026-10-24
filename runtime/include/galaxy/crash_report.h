// SPDX-License-Identifier: GPL-3.0-only
// Turns what the crash handler knows (where, which thread was doing what, this session's mods, the last mod file the
// game read) into a code the player can look up (docs/ERROR_CODES.md), a sentence and what to do. classify() and
// format_report() take no locks and allocate nothing: they run in a process that just crashed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace galaxy::crash {

// What a thread works on that a crash there should name.
enum class Activity : std::uint8_t { None, TexturePack, Movie };

// For the scope's life the calling thread works on `file` (a texture pack image, an HD movie).
class ScopedActivity {
public:
    ScopedActivity(Activity activity, const std::filesystem::path& file) noexcept;
    ~ScopedActivity();
    ScopedActivity(const ScopedActivity&) = delete;
    ScopedActivity& operator=(const ScopedActivity&) = delete;

private:
    Activity previous_;
};

// The calling thread's activity, its file copied (UTF-8) into `file`. For the crash handler on the failing thread.
Activity current_activity(char* file, std::size_t size) noexcept;

// The disc reader: the game read `disc_path` from mod `mod`. Kept with the time; one thread calls it.
void note_mod_read(std::string_view mod, std::string_view disc_path) noexcept;
// The last such read as "<mod>: <disc path>" in `out` and seconds since it; false when there was none.
bool last_mod_read(char* out, std::size_t size, double* seconds_ago) noexcept;

// The mods this session started with, one line each ("name (game files, code)"); `change_game`: at least one
// changes the game's files or code (not only textures or movies).
void set_session_mods(std::string_view lines, bool change_game) noexcept;
std::string_view session_mods() noexcept;
bool session_mods_change_game() noexcept;

struct Facts {
    enum class Kind : std::uint8_t {
        Fault,      // an access violation or other hardware exception
        GameFatal,  // the game's own fatal stop (OSPanic, a failed assertion)
        Exception,  // an uncaught C++ exception
        Abort,      // abort()
        Startup,    // Nebula could not start or stopped itself (die())
    };
    Kind kind = Kind::Fault;
    bool in_game = false;         // Fault: in the game's code (not Nebula's)
    bool stack_overflow = false;  // Fault
    std::string_view text;        // the fatal text, the exception's what(), the die() message, the fault's location
    Activity activity = Activity::None;
    std::string_view activity_file;
    std::string_view last_mod_read;  // "<mod>: <disc path>"
    double last_mod_seconds = -1.0;  // since that read; below 0: none
    bool mods_change_game = false;
};

struct Report {
    char code[16]{};
    char title[96]{};
    char detail[640]{};
    char advice[320]{};
};

// The code, title, detail and advice for these facts.
Report classify(const Facts& facts) noexcept;

// The report as the text file and dialog show it: "NEB-xxx: title", the detail, the advice, then `technical`
// (the exception line, the dump's path) and the session's mods. Returns the length written (without the terminator).
std::size_t format_report(const Report& report, std::string_view technical, char* out, std::size_t size) noexcept;

// A mod read within this many seconds before the game failed is named as the likely cause.
inline constexpr double kRecentModRead = 15.0;

}  // namespace galaxy::crash
