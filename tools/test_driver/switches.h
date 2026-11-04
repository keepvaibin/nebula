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
#if defined(NEBULA_RELEASE)
inline const char* frame_timeline() noexcept { return nullptr; }
inline constexpr const char* frame_timeline_name() noexcept { return ""; }
inline constexpr const wchar_t* frame_timeline_wide_name() noexcept { return L""; }
#else
inline const char* frame_timeline() noexcept { return std::getenv("GALAXY_FRAME_TIMELINE"); }
inline constexpr const char* frame_timeline_name() noexcept { return "GALAXY_FRAME_TIMELINE"; }
inline constexpr const wchar_t* frame_timeline_wide_name() noexcept { return L"GALAXY_FRAME_TIMELINE"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* frame_timeline_events() noexcept { return nullptr; }
inline constexpr const char* frame_timeline_events_name() noexcept { return ""; }
inline constexpr const wchar_t* frame_timeline_events_wide_name() noexcept { return L""; }
#else
inline const char* frame_timeline_events() noexcept { return std::getenv("GALAXY_FRAME_TIMELINE_EVENTS"); }
inline constexpr const char* frame_timeline_events_name() noexcept { return "GALAXY_FRAME_TIMELINE_EVENTS"; }
inline constexpr const wchar_t* frame_timeline_events_wide_name() noexcept { return L"GALAXY_FRAME_TIMELINE_EVENTS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* monitor_frame_tails() noexcept { return nullptr; }
inline constexpr const char* monitor_frame_tails_name() noexcept { return ""; }
inline constexpr const wchar_t* monitor_frame_tails_wide_name() noexcept { return L""; }
#else
inline const char* monitor_frame_tails() noexcept { return std::getenv("GALAXY_MONITOR_FRAME_TAILS"); }
inline constexpr const char* monitor_frame_tails_name() noexcept { return "GALAXY_MONITOR_FRAME_TAILS"; }
inline constexpr const wchar_t* monitor_frame_tails_wide_name() noexcept { return L"GALAXY_MONITOR_FRAME_TAILS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* monitor_pointer_latency() noexcept { return nullptr; }
inline constexpr const char* monitor_pointer_latency_name() noexcept { return ""; }
inline constexpr const wchar_t* monitor_pointer_latency_wide_name() noexcept { return L""; }
#else
inline const char* monitor_pointer_latency() noexcept { return std::getenv("GALAXY_MONITOR_POINTER_LATENCY"); }
inline constexpr const char* monitor_pointer_latency_name() noexcept { return "GALAXY_MONITOR_POINTER_LATENCY"; }
inline constexpr const wchar_t* monitor_pointer_latency_wide_name() noexcept { return L"GALAXY_MONITOR_POINTER_LATENCY"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* frame_timeline_path() noexcept { return nullptr; }
inline constexpr const char* frame_timeline_path_name() noexcept { return ""; }
inline constexpr const wchar_t* frame_timeline_path_wide_name() noexcept { return L""; }
#else
inline const char* frame_timeline_path() noexcept { return std::getenv("GALAXY_FRAME_TIMELINE_PATH"); }
inline constexpr const char* frame_timeline_path_name() noexcept { return "GALAXY_FRAME_TIMELINE_PATH"; }
inline constexpr const wchar_t* frame_timeline_path_wide_name() noexcept { return L"GALAXY_FRAME_TIMELINE_PATH"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* audio_wav_dump() noexcept { return nullptr; }
inline constexpr const char* audio_wav_dump_name() noexcept { return ""; }
inline constexpr const wchar_t* audio_wav_dump_wide_name() noexcept { return L""; }
#else
inline const char* audio_wav_dump() noexcept { return std::getenv("GALAXY_AUDIO_WAV_DUMP"); }
inline constexpr const char* audio_wav_dump_name() noexcept { return "GALAXY_AUDIO_WAV_DUMP"; }
inline constexpr const wchar_t* audio_wav_dump_wide_name() noexcept { return L"GALAXY_AUDIO_WAV_DUMP"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* audio_wav_dump_only() noexcept { return nullptr; }
inline constexpr const char* audio_wav_dump_only_name() noexcept { return ""; }
inline constexpr const wchar_t* audio_wav_dump_only_wide_name() noexcept { return L""; }
#else
inline const char* audio_wav_dump_only() noexcept { return std::getenv("GALAXY_AUDIO_WAV_DUMP_ONLY"); }
inline constexpr const char* audio_wav_dump_only_name() noexcept { return "GALAXY_AUDIO_WAV_DUMP_ONLY"; }
inline constexpr const wchar_t* audio_wav_dump_only_wide_name() noexcept { return L"GALAXY_AUDIO_WAV_DUMP_ONLY"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* audio_test_device_cycle() noexcept { return nullptr; }
inline constexpr const char* audio_test_device_cycle_name() noexcept { return ""; }
inline constexpr const wchar_t* audio_test_device_cycle_wide_name() noexcept { return L""; }
#else
inline const char* audio_test_device_cycle() noexcept { return std::getenv("GALAXY_AUDIO_TEST_DEVICE_CYCLE"); }
inline constexpr const char* audio_test_device_cycle_name() noexcept { return "GALAXY_AUDIO_TEST_DEVICE_CYCLE"; }
inline constexpr const wchar_t* audio_test_device_cycle_wide_name() noexcept { return L"GALAXY_AUDIO_TEST_DEVICE_CYCLE"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* audio_test_reopen() noexcept { return nullptr; }
inline constexpr const char* audio_test_reopen_name() noexcept { return ""; }
inline constexpr const wchar_t* audio_test_reopen_wide_name() noexcept { return L""; }
#else
inline const char* audio_test_reopen() noexcept { return std::getenv("GALAXY_AUDIO_TEST_REOPEN"); }
inline constexpr const char* audio_test_reopen_name() noexcept { return "GALAXY_AUDIO_TEST_REOPEN"; }
inline constexpr const wchar_t* audio_test_reopen_wide_name() noexcept { return L"GALAXY_AUDIO_TEST_REOPEN"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* audio_test_gaps() noexcept { return nullptr; }
inline constexpr const char* audio_test_gaps_name() noexcept { return ""; }
inline constexpr const wchar_t* audio_test_gaps_wide_name() noexcept { return L""; }
#else
inline const char* audio_test_gaps() noexcept { return std::getenv("GALAXY_AUDIO_TEST_GAPS"); }
inline constexpr const char* audio_test_gaps_name() noexcept { return "GALAXY_AUDIO_TEST_GAPS"; }
inline constexpr const wchar_t* audio_test_gaps_wide_name() noexcept { return L"GALAXY_AUDIO_TEST_GAPS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* audio_wav_dump_seconds() noexcept { return nullptr; }
inline constexpr const char* audio_wav_dump_seconds_name() noexcept { return ""; }
inline constexpr const wchar_t* audio_wav_dump_seconds_wide_name() noexcept { return L""; }
#else
inline const char* audio_wav_dump_seconds() noexcept { return std::getenv("GALAXY_AUDIO_WAV_DUMP_SECONDS"); }
inline constexpr const char* audio_wav_dump_seconds_name() noexcept { return "GALAXY_AUDIO_WAV_DUMP_SECONDS"; }
inline constexpr const wchar_t* audio_wav_dump_seconds_wide_name() noexcept { return L"GALAXY_AUDIO_WAV_DUMP_SECONDS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* boot_fast() noexcept { return nullptr; }
inline constexpr const char* boot_fast_name() noexcept { return ""; }
inline constexpr const wchar_t* boot_fast_wide_name() noexcept { return L""; }
#else
inline const char* boot_fast() noexcept { return std::getenv("GALAXY_BOOT_FAST"); }
inline constexpr const char* boot_fast_name() noexcept { return "GALAXY_BOOT_FAST"; }
inline constexpr const wchar_t* boot_fast_wide_name() noexcept { return L"GALAXY_BOOT_FAST"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* crash_dump_dir() noexcept { return nullptr; }
inline constexpr const char* crash_dump_dir_name() noexcept { return ""; }
inline constexpr const wchar_t* crash_dump_dir_wide_name() noexcept { return L""; }
#else
inline const char* crash_dump_dir() noexcept { return std::getenv("GALAXY_CRASH_DUMP_DIR"); }
inline constexpr const char* crash_dump_dir_name() noexcept { return "GALAXY_CRASH_DUMP_DIR"; }
inline constexpr const wchar_t* crash_dump_dir_wide_name() noexcept { return L"GALAXY_CRASH_DUMP_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_window_off_desktop() noexcept { return nullptr; }
inline constexpr const char* test_window_off_desktop_name() noexcept { return ""; }
inline constexpr const wchar_t* test_window_off_desktop_wide_name() noexcept { return L""; }
#else
inline const char* test_window_off_desktop() noexcept { return std::getenv("GALAXY_TEST_WINDOW_OFF_DESKTOP"); }
inline constexpr const char* test_window_off_desktop_name() noexcept { return "GALAXY_TEST_WINDOW_OFF_DESKTOP"; }
inline constexpr const wchar_t* test_window_off_desktop_wide_name() noexcept { return L"GALAXY_TEST_WINDOW_OFF_DESKTOP"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const wchar_t* xess_dir_wide_name() noexcept { return L""; }
inline constexpr const wchar_t* streamline_dir_wide_name() noexcept { return L""; }
#else
inline constexpr const wchar_t* xess_dir_wide_name() noexcept { return L"GALAXY_XESS_DIR"; }
inline constexpr const wchar_t* streamline_dir_wide_name() noexcept { return L"GALAXY_STREAMLINE_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* hide_window() noexcept { return nullptr; }
inline constexpr const char* hide_window_name() noexcept { return ""; }
inline constexpr const wchar_t* hide_window_wide_name() noexcept { return L""; }
#else
inline const char* hide_window() noexcept { return std::getenv("GALAXY_HIDE_WINDOW"); }
inline constexpr const char* hide_window_name() noexcept { return "GALAXY_HIDE_WINDOW"; }
inline constexpr const wchar_t* hide_window_wide_name() noexcept { return L"GALAXY_HIDE_WINDOW"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_autopress_script() noexcept { return nullptr; }
inline constexpr const char* input_autopress_script_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_script_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_script() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_SCRIPT"); }
inline constexpr const char* input_autopress_script_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_SCRIPT"; }
inline constexpr const wchar_t* input_autopress_script_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_SCRIPT"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_mode() noexcept { return nullptr; }
inline constexpr const char* input_mode_name() noexcept { return ""; }
inline constexpr const wchar_t* input_mode_wide_name() noexcept { return L""; }
#else
inline const char* input_mode() noexcept { return std::getenv("GALAXY_INPUT_MODE"); }
inline constexpr const char* input_mode_name() noexcept { return "GALAXY_INPUT_MODE"; }
inline constexpr const wchar_t* input_mode_wide_name() noexcept { return L"GALAXY_INPUT_MODE"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_pointer2_duration() noexcept { return nullptr; }
inline constexpr const char* input_pointer2_duration_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer2_duration_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer2_duration() noexcept { return std::getenv("GALAXY_INPUT_POINTER2_DURATION"); }
inline constexpr const char* input_pointer2_duration_name() noexcept { return "GALAXY_INPUT_POINTER2_DURATION"; }
inline constexpr const wchar_t* input_pointer2_duration_wide_name() noexcept { return L"GALAXY_INPUT_POINTER2_DURATION"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_pointer2_frame() noexcept { return nullptr; }
inline constexpr const char* input_pointer2_frame_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer2_frame_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer2_frame() noexcept { return std::getenv("GALAXY_INPUT_POINTER2_FRAME"); }
inline constexpr const char* input_pointer2_frame_name() noexcept { return "GALAXY_INPUT_POINTER2_FRAME"; }
inline constexpr const wchar_t* input_pointer2_frame_wide_name() noexcept { return L"GALAXY_INPUT_POINTER2_FRAME"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_pointer2_x() noexcept { return nullptr; }
inline constexpr const char* input_pointer2_x_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer2_x_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer2_x() noexcept { return std::getenv("GALAXY_INPUT_POINTER2_X"); }
inline constexpr const char* input_pointer2_x_name() noexcept { return "GALAXY_INPUT_POINTER2_X"; }
inline constexpr const wchar_t* input_pointer2_x_wide_name() noexcept { return L"GALAXY_INPUT_POINTER2_X"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_pointer2_y() noexcept { return nullptr; }
inline constexpr const char* input_pointer2_y_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer2_y_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer2_y() noexcept { return std::getenv("GALAXY_INPUT_POINTER2_Y"); }
inline constexpr const char* input_pointer2_y_name() noexcept { return "GALAXY_INPUT_POINTER2_Y"; }
inline constexpr const wchar_t* input_pointer2_y_wide_name() noexcept { return L"GALAXY_INPUT_POINTER2_Y"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_pointer_path() noexcept { return nullptr; }
inline constexpr const char* input_pointer_path_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer_path_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer_path() noexcept { return std::getenv("GALAXY_INPUT_POINTER_PATH"); }
inline constexpr const char* input_pointer_path_name() noexcept { return "GALAXY_INPUT_POINTER_PATH"; }
inline constexpr const wchar_t* input_pointer_path_wide_name() noexcept { return L"GALAXY_INPUT_POINTER_PATH"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_pointer_x() noexcept { return nullptr; }
inline constexpr const char* input_pointer_x_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer_x_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer_x() noexcept { return std::getenv("GALAXY_INPUT_POINTER_X"); }
inline constexpr const char* input_pointer_x_name() noexcept { return "GALAXY_INPUT_POINTER_X"; }
inline constexpr const wchar_t* input_pointer_x_wide_name() noexcept { return L"GALAXY_INPUT_POINTER_X"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_pointer_y() noexcept { return nullptr; }
inline constexpr const char* input_pointer_y_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer_y_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer_y() noexcept { return std::getenv("GALAXY_INPUT_POINTER_Y"); }
inline constexpr const char* input_pointer_y_name() noexcept { return "GALAXY_INPUT_POINTER_Y"; }
inline constexpr const wchar_t* input_pointer_y_wide_name() noexcept { return L"GALAXY_INPUT_POINTER_Y"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_replay_log() noexcept { return nullptr; }
inline constexpr const char* input_replay_log_name() noexcept { return ""; }
inline constexpr const wchar_t* input_replay_log_wide_name() noexcept { return L""; }
#else
inline const char* input_replay_log() noexcept { return std::getenv("GALAXY_INPUT_REPLAY_LOG"); }
inline constexpr const char* input_replay_log_name() noexcept { return "GALAXY_INPUT_REPLAY_LOG"; }
inline constexpr const wchar_t* input_replay_log_wide_name() noexcept { return L"GALAXY_INPUT_REPLAY_LOG"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_shake_script() noexcept { return nullptr; }
inline constexpr const char* input_shake_script_name() noexcept { return ""; }
inline constexpr const wchar_t* input_shake_script_wide_name() noexcept { return L""; }
#else
inline const char* input_shake_script() noexcept { return std::getenv("GALAXY_INPUT_SHAKE_SCRIPT"); }
inline constexpr const char* input_shake_script_name() noexcept { return "GALAXY_INPUT_SHAKE_SCRIPT"; }
inline constexpr const wchar_t* input_shake_script_wide_name() noexcept { return L"GALAXY_INPUT_SHAKE_SCRIPT"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* input_stick_script() noexcept { return nullptr; }
inline constexpr const char* input_stick_script_name() noexcept { return ""; }
inline constexpr const wchar_t* input_stick_script_wide_name() noexcept { return L""; }
#else
inline const char* input_stick_script() noexcept { return std::getenv("GALAXY_INPUT_STICK_SCRIPT"); }
inline constexpr const char* input_stick_script_name() noexcept { return "GALAXY_INPUT_STICK_SCRIPT"; }
inline constexpr const wchar_t* input_stick_script_wide_name() noexcept { return L"GALAXY_INPUT_STICK_SCRIPT"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* interp_snapshot_dir() noexcept { return nullptr; }
inline constexpr const char* interp_snapshot_dir_name() noexcept { return ""; }
inline constexpr const wchar_t* interp_snapshot_dir_wide_name() noexcept { return L""; }
#else
inline const char* interp_snapshot_dir() noexcept { return std::getenv("GALAXY_INTERP_SNAPSHOT_DIR"); }
inline constexpr const char* interp_snapshot_dir_name() noexcept { return "GALAXY_INTERP_SNAPSHOT_DIR"; }
inline constexpr const wchar_t* interp_snapshot_dir_wide_name() noexcept { return L"GALAXY_INTERP_SNAPSHOT_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* interp_snapshot_first() noexcept { return nullptr; }
inline constexpr const char* interp_snapshot_first_name() noexcept { return ""; }
inline constexpr const wchar_t* interp_snapshot_first_wide_name() noexcept { return L""; }
#else
inline const char* interp_snapshot_first() noexcept { return std::getenv("GALAXY_INTERP_SNAPSHOT_FIRST"); }
inline constexpr const char* interp_snapshot_first_name() noexcept { return "GALAXY_INTERP_SNAPSHOT_FIRST"; }
inline constexpr const wchar_t* interp_snapshot_first_wide_name() noexcept { return L"GALAXY_INTERP_SNAPSHOT_FIRST"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* interp_snapshot_last() noexcept { return nullptr; }
inline constexpr const char* interp_snapshot_last_name() noexcept { return ""; }
inline constexpr const wchar_t* interp_snapshot_last_wide_name() noexcept { return L""; }
#else
inline const char* interp_snapshot_last() noexcept { return std::getenv("GALAXY_INTERP_SNAPSHOT_LAST"); }
inline constexpr const char* interp_snapshot_last_name() noexcept { return "GALAXY_INTERP_SNAPSHOT_LAST"; }
inline constexpr const wchar_t* interp_snapshot_last_wide_name() noexcept { return L"GALAXY_INTERP_SNAPSHOT_LAST"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* log_to_console() noexcept { return nullptr; }
inline constexpr const char* log_to_console_name() noexcept { return ""; }
inline constexpr const wchar_t* log_to_console_wide_name() noexcept { return L""; }
#else
inline const char* log_to_console() noexcept { return std::getenv("GALAXY_LOG_TO_CONSOLE"); }
inline constexpr const char* log_to_console_name() noexcept { return "GALAXY_LOG_TO_CONSOLE"; }
inline constexpr const wchar_t* log_to_console_wide_name() noexcept { return L"GALAXY_LOG_TO_CONSOLE"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* present_snapshot_dir() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_dir_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_dir_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_dir() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_DIR"); }
inline constexpr const char* present_snapshot_dir_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_DIR"; }
inline constexpr const wchar_t* present_snapshot_dir_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* present_snapshot_every() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_every_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_every_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_every() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_EVERY"); }
inline constexpr const char* present_snapshot_every_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_EVERY"; }
inline constexpr const wchar_t* present_snapshot_every_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_EVERY"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* present_snapshot_first() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_first_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_first_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_first() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_FIRST"); }
inline constexpr const char* present_snapshot_first_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_FIRST"; }
inline constexpr const wchar_t* present_snapshot_first_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_FIRST"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* present_snapshot_last() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_last_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_last_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_last() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_LAST"); }
inline constexpr const char* present_snapshot_last_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_LAST"; }
inline constexpr const wchar_t* present_snapshot_last_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_LAST"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* profile_game() noexcept { return nullptr; }
inline constexpr const char* profile_game_name() noexcept { return ""; }
inline constexpr const wchar_t* profile_game_wide_name() noexcept { return L""; }
#else
inline const char* profile_game() noexcept { return std::getenv("GALAXY_PROFILE_GAME"); }
inline constexpr const char* profile_game_name() noexcept { return "GALAXY_PROFILE_GAME"; }
inline constexpr const wchar_t* profile_game_wide_name() noexcept { return L"GALAXY_PROFILE_GAME"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_() noexcept { return nullptr; }
inline constexpr const char* test__name() noexcept { return ""; }
inline constexpr const wchar_t* test__wide_name() noexcept { return L""; }
#else
inline const char* test_() noexcept { return std::getenv("GALAXY_TEST_"); }
inline constexpr const char* test__name() noexcept { return "GALAXY_TEST_"; }
inline constexpr const wchar_t* test__wide_name() noexcept { return L"GALAXY_TEST_"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_arena_fill() noexcept { return nullptr; }
inline constexpr const char* test_arena_fill_name() noexcept { return ""; }
inline constexpr const wchar_t* test_arena_fill_wide_name() noexcept { return L""; }
#else
inline const char* test_arena_fill() noexcept { return std::getenv("GALAXY_TEST_ARENA_FILL"); }
inline constexpr const char* test_arena_fill_name() noexcept { return "GALAXY_TEST_ARENA_FILL"; }
inline constexpr const wchar_t* test_arena_fill_wide_name() noexcept { return L"GALAXY_TEST_ARENA_FILL"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_cpu_affinity() noexcept { return nullptr; }
inline constexpr const char* test_cpu_affinity_name() noexcept { return ""; }
inline constexpr const wchar_t* test_cpu_affinity_wide_name() noexcept { return L""; }
#else
inline const char* test_cpu_affinity() noexcept { return std::getenv("GALAXY_TEST_CPU_AFFINITY"); }
inline constexpr const char* test_cpu_affinity_name() noexcept { return "GALAXY_TEST_CPU_AFFINITY"; }
inline constexpr const wchar_t* test_cpu_affinity_wide_name() noexcept { return L"GALAXY_TEST_CPU_AFFINITY"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_deterministic() noexcept { return nullptr; }
inline constexpr const char* test_deterministic_name() noexcept { return ""; }
inline constexpr const wchar_t* test_deterministic_wide_name() noexcept { return L""; }
#else
inline const char* test_deterministic() noexcept { return std::getenv("GALAXY_TEST_DETERMINISTIC"); }
inline constexpr const char* test_deterministic_name() noexcept { return "GALAXY_TEST_DETERMINISTIC"; }
inline constexpr const wchar_t* test_deterministic_wide_name() noexcept { return L"GALAXY_TEST_DETERMINISTIC"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_disc_delay_us() noexcept { return nullptr; }
inline constexpr const char* test_disc_delay_us_name() noexcept { return ""; }
inline constexpr const wchar_t* test_disc_delay_us_wide_name() noexcept { return L""; }
#else
inline const char* test_disc_delay_us() noexcept { return std::getenv("GALAXY_TEST_DISC_DELAY_US"); }
inline constexpr const char* test_disc_delay_us_name() noexcept { return "GALAXY_TEST_DISC_DELAY_US"; }
inline constexpr const wchar_t* test_disc_delay_us_wide_name() noexcept { return L"GALAXY_TEST_DISC_DELAY_US"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_events() noexcept { return nullptr; }
inline constexpr const char* test_events_name() noexcept { return ""; }
inline constexpr const wchar_t* test_events_wide_name() noexcept { return L""; }
#else
inline const char* test_events() noexcept { return std::getenv("GALAXY_TEST_EVENTS"); }
inline constexpr const char* test_events_name() noexcept { return "GALAXY_TEST_EVENTS"; }
inline constexpr const wchar_t* test_events_wide_name() noexcept { return L"GALAXY_TEST_EVENTS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_event_flags() noexcept { return nullptr; }
inline constexpr const char* test_event_flags_name() noexcept { return ""; }
inline constexpr const wchar_t* test_event_flags_wide_name() noexcept { return L""; }
#else
inline const char* test_event_flags() noexcept { return std::getenv("GALAXY_TEST_EVENT_FLAGS"); }
inline constexpr const char* test_event_flags_name() noexcept { return "GALAXY_TEST_EVENT_FLAGS"; }
inline constexpr const wchar_t* test_event_flags_wide_name() noexcept { return L"GALAXY_TEST_EVENT_FLAGS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* present_snapshot_by_stop() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_by_stop_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_by_stop_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_by_stop() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_BY_STOP"); }
inline constexpr const char* present_snapshot_by_stop_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_BY_STOP"; }
inline constexpr const wchar_t* present_snapshot_by_stop_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_BY_STOP"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_galaxy_tour() noexcept { return nullptr; }
inline constexpr const char* test_galaxy_tour_name() noexcept { return ""; }
inline constexpr const wchar_t* test_galaxy_tour_wide_name() noexcept { return L""; }
#else
inline const char* test_galaxy_tour() noexcept { return std::getenv("GALAXY_TEST_GALAXY_TOUR"); }
inline constexpr const char* test_galaxy_tour_name() noexcept { return "GALAXY_TEST_GALAXY_TOUR"; }
inline constexpr const wchar_t* test_galaxy_tour_wide_name() noexcept { return L"GALAXY_TEST_GALAXY_TOUR"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_galaxy_tour_frames() noexcept { return nullptr; }
inline constexpr const char* test_galaxy_tour_frames_name() noexcept { return ""; }
inline constexpr const wchar_t* test_galaxy_tour_frames_wide_name() noexcept { return L""; }
#else
inline const char* test_galaxy_tour_frames() noexcept { return std::getenv("GALAXY_TEST_GALAXY_TOUR_FRAMES"); }
inline constexpr const char* test_galaxy_tour_frames_name() noexcept { return "GALAXY_TEST_GALAXY_TOUR_FRAMES"; }
inline constexpr const wchar_t* test_galaxy_tour_frames_wide_name() noexcept { return L"GALAXY_TEST_GALAXY_TOUR_FRAMES"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_game_stall_ms() noexcept { return nullptr; }
inline constexpr const char* test_game_stall_ms_name() noexcept { return ""; }
inline constexpr const wchar_t* test_game_stall_ms_wide_name() noexcept { return L""; }
#else
inline const char* test_game_stall_ms() noexcept { return std::getenv("GALAXY_TEST_GAME_STALL_MS"); }
inline constexpr const char* test_game_stall_ms_name() noexcept { return "GALAXY_TEST_GAME_STALL_MS"; }
inline constexpr const wchar_t* test_game_stall_ms_wide_name() noexcept { return L"GALAXY_TEST_GAME_STALL_MS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_gpu_lost_frame() noexcept { return nullptr; }
inline constexpr const char* test_gpu_lost_frame_name() noexcept { return ""; }
inline constexpr const wchar_t* test_gpu_lost_frame_wide_name() noexcept { return L""; }
#else
inline const char* test_gpu_lost_frame() noexcept { return std::getenv("GALAXY_TEST_GPU_LOST_FRAME"); }
inline constexpr const char* test_gpu_lost_frame_name() noexcept { return "GALAXY_TEST_GPU_LOST_FRAME"; }
inline constexpr const wchar_t* test_gpu_lost_frame_wide_name() noexcept { return L"GALAXY_TEST_GPU_LOST_FRAME"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_gpu_lost_times() noexcept { return nullptr; }
inline constexpr const char* test_gpu_lost_times_name() noexcept { return ""; }
inline constexpr const wchar_t* test_gpu_lost_times_wide_name() noexcept { return L""; }
#else
inline const char* test_gpu_lost_times() noexcept { return std::getenv("GALAXY_TEST_GPU_LOST_TIMES"); }
inline constexpr const char* test_gpu_lost_times_name() noexcept { return "GALAXY_TEST_GPU_LOST_TIMES"; }
inline constexpr const wchar_t* test_gpu_lost_times_wide_name() noexcept { return L"GALAXY_TEST_GPU_LOST_TIMES"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_grand_stars() noexcept { return nullptr; }
inline constexpr const char* test_grand_stars_name() noexcept { return ""; }
inline constexpr const wchar_t* test_grand_stars_wide_name() noexcept { return L""; }
#else
inline const char* test_grand_stars() noexcept { return std::getenv("GALAXY_TEST_GRAND_STARS"); }
inline constexpr const char* test_grand_stars_name() noexcept { return "GALAXY_TEST_GRAND_STARS"; }
inline constexpr const wchar_t* test_grand_stars_wide_name() noexcept { return L"GALAXY_TEST_GRAND_STARS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_home_row() noexcept { return nullptr; }
inline constexpr const char* test_home_row_name() noexcept { return ""; }
inline constexpr const wchar_t* test_home_row_wide_name() noexcept { return L""; }
#else
inline const char* test_home_row() noexcept { return std::getenv("GALAXY_TEST_HOME_ROW"); }
inline constexpr const char* test_home_row_name() noexcept { return "GALAXY_TEST_HOME_ROW"; }
inline constexpr const wchar_t* test_home_row_wide_name() noexcept { return L"GALAXY_TEST_HOME_ROW"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_joycon() noexcept { return nullptr; }
inline constexpr const char* test_joycon_name() noexcept { return ""; }
inline constexpr const wchar_t* test_joycon_wide_name() noexcept { return L""; }
#else
inline const char* test_joycon() noexcept { return std::getenv("GALAXY_TEST_JOYCON"); }
inline constexpr const char* test_joycon_name() noexcept { return "GALAXY_TEST_JOYCON"; }
inline constexpr const wchar_t* test_joycon_wide_name() noexcept { return L"GALAXY_TEST_JOYCON"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_memory_at() noexcept { return nullptr; }
inline constexpr const char* test_memory_at_name() noexcept { return ""; }
inline constexpr const wchar_t* test_memory_at_wide_name() noexcept { return L""; }
#else
inline const char* test_memory_at() noexcept { return std::getenv("GALAXY_TEST_MEMORY_AT"); }
inline constexpr const char* test_memory_at_name() noexcept { return "GALAXY_TEST_MEMORY_AT"; }
inline constexpr const wchar_t* test_memory_at_wide_name() noexcept { return L"GALAXY_TEST_MEMORY_AT"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_one_game() noexcept { return nullptr; }
inline constexpr const char* test_one_game_name() noexcept { return ""; }
inline constexpr const wchar_t* test_one_game_wide_name() noexcept { return L""; }
#else
inline const char* test_one_game() noexcept { return std::getenv("GALAXY_TEST_ONE_GAME"); }
inline constexpr const char* test_one_game_name() noexcept { return "GALAXY_TEST_ONE_GAME"; }
inline constexpr const wchar_t* test_one_game_wide_name() noexcept { return L"GALAXY_TEST_ONE_GAME"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_post_fault() noexcept { return nullptr; }
inline constexpr const char* test_post_fault_name() noexcept { return ""; }
inline constexpr const wchar_t* test_post_fault_wide_name() noexcept { return L""; }
#else
inline const char* test_post_fault() noexcept { return std::getenv("GALAXY_TEST_POST_FAULT"); }
inline constexpr const char* test_post_fault_name() noexcept { return "GALAXY_TEST_POST_FAULT"; }
inline constexpr const wchar_t* test_post_fault_wide_name() noexcept { return L"GALAXY_TEST_POST_FAULT"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_rt_effect_ab() noexcept { return nullptr; }
inline constexpr const char* test_rt_effect_ab_name() noexcept { return ""; }
inline constexpr const wchar_t* test_rt_effect_ab_wide_name() noexcept { return L""; }
#else
inline const char* test_rt_effect_ab() noexcept { return std::getenv("GALAXY_TEST_RT_EFFECT_AB"); }
inline constexpr const char* test_rt_effect_ab_name() noexcept { return "GALAXY_TEST_RT_EFFECT_AB"; }
inline constexpr const wchar_t* test_rt_effect_ab_wide_name() noexcept { return L"GALAXY_TEST_RT_EFFECT_AB"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_script_clock() noexcept { return nullptr; }
inline constexpr const char* test_script_clock_name() noexcept { return ""; }
inline constexpr const wchar_t* test_script_clock_wide_name() noexcept { return L""; }
#else
inline const char* test_script_clock() noexcept { return std::getenv("GALAXY_TEST_SCRIPT_CLOCK"); }
inline constexpr const char* test_script_clock_name() noexcept { return "GALAXY_TEST_SCRIPT_CLOCK"; }
inline constexpr const wchar_t* test_script_clock_wide_name() noexcept { return L"GALAXY_TEST_SCRIPT_CLOCK"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_settings_keys() noexcept { return nullptr; }
inline constexpr const char* test_settings_keys_name() noexcept { return ""; }
inline constexpr const wchar_t* test_settings_keys_wide_name() noexcept { return L""; }
#else
inline const char* test_settings_keys() noexcept { return std::getenv("GALAXY_TEST_SETTINGS_KEYS"); }
inline constexpr const char* test_settings_keys_name() noexcept { return "GALAXY_TEST_SETTINGS_KEYS"; }
inline constexpr const wchar_t* test_settings_keys_wide_name() noexcept { return L"GALAXY_TEST_SETTINGS_KEYS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_motion_dump() noexcept { return nullptr; }
inline constexpr const char* test_motion_dump_name() noexcept { return ""; }
inline constexpr const wchar_t* test_motion_dump_wide_name() noexcept { return L""; }
#else
inline const char* test_motion_dump() noexcept { return std::getenv("GALAXY_TEST_MOTION_DUMP"); }
inline constexpr const char* test_motion_dump_name() noexcept { return "GALAXY_TEST_MOTION_DUMP"; }
inline constexpr const wchar_t* test_motion_dump_wide_name() noexcept { return L"GALAXY_TEST_MOTION_DUMP"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_settings_open() noexcept { return nullptr; }
inline constexpr const char* test_settings_open_name() noexcept { return ""; }
inline constexpr const wchar_t* test_settings_open_wide_name() noexcept { return L""; }
#else
inline const char* test_settings_open() noexcept { return std::getenv("GALAXY_TEST_SETTINGS_OPEN"); }
inline constexpr const char* test_settings_open_name() noexcept { return "GALAXY_TEST_SETTINGS_OPEN"; }
inline constexpr const wchar_t* test_settings_open_wide_name() noexcept { return L"GALAXY_TEST_SETTINGS_OPEN"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_shown_window() noexcept { return nullptr; }
inline constexpr const char* test_shown_window_name() noexcept { return ""; }
inline constexpr const wchar_t* test_shown_window_wide_name() noexcept { return L""; }
#else
inline const char* test_shown_window() noexcept { return std::getenv("GALAXY_TEST_SHOWN_WINDOW"); }
inline constexpr const char* test_shown_window_name() noexcept { return "GALAXY_TEST_SHOWN_WINDOW"; }
inline constexpr const wchar_t* test_shown_window_wide_name() noexcept { return L"GALAXY_TEST_SHOWN_WINDOW"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_teleport() noexcept { return nullptr; }
inline constexpr const char* test_teleport_name() noexcept { return ""; }
inline constexpr const wchar_t* test_teleport_wide_name() noexcept { return L""; }
#else
inline const char* test_teleport() noexcept { return std::getenv("GALAXY_TEST_TELEPORT"); }
inline constexpr const char* test_teleport_name() noexcept { return "GALAXY_TEST_TELEPORT"; }
inline constexpr const wchar_t* test_teleport_wide_name() noexcept { return L"GALAXY_TEST_TELEPORT"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_walk() noexcept { return nullptr; }
inline constexpr const char* test_walk_name() noexcept { return ""; }
inline constexpr const wchar_t* test_walk_wide_name() noexcept { return L""; }
#else
inline const char* test_walk() noexcept { return std::getenv("GALAXY_TEST_WALK"); }
inline constexpr const char* test_walk_name() noexcept { return "GALAXY_TEST_WALK"; }
inline constexpr const wchar_t* test_walk_wide_name() noexcept { return L"GALAXY_TEST_WALK"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_window_sizes() noexcept { return nullptr; }
inline constexpr const char* test_window_sizes_name() noexcept { return ""; }
inline constexpr const wchar_t* test_window_sizes_wide_name() noexcept { return L""; }
#else
inline const char* test_window_sizes() noexcept { return std::getenv("GALAXY_TEST_WINDOW_SIZES"); }
inline constexpr const char* test_window_sizes_name() noexcept { return "GALAXY_TEST_WINDOW_SIZES"; }
inline constexpr const wchar_t* test_window_sizes_wide_name() noexcept { return L"GALAXY_TEST_WINDOW_SIZES"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* trace_file_reads() noexcept { return nullptr; }
inline constexpr const char* trace_file_reads_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_file_reads_wide_name() noexcept { return L""; }
#else
inline const char* trace_file_reads() noexcept { return std::getenv("GALAXY_TRACE_FILE_READS"); }
inline constexpr const char* trace_file_reads_name() noexcept { return "GALAXY_TRACE_FILE_READS"; }
inline constexpr const wchar_t* trace_file_reads_wide_name() noexcept { return L"GALAXY_TRACE_FILE_READS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* test_mods_screen() noexcept { return nullptr; }
inline const char* test_mods_changed() noexcept { return nullptr; }
inline const char* trace_loads() noexcept { return nullptr; }
inline constexpr const char* trace_loads_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_loads_wide_name() noexcept { return L""; }
#else
inline const char* test_mods_screen() noexcept { return std::getenv("GALAXY_TEST_MODS_SCREEN"); }
inline const char* test_mods_changed() noexcept { return std::getenv("GALAXY_TEST_MODS_CHANGED"); }
inline const char* trace_loads() noexcept { return std::getenv("GALAXY_TRACE_LOADS"); }
inline constexpr const char* trace_loads_name() noexcept { return "GALAXY_TRACE_LOADS"; }
inline constexpr const wchar_t* trace_loads_wide_name() noexcept { return L"GALAXY_TRACE_LOADS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* trace_memory() noexcept { return nullptr; }
inline constexpr const char* trace_memory_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_memory_wide_name() noexcept { return L""; }
#else
inline const char* trace_memory() noexcept { return std::getenv("GALAXY_TRACE_MEMORY"); }
inline constexpr const char* trace_memory_name() noexcept { return "GALAXY_TRACE_MEMORY"; }
inline constexpr const wchar_t* trace_memory_wide_name() noexcept { return L"GALAXY_TRACE_MEMORY"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* trace_present_stats() noexcept { return nullptr; }
inline constexpr const char* trace_present_stats_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_present_stats_wide_name() noexcept { return L""; }
#else
inline const char* trace_present_stats() noexcept { return std::getenv("GALAXY_TRACE_PRESENT_STATS"); }
inline constexpr const char* trace_present_stats_name() noexcept { return "GALAXY_TRACE_PRESENT_STATS"; }
inline constexpr const wchar_t* trace_present_stats_wide_name() noexcept { return L"GALAXY_TRACE_PRESENT_STATS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* trace_thread_cpu() noexcept { return nullptr; }
inline constexpr const char* trace_thread_cpu_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_thread_cpu_wide_name() noexcept { return L""; }
#else
inline const char* trace_thread_cpu() noexcept { return std::getenv("GALAXY_TRACE_THREAD_CPU"); }
inline constexpr const char* trace_thread_cpu_name() noexcept { return "GALAXY_TRACE_THREAD_CPU"; }
inline constexpr const wchar_t* trace_thread_cpu_wide_name() noexcept { return L"GALAXY_TRACE_THREAD_CPU"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* trace_frame_times() noexcept { return nullptr; }
inline constexpr const char* trace_frame_times_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_frame_times_wide_name() noexcept { return L""; }
#else
inline const char* trace_frame_times() noexcept { return std::getenv("GALAXY_TRACE_FRAME_TIMES"); }
inline constexpr const char* trace_frame_times_name() noexcept { return "GALAXY_TRACE_FRAME_TIMES"; }
inline constexpr const wchar_t* trace_frame_times_wide_name() noexcept { return L"GALAXY_TRACE_FRAME_TIMES"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* vk_simulate_driver() noexcept { return nullptr; }
inline constexpr const char* vk_simulate_driver_name() noexcept { return ""; }
inline constexpr const wchar_t* vk_simulate_driver_wide_name() noexcept { return L""; }
#else
inline const char* vk_simulate_driver() noexcept { return std::getenv("GALAXY_VK_SIMULATE_DRIVER"); }
inline constexpr const char* vk_simulate_driver_name() noexcept { return "GALAXY_VK_SIMULATE_DRIVER"; }
inline constexpr const wchar_t* vk_simulate_driver_wide_name() noexcept { return L"GALAXY_VK_SIMULATE_DRIVER"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* trace_interp_slots() noexcept { return nullptr; }
inline constexpr const char* trace_interp_slots_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_interp_slots_wide_name() noexcept { return L""; }
#else
inline const char* trace_interp_slots() noexcept { return std::getenv("GALAXY_TRACE_INTERP_SLOTS"); }
inline constexpr const char* trace_interp_slots_name() noexcept { return "GALAXY_TRACE_INTERP_SLOTS"; }
inline constexpr const wchar_t* trace_interp_slots_wide_name() noexcept { return L"GALAXY_TRACE_INTERP_SLOTS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* vk_test_no_feature() noexcept { return nullptr; }
inline constexpr const char* vk_test_no_feature_name() noexcept { return ""; }
inline constexpr const wchar_t* vk_test_no_feature_wide_name() noexcept { return L""; }
#else
inline const char* vk_test_no_feature() noexcept { return std::getenv("GALAXY_VK_TEST_NO_FEATURE"); }
inline constexpr const char* vk_test_no_feature_name() noexcept { return "GALAXY_VK_TEST_NO_FEATURE"; }
inline constexpr const wchar_t* vk_test_no_feature_wide_name() noexcept { return L"GALAXY_VK_TEST_NO_FEATURE"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* vk_test_refuse() noexcept { return nullptr; }
inline constexpr const char* vk_test_refuse_name() noexcept { return ""; }
inline constexpr const wchar_t* vk_test_refuse_wide_name() noexcept { return L""; }
#else
inline const char* vk_test_refuse() noexcept { return std::getenv("GALAXY_VK_TEST_REFUSE"); }
inline constexpr const char* vk_test_refuse_name() noexcept { return "GALAXY_VK_TEST_REFUSE"; }
inline constexpr const wchar_t* vk_test_refuse_wide_name() noexcept { return L"GALAXY_VK_TEST_REFUSE"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_path_guard_name() noexcept { return ""; }
#else
inline constexpr const char* test_path_guard_name() noexcept { return "GALAXY_TEST_PATH_GUARD"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* d3d_debug() noexcept { return nullptr; }
inline constexpr const char* d3d_debug_name() noexcept { return ""; }
inline constexpr const wchar_t* d3d_debug_wide_name() noexcept { return L""; }
#else
inline const char* d3d_debug() noexcept { return std::getenv("GALAXY_D3D_DEBUG"); }
inline constexpr const char* d3d_debug_name() noexcept { return "GALAXY_D3D_DEBUG"; }
inline constexpr const wchar_t* d3d_debug_wide_name() noexcept { return L"GALAXY_D3D_DEBUG"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* d3d_dred() noexcept { return nullptr; }
inline constexpr const char* d3d_dred_name() noexcept { return ""; }
inline constexpr const wchar_t* d3d_dred_wide_name() noexcept { return L""; }
#else
inline const char* d3d_dred() noexcept { return std::getenv("GALAXY_D3D_DRED"); }
inline constexpr const char* d3d_dred_name() noexcept { return "GALAXY_D3D_DRED"; }
inline constexpr const wchar_t* d3d_dred_wide_name() noexcept { return L"GALAXY_D3D_DRED"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* discord_client_id() noexcept { return nullptr; }
inline constexpr const char* discord_client_id_name() noexcept { return ""; }
inline constexpr const wchar_t* discord_client_id_wide_name() noexcept { return L""; }
#else
inline const char* discord_client_id() noexcept { return std::getenv("GALAXY_DISCORD_CLIENT_ID"); }
inline constexpr const char* discord_client_id_name() noexcept { return "GALAXY_DISCORD_CLIENT_ID"; }
inline constexpr const wchar_t* discord_client_id_wide_name() noexcept { return L"GALAXY_DISCORD_CLIENT_ID"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* gpu_draws() noexcept { return nullptr; }
inline constexpr const char* gpu_draws_name() noexcept { return ""; }
inline constexpr const wchar_t* gpu_draws_wide_name() noexcept { return L""; }
#else
inline const char* gpu_draws() noexcept { return std::getenv("GALAXY_GPU_DRAWS"); }
inline constexpr const char* gpu_draws_name() noexcept { return "GALAXY_GPU_DRAWS"; }
inline constexpr const wchar_t* gpu_draws_wide_name() noexcept { return L"GALAXY_GPU_DRAWS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* gpu_passes() noexcept { return nullptr; }
inline constexpr const char* gpu_passes_name() noexcept { return ""; }
inline constexpr const wchar_t* gpu_passes_wide_name() noexcept { return L""; }
#else
inline const char* gpu_passes() noexcept { return std::getenv("GALAXY_GPU_PASSES"); }
inline constexpr const char* gpu_passes_name() noexcept { return "GALAXY_GPU_PASSES"; }
inline constexpr const wchar_t* gpu_passes_wide_name() noexcept { return L"GALAXY_GPU_PASSES"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* gpu_preference() noexcept { return nullptr; }
inline constexpr const char* gpu_preference_name() noexcept { return ""; }
inline constexpr const wchar_t* gpu_preference_wide_name() noexcept { return L""; }
#else
inline const char* gpu_preference() noexcept { return std::getenv("GALAXY_GPU_PREFERENCE"); }
inline constexpr const char* gpu_preference_name() noexcept { return "GALAXY_GPU_PREFERENCE"; }
inline constexpr const wchar_t* gpu_preference_wide_name() noexcept { return L"GALAXY_GPU_PREFERENCE"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* movie_dir() noexcept { return nullptr; }
inline constexpr const char* movie_dir_name() noexcept { return ""; }
inline constexpr const wchar_t* movie_dir_wide_name() noexcept { return L""; }
#else
inline const char* movie_dir() noexcept { return std::getenv("GALAXY_MOVIE_DIR"); }
inline constexpr const char* movie_dir_name() noexcept { return "GALAXY_MOVIE_DIR"; }
inline constexpr const wchar_t* movie_dir_wide_name() noexcept { return L"GALAXY_MOVIE_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* no_pipeline_prepare() noexcept { return nullptr; }
inline constexpr const char* no_pipeline_prepare_name() noexcept { return ""; }
inline constexpr const wchar_t* no_pipeline_prepare_wide_name() noexcept { return L""; }
#else
inline const char* no_pipeline_prepare() noexcept { return std::getenv("GALAXY_NO_PIPELINE_PREPARE"); }
inline constexpr const char* no_pipeline_prepare_name() noexcept { return "GALAXY_NO_PIPELINE_PREPARE"; }
inline constexpr const wchar_t* no_pipeline_prepare_wide_name() noexcept { return L"GALAXY_NO_PIPELINE_PREPARE"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* pipeline_seed_out() noexcept { return nullptr; }
inline constexpr const char* pipeline_seed_out_name() noexcept { return ""; }
inline constexpr const wchar_t* pipeline_seed_out_wide_name() noexcept { return L""; }
#else
inline const char* pipeline_seed_out() noexcept { return std::getenv("GALAXY_PIPELINE_SEED_OUT"); }
inline constexpr const char* pipeline_seed_out_name() noexcept { return "GALAXY_PIPELINE_SEED_OUT"; }
inline constexpr const wchar_t* pipeline_seed_out_wide_name() noexcept { return L"GALAXY_PIPELINE_SEED_OUT"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* render_config_path() noexcept { return nullptr; }
inline constexpr const char* render_config_path_name() noexcept { return ""; }
inline constexpr const wchar_t* render_config_path_wide_name() noexcept { return L""; }
#else
inline const char* render_config_path() noexcept { return std::getenv("GALAXY_RENDER_CONFIG_PATH"); }
inline constexpr const char* render_config_path_name() noexcept { return "GALAXY_RENDER_CONFIG_PATH"; }
inline constexpr const wchar_t* render_config_path_wide_name() noexcept { return L"GALAXY_RENDER_CONFIG_PATH"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* rt_log_glows() noexcept { return nullptr; }
inline constexpr const char* rt_log_glows_name() noexcept { return ""; }
inline constexpr const wchar_t* rt_log_glows_wide_name() noexcept { return L""; }
#else
inline const char* rt_log_glows() noexcept { return std::getenv("GALAXY_RT_LOG_GLOWS"); }
inline constexpr const char* rt_log_glows_name() noexcept { return "GALAXY_RT_LOG_GLOWS"; }
inline constexpr const wchar_t* rt_log_glows_wide_name() noexcept { return L"GALAXY_RT_LOG_GLOWS"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* runtime_settings_path() noexcept { return nullptr; }
inline constexpr const char* runtime_settings_path_name() noexcept { return ""; }
inline constexpr const wchar_t* runtime_settings_path_wide_name() noexcept { return L""; }
#else
inline const char* runtime_settings_path() noexcept { return std::getenv("GALAXY_RUNTIME_SETTINGS_PATH"); }
inline constexpr const char* runtime_settings_path_name() noexcept { return "GALAXY_RUNTIME_SETTINGS_PATH"; }
inline constexpr const wchar_t* runtime_settings_path_wide_name() noexcept { return L"GALAXY_RUNTIME_SETTINGS_PATH"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* texture_dump_dir() noexcept { return nullptr; }
inline constexpr const char* texture_dump_dir_name() noexcept { return ""; }
inline constexpr const wchar_t* texture_dump_dir_wide_name() noexcept { return L""; }
#else
inline const char* texture_dump_dir() noexcept { return std::getenv("GALAXY_TEXTURE_DUMP_DIR"); }
inline constexpr const char* texture_dump_dir_name() noexcept { return "GALAXY_TEXTURE_DUMP_DIR"; }
inline constexpr const wchar_t* texture_dump_dir_wide_name() noexcept { return L"GALAXY_TEXTURE_DUMP_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* texture_pack_dir() noexcept { return nullptr; }
inline constexpr const char* texture_pack_dir_name() noexcept { return ""; }
inline constexpr const wchar_t* texture_pack_dir_wide_name() noexcept { return L""; }
#else
inline const char* texture_pack_dir() noexcept { return std::getenv("GALAXY_TEXTURE_PACK_DIR"); }
inline constexpr const char* texture_pack_dir_name() noexcept { return "GALAXY_TEXTURE_PACK_DIR"; }
inline constexpr const wchar_t* texture_pack_dir_wide_name() noexcept { return L"GALAXY_TEXTURE_PACK_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* texture_pack_trace() noexcept { return nullptr; }
inline constexpr const char* texture_pack_trace_name() noexcept { return ""; }
inline constexpr const wchar_t* texture_pack_trace_wide_name() noexcept { return L""; }
#else
inline const char* texture_pack_trace() noexcept { return std::getenv("GALAXY_TEXTURE_PACK_TRACE"); }
inline constexpr const char* texture_pack_trace_name() noexcept { return "GALAXY_TEXTURE_PACK_TRACE"; }
inline constexpr const wchar_t* texture_pack_trace_wide_name() noexcept { return L"GALAXY_TEXTURE_PACK_TRACE"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* vulkan_validation() noexcept { return nullptr; }
inline constexpr const char* vulkan_validation_name() noexcept { return ""; }
inline constexpr const wchar_t* vulkan_validation_wide_name() noexcept { return L""; }
#else
inline const char* vulkan_validation() noexcept { return std::getenv("GALAXY_VULKAN_VALIDATION"); }
inline constexpr const char* vulkan_validation_name() noexcept { return "GALAXY_VULKAN_VALIDATION"; }
inline constexpr const wchar_t* vulkan_validation_wide_name() noexcept { return L"GALAXY_VULKAN_VALIDATION"; }
#endif
#if defined(NEBULA_RELEASE)
inline const char* xinput_require_focus() noexcept { return nullptr; }
inline constexpr const char* xinput_require_focus_name() noexcept { return ""; }
inline constexpr const wchar_t* xinput_require_focus_wide_name() noexcept { return L""; }
#else
inline const char* xinput_require_focus() noexcept { return std::getenv("GALAXY_XINPUT_REQUIRE_FOCUS"); }
inline constexpr const char* xinput_require_focus_name() noexcept { return "GALAXY_XINPUT_REQUIRE_FOCUS"; }
inline constexpr const wchar_t* xinput_require_focus_wide_name() noexcept { return L"GALAXY_XINPUT_REQUIRE_FOCUS"; }
#endif
}  // namespace nebula_switch

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
