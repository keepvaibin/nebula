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

// A run the player asked to record (--diagnostics 1, in a release too): the frame and pointer monitors and the timeline.
// Set once, before the threads that read it start.
inline bool g_diagnostics_recording = false;
[[nodiscard]] inline bool diagnostics_recording() noexcept { return g_diagnostics_recording; }

struct RunOptions {
    const char* frame_timeline = nullptr;
    const char* frame_timeline_events = nullptr;
    const char* monitor_frame_tails = nullptr;
    const char* monitor_pointer_latency = nullptr;
    const char* audio_wav_dump = nullptr;
    const char* audio_wav_dump_only = nullptr;
    const char* audio_wav_dump_seconds = nullptr;
    const char* audio_test_device_cycle = nullptr;
    const char* audio_test_reopen = nullptr;
    const char* audio_test_gaps = nullptr;
    const char* boot_fast = nullptr;
    const char* crash_dump_dir = nullptr;
    const char* d3d_debug = nullptr;
    const char* gpu_draws = nullptr;
    const char* gpu_passes = nullptr;
    const char* gpu_preference = nullptr;
    const char* hide_window = nullptr;
    const char* test_window_off_desktop = nullptr;
    const char* log_to_console = nullptr;
    const char* movie_dir = nullptr;
    const char* pipeline_seed_out = nullptr;
    const char* present_snapshot_dir = nullptr;
    const char* present_snapshot_every = nullptr;
    const char* present_snapshot_first = nullptr;
    const char* present_snapshot_last = nullptr;
    const char* profile_game = nullptr;
    const char* render_config_path = nullptr;
    const char* runtime_settings_path = nullptr;
    const char* test_arena_fill = nullptr;
    const char* test_cpu_affinity = nullptr;
    const char* test_deterministic = nullptr;
    const char* test_disc_delay_us = nullptr;
    const char* test_gpu_lost_frame = nullptr;
    const char* test_gpu_lost_times = nullptr;
    const char* test_home_row = nullptr;
    const char* test_memory_at = nullptr;
    const char* test_one_game = nullptr;
    const char* test_post_fault = nullptr;
    const char* test_script_clock = nullptr;
    const char* test_settings_keys = nullptr;
    const char* test_motion_dump = nullptr;
    const char* test_settings_open = nullptr;
    const char* test_window_sizes = nullptr;
    const char* texture_dump_dir = nullptr;
    const char* texture_pack_dir = nullptr;
    const char* trace_file_reads = nullptr;
    const char* trace_frame_times = nullptr;
    const char* trace_loads = nullptr;
    const char* test_mods_screen = nullptr;
    const char* test_mods_changed = nullptr;
    const char* trace_memory = nullptr;
    const char* trace_interp_slots = nullptr;
    const char* trace_present_stats = nullptr;
    const char* trace_thread_cpu = nullptr;
    const char* vk_simulate_driver = nullptr;
    const char* vk_test_no_feature = nullptr;
    const char* vk_test_refuse = nullptr;
    const char* vulkan_validation = nullptr;
    const char* xinput_require_focus = nullptr;
#if defined(_WIN32)
    const wchar_t* frame_timeline_path_w = nullptr;
    const wchar_t* xess_dir_w = nullptr;
    const wchar_t* streamline_dir_w = nullptr;
    const wchar_t* render_config_path_w = nullptr;
    const wchar_t* runtime_settings_path_w = nullptr;
    const wchar_t* crash_dump_dir_w = nullptr;
    const wchar_t* audio_wav_dump_w = nullptr;
    const wchar_t* texture_pack_dir_w = nullptr;
    const wchar_t* texture_dump_dir_w = nullptr;
    const wchar_t* movie_dir_w = nullptr;
#endif
};

#if defined(NEBULA_RELEASE)
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

// As run_option_read, its length without the terminator, 0 when unset or when it does not fit.
[[nodiscard]] inline unsigned long run_option_copy(const char* value, char* buffer, std::size_t size) noexcept {
    std::size_t length = 0u;
    return run_option_read(&length, buffer, size, value) == 0 && length != 0u ? static_cast<unsigned long>(length - 1u) : 0ul;
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

[[nodiscard]] inline std::optional<std::string> run_option_string(const char* value) {
    return value != nullptr && value[0] != '\0' ? std::optional<std::string>(value) : std::nullopt;
}

}  // namespace galaxy
