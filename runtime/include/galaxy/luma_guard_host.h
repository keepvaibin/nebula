// SPDX-License-Identifier: GPL-3.0-only
// The guard's host half (plan 1.6, contract guard; native/host/luma_guard.cpp).
#pragma once

namespace galaxy::luma_guard {

// Once, before any LumaCore mod loads: the fault handler (Windows: the first vectored handler; POSIX: sigaction on an
// alternate stack, the previous handlers kept to pass on to) and the game module's luma_guard_install. False: the guard
// is off (no export, or the system refused); mods still load, a fault in their code reaches the crash report.
bool install(void* (*find_symbol)(const char*)) noexcept;

// A fault in a mod's code the guard did not resume (the first of the session), when it is the one at `pc` (the crash
// being reported): the sentence for the crash report; note_mod then gives the mod's id. Null otherwise.
[[nodiscard]] const char* note_text(const void* pc) noexcept;
[[nodiscard]] const char* note_mod() noexcept;

// Linux and macOS: the host's writer of a crash report line (its log's form, "[time] <tag> <text>"), called from the
// fault handler for a fault no other handler takes, so it may only use what a signal handler may. Without one the
// lines go to standard error. The default action of the signal follows the report (the process ends with it).
using CrashLine = void (*)(const char* tag, const char* text) noexcept;
void set_crash_line(CrashLine line) noexcept;

}  // namespace galaxy::luma_guard
