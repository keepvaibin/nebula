// SPDX-License-Identifier: GPL-3.0-only
// The test runs' own policy on top of the host's hidden start (galaxy/hidden_start.h): everything a test run reads as
// the player's or writes stays in its work folder, NEBULA_WORK.
#pragma once

#include "galaxy/hidden_start.h"

#include <string>

namespace test_guard {

// Reads one environment variable; empty when it is not set.
using Environment = galaxy::host::ArgText (*)(const galaxy::host::ArgChar* name);

// Empty when the run may go on: NEBULA_WORK is absolute and apart from the player's data folder (`player`);
// NEBULA_SETTINGS, NEBULA_APPDATA, NEBULA_SAVES and the variable the game finds its data folder by (LOCALAPPDATA on
// Windows, XDG_DATA_HOME on Linux, HOME on macOS) are inside it; the outputs this extension writes
// (GALAXY_AUDIO_WAV_DUMP, GALAXY_PRESENT_SNAPSHOT_DIR) are inside it when set; and the process's folder arguments
// (--saves, --data, --settings, --crash-dumps) are inside it, --settings and --crash-dumps given.
[[nodiscard]] const char* error(int argc, const galaxy::host::ArgChar* const* argv, Environment environment,
                                const galaxy::host::ArgText& player);

}  // namespace test_guard
