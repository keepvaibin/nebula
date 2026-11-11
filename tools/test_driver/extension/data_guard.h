// SPDX-License-Identifier: GPL-3.0-only
// The test runs' own policy on top of the host's hidden start (galaxy/hidden_start.h): everything a test run reads as
// the player's or writes stays in its work folder, NEBULA_WORK.
#pragma once

#include <string>

namespace test_guard {

// Reads one environment variable; empty when it is not set.
using Environment = std::wstring (*)(const wchar_t* name);

// Empty when the run may go on: NEBULA_WORK is absolute and apart from the player's data folder (`player`);
// NEBULA_SETTINGS, NEBULA_APPDATA, NEBULA_SAVES and LOCALAPPDATA are inside it; the outputs this extension writes
// (GALAXY_AUDIO_WAV_DUMP, GALAXY_PRESENT_SNAPSHOT_DIR) are inside it when set; and the process's folder arguments
// (--saves, --data, --settings, --crash-dumps) are inside it, --settings and --crash-dumps given.
[[nodiscard]] const char* error(int argc, const wchar_t* const* argv, Environment environment, const std::wstring& player);

}  // namespace test_guard
