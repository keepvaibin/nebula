// SPDX-License-Identifier: GPL-3.0-only
// The update's install step on Linux, for the two ways Nebula is installed there:
// - install.sh's install (installer/linux; the release's tar.gz): the running program is <root>/versions/<version>/nebula
//   beside <root>/current.json. The verified package is unpacked in the staging folder and its own install.sh runs with
//   --yes (and --portable <root> for a portable folder): it puts the new version beside the running one, keeps the game,
//   and names the new version in current.json, which nebula-launch starts next time (P-125).
// - an AppImage: $APPIMAGE names the running file (the AppImage runtime sets it); the verified package is copied beside
//   it, made executable and flushed, the running file kept as "<name>.previous" (a hard link, so the replacement below
//   stays one atomic rename), then renamed over it.
// Anything else (a distribution's package, a build tree): a newer package is only named.
#include "update_internal.h"
#include "update_install_linux.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <optional>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace galaxy::update::detail {

namespace {

std::filesystem::path running_appimage() {
    const char* path = std::getenv("APPIMAGE");
    if (path == nullptr || path[0] != '/') return {};
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return {};
    return path;
}

std::filesystem::path running_program() {
    std::error_code error;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path() : self;
}

std::string arch_name() {
#if defined(__x86_64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#else
    return {};
#endif
}

// `argv` run with stdin from /dev/null and its output to `log`, waited for: its exit code, or -1 when it could not run.
// DISPLAY and WAYLAND_DISPLAY are left out of its environment: install.sh then asks nothing of the desktop and opens no
// window; what it says is in the log, and the game shows the failure.
int run_and_wait(const std::vector<std::string>& argv, const std::filesystem::path& log) {
    std::vector<char*> args;
    for (const std::string& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
    args.push_back(nullptr);
    std::vector<char*> env;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        if (std::strncmp(*entry, "DISPLAY=", 8) == 0 || std::strncmp(*entry, "WAYLAND_DISPLAY=", 16) == 0) continue;
        env.push_back(*entry);
    }
    env.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return -1;
    (void)posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    (void)posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    (void)posix_spawn_file_actions_adddup2(&actions, 1, 2);
    pid_t child = 0;
    const int spawned = posix_spawnp(&child, args[0], &actions, nullptr, args.data(), env.data());
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) return -1;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// The last line install.sh gave for its failure ("Nebula was not installed: ..."), or "".
std::string install_failure(const std::filesystem::path& log) {
    std::ifstream in(log);
    std::string line;
    std::string found;
    while (std::getline(in, line)) {
        if (const std::size_t at = line.find("Nebula was not installed: "); at != std::string::npos) {
            found = line.substr(at + std::strlen("Nebula was not installed: "));
        }
    }
    return found;
}

bool install_appimage(const std::filesystem::path& image, const std::filesystem::path& package, std::string& error) {
    const std::filesystem::path next = image.string() + ".next";
    const std::filesystem::path previous = image.string() + ".previous";
    std::error_code failure;
    std::filesystem::remove(next, failure);
    if (!std::filesystem::copy_file(package, next, std::filesystem::copy_options::overwrite_existing, failure)) {
        error = "could not write beside the AppImage: " + failure.message();
        return false;
    }
    const int fd = open(next.c_str(), O_RDONLY);
    const bool flushed = fd >= 0 && fsync(fd) == 0;
    if (fd >= 0) close(fd);
    if (!flushed || chmod(next.c_str(), 0755) != 0) {
        std::filesystem::remove(next, failure);
        error = "could not prepare the new AppImage";
        return false;
    }
    // The running version kept for rollback (rename "<name>.previous" back over it).
    std::filesystem::remove(previous, failure);
    if (link(image.c_str(), previous.c_str()) != 0) {
        std::filesystem::copy_file(image, previous, std::filesystem::copy_options::overwrite_existing, failure);
    }
    if (std::rename(next.c_str(), image.c_str()) != 0) {
        std::filesystem::remove(next, failure);
        error = "could not replace the AppImage";
        return false;
    }
    std::filesystem::remove(package, failure);
    return true;
}

}  // namespace

std::optional<ScriptInstall> script_install_of(const std::filesystem::path& program, const char* xdg_data_home,
                                               const char* home) {
    std::error_code error;
    const std::filesystem::path versions = program.parent_path().parent_path();
    if (program.filename() != "nebula" || versions.filename() != "versions") return std::nullopt;
    const std::filesystem::path root = versions.parent_path();
    if (!std::filesystem::is_regular_file(root / "current.json", error) ||
        !std::filesystem::is_regular_file(root / "nebula-launch", error)) {
        return std::nullopt;
    }
    ScriptInstall install{root, std::filesystem::is_regular_file(root / "portable.txt", error)};
    if (!install.portable) {
        // install.sh installs for the user into ${XDG_DATA_HOME:-$HOME/.local/share}/Nebula: only that one is updated.
        std::filesystem::path share;
        if (xdg_data_home != nullptr && xdg_data_home[0] != '\0') {
            share = xdg_data_home;
        } else if (home != nullptr && home[0] != '\0') {
            share = std::filesystem::path(home) / ".local" / "share";
        } else {
            return std::nullopt;
        }
        if (!std::filesystem::equivalent(share / "Nebula", root, error) || error) return std::nullopt;
    }
    if (access(root.c_str(), W_OK) != 0) return std::nullopt;
    return install;
}

bool install_with_script(const std::filesystem::path& package, const ScriptInstall& install,
                         const std::filesystem::path& work, std::string& error) {
    std::error_code failure;
    std::filesystem::remove_all(work, failure);
    if (!std::filesystem::create_directories(work, failure)) {
        error = "could not make " + work.string();
        return false;
    }
    const std::filesystem::path log = work.parent_path() / "update-install.log";
    std::filesystem::remove(log, failure);
    const auto done = [&](bool ok) {
        std::filesystem::remove_all(work, failure);
        if (ok) std::filesystem::remove(package, failure);
        return ok;
    };
    if (run_and_wait({"tar", "-xzf", package.string(), "-C", work.string()}, log) != 0) {
        error = "the package could not be unpacked";
        return done(false);
    }
    // package-linux.sh's archive holds one folder, Nebula-<version>-linux-<arch>, with install.sh in it.
    std::filesystem::path script;
    for (const auto& entry : std::filesystem::directory_iterator(work, failure)) {
        if (entry.is_directory(failure) && std::filesystem::is_regular_file(entry.path() / "install.sh", failure)) {
            if (!script.empty()) {
                script.clear();
                break;
            }
            script = entry.path() / "install.sh";
        }
    }
    if (script.empty()) {
        error = "the package holds no install.sh";
        return done(false);
    }
    std::vector<std::string> argv{"/bin/sh", script.string(), "--yes"};
    if (install.portable) {
        argv.push_back("--portable");
        argv.push_back(install.root.string());
    }
    const int code = run_and_wait(argv, log);
    if (code != 0) {
        const std::string why = install_failure(log);
        error = !why.empty() ? why : "install.sh ended with " + std::to_string(code);
        return done(false);
    }
    return done(true);
}

std::filesystem::path next_program_of(const std::filesystem::path& program) {
    const std::filesystem::path versions = program.parent_path().parent_path();
    if (program.filename() != "nebula" || versions.filename() != "versions") return {};
    std::ifstream in(versions.parent_path() / "current.json");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // install.sh's current.json: {"version": "<version>", "previous": ...}
    const std::size_t key = text.find("\"version\"");
    const std::size_t open = key == std::string::npos ? key : text.find('"', text.find(':', key));
    const std::size_t close = open == std::string::npos ? open : text.find('"', open + 1u);
    if (close == std::string::npos) return {};
    const std::string version = text.substr(open + 1u, close - open - 1u);
    if (version.empty() || version.find('/') != std::string::npos || version == "." || version == ".." ||
        version == program.parent_path().filename().string()) {
        return {};
    }
    const std::filesystem::path next = versions / version / "nebula";
    std::error_code error;
    return std::filesystem::is_regular_file(next, error) && access(next.c_str(), X_OK) == 0 ? next : std::filesystem::path();
}

std::string library_path_for(std::string_view library_path, const std::filesystem::path& from,
                             const std::filesystem::path& to) {
    std::string out;
    std::size_t start = 0;
    while (start <= library_path.size()) {
        const std::size_t end = std::min(library_path.find(':', start), library_path.size());
        const std::string_view entry = library_path.substr(start, end - start);
        if (!out.empty() || start > 0) out += ':';
        out += std::filesystem::path(entry).lexically_normal() == from.lexically_normal() ? to.string() : std::string(entry);
        start = end + 1u;
    }
    return out;
}

std::string platform_asset(bool beta) {
#if defined(__ANDROID__)
    (void)beta;
    return {};  // the Play Store, or the Android launcher's own path
#else
    const std::string arch = arch_name();
    if (arch.empty()) return {};
    if (!running_appimage().empty()) return std::string(beta ? "Nebula-Beta-Linux-" : "Nebula-Linux-") + arch + ".AppImage";
    // The package install.sh installs from (installer/package-linux.sh).
    return std::string(beta ? "Nebula-Beta-Linux-" : "Nebula-Linux-") + (arch == "x86_64" ? "x64" : "arm64") + ".tar.gz";
#endif
}

bool platform_can_install() noexcept {
#if defined(__ANDROID__)
    return false;
#else
    const std::filesystem::path image = running_appimage();
    if (!image.empty()) return access(image.parent_path().c_str(), W_OK) == 0;
    try {
        return script_install_of(running_program(), std::getenv("XDG_DATA_HOME"), std::getenv("HOME")).has_value();
    } catch (...) {
        return false;
    }
#endif
}

bool platform_install(const std::filesystem::path& package, std::string& error) {
    if (const std::filesystem::path image = running_appimage(); !image.empty()) {
        return install_appimage(image, package, error);
    }
    const std::optional<ScriptInstall> install =
        script_install_of(running_program(), std::getenv("XDG_DATA_HOME"), std::getenv("HOME"));
    if (!install) {
        error = "not installed by install.sh or as an AppImage";
        return false;
    }
    return install_with_script(package, *install, package.parent_path() / "unpacked", error);
}

}  // namespace galaxy::update::detail

namespace galaxy::update {

std::string switch_to_installed_update() {
    const std::filesystem::path program = detail::running_program();
    const std::filesystem::path next = detail::next_program_of(program);
    if (next.empty()) return {};
    if (const char* library_path = std::getenv("LD_LIBRARY_PATH"); library_path != nullptr) {
        const std::string changed = detail::library_path_for(library_path, program.parent_path(), next.parent_path());
        (void)setenv("LD_LIBRARY_PATH", changed.c_str(), 1);
    }
    return next.string();
}

}  // namespace galaxy::update
