// SPDX-License-Identifier: GPL-3.0-only
// A test run's options, read from its environment (galaxy/run_options.h). Not in a release.
#if defined(NEBULA_DEV)
#include "galaxy/run_options.h"
#include "galaxy/platform_paths.h"
#include "switches.h"

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#endif

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>

namespace galaxy {
namespace {

#if defined(__ANDROID__)
// The activity's process has no environment a test can set: its switches are lines of test_env in the app's data.
void load_test_environment() {
    const std::filesystem::path data = nebula_data_directory();
    if (data.empty()) {
        return;
    }
    std::FILE* const file = std::fopen((data / "test_env").c_str(), "r");
    if (file == nullptr) {
        return;
    }
    char line[1024];
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        line[std::strcspn(line, "\r\n")] = 0;
        if (char* const equals = std::strchr(line, '=')) {
            *equals = 0;
            setenv(line, equals + 1, 1);
        }
    }
    std::fclose(file);
}
#endif

RunOptions read_options() {
#if defined(__ANDROID__)
    load_test_environment();
#endif
    RunOptions o;
    o.audio_wav_dump = nebula_switch::audio_wav_dump();
    o.audio_wav_dump_only = nebula_switch::audio_wav_dump_only();
    o.audio_wav_dump_seconds = nebula_switch::audio_wav_dump_seconds();
    o.audio_test_device_cycle = nebula_switch::audio_test_device_cycle();
    o.audio_test_reopen = nebula_switch::audio_test_reopen();
    o.audio_test_gaps = nebula_switch::audio_test_gaps();
    o.boot_fast = nebula_switch::boot_fast();
    o.present_snapshot_dir = nebula_switch::present_snapshot_dir();
    o.present_snapshot_every = nebula_switch::present_snapshot_every();
    o.present_snapshot_first = nebula_switch::present_snapshot_first();
    o.present_snapshot_last = nebula_switch::present_snapshot_last();
    o.present_snapshot_game_only = nebula_switch::present_snapshot_game_only();
    o.test_deterministic = nebula_switch::test_deterministic();
    o.test_disc_delay_us = nebula_switch::test_disc_delay_us();
    o.test_device_lost = nebula_switch::test_device_lost();
    o.test_gpu_lost_frame = nebula_switch::test_gpu_lost_frame();
    o.test_gpu_lost_times = nebula_switch::test_gpu_lost_times();
    o.test_home_row = nebula_switch::test_home_row();
    o.test_script_clock = nebula_switch::test_script_clock();
    o.test_settings_keys = nebula_switch::test_settings_keys();
    o.test_settings_open = nebula_switch::test_settings_open();
    o.test_vram_budget_mb = nebula_switch::test_vram_budget_mb();
    o.test_window_sizes = nebula_switch::test_window_sizes();
    o.test_mods_screen = nebula_switch::test_mods_screen();
    o.test_mods_changed = nebula_switch::test_mods_changed();
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
    o.audio_wav_dump_w = read_wide(nebula_switch::audio_wav_dump_wide_name());
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
