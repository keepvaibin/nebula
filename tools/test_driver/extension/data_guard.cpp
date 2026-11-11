// SPDX-License-Identifier: GPL-3.0-only
// The test runs' work-folder policy (data_guard.h).
#include "data_guard.h"

#include "galaxy/hidden_start.h"

#include <cwchar>
#include <filesystem>

namespace test_guard {

const char* error(int argc, const wchar_t* const* argv, Environment environment, const std::wstring& player) {
    namespace fs = std::filesystem;
    const std::wstring work_text = environment(L"NEBULA_WORK");
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
    for (const auto* name : {L"NEBULA_SETTINGS", L"NEBULA_APPDATA", L"NEBULA_SAVES", L"LOCALAPPDATA"}) {
        const std::wstring text = environment(name);
        const fs::path path(text);
        if (text.empty() || !path_is_within(path, work) || path_is_within(path, protected_root)) {
            return "tests require private settings, saves, app data and crash paths inside NEBULA_WORK";
        }
    }
    // Folders and files this extension writes to when a run names them: each is private too.
    for (const auto* name : {L"GALAXY_AUDIO_WAV_DUMP", L"GALAXY_PRESENT_SNAPSHOT_DIR"}) {
        const std::wstring text = environment(name);
        const fs::path path(text);
        if (!text.empty() && (!path_is_within(path, work) || path_is_within(path, protected_root))) {
            return "tests require output and download paths inside NEBULA_WORK";
        }
    }
    // The run's own folders: settings and crash dumps are required, saves and data checked when named.
    bool settings = false;
    bool crash_dumps = false;
    for (int i = 1; i + 1 < argc; ++i) {
        const bool folder = std::wcscmp(argv[i], L"--saves") == 0 || std::wcscmp(argv[i], L"--data") == 0 ||
                            std::wcscmp(argv[i], L"--settings") == 0 || std::wcscmp(argv[i], L"--crash-dumps") == 0;
        if (!folder) {
            continue;
        }
        const fs::path path(argv[i + 1]);
        if (!path_is_within(path, work) || path_is_within(path, protected_root)) {
            return "tests require their save, data, settings and crash folders inside NEBULA_WORK";
        }
        settings |= std::wcscmp(argv[i], L"--settings") == 0;
        crash_dumps |= std::wcscmp(argv[i], L"--crash-dumps") == 0;
    }
    if (!settings || !crash_dumps) {
        return "tests require --settings and --crash-dumps inside NEBULA_WORK";
    }
    return nullptr;
}

}  // namespace test_guard
