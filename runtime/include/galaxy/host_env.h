// Environment variables the host reads, and the test runs' data guard.
#pragma once

#include <filesystem>
#include <optional>

namespace galaxy::host {

[[nodiscard]] std::optional<std::filesystem::path> get_env_path(const wchar_t* name);

// Empty on success. Called before startup can write settings, saves or reports.
#if !defined(NEBULA_DEV)
[[nodiscard]] inline const char* test_data_paths_error(int, char**) {
    return nullptr;
}
#else
[[nodiscard]] const char* test_data_paths_error(int argc, char** argv);
#endif

}  // namespace galaxy::host
