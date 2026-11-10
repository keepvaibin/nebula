// SPDX-License-Identifier: GPL-3.0-only
// Nebula's data folder and file opening on every platform. The folder is the one --data names (launch_args.h; the
// Android app passes its private files folder), else %LOCALAPPDATA%\Nebula on Windows (the variable, so a test's
// private one holds everything; the shell's folder without it), ~/Library/Application Support/Nebula on Apple's
// systems, else $XDG_DATA_HOME/Nebula, else ~/.local/share/Nebula.
#pragma once

#include "galaxy/launch_args.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include "galaxy/windows_paths.h"
#include <Shlobj.h>
#else
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>

#include <cstdint>
#include <string>
#endif

namespace galaxy {

// Empty when the platform gives no folder.
[[nodiscard]] inline std::filesystem::path nebula_data_directory() {
    if (!launch_args().data.empty()) {
        return launch_args().data;
    }
#if defined(_WIN32)
    const std::wstring local = windows::environment_text(L"LOCALAPPDATA");
    if (!local.empty()) {
        return std::filesystem::path(local) / L"Nebula";
    }
    wchar_t app_data[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, app_data))) {
        return {};
    }
    return std::filesystem::path(app_data) / L"Nebula";
#elif defined(__APPLE__)
    // The user's Library on a Mac; the app's own container on an iPhone or iPad, where HOME is the container.
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
        return std::filesystem::path(home) / "Library" / "Application Support" / "Nebula";
    }
    return {};
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] == '/') {
        return std::filesystem::path(xdg) / "Nebula";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
        return std::filesystem::path(home) / ".local" / "share" / "Nebula";
    }
    return {};
#endif
}

// Where --diagnostics writes its files: <data>/diagnostics, made when first asked for; empty without a data folder.
[[nodiscard]] inline std::filesystem::path nebula_diagnostics_directory() {
    const std::filesystem::path data = nebula_data_directory();
    if (data.empty()) {
        return {};
    }
    std::error_code error;
    std::filesystem::create_directories(data / "diagnostics", error);
    return data / "diagnostics";
}

// This process's id (a diagnostics file's name: runs sharing a data folder keep their own).
[[nodiscard]] inline unsigned long current_process_id() noexcept {
#if defined(_WIN32)
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

// The running executable's folder (where a release keeps its data tables); empty when unknown.
[[nodiscard]] inline std::filesystem::path program_directory() {
#if defined(_WIN32)
    const std::wstring exe = windows::executable_path();
    return exe.empty() ? std::filesystem::path{} : std::filesystem::path(exe).parent_path();
#elif defined(__APPLE__)
    std::uint32_t size = 0u;
    _NSGetExecutablePath(nullptr, &size);
    std::string exe(size, '\0');
    if (_NSGetExecutablePath(exe.data(), &size) != 0) {
        return {};
    }
    std::error_code error;
    const std::filesystem::path real = std::filesystem::canonical(exe.c_str(), error);
    return error ? std::filesystem::path{} : real.parent_path();
#else
    std::error_code error;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : exe.parent_path();
#endif
}

// fopen on a path (UTF-16 on Windows); null on failure, with errno set.
[[nodiscard]] inline std::FILE* open_path(const std::filesystem::path& path, const char* mode) {
#if defined(_WIN32)
    wchar_t wide_mode[8]{};
    for (int i = 0; i < 7 && mode[i] != '\0'; ++i) {
        wide_mode[i] = static_cast<wchar_t>(mode[i]);
    }
    std::FILE* file = nullptr;
    return _wfopen_s(&file, path.c_str(), wide_mode) == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), mode);
#endif
}

}  // namespace galaxy
