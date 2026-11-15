// SPDX-License-Identifier: GPL-3.0-only
// A hidden start keeps off the player's data (hidden_start.h).
#include "galaxy/hidden_start.h"

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <Shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace galaxy::host {

std::filesystem::path player_data_folder() {
#if defined(_WIN32)
    wchar_t local[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local))) {
        return {};
    }
    return std::filesystem::path(local) / L"Nebula";
#else
    const passwd* const account = getpwuid(getuid());
    if (account == nullptr || account->pw_dir == nullptr || account->pw_dir[0] != '/') {
        return {};
    }
#if defined(__APPLE__)
    return std::filesystem::path(account->pw_dir) / "Library" / "Application Support" / "Nebula";
#else
    return std::filesystem::path(account->pw_dir) / ".local" / "share" / "Nebula";
#endif
#endif
}

const char* hidden_start_error(bool hidden, int argc, const ArgChar* const* argv) {
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
        const ArgChar* const name = argv[i];
        const bool folder = same_arg(name, NEBULA_ARG_TEXT("--saves")) || same_arg(name, NEBULA_ARG_TEXT("--data")) ||
                            same_arg(name, NEBULA_ARG_TEXT("--settings")) || same_arg(name, NEBULA_ARG_TEXT("--crash-dumps"));
        if (!folder) {
            continue;
        }
        const std::filesystem::path path(argv[i + 1]);
        if (!path.is_absolute() || path_is_within(path, player) || path_is_within(player, path)) {
            return "a hidden start names its folders (--saves, --data, --settings, --crash-dumps) absolute and away "
                   "from the player's data folder";
        }
        settings |= same_arg(name, NEBULA_ARG_TEXT("--settings"));
        crash_dumps |= same_arg(name, NEBULA_ARG_TEXT("--crash-dumps"));
    }
    if (!settings || !crash_dumps) {
        return "a hidden start names its own --settings and --crash-dumps folders";
    }
    return nullptr;
}

}  // namespace galaxy::host
