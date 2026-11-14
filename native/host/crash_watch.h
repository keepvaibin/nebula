// SPDX-License-Identifier: GPL-3.0-only
// The crash watcher (Windows): a second, idle instance of the executable, started with the game, that writes the
// game's minidump from outside when the game crashes.
//
// Why outside: MiniDumpWriteDump called in the crashing process suspends every other thread and then takes locks of
// its own process (the heap, the loader); a thread suspended while holding one of them stops it for ever, and with it
// the crashed thread it suspended too, so the process neither writes its dump nor ends (an empty .dmp and a game left
// running, seen under load: 1633, 1130, and the driver faults of p0432 to p0451). From another process it reads the
// game's memory and takes no lock of the game's.
//
// The game's side needs no heap at the crash: the folder is kept from the start, the dump's path and the request go
// into memory shared with the watcher, and two events carry the request and the answer.
#pragma once

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstddef>
#include <filesystem>

namespace galaxy::crash_watch {

// The watcher's command line: argv[1] is kSwitch, argv[2] the game's process id, argv[3] the shared objects' name.
inline constexpr const char* kSwitch = "--crash-watch";

// The watcher's whole run: waits for the game's request or its end, writes the dump asked for, ends when the game
// does. A game that ended with a fault's status (0xC0000409 of a fast fail, which no handler in the process sees, or
// any other the game's handlers did not end it with) gets crash lines in the game's log from here. Returns the
// process's exit code.
int run(int argc, char** argv);

// The game's side, at its start: starts the watcher (dumps go to `folder`, made when the first is written; its lines
// go to this process's standard output, timed from `log_zero` as the game's own). A watcher that cannot start leaves
// write_dump answering kNoWatcher.
void start(const std::filesystem::path& folder, std::chrono::steady_clock::time_point log_zero);

enum class Result { kNoWatcher, kWritten, kFailed };

// The game's side, at a crash (any thread but the crashed one): the watcher writes the dump of this process with
// `thread`'s exception `pointers` (null: no exception) of MINIDUMP_TYPE `type` and names it in `path` (`size`
// characters with its NUL). kNoWatcher: none runs (the caller writes the dump itself); kFailed: *error says why
// (a Windows error code), and `path` is still the file it tried. *snapshot: 0 when the dump read a snapshot of the
// process (the game's threads ran on unseen), else why it read the live process (ERROR_NOT_READY: no dump was tried).
// Waits for the watcher at most `wait_ms`. `kind` begins the file's name: nebula-native for a crash, nebula-hang for a
// game that stopped making frames (a test run's stall).
Result write_dump(DWORD thread, EXCEPTION_POINTERS* pointers, unsigned type, wchar_t* path, std::size_t size, DWORD* error,
                  DWORD* snapshot, DWORD wait_ms, const wchar_t* kind = L"nebula-native") noexcept;

}  // namespace galaxy::crash_watch
#endif
