// SPDX-License-Identifier: GPL-3.0-only
// The test runs' work-folder policy (data_guard.h).
#include "data_guard.h"

#include <filesystem>

namespace test_guard {

using galaxy::host::ArgChar;
using galaxy::host::ArgText;
using galaxy::host::same_arg;

const char* error(int argc, const ArgChar* const* argv, Environment environment, const ArgText& player) {
    namespace fs = std::filesystem;
    const ArgText work_text = environment(NEBULA_ARG_TEXT("NEBULA_WORK"));
    const fs::path work(work_text);
    if (work_text.empty() || !work.is_absolute()) {
        return "tests require an absolute NEBULA_WORK";
    }
    const fs::path protected_root(player);
    if (player.empty()) {
        return "cannot locate the protected user data folder";
    }
    using galaxy::host::path_is_within;
    if (path_is_within(work, protected_root) || path_is_within(protected_root, work)) {
        return "NEBULA_WORK overlaps the user data folder";
    }
    // The last name is the variable the game finds its own data folder by (platform_paths.h nebula_data_directory).
    for (const ArgChar* name : {NEBULA_ARG_TEXT("NEBULA_SETTINGS"), NEBULA_ARG_TEXT("NEBULA_APPDATA"), NEBULA_ARG_TEXT("NEBULA_SAVES"),
#if defined(_WIN32)
                                NEBULA_ARG_TEXT("LOCALAPPDATA")
#elif defined(__APPLE__)
                                NEBULA_ARG_TEXT("HOME")
#else
                                NEBULA_ARG_TEXT("XDG_DATA_HOME")
#endif
         }) {
        const ArgText text = environment(name);
        const fs::path path(text);
        if (text.empty() || !path_is_within(path, work) || path_is_within(path, protected_root)) {
            return "tests require private settings, saves, app data and crash paths inside NEBULA_WORK";
        }
    }
    // Folders and files this extension writes to when a run names them: each is private too.
    for (const ArgChar* name : {NEBULA_ARG_TEXT("GALAXY_AUDIO_WAV_DUMP"), NEBULA_ARG_TEXT("GALAXY_PRESENT_SNAPSHOT_DIR")}) {
        const ArgText text = environment(name);
        const fs::path path(text);
        if (!text.empty() && (!path_is_within(path, work) || path_is_within(path, protected_root))) {
            return "tests require output and download paths inside NEBULA_WORK";
        }
    }
    // The run's own folders: settings and crash dumps are required, saves and data checked when named.
    bool settings = false;
    bool crash_dumps = false;
    for (int i = 1; i + 1 < argc; ++i) {
        const bool folder = same_arg(argv[i], NEBULA_ARG_TEXT("--saves")) || same_arg(argv[i], NEBULA_ARG_TEXT("--data")) ||
                            same_arg(argv[i], NEBULA_ARG_TEXT("--settings")) || same_arg(argv[i], NEBULA_ARG_TEXT("--crash-dumps"));
        if (!folder) {
            continue;
        }
        const fs::path path(argv[i + 1]);
        if (!path_is_within(path, work) || path_is_within(path, protected_root)) {
            return "tests require their save, data, settings and crash folders inside NEBULA_WORK";
        }
        settings |= same_arg(argv[i], NEBULA_ARG_TEXT("--settings"));
        crash_dumps |= same_arg(argv[i], NEBULA_ARG_TEXT("--crash-dumps"));
    }
    if (!settings || !crash_dumps) {
        return "tests require --settings and --crash-dumps inside NEBULA_WORK";
    }
    return nullptr;
}

}  // namespace test_guard
