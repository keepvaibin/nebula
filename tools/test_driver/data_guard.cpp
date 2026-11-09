// SPDX-License-Identifier: GPL-3.0-only
// The guard that keeps a test run off a player's data (host_env.h). Not in a release.
#if defined(NEBULA_DEV) && defined(_WIN32)
#include "galaxy/host_env.h"
#include "galaxy/launch_args.h"
#include "galaxy/test_data_paths.h"
#include "switches.h"

#include "galaxy/windows_api.h"
#include <Shlobj.h>
#include <shellapi.h>

#include <cstring>
#include <cwchar>
#include <filesystem>

namespace galaxy::host {
const char* test_data_paths_error(int argc, char** argv) {
    bool test = ::galaxy::launch_args().started_hidden;
    wchar_t* environment = GetEnvironmentStringsW();
    if (!environment) return "cannot inspect the test environment";
    for (const wchar_t* entry = environment; *entry; entry += std::wcslen(entry) + 1) {
        if (_wcsnicmp(entry, nebula_switch::test__wide_name(), 12) == 0) test = true;
    }
    FreeEnvironmentStringsW(environment);
    if (!test) return nullptr;
    const auto work = get_env_path(L"NEBULA_WORK");
    if (!work || !work->is_absolute()) return "tests require an absolute NEBULA_WORK";
    wchar_t user_data[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, user_data))) {
        return "cannot locate the protected user data folder";
    }
    const auto protected_root = std::filesystem::path(user_data) / L"Nebula";
    if (path_is_within(*work, protected_root) || path_is_within(protected_root, *work)) {
        return "NEBULA_WORK overlaps the user data folder";
    }
    for (const auto* name : {L"NEBULA_SETTINGS", L"NEBULA_APPDATA", L"NEBULA_SAVES", L"LOCALAPPDATA"}) {
        const auto path = get_env_path(name);
        if (!path || !path_is_within(*path, *work) || path_is_within(*path, protected_root)) {
            return "tests require private settings, saves, app data and crash paths inside NEBULA_WORK";
        }
    }
    // Folders and files a run writes or downloads to when it names them: each is private too when set.
    for (const auto* name : {L"GALAXY_AUDIO_WAV_DUMP", L"GALAXY_PRESENT_SNAPSHOT_DIR"}) {
        const auto path = get_env_path(name);
        if (path && (!path_is_within(*path, *work) || path_is_within(*path, protected_root))) {
            return "tests require output and download paths inside NEBULA_WORK";
        }
    }
    // The run's own folders: saves, settings and crash dumps are required, the data folder checked when named.
    bool settings = false;
    bool crash_dumps = false;
    for (int i = 1; i + 1 < argc; ++i) {
        const bool folder = std::strcmp(argv[i], "--saves") == 0 || std::strcmp(argv[i], "--data") == 0 ||
                            std::strcmp(argv[i], "--settings") == 0 || std::strcmp(argv[i], "--crash-dumps") == 0;
        if (!folder) {
            continue;
        }
        int count = 0;
        LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
        const std::filesystem::path path = wide != nullptr && i + 1 < count ? std::filesystem::path(wide[i + 1])
                                                                           : std::filesystem::path(argv[i + 1]);
        if (wide != nullptr) {
            LocalFree(wide);
        }
        if (!path_is_within(path, *work) || path_is_within(path, protected_root)) {
            return "tests require their save, data, settings and crash folders inside NEBULA_WORK";
        }
        settings |= std::strcmp(argv[i], "--settings") == 0;
        crash_dumps |= std::strcmp(argv[i], "--crash-dumps") == 0;
    }
    if (!settings || !crash_dumps) {
        return "tests require --settings and --crash-dumps inside NEBULA_WORK";
    }
    return nullptr;
}
}  // namespace galaxy::host
#endif
