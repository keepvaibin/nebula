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

}  // namespace galaxy::luma_guard
