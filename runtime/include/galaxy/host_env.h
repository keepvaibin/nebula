// Environment variables for the host's switches and test hooks.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace galaxy::host {

[[nodiscard]] std::optional<std::filesystem::path> get_env_path(const wchar_t* name);
// A flag: set and not starting with 0, f or n.
[[nodiscard]] bool read_env_flag_uncached(const char* name, bool default_value);
[[nodiscard]] bool read_env_flag(const char* name, bool default_value = false);
[[nodiscard]] std::optional<std::string> read_env_string_uncached(const char* name);
[[nodiscard]] std::optional<std::string> read_env_string(const char* name);
// Once enabled, read_env_flag and read_env_string cache each value by literal name (the pointer is the key): the
// input polls read their script switches every time. The host enables it after its startup configuration; tests
// leave it off so the variables they change stay visible.
void set_env_flag_cache_enabled(bool enabled) noexcept;

// Empty on success. Called before startup can write settings, saves or reports.
#if !defined(NEBULA_DEV)
[[nodiscard]] inline const char* test_data_paths_error(int, char**) {
    return nullptr;
}
#else
[[nodiscard]] const char* test_data_paths_error(int argc, char** argv);
#endif

}  // namespace galaxy::host
