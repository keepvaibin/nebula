// SPDX-License-Identifier: GPL-3.0-only
// Nebula's data folder and file opening on every platform. The folder is %LOCALAPPDATA%\Nebula on Windows (the
// variable, as the shader cache and crash dumps read it, so a test's private one holds everything; the shell's folder
// without it); elsewhere NEBULA_DATA_DIR (the Android app sets it to its private files folder), else
// $XDG_DATA_HOME/Nebula, else ~/.local/share/Nebula.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Shlobj.h>
#endif

namespace galaxy {

// Empty when the platform gives no folder.
[[nodiscard]] inline std::filesystem::path nebula_data_directory() {
#if defined(_WIN32)
    wchar_t app_data[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", app_data, MAX_PATH);
    if (length != 0u && length < MAX_PATH) {
        return std::filesystem::path(app_data) / L"Nebula";
    }
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, app_data))) {
        return {};
    }
    return std::filesystem::path(app_data) / L"Nebula";
#else
    if (const char* dir = std::getenv("NEBULA_DATA_DIR"); dir != nullptr && dir[0] != '\0') {
        return dir;
    }
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] == '/') {
        return std::filesystem::path(xdg) / "Nebula";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] == '/') {
        return std::filesystem::path(home) / ".local" / "share" / "Nebula";
    }
    return {};
#endif
}

// The running executable's folder (where a release keeps its data tables); empty when unknown.
[[nodiscard]] inline std::filesystem::path program_directory() {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return length == 0u || length >= MAX_PATH ? std::filesystem::path{}
                                              : std::filesystem::path(exe, exe + length).parent_path();
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
