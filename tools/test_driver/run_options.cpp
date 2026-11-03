// SPDX-License-Identifier: GPL-3.0-only
// A test run's options, read from its environment (galaxy/run_options.h). Not in a release.
#if !defined(NEBULA_RELEASE)
#include "galaxy/run_options.h"
#include "switches.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <atomic>
#include <deque>
#include <string>

namespace galaxy {
namespace {

RunOptions read_options() {
    RunOptions o;
    o.frame_timeline = nebula_switch::frame_timeline();
    o.frame_timeline_events = nebula_switch::frame_timeline_events();
    o.monitor_frame_tails = nebula_switch::monitor_frame_tails();
    o.monitor_pointer_latency = nebula_switch::monitor_pointer_latency();
    o.audio_wav_dump = nebula_switch::audio_wav_dump();
    o.audio_wav_dump_only = nebula_switch::audio_wav_dump_only();
    o.audio_wav_dump_seconds = nebula_switch::audio_wav_dump_seconds();
    o.boot_fast = nebula_switch::boot_fast();
    o.crash_dump_dir = nebula_switch::crash_dump_dir();
    o.d3d_debug = nebula_switch::d3d_debug();
    o.gpu_draws = nebula_switch::gpu_draws();
    o.gpu_passes = nebula_switch::gpu_passes();
    o.gpu_preference = nebula_switch::gpu_preference();
    o.hide_window = nebula_switch::hide_window();
    o.log_to_console = nebula_switch::log_to_console();
    o.movie_dir = nebula_switch::movie_dir();
    o.pipeline_seed_out = nebula_switch::pipeline_seed_out();
    o.present_snapshot_dir = nebula_switch::present_snapshot_dir();
    o.present_snapshot_every = nebula_switch::present_snapshot_every();
    o.present_snapshot_first = nebula_switch::present_snapshot_first();
    o.present_snapshot_last = nebula_switch::present_snapshot_last();
    o.profile_game = nebula_switch::profile_game();
    o.render_config_path = nebula_switch::render_config_path();
    o.runtime_settings_path = nebula_switch::runtime_settings_path();
    o.test_arena_fill = nebula_switch::test_arena_fill();
    o.test_cpu_affinity = nebula_switch::test_cpu_affinity();
    o.test_deterministic = nebula_switch::test_deterministic();
    o.test_disc_delay_us = nebula_switch::test_disc_delay_us();
    o.test_gpu_lost_frame = nebula_switch::test_gpu_lost_frame();
    o.test_gpu_lost_times = nebula_switch::test_gpu_lost_times();
    o.test_home_row = nebula_switch::test_home_row();
    o.test_memory_at = nebula_switch::test_memory_at();
    o.test_one_game = nebula_switch::test_one_game();
    o.test_post_fault = nebula_switch::test_post_fault();
    o.test_script_clock = nebula_switch::test_script_clock();
    o.test_settings_keys = nebula_switch::test_settings_keys();
    o.test_settings_open = nebula_switch::test_settings_open();
    o.test_window_sizes = nebula_switch::test_window_sizes();
    o.texture_dump_dir = nebula_switch::texture_dump_dir();
    o.texture_pack_dir = nebula_switch::texture_pack_dir();
    o.trace_file_reads = nebula_switch::trace_file_reads();
    o.trace_frame_times = nebula_switch::trace_frame_times();
    o.trace_loads = nebula_switch::trace_loads();
    o.trace_memory = nebula_switch::trace_memory();
    o.trace_present_stats = nebula_switch::trace_present_stats();
    o.trace_thread_cpu = nebula_switch::trace_thread_cpu();
    o.vk_simulate_driver = nebula_switch::vk_simulate_driver();
    o.vk_test_no_feature = nebula_switch::vk_test_no_feature();
    o.vk_test_refuse = nebula_switch::vk_test_refuse();
    o.vulkan_validation = nebula_switch::vulkan_validation();
    o.xinput_require_focus = nebula_switch::xinput_require_focus();
#if defined(_WIN32)
    // Set but empty counts as set: an empty path never falls back to a player's.
    static std::deque<std::wstring> wide;
    const auto read_wide = [](const wchar_t* name) -> const wchar_t* {
        SetLastError(ERROR_SUCCESS);
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required == 0u && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            return nullptr;
        }
        std::wstring value(required, L'\0');
        const DWORD copied = required != 0u ? GetEnvironmentVariableW(name, value.data(), required) : 0u;
        value.resize(copied);
        wide.push_back(std::move(value));
        return wide.back().c_str();
    };
    o.frame_timeline_path_w = read_wide(nebula_switch::frame_timeline_path_wide_name());
    o.render_config_path_w = read_wide(nebula_switch::render_config_path_wide_name());
    o.runtime_settings_path_w = read_wide(nebula_switch::runtime_settings_path_wide_name());
    o.crash_dump_dir_w = read_wide(nebula_switch::crash_dump_dir_wide_name());
    o.audio_wav_dump_w = read_wide(nebula_switch::audio_wav_dump_wide_name());
    o.texture_pack_dir_w = read_wide(nebula_switch::texture_pack_dir_wide_name());
    o.texture_dump_dir_w = read_wide(nebula_switch::texture_dump_dir_wide_name());
    o.movie_dir_w = read_wide(nebula_switch::movie_dir_wide_name());
#endif
    return o;
}

std::atomic<bool> s_final{false};

RunOptions& stored() {
    static RunOptions options = read_options();
    return options;
}

}  // namespace

const RunOptions& run_options() noexcept {
    // Until the host says the environment is final, options follow it: unit tests change it.
    if (!s_final.load(std::memory_order_acquire)) {
        stored() = read_options();
    }
    return stored();
}

void set_run_options_final(bool final_values) noexcept {
    if (final_values) {
        stored() = read_options();
    }
    s_final.store(final_values, std::memory_order_release);
}

}  // namespace galaxy

#endif
