// SPDX-License-Identifier: GPL-3.0-only
// Whether this program has mod support. A build made with CMake NEBULA_MODS=OFF (every top-level project defines
// NEBULA_NO_MODS then) has none: the host never loads the game module that carries the mod rows (the plain module
// plays), no screen or entry names mods, no mod folder is made and nothing opens or imports a mod. The one exception is
// a test run (a host extension is named: only the test packages carry one, and the test harness's game half is a mod):
// it keeps what it needs.
#pragma once

#include "galaxy/launch_args.h"

namespace galaxy {

#if defined(NEBULA_NO_MODS)
inline constexpr bool kModsInBuild = false;
#else
inline constexpr bool kModsInBuild = true;
#endif

// The rule, from its two facts.
[[nodiscard]] constexpr bool mod_support_for(bool in_build, bool test_run) noexcept {
    return in_build || test_run;
}

[[nodiscard]] inline bool mod_support() noexcept {
    return mod_support_for(kModsInBuild, !g_launch_args.extensions.empty());
}

}  // namespace galaxy
