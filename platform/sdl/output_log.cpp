// SPDX-License-Identifier: GPL-3.0-only
// Where the output goes when nobody reads it (sdl_host.h, keep_output_in_log): Linux, macOS, iOS and iPadOS. Android
// sends it to logcat and the log file (host_window_sdl.cpp forward_output_to_logcat); Windows opens its own log
// (host_main.cpp).

#include "sdl_host.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace galaxy::sdl {

#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__)

namespace {

// Whether someone reads what `fd` takes: a terminal, a pipe or a file (a shell's, a test's). An app started from a menu,
// the Finder or the home screen has /dev/null (macOS, iOS and iPadOS, some Linux desktops) or the user journal's socket
// (systemd desktops), which no player and no support request reads.
bool read_by_someone(int fd) noexcept {
    if (isatty(fd) != 0) {
        return true;
    }
    struct stat about {};
    return fstat(fd, &about) == 0 && (S_ISREG(about.st_mode) || S_ISFIFO(about.st_mode));
}

}  // namespace

void keep_output_in_log(const std::filesystem::path& data) noexcept {
    if (data.empty() || read_by_someone(STDERR_FILENO)) {
        return;
    }
    std::error_code error;
    const std::filesystem::path logs = data / "logs";
    std::filesystem::create_directories(logs, error);
    std::filesystem::rename(logs / "game.log", logs / "game.previous.log", error);
    const std::string log = (logs / "game.log").string();
    if (std::freopen(log.c_str(), "w", stdout) != nullptr) {
        dup2(fileno(stdout), STDERR_FILENO);
        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
}

#else

void keep_output_in_log(const std::filesystem::path&) noexcept {}

#endif

}  // namespace galaxy::sdl
