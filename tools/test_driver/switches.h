// SPDX-License-Identifier: GPL-3.0-only
// A test run's switches, read from its environment. Not in a release (NEBULA_RELEASE): nothing can turn one on, and no
// name is built.
#pragma once

#include <cstdlib>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

namespace nebula_switch {
#if !defined(NEBULA_DEV)
inline const char* audio_wav_dump() noexcept { return nullptr; }
inline constexpr const wchar_t* audio_wav_dump_wide_name() noexcept { return L""; }
#else
inline const char* audio_wav_dump() noexcept { return std::getenv("GALAXY_AUDIO_WAV_DUMP"); }
inline constexpr const wchar_t* audio_wav_dump_wide_name() noexcept { return L"GALAXY_AUDIO_WAV_DUMP"; }
#endif
#if !defined(NEBULA_DEV)
inline const char* audio_wav_dump_only() noexcept { return nullptr; }
#else
inline const char* audio_wav_dump_only() noexcept { return std::getenv("GALAXY_AUDIO_WAV_DUMP_ONLY"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* audio_test_device_cycle() noexcept { return nullptr; }
#else
inline const char* audio_test_device_cycle() noexcept { return std::getenv("GALAXY_AUDIO_TEST_DEVICE_CYCLE"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* audio_test_reopen() noexcept { return nullptr; }
#else
inline const char* audio_test_reopen() noexcept { return std::getenv("GALAXY_AUDIO_TEST_REOPEN"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* audio_test_gaps() noexcept { return nullptr; }
#else
inline const char* audio_test_gaps() noexcept { return std::getenv("GALAXY_AUDIO_TEST_GAPS"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* audio_wav_dump_seconds() noexcept { return nullptr; }
#else
inline const char* audio_wav_dump_seconds() noexcept { return std::getenv("GALAXY_AUDIO_WAV_DUMP_SECONDS"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* boot_fast() noexcept { return nullptr; }
#else
inline const char* boot_fast() noexcept { return std::getenv("GALAXY_BOOT_FAST"); }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_autopress_script_name() noexcept { return ""; }
#else
inline constexpr const char* input_autopress_script_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_SCRIPT"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_pointer2_duration_name() noexcept { return ""; }
#else
inline constexpr const char* input_pointer2_duration_name() noexcept { return "GALAXY_INPUT_POINTER2_DURATION"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_pointer2_frame_name() noexcept { return ""; }
#else
inline constexpr const char* input_pointer2_frame_name() noexcept { return "GALAXY_INPUT_POINTER2_FRAME"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_pointer2_x_name() noexcept { return ""; }
#else
inline constexpr const char* input_pointer2_x_name() noexcept { return "GALAXY_INPUT_POINTER2_X"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_pointer2_y_name() noexcept { return ""; }
#else
inline constexpr const char* input_pointer2_y_name() noexcept { return "GALAXY_INPUT_POINTER2_Y"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_pointer_path_name() noexcept { return ""; }
#else
inline constexpr const char* input_pointer_path_name() noexcept { return "GALAXY_INPUT_POINTER_PATH"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_pointer_x_name() noexcept { return ""; }
#else
inline constexpr const char* input_pointer_x_name() noexcept { return "GALAXY_INPUT_POINTER_X"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_pointer_y_name() noexcept { return ""; }
#else
inline constexpr const char* input_pointer_y_name() noexcept { return "GALAXY_INPUT_POINTER_Y"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_replay_log_name() noexcept { return ""; }
#else
inline constexpr const char* input_replay_log_name() noexcept { return "GALAXY_INPUT_REPLAY_LOG"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_shake_script_name() noexcept { return ""; }
#else
inline constexpr const char* input_shake_script_name() noexcept { return "GALAXY_INPUT_SHAKE_SCRIPT"; }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* input_stick_script_name() noexcept { return ""; }
#else
inline constexpr const char* input_stick_script_name() noexcept { return "GALAXY_INPUT_STICK_SCRIPT"; }
#endif
#if !defined(NEBULA_DEV)
inline const char* present_snapshot_dir() noexcept { return nullptr; }
#else
inline const char* present_snapshot_dir() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_DIR"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* present_snapshot_every() noexcept { return nullptr; }
#else
inline const char* present_snapshot_every() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_EVERY"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* present_snapshot_first() noexcept { return nullptr; }
#else
inline const char* present_snapshot_first() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_FIRST"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* present_snapshot_last() noexcept { return nullptr; }
#else
inline const char* present_snapshot_last() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_LAST"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* present_snapshot_game_only() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_game_only_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_game_only_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_game_only() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_GAME_ONLY"); }
inline constexpr const char* present_snapshot_game_only_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_GAME_ONLY"; }
inline constexpr const wchar_t* present_snapshot_game_only_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_GAME_ONLY"; }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_() noexcept { return nullptr; }
inline constexpr const wchar_t* test__wide_name() noexcept { return L""; }
#else
inline const char* test_() noexcept { return std::getenv("GALAXY_TEST_"); }
inline constexpr const wchar_t* test__wide_name() noexcept { return L"GALAXY_TEST_"; }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_deterministic() noexcept { return nullptr; }
#else
inline const char* test_deterministic() noexcept { return std::getenv("GALAXY_TEST_DETERMINISTIC"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_disc_delay_us() noexcept { return nullptr; }
#else
inline const char* test_disc_delay_us() noexcept { return std::getenv("GALAXY_TEST_DISC_DELAY_US"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_events() noexcept { return nullptr; }
#else
inline const char* test_events() noexcept { return std::getenv("GALAXY_TEST_EVENTS"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_event_flags() noexcept { return nullptr; }
#else
inline const char* test_event_flags() noexcept { return std::getenv("GALAXY_TEST_EVENT_FLAGS"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* present_snapshot_by_stop() noexcept { return nullptr; }
#else
inline const char* present_snapshot_by_stop() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_BY_STOP"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_galaxy_tour() noexcept { return nullptr; }
#else
inline const char* test_galaxy_tour() noexcept { return std::getenv("GALAXY_TEST_GALAXY_TOUR"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_galaxy_tour_frames() noexcept { return nullptr; }
#else
inline const char* test_galaxy_tour_frames() noexcept { return std::getenv("GALAXY_TEST_GALAXY_TOUR_FRAMES"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_game_stall_ms() noexcept { return nullptr; }
#else
inline const char* test_game_stall_ms() noexcept { return std::getenv("GALAXY_TEST_GAME_STALL_MS"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_device_lost() noexcept { return nullptr; }
inline constexpr const char* test_device_lost_name() noexcept { return ""; }
inline constexpr const wchar_t* test_device_lost_wide_name() noexcept { return L""; }
#else
inline const char* test_device_lost() noexcept { return std::getenv("GALAXY_TEST_DEVICE_LOST"); }
inline constexpr const char* test_device_lost_name() noexcept { return "GALAXY_TEST_DEVICE_LOST"; }
inline constexpr const wchar_t* test_device_lost_wide_name() noexcept { return L"GALAXY_TEST_DEVICE_LOST"; }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_vram_budget_mb() noexcept { return nullptr; }
inline constexpr const char* test_vram_budget_mb_name() noexcept { return ""; }
inline constexpr const wchar_t* test_vram_budget_mb_wide_name() noexcept { return L""; }
#else
inline const char* test_vram_budget_mb() noexcept { return std::getenv("GALAXY_TEST_VRAM_BUDGET_MB"); }
inline constexpr const char* test_vram_budget_mb_name() noexcept { return "GALAXY_TEST_VRAM_BUDGET_MB"; }
inline constexpr const wchar_t* test_vram_budget_mb_wide_name() noexcept { return L"GALAXY_TEST_VRAM_BUDGET_MB"; }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_gpu_lost_frame() noexcept { return nullptr; }
#else
inline const char* test_gpu_lost_frame() noexcept { return std::getenv("GALAXY_TEST_GPU_LOST_FRAME"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_gpu_lost_times() noexcept { return nullptr; }
#else
inline const char* test_gpu_lost_times() noexcept { return std::getenv("GALAXY_TEST_GPU_LOST_TIMES"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_grand_stars() noexcept { return nullptr; }
#else
inline const char* test_grand_stars() noexcept { return std::getenv("GALAXY_TEST_GRAND_STARS"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_home_row() noexcept { return nullptr; }
#else
inline const char* test_home_row() noexcept { return std::getenv("GALAXY_TEST_HOME_ROW"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_script_clock() noexcept { return nullptr; }
#else
inline const char* test_script_clock() noexcept { return std::getenv("GALAXY_TEST_SCRIPT_CLOCK"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_settings_keys() noexcept { return nullptr; }
#else
inline const char* test_settings_keys() noexcept { return std::getenv("GALAXY_TEST_SETTINGS_KEYS"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_settings_open() noexcept { return nullptr; }
#else
inline const char* test_settings_open() noexcept { return std::getenv("GALAXY_TEST_SETTINGS_OPEN"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_teleport() noexcept { return nullptr; }
#else
inline const char* test_teleport() noexcept { return std::getenv("GALAXY_TEST_TELEPORT"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_walk() noexcept { return nullptr; }
#else
inline const char* test_walk() noexcept { return std::getenv("GALAXY_TEST_WALK"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_window_sizes() noexcept { return nullptr; }
#else
inline const char* test_window_sizes() noexcept { return std::getenv("GALAXY_TEST_WINDOW_SIZES"); }
#endif
#if !defined(NEBULA_DEV)
inline const char* test_mods_screen() noexcept { return nullptr; }
inline const char* test_mods_changed() noexcept { return nullptr; }
#else
inline const char* test_mods_screen() noexcept { return std::getenv("GALAXY_TEST_MODS_SCREEN"); }
inline const char* test_mods_changed() noexcept { return std::getenv("GALAXY_TEST_MODS_CHANGED"); }
#endif
#if !defined(NEBULA_DEV)
inline constexpr const char* test_path_guard_name() noexcept { return ""; }
#else
inline constexpr const char* test_path_guard_name() noexcept { return "GALAXY_TEST_PATH_GUARD"; }
#endif
#if !defined(NEBULA_DEV)
inline const char* rt_log_glows() noexcept { return nullptr; }
#else
inline const char* rt_log_glows() noexcept { return std::getenv("GALAXY_RT_LOG_GLOWS"); }
#endif
}  // namespace nebula_switch

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
