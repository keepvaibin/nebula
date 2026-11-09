// SPDX-License-Identifier: GPL-3.0-only
// What this run was started with. A player's run has every value unset; only a test build fills them
// (tools/test_driver/run_options.cpp), once, and a release does not contain that file.
#pragma once

#include "galaxy/portable.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace galaxy {

struct RunOptions {
    const char* audio_wav_dump = nullptr;
    const char* audio_wav_dump_only = nullptr;
    const char* audio_wav_dump_seconds = nullptr;
    const char* audio_test_device_cycle = nullptr;
    const char* audio_test_reopen = nullptr;
    const char* audio_test_gaps = nullptr;
    const char* boot_fast = nullptr;
    const char* present_snapshot_dir = nullptr;
    const char* present_snapshot_every = nullptr;
    const char* present_snapshot_first = nullptr;
    const char* present_snapshot_last = nullptr;
    const char* present_snapshot_game_only = nullptr;  // a generated (interpolated) present is neither captured nor counted
    const char* test_deterministic = nullptr;
    const char* test_disc_delay_us = nullptr;
    const char* test_device_lost = nullptr;
    const char* test_gpu_lost_frame = nullptr;
    const char* test_gpu_lost_times = nullptr;
    const char* test_home_row = nullptr;
    const char* test_script_clock = nullptr;
    const char* test_settings_keys = nullptr;
    const char* test_settings_open = nullptr;
    const char* test_vram_budget_mb = nullptr;
    const char* test_window_sizes = nullptr;
    const char* test_mods_screen = nullptr;
    const char* test_mods_changed = nullptr;
#if defined(_WIN32)
    const wchar_t* audio_wav_dump_w = nullptr;
#endif
};

#if !defined(NEBULA_DEV)
[[nodiscard]] inline const RunOptions& run_options() noexcept {
    static constexpr RunOptions kNothing{};
    return kNothing;
}
inline void set_run_options_final(bool) noexcept {}
#else
[[nodiscard]] const RunOptions& run_options() noexcept;
// The environment no longer changes: values stay as they are now.
void set_run_options_final(bool final_values) noexcept;
#endif

// `value`'s text into `buffer`; `*length` its size with the terminator, 0 when unset; non-zero when it does not fit.
[[nodiscard]] inline int run_option_read(std::size_t* length, char* buffer, std::size_t size, const char* value) noexcept {
    *length = 0u;
    if (size != 0u) {
        buffer[0] = '\0';
    }
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    const std::size_t needed = std::strlen(value) + 1u;
    *length = needed;
    if (needed > size) {
        return 34;
    }
    std::memcpy(buffer, value, needed);
    return 0;
}

[[nodiscard]] inline std::uint64_t run_option_u64(const char* value, std::uint64_t fallback) noexcept {
    return value != nullptr && value[0] != '\0' ? std::strtoull(value, nullptr, 10) : fallback;
}

// On unless it starts 0, f or n.
[[nodiscard]] inline bool run_option_flag(const char* value, bool fallback) noexcept {
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value[0] != '0' && value[0] != 'f' && value[0] != 'F' && value[0] != 'n' && value[0] != 'N';
}

}  // namespace galaxy
