#pragma once

// Test hooks: scripted input (GALAXY_INPUT_* scripts, pointer and button windows, replay logs, the stick command
// file), teleports and galaxy tours, forced faults and stalls (GALAXY_TEST_*, GALAXY_CRASH_TEST), the refresh-rate
// override, interpolation A/B switches, and picture snapshots and captures (GALAXY_PRESENT_SNAPSHOT_*,
// GALAXY_INTERP_SNAPSHOT_*, GALAXY_GX_CAPTURE_*). They exist in test builds only: CMake NEBULA_TEST_HOOKS (on by
// default) defines GALAXY_TEST_HOOKS=1, and build-release.ps1 turns it off. A release runtime never runs them and
// removes their variables from its environment at startup (test_hook_scrub.cpp).
namespace galaxy {

#if defined(GALAXY_TEST_HOOKS) && GALAXY_TEST_HOOKS
inline constexpr bool kTestHooks = true;
#else
inline constexpr bool kTestHooks = false;
#endif

}  // namespace galaxy
