// SPDX-License-Identifier: GPL-3.0-only
// The update's install step on Linux: an AppImage replaced in place. $APPIMAGE names the running file (the AppImage
// runtime sets it); the verified package is copied beside it, made executable and flushed, the running file kept as
// "<name>.previous" (a hard link, so the replacement below stays one atomic rename), then renamed over it. Without
// $APPIMAGE (a distribution's package, Flatpak, a build tree) the system updates Nebula and nothing is offered.
#include "update_internal.h"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace galaxy::update::detail {

namespace {

std::filesystem::path running_appimage() {
    const char* path = std::getenv("APPIMAGE");
    if (path == nullptr || path[0] != '/') return {};
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return {};
    return path;
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

}  // namespace

std::string platform_asset(bool beta) {
#if defined(__ANDROID__)
    (void)beta;
    return {};  // the Play Store, or the Android launcher's own path
#else
    const std::string arch = arch_name();
    if (arch.empty()) return {};
    return std::string(beta ? "Nebula-Beta-Linux-" : "Nebula-Linux-") + arch + ".AppImage";
#endif
}

bool platform_can_install() noexcept {
#if defined(__ANDROID__)
    return false;
#else
    const std::filesystem::path image = running_appimage();
    return !image.empty() && access(image.parent_path().c_str(), W_OK) == 0;
#endif
}

bool platform_install(const std::filesystem::path& package, std::string& error) {
    const std::filesystem::path image = running_appimage();
    if (image.empty()) {
        error = "not running from an AppImage";
        return false;
    }
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

}  // namespace galaxy::update::detail
