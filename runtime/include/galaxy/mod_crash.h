// SPDX-License-Identifier: GPL-3.0-only
// The crash record (when-a-mod-fails.md, "The next start after a crash"): what a crash with mods on leaves for the next start, in the
// mods folder's .crash. The next start reads it (mods.h take_safe_start): the mod it names is off for that start and
// the Mods page asks what to do with it; with no mod named, every mod's code is off for that start (or every mod, when
// no code was on).
//
// A fault handler writes it with write(), which uses only what a signal handler may (open, write, close of bytes made
// from what arm() prepared). Every platform's handler calls write() for a fault, an abort and the game's own fatal
// error while the record is armed; the guard's handler and Windows' crash filter name the mod when they can.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace galaxy::mod_crash {

// When the crash came: before the first scene's first frame (the mods' start and the game's), or after it.
enum class Phase : std::uint8_t { kStart, kPlay };

struct Record {
    Phase phase = Phase::kStart;
    bool code_on = false;  // a mod's code was running
    std::string mod;       // the mod whose code, or a call its code made, crashed; empty when none can be named
};

// The longest mod id a record holds (a mod id is at most 64 characters: mods.md).
inline constexpr std::size_t kModMax = 64;

// The record's text: "nebula crash 1\nphase start\ncode 1\nmod some.id\n" (the mod line only with a mod).
[[nodiscard]] std::string format(const Record& record);
// A record from its text; nullopt for anything else (another file, a cut or changed one, an id with other characters).
[[nodiscard]] std::optional<Record> parse(std::string_view text);

// From now a crash writes the record to `file` (empty: none). `code_on`: mods' code runs.
void arm(const std::filesystem::path& file, bool code_on) noexcept;
void set_code_on(bool code_on) noexcept;
void set_phase(Phase phase) noexcept;
[[nodiscard]] Phase phase() noexcept;
[[nodiscard]] bool armed() noexcept;
// No record from now (a test's end; a start that changed its mind).
void disarm() noexcept;
// Writes the record now. `mod` names the mod, or is null. A record that names a mod stays as written: a later write
// of the same crash without one (the platform's handler after the guard's) leaves it. Characters a mod id cannot hold
// end the id. Only what a signal handler may use.
void write(const char* mod) noexcept;
// The record `file` holds, read and removed; nullopt when there is none or it is not a record (it is removed too).
[[nodiscard]] std::optional<Record> take(const std::filesystem::path& file);

}  // namespace galaxy::mod_crash
