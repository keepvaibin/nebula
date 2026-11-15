// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Windows HDR for the display the game is on: what the display takes, and Windows HDR turned on while the game shows an
// HDR picture on a display that had it off, then off again.
//
// The game turns HDR on only for the display its window is on, only while HDR is on in its settings and the picture can
// be HDR (frame generation keeps it SDR). It turns off only what it turned on: when HDR goes off in the settings, when
// the window moves to another display (one second after the change, so a passing change does not switch the display
// twice), and when the game ends. A display whose HDR is turned off outside the game while the game wants it on keeps
// that choice until the game's wish changes. Windows 11 24H2 has its own HDR state apart from wide colour on SDR
// displays (DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE); older Windows has the advanced-colour state, which is HDR on an HDR
// display (DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE).
//
// The displays turned on are listed in a file in the data folder (list_path). The crash watcher (native/host/
// crash_watch.h), a second process that outlives the game, turns them off when the game ends any other way (a fault, a
// fast fail, the process ended from outside). A start that finds a list takes it as its own (adopt): a restart hands its
// displays over so the display does not switch off and on again, and an end the watcher missed is put right.

#include <string>
#include <vector>

#if defined(_WIN32)
#include "galaxy/windows_api.h"

#include <filesystem>
#endif

namespace galaxy::render::os_hdr {

// What the game does with what it sees, apart from the system calls (tested on every platform).
namespace plan {

struct Target {
    std::string path;  // the monitor's device path: the same display across starts and restarts
    bool supported = false;
    bool on = false;
    bool blocked = false;  // a system policy keeps HDR off (battery saver, a remote session)
};

struct Memory {
    std::vector<std::string> owned;     // turned on by the game: to turn off again
    std::vector<std::string> declined;  // turned off outside the game while wished on: left off until the wish changes
    std::vector<std::string> seen_on;   // wished displays last seen with HDR on
};

struct Steps {
    std::vector<std::string> off;  // owned and no longer wished: turn off (only when the change is due)
    std::vector<std::string> on;   // wished, off, not declined: turn on
    bool refused = false;          // wished on and kept off (declined or blocked)
};

// `now`: the active displays. `wished`: the window display's targets (a cloned display has more than one). `off_due`:
// the wish has held long enough to turn displays off. Updates `memory` for what it saw (a display turned off outside
// the game); the caller records the switches it makes (owned, seen_on).
[[nodiscard]] Steps next(const std::vector<Target>& now, const std::vector<std::string>& wished, bool on, bool off_due,
                         Memory& memory);

// The list file: the process that owns the displays ("process <id>"), then one device path a line.
struct List {
    unsigned long process = 0;
    std::vector<std::string> paths;
};
[[nodiscard]] std::string list_text(const List& list);
[[nodiscard]] List read_list(const std::string& text);

}  // namespace plan

#if defined(_WIN32)
struct Display {
    bool known = false;      // Windows describes the display (DisplayConfig)
    bool supported = false;  // it takes HDR
    bool on = false;         // Windows has HDR on for it
    bool blocked = false;    // a system policy keeps HDR off
};
[[nodiscard]] Display read(HMONITOR monitor);

// The game's wish for `monitor` (the window's display): HDR on or not. Returns at once; the switch runs on a thread of
// its own, and the display's change reaches the renderers as WM_DISPLAYCHANGE.
void want(HMONITOR monitor, bool on);
// A display's state may have changed outside the game (the renderers read the display again): checked on the thread.
void display_changed();
// The game wants HDR on and Windows keeps it off (it refused, a policy blocks it, or it was turned off outside the game).
[[nodiscard]] bool refused() noexcept;

// At the start: a list another run left is taken as this run's (displays not wished within ten seconds are turned off).
void adopt();
// At the end: with `hand_over` (a restart) the displays stay on and the list goes to the next start; else they are
// turned off and the list removed.
void finish(bool hand_over);

// The list's file (empty without a data folder), and the watcher's restore of what a list names once the game (process
// `game`) has ended: only a list that process owns (a restart's next process may own it already).
[[nodiscard]] std::filesystem::path list_path();
void restore_listed(const std::filesystem::path& list, unsigned long game);
#endif

}  // namespace galaxy::render::os_hdr
