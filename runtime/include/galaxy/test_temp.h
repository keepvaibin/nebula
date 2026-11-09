#pragma once

// A temporary path for a test that writes files: the system's temporary folder, `name` and this process's id, so two builds
// running the same test at once do not share (and delete) each other's files. Each test removes what it made at its end.

#include <filesystem>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace galaxy {

[[nodiscard]] inline std::filesystem::path test_temp_path(std::string_view name) {
#if defined(_WIN32)
    const long process = static_cast<long>(_getpid());
#else
    const long process = static_cast<long>(getpid());
#endif
    return std::filesystem::temp_directory_path() / (std::string(name) + "-" + std::to_string(process));
}

}  // namespace galaxy
