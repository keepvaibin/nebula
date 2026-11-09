// SPDX-License-Identifier: GPL-3.0-only
// A hidden start keeps off the player's data (hidden_start.h).
#include "galaxy/hidden_start.h"

#include "galaxy/windows_api.h"
#include <Shlobj.h>

#include <cwchar>

namespace galaxy::host {

std::filesystem::path player_data_folder() {
    wchar_t local[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local))) {
        return {};
    }
    return std::filesystem::path(local) / L"Nebula";
}

const char* hidden_start_error(bool hidden, int argc, const wchar_t* const* argv) {
    if (!hidden) {
        return nullptr;
    }
    const std::filesystem::path player = player_data_folder();
    if (player.empty()) {
        return "a hidden start: the player's data folder cannot be found, so it cannot be kept out of";
    }
    bool settings = false;
    bool crash_dumps = false;
    for (int i = 1; i + 1 < argc; ++i) {
        const wchar_t* const name = argv[i];
        const bool folder = std::wcscmp(name, L"--saves") == 0 || std::wcscmp(name, L"--data") == 0 ||
                            std::wcscmp(name, L"--settings") == 0 || std::wcscmp(name, L"--crash-dumps") == 0;
        if (!folder) {
            continue;
        }
        const std::filesystem::path path(argv[i + 1]);
        if (!path.is_absolute() || path_is_within(path, player) || path_is_within(player, path)) {
            return "a hidden start names its folders (--saves, --data, --settings, --crash-dumps) absolute and away "
                   "from the player's data folder";
        }
        settings |= std::wcscmp(name, L"--settings") == 0;
        crash_dumps |= std::wcscmp(name, L"--crash-dumps") == 0;
    }
    if (!settings || !crash_dumps) {
        return "a hidden start names its own --settings and --crash-dumps folders";
    }
    return nullptr;
}

}  // namespace galaxy::host
