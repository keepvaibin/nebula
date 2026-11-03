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
inline constexpr const char* audio_wav_dump() noexcept { return nullptr; }
inline constexpr const char* audio_wav_dump_name() noexcept { return ""; }
inline constexpr const wchar_t* audio_wav_dump_wide_name() noexcept { return L""; }
#else
inline const char* audio_wav_dump() noexcept { return std::getenv("GALAXY_AUDIO_WAV_DUMP"); }
inline constexpr const char* audio_wav_dump_name() noexcept { return "GALAXY_AUDIO_WAV_DUMP"; }
inline constexpr const wchar_t* audio_wav_dump_wide_name() noexcept { return L"GALAXY_AUDIO_WAV_DUMP"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* audio_wav_dump_only() noexcept { return nullptr; }
inline constexpr const char* audio_wav_dump_only_name() noexcept { return ""; }
inline constexpr const wchar_t* audio_wav_dump_only_wide_name() noexcept { return L""; }
#else
inline const char* audio_wav_dump_only() noexcept { return std::getenv("GALAXY_AUDIO_WAV_DUMP_ONLY"); }
inline constexpr const char* audio_wav_dump_only_name() noexcept { return "GALAXY_AUDIO_WAV_DUMP_ONLY"; }
inline constexpr const wchar_t* audio_wav_dump_only_wide_name() noexcept { return L"GALAXY_AUDIO_WAV_DUMP_ONLY"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* audio_wav_dump_seconds() noexcept { return nullptr; }
inline constexpr const char* audio_wav_dump_seconds_name() noexcept { return ""; }
inline constexpr const wchar_t* audio_wav_dump_seconds_wide_name() noexcept { return L""; }
#else
inline const char* audio_wav_dump_seconds() noexcept { return std::getenv("GALAXY_AUDIO_WAV_DUMP_SECONDS"); }
inline constexpr const char* audio_wav_dump_seconds_name() noexcept { return "GALAXY_AUDIO_WAV_DUMP_SECONDS"; }
inline constexpr const wchar_t* audio_wav_dump_seconds_wide_name() noexcept { return L"GALAXY_AUDIO_WAV_DUMP_SECONDS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* boot_fast() noexcept { return nullptr; }
inline constexpr const char* boot_fast_name() noexcept { return ""; }
inline constexpr const wchar_t* boot_fast_wide_name() noexcept { return L""; }
#else
inline const char* boot_fast() noexcept { return std::getenv("GALAXY_BOOT_FAST"); }
inline constexpr const char* boot_fast_name() noexcept { return "GALAXY_BOOT_FAST"; }
inline constexpr const wchar_t* boot_fast_wide_name() noexcept { return L"GALAXY_BOOT_FAST"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* crash_dump_dir() noexcept { return nullptr; }
inline constexpr const char* crash_dump_dir_name() noexcept { return ""; }
inline constexpr const wchar_t* crash_dump_dir_wide_name() noexcept { return L""; }
#else
inline const char* crash_dump_dir() noexcept { return std::getenv("GALAXY_CRASH_DUMP_DIR"); }
inline constexpr const char* crash_dump_dir_name() noexcept { return "GALAXY_CRASH_DUMP_DIR"; }
inline constexpr const wchar_t* crash_dump_dir_wide_name() noexcept { return L"GALAXY_CRASH_DUMP_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* export_art() noexcept { return nullptr; }
inline constexpr const char* export_art_name() noexcept { return ""; }
inline constexpr const wchar_t* export_art_wide_name() noexcept { return L""; }
#else
inline const char* export_art() noexcept { return std::getenv("GALAXY_EXPORT_ART"); }
inline constexpr const char* export_art_name() noexcept { return "GALAXY_EXPORT_ART"; }
inline constexpr const wchar_t* export_art_wide_name() noexcept { return L"GALAXY_EXPORT_ART"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* hide_window() noexcept { return nullptr; }
inline constexpr const char* hide_window_name() noexcept { return ""; }
inline constexpr const wchar_t* hide_window_wide_name() noexcept { return L""; }
#else
inline const char* hide_window() noexcept { return std::getenv("GALAXY_HIDE_WINDOW"); }
inline constexpr const char* hide_window_name() noexcept { return "GALAXY_HIDE_WINDOW"; }
inline constexpr const wchar_t* hide_window_wide_name() noexcept { return L"GALAXY_HIDE_WINDOW"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_a() noexcept { return nullptr; }
inline constexpr const char* input_autopress_a_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_a_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_a() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_A"); }
inline constexpr const char* input_autopress_a_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_A"; }
inline constexpr const wchar_t* input_autopress_a_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_A"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_a2() noexcept { return nullptr; }
inline constexpr const char* input_autopress_a2_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_a2_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_a2() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_A2"); }
inline constexpr const char* input_autopress_a2_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_A2"; }
inline constexpr const wchar_t* input_autopress_a2_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_A2"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_a3() noexcept { return nullptr; }
inline constexpr const char* input_autopress_a3_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_a3_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_a3() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_A3"); }
inline constexpr const char* input_autopress_a3_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_A3"; }
inline constexpr const wchar_t* input_autopress_a3_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_A3"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_a4() noexcept { return nullptr; }
inline constexpr const char* input_autopress_a4_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_a4_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_a4() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_A4"); }
inline constexpr const char* input_autopress_a4_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_A4"; }
inline constexpr const wchar_t* input_autopress_a4_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_A4"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_a5() noexcept { return nullptr; }
inline constexpr const char* input_autopress_a5_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_a5_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_a5() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_A5"); }
inline constexpr const char* input_autopress_a5_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_A5"; }
inline constexpr const wchar_t* input_autopress_a5_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_A5"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_a6() noexcept { return nullptr; }
inline constexpr const char* input_autopress_a6_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_a6_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_a6() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_A6"); }
inline constexpr const char* input_autopress_a6_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_A6"; }
inline constexpr const wchar_t* input_autopress_a6_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_A6"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_a7() noexcept { return nullptr; }
inline constexpr const char* input_autopress_a7_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_a7_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_a7() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_A7"); }
inline constexpr const char* input_autopress_a7_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_A7"; }
inline constexpr const wchar_t* input_autopress_a7_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_A7"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_a8() noexcept { return nullptr; }
inline constexpr const char* input_autopress_a8_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_a8_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_a8() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_A8"); }
inline constexpr const char* input_autopress_a8_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_A8"; }
inline constexpr const wchar_t* input_autopress_a8_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_A8"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_ab() noexcept { return nullptr; }
inline constexpr const char* input_autopress_ab_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_ab_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_ab() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_AB"); }
inline constexpr const char* input_autopress_ab_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_AB"; }
inline constexpr const wchar_t* input_autopress_ab_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_AB"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_a_duration() noexcept { return nullptr; }
inline constexpr const char* input_autopress_a_duration_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_a_duration_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_a_duration() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_A_DURATION"); }
inline constexpr const char* input_autopress_a_duration_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_A_DURATION"; }
inline constexpr const wchar_t* input_autopress_a_duration_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_A_DURATION"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_script() noexcept { return nullptr; }
inline constexpr const char* input_autopress_script_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_script_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_script() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_SCRIPT"); }
inline constexpr const char* input_autopress_script_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_SCRIPT"; }
inline constexpr const wchar_t* input_autopress_script_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_SCRIPT"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_script_stop_at_mario_control() noexcept { return nullptr; }
inline constexpr const char* input_autopress_script_stop_at_mario_control_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_script_stop_at_mario_control_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_script_stop_at_mario_control() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL"); }
inline constexpr const char* input_autopress_script_stop_at_mario_control_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL"; }
inline constexpr const wchar_t* input_autopress_script_stop_at_mario_control_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_autopress_script_stop_fallback_vi() noexcept { return nullptr; }
inline constexpr const char* input_autopress_script_stop_fallback_vi_name() noexcept { return ""; }
inline constexpr const wchar_t* input_autopress_script_stop_fallback_vi_wide_name() noexcept { return L""; }
#else
inline const char* input_autopress_script_stop_fallback_vi() noexcept { return std::getenv("GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI"); }
inline constexpr const char* input_autopress_script_stop_fallback_vi_name() noexcept { return "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI"; }
inline constexpr const wchar_t* input_autopress_script_stop_fallback_vi_wide_name() noexcept { return L"GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_mode() noexcept { return nullptr; }
inline constexpr const char* input_mode_name() noexcept { return ""; }
inline constexpr const wchar_t* input_mode_wide_name() noexcept { return L""; }
#else
inline const char* input_mode() noexcept { return std::getenv("GALAXY_INPUT_MODE"); }
inline constexpr const char* input_mode_name() noexcept { return "GALAXY_INPUT_MODE"; }
inline constexpr const wchar_t* input_mode_wide_name() noexcept { return L"GALAXY_INPUT_MODE"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer2_duration() noexcept { return nullptr; }
inline constexpr const char* input_pointer2_duration_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer2_duration_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer2_duration() noexcept { return std::getenv("GALAXY_INPUT_POINTER2_DURATION"); }
inline constexpr const char* input_pointer2_duration_name() noexcept { return "GALAXY_INPUT_POINTER2_DURATION"; }
inline constexpr const wchar_t* input_pointer2_duration_wide_name() noexcept { return L"GALAXY_INPUT_POINTER2_DURATION"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer2_vi() noexcept { return nullptr; }
inline constexpr const char* input_pointer2_vi_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer2_vi_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer2_vi() noexcept { return std::getenv("GALAXY_INPUT_POINTER2_VI"); }
inline constexpr const char* input_pointer2_vi_name() noexcept { return "GALAXY_INPUT_POINTER2_VI"; }
inline constexpr const wchar_t* input_pointer2_vi_wide_name() noexcept { return L"GALAXY_INPUT_POINTER2_VI"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer2_x() noexcept { return nullptr; }
inline constexpr const char* input_pointer2_x_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer2_x_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer2_x() noexcept { return std::getenv("GALAXY_INPUT_POINTER2_X"); }
inline constexpr const char* input_pointer2_x_name() noexcept { return "GALAXY_INPUT_POINTER2_X"; }
inline constexpr const wchar_t* input_pointer2_x_wide_name() noexcept { return L"GALAXY_INPUT_POINTER2_X"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer2_y() noexcept { return nullptr; }
inline constexpr const char* input_pointer2_y_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer2_y_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer2_y() noexcept { return std::getenv("GALAXY_INPUT_POINTER2_Y"); }
inline constexpr const char* input_pointer2_y_name() noexcept { return "GALAXY_INPUT_POINTER2_Y"; }
inline constexpr const wchar_t* input_pointer2_y_wide_name() noexcept { return L"GALAXY_INPUT_POINTER2_Y"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer3_duration() noexcept { return nullptr; }
inline constexpr const char* input_pointer3_duration_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer3_duration_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer3_duration() noexcept { return std::getenv("GALAXY_INPUT_POINTER3_DURATION"); }
inline constexpr const char* input_pointer3_duration_name() noexcept { return "GALAXY_INPUT_POINTER3_DURATION"; }
inline constexpr const wchar_t* input_pointer3_duration_wide_name() noexcept { return L"GALAXY_INPUT_POINTER3_DURATION"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer3_vi() noexcept { return nullptr; }
inline constexpr const char* input_pointer3_vi_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer3_vi_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer3_vi() noexcept { return std::getenv("GALAXY_INPUT_POINTER3_VI"); }
inline constexpr const char* input_pointer3_vi_name() noexcept { return "GALAXY_INPUT_POINTER3_VI"; }
inline constexpr const wchar_t* input_pointer3_vi_wide_name() noexcept { return L"GALAXY_INPUT_POINTER3_VI"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer3_x() noexcept { return nullptr; }
inline constexpr const char* input_pointer3_x_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer3_x_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer3_x() noexcept { return std::getenv("GALAXY_INPUT_POINTER3_X"); }
inline constexpr const char* input_pointer3_x_name() noexcept { return "GALAXY_INPUT_POINTER3_X"; }
inline constexpr const wchar_t* input_pointer3_x_wide_name() noexcept { return L"GALAXY_INPUT_POINTER3_X"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer3_y() noexcept { return nullptr; }
inline constexpr const char* input_pointer3_y_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer3_y_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer3_y() noexcept { return std::getenv("GALAXY_INPUT_POINTER3_Y"); }
inline constexpr const char* input_pointer3_y_name() noexcept { return "GALAXY_INPUT_POINTER3_Y"; }
inline constexpr const wchar_t* input_pointer3_y_wide_name() noexcept { return L"GALAXY_INPUT_POINTER3_Y"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer4_duration() noexcept { return nullptr; }
inline constexpr const char* input_pointer4_duration_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer4_duration_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer4_duration() noexcept { return std::getenv("GALAXY_INPUT_POINTER4_DURATION"); }
inline constexpr const char* input_pointer4_duration_name() noexcept { return "GALAXY_INPUT_POINTER4_DURATION"; }
inline constexpr const wchar_t* input_pointer4_duration_wide_name() noexcept { return L"GALAXY_INPUT_POINTER4_DURATION"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer4_vi() noexcept { return nullptr; }
inline constexpr const char* input_pointer4_vi_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer4_vi_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer4_vi() noexcept { return std::getenv("GALAXY_INPUT_POINTER4_VI"); }
inline constexpr const char* input_pointer4_vi_name() noexcept { return "GALAXY_INPUT_POINTER4_VI"; }
inline constexpr const wchar_t* input_pointer4_vi_wide_name() noexcept { return L"GALAXY_INPUT_POINTER4_VI"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer4_x() noexcept { return nullptr; }
inline constexpr const char* input_pointer4_x_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer4_x_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer4_x() noexcept { return std::getenv("GALAXY_INPUT_POINTER4_X"); }
inline constexpr const char* input_pointer4_x_name() noexcept { return "GALAXY_INPUT_POINTER4_X"; }
inline constexpr const wchar_t* input_pointer4_x_wide_name() noexcept { return L"GALAXY_INPUT_POINTER4_X"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer4_y() noexcept { return nullptr; }
inline constexpr const char* input_pointer4_y_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer4_y_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer4_y() noexcept { return std::getenv("GALAXY_INPUT_POINTER4_Y"); }
inline constexpr const char* input_pointer4_y_name() noexcept { return "GALAXY_INPUT_POINTER4_Y"; }
inline constexpr const wchar_t* input_pointer4_y_wide_name() noexcept { return L"GALAXY_INPUT_POINTER4_Y"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer5_duration() noexcept { return nullptr; }
inline constexpr const char* input_pointer5_duration_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer5_duration_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer5_duration() noexcept { return std::getenv("GALAXY_INPUT_POINTER5_DURATION"); }
inline constexpr const char* input_pointer5_duration_name() noexcept { return "GALAXY_INPUT_POINTER5_DURATION"; }
inline constexpr const wchar_t* input_pointer5_duration_wide_name() noexcept { return L"GALAXY_INPUT_POINTER5_DURATION"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer5_vi() noexcept { return nullptr; }
inline constexpr const char* input_pointer5_vi_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer5_vi_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer5_vi() noexcept { return std::getenv("GALAXY_INPUT_POINTER5_VI"); }
inline constexpr const char* input_pointer5_vi_name() noexcept { return "GALAXY_INPUT_POINTER5_VI"; }
inline constexpr const wchar_t* input_pointer5_vi_wide_name() noexcept { return L"GALAXY_INPUT_POINTER5_VI"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer5_x() noexcept { return nullptr; }
inline constexpr const char* input_pointer5_x_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer5_x_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer5_x() noexcept { return std::getenv("GALAXY_INPUT_POINTER5_X"); }
inline constexpr const char* input_pointer5_x_name() noexcept { return "GALAXY_INPUT_POINTER5_X"; }
inline constexpr const wchar_t* input_pointer5_x_wide_name() noexcept { return L"GALAXY_INPUT_POINTER5_X"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer5_y() noexcept { return nullptr; }
inline constexpr const char* input_pointer5_y_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer5_y_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer5_y() noexcept { return std::getenv("GALAXY_INPUT_POINTER5_Y"); }
inline constexpr const char* input_pointer5_y_name() noexcept { return "GALAXY_INPUT_POINTER5_Y"; }
inline constexpr const wchar_t* input_pointer5_y_wide_name() noexcept { return L"GALAXY_INPUT_POINTER5_Y"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer6_duration() noexcept { return nullptr; }
inline constexpr const char* input_pointer6_duration_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer6_duration_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer6_duration() noexcept { return std::getenv("GALAXY_INPUT_POINTER6_DURATION"); }
inline constexpr const char* input_pointer6_duration_name() noexcept { return "GALAXY_INPUT_POINTER6_DURATION"; }
inline constexpr const wchar_t* input_pointer6_duration_wide_name() noexcept { return L"GALAXY_INPUT_POINTER6_DURATION"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer6_vi() noexcept { return nullptr; }
inline constexpr const char* input_pointer6_vi_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer6_vi_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer6_vi() noexcept { return std::getenv("GALAXY_INPUT_POINTER6_VI"); }
inline constexpr const char* input_pointer6_vi_name() noexcept { return "GALAXY_INPUT_POINTER6_VI"; }
inline constexpr const wchar_t* input_pointer6_vi_wide_name() noexcept { return L"GALAXY_INPUT_POINTER6_VI"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer6_x() noexcept { return nullptr; }
inline constexpr const char* input_pointer6_x_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer6_x_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer6_x() noexcept { return std::getenv("GALAXY_INPUT_POINTER6_X"); }
inline constexpr const char* input_pointer6_x_name() noexcept { return "GALAXY_INPUT_POINTER6_X"; }
inline constexpr const wchar_t* input_pointer6_x_wide_name() noexcept { return L"GALAXY_INPUT_POINTER6_X"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer6_y() noexcept { return nullptr; }
inline constexpr const char* input_pointer6_y_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer6_y_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer6_y() noexcept { return std::getenv("GALAXY_INPUT_POINTER6_Y"); }
inline constexpr const char* input_pointer6_y_name() noexcept { return "GALAXY_INPUT_POINTER6_Y"; }
inline constexpr const wchar_t* input_pointer6_y_wide_name() noexcept { return L"GALAXY_INPUT_POINTER6_Y"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer_path() noexcept { return nullptr; }
inline constexpr const char* input_pointer_path_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer_path_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer_path() noexcept { return std::getenv("GALAXY_INPUT_POINTER_PATH"); }
inline constexpr const char* input_pointer_path_name() noexcept { return "GALAXY_INPUT_POINTER_PATH"; }
inline constexpr const wchar_t* input_pointer_path_wide_name() noexcept { return L"GALAXY_INPUT_POINTER_PATH"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer_sweep_duration() noexcept { return nullptr; }
inline constexpr const char* input_pointer_sweep_duration_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer_sweep_duration_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer_sweep_duration() noexcept { return std::getenv("GALAXY_INPUT_POINTER_SWEEP_DURATION"); }
inline constexpr const char* input_pointer_sweep_duration_name() noexcept { return "GALAXY_INPUT_POINTER_SWEEP_DURATION"; }
inline constexpr const wchar_t* input_pointer_sweep_duration_wide_name() noexcept { return L"GALAXY_INPUT_POINTER_SWEEP_DURATION"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer_sweep_vi() noexcept { return nullptr; }
inline constexpr const char* input_pointer_sweep_vi_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer_sweep_vi_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer_sweep_vi() noexcept { return std::getenv("GALAXY_INPUT_POINTER_SWEEP_VI"); }
inline constexpr const char* input_pointer_sweep_vi_name() noexcept { return "GALAXY_INPUT_POINTER_SWEEP_VI"; }
inline constexpr const wchar_t* input_pointer_sweep_vi_wide_name() noexcept { return L"GALAXY_INPUT_POINTER_SWEEP_VI"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer_x() noexcept { return nullptr; }
inline constexpr const char* input_pointer_x_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer_x_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer_x() noexcept { return std::getenv("GALAXY_INPUT_POINTER_X"); }
inline constexpr const char* input_pointer_x_name() noexcept { return "GALAXY_INPUT_POINTER_X"; }
inline constexpr const wchar_t* input_pointer_x_wide_name() noexcept { return L"GALAXY_INPUT_POINTER_X"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_pointer_y() noexcept { return nullptr; }
inline constexpr const char* input_pointer_y_name() noexcept { return ""; }
inline constexpr const wchar_t* input_pointer_y_wide_name() noexcept { return L""; }
#else
inline const char* input_pointer_y() noexcept { return std::getenv("GALAXY_INPUT_POINTER_Y"); }
inline constexpr const char* input_pointer_y_name() noexcept { return "GALAXY_INPUT_POINTER_Y"; }
inline constexpr const wchar_t* input_pointer_y_wide_name() noexcept { return L"GALAXY_INPUT_POINTER_Y"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_read_sample() noexcept { return nullptr; }
inline constexpr const char* input_read_sample_name() noexcept { return ""; }
inline constexpr const wchar_t* input_read_sample_wide_name() noexcept { return L""; }
#else
inline const char* input_read_sample() noexcept { return std::getenv("GALAXY_INPUT_READ_SAMPLE"); }
inline constexpr const char* input_read_sample_name() noexcept { return "GALAXY_INPUT_READ_SAMPLE"; }
inline constexpr const wchar_t* input_read_sample_wide_name() noexcept { return L"GALAXY_INPUT_READ_SAMPLE"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_replay_log() noexcept { return nullptr; }
inline constexpr const char* input_replay_log_name() noexcept { return ""; }
inline constexpr const wchar_t* input_replay_log_wide_name() noexcept { return L""; }
#else
inline const char* input_replay_log() noexcept { return std::getenv("GALAXY_INPUT_REPLAY_LOG"); }
inline constexpr const char* input_replay_log_name() noexcept { return "GALAXY_INPUT_REPLAY_LOG"; }
inline constexpr const wchar_t* input_replay_log_wide_name() noexcept { return L"GALAXY_INPUT_REPLAY_LOG"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_script_pointer_force() noexcept { return nullptr; }
inline constexpr const char* input_script_pointer_force_name() noexcept { return ""; }
inline constexpr const wchar_t* input_script_pointer_force_wide_name() noexcept { return L""; }
#else
inline const char* input_script_pointer_force() noexcept { return std::getenv("GALAXY_INPUT_SCRIPT_POINTER_FORCE"); }
inline constexpr const char* input_script_pointer_force_name() noexcept { return "GALAXY_INPUT_SCRIPT_POINTER_FORCE"; }
inline constexpr const wchar_t* input_script_pointer_force_wide_name() noexcept { return L"GALAXY_INPUT_SCRIPT_POINTER_FORCE"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_script_pointer_live_mouse_ms() noexcept { return nullptr; }
inline constexpr const char* input_script_pointer_live_mouse_ms_name() noexcept { return ""; }
inline constexpr const wchar_t* input_script_pointer_live_mouse_ms_wide_name() noexcept { return L""; }
#else
inline const char* input_script_pointer_live_mouse_ms() noexcept { return std::getenv("GALAXY_INPUT_SCRIPT_POINTER_LIVE_MOUSE_MS"); }
inline constexpr const char* input_script_pointer_live_mouse_ms_name() noexcept { return "GALAXY_INPUT_SCRIPT_POINTER_LIVE_MOUSE_MS"; }
inline constexpr const wchar_t* input_script_pointer_live_mouse_ms_wide_name() noexcept { return L"GALAXY_INPUT_SCRIPT_POINTER_LIVE_MOUSE_MS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_shake_script() noexcept { return nullptr; }
inline constexpr const char* input_shake_script_name() noexcept { return ""; }
inline constexpr const wchar_t* input_shake_script_wide_name() noexcept { return L""; }
#else
inline const char* input_shake_script() noexcept { return std::getenv("GALAXY_INPUT_SHAKE_SCRIPT"); }
inline constexpr const char* input_shake_script_name() noexcept { return "GALAXY_INPUT_SHAKE_SCRIPT"; }
inline constexpr const wchar_t* input_shake_script_wide_name() noexcept { return L"GALAXY_INPUT_SHAKE_SCRIPT"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_stick_command_file() noexcept { return nullptr; }
inline constexpr const char* input_stick_command_file_name() noexcept { return ""; }
inline constexpr const wchar_t* input_stick_command_file_wide_name() noexcept { return L""; }
#else
inline const char* input_stick_command_file() noexcept { return std::getenv("GALAXY_INPUT_STICK_COMMAND_FILE"); }
inline constexpr const char* input_stick_command_file_name() noexcept { return "GALAXY_INPUT_STICK_COMMAND_FILE"; }
inline constexpr const wchar_t* input_stick_command_file_wide_name() noexcept { return L"GALAXY_INPUT_STICK_COMMAND_FILE"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_stick_script() noexcept { return nullptr; }
inline constexpr const char* input_stick_script_name() noexcept { return ""; }
inline constexpr const wchar_t* input_stick_script_wide_name() noexcept { return L""; }
#else
inline const char* input_stick_script() noexcept { return std::getenv("GALAXY_INPUT_STICK_SCRIPT"); }
inline constexpr const char* input_stick_script_name() noexcept { return "GALAXY_INPUT_STICK_SCRIPT"; }
inline constexpr const wchar_t* input_stick_script_wide_name() noexcept { return L"GALAXY_INPUT_STICK_SCRIPT"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_stick_script_relative_fallback_vi() noexcept { return nullptr; }
inline constexpr const char* input_stick_script_relative_fallback_vi_name() noexcept { return ""; }
inline constexpr const wchar_t* input_stick_script_relative_fallback_vi_wide_name() noexcept { return L""; }
#else
inline const char* input_stick_script_relative_fallback_vi() noexcept { return std::getenv("GALAXY_INPUT_STICK_SCRIPT_RELATIVE_FALLBACK_VI"); }
inline constexpr const char* input_stick_script_relative_fallback_vi_name() noexcept { return "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_FALLBACK_VI"; }
inline constexpr const wchar_t* input_stick_script_relative_fallback_vi_wide_name() noexcept { return L"GALAXY_INPUT_STICK_SCRIPT_RELATIVE_FALLBACK_VI"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* input_stick_script_relative_mario_control() noexcept { return nullptr; }
inline constexpr const char* input_stick_script_relative_mario_control_name() noexcept { return ""; }
inline constexpr const wchar_t* input_stick_script_relative_mario_control_wide_name() noexcept { return L""; }
#else
inline const char* input_stick_script_relative_mario_control() noexcept { return std::getenv("GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL"); }
inline constexpr const char* input_stick_script_relative_mario_control_name() noexcept { return "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL"; }
inline constexpr const wchar_t* input_stick_script_relative_mario_control_wide_name() noexcept { return L"GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* interp_snapshot_dir() noexcept { return nullptr; }
inline constexpr const char* interp_snapshot_dir_name() noexcept { return ""; }
inline constexpr const wchar_t* interp_snapshot_dir_wide_name() noexcept { return L""; }
#else
inline const char* interp_snapshot_dir() noexcept { return std::getenv("GALAXY_INTERP_SNAPSHOT_DIR"); }
inline constexpr const char* interp_snapshot_dir_name() noexcept { return "GALAXY_INTERP_SNAPSHOT_DIR"; }
inline constexpr const wchar_t* interp_snapshot_dir_wide_name() noexcept { return L"GALAXY_INTERP_SNAPSHOT_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* interp_snapshot_first() noexcept { return nullptr; }
inline constexpr const char* interp_snapshot_first_name() noexcept { return ""; }
inline constexpr const wchar_t* interp_snapshot_first_wide_name() noexcept { return L""; }
#else
inline const char* interp_snapshot_first() noexcept { return std::getenv("GALAXY_INTERP_SNAPSHOT_FIRST"); }
inline constexpr const char* interp_snapshot_first_name() noexcept { return "GALAXY_INTERP_SNAPSHOT_FIRST"; }
inline constexpr const wchar_t* interp_snapshot_first_wide_name() noexcept { return L"GALAXY_INTERP_SNAPSHOT_FIRST"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* interp_snapshot_last() noexcept { return nullptr; }
inline constexpr const char* interp_snapshot_last_name() noexcept { return ""; }
inline constexpr const wchar_t* interp_snapshot_last_wide_name() noexcept { return L""; }
#else
inline const char* interp_snapshot_last() noexcept { return std::getenv("GALAXY_INTERP_SNAPSHOT_LAST"); }
inline constexpr const char* interp_snapshot_last_name() noexcept { return "GALAXY_INTERP_SNAPSHOT_LAST"; }
inline constexpr const wchar_t* interp_snapshot_last_wide_name() noexcept { return L"GALAXY_INTERP_SNAPSHOT_LAST"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* log_to_console() noexcept { return nullptr; }
inline constexpr const char* log_to_console_name() noexcept { return ""; }
inline constexpr const wchar_t* log_to_console_wide_name() noexcept { return L""; }
#else
inline const char* log_to_console() noexcept { return std::getenv("GALAXY_LOG_TO_CONSOLE"); }
inline constexpr const char* log_to_console_name() noexcept { return "GALAXY_LOG_TO_CONSOLE"; }
inline constexpr const wchar_t* log_to_console_wide_name() noexcept { return L"GALAXY_LOG_TO_CONSOLE"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* present_snapshot_dir() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_dir_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_dir_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_dir() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_DIR"); }
inline constexpr const char* present_snapshot_dir_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_DIR"; }
inline constexpr const wchar_t* present_snapshot_dir_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_DIR"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* present_snapshot_every() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_every_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_every_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_every() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_EVERY"); }
inline constexpr const char* present_snapshot_every_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_EVERY"; }
inline constexpr const wchar_t* present_snapshot_every_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_EVERY"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* present_snapshot_first() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_first_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_first_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_first() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_FIRST"); }
inline constexpr const char* present_snapshot_first_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_FIRST"; }
inline constexpr const wchar_t* present_snapshot_first_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_FIRST"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* present_snapshot_last() noexcept { return nullptr; }
inline constexpr const char* present_snapshot_last_name() noexcept { return ""; }
inline constexpr const wchar_t* present_snapshot_last_wide_name() noexcept { return L""; }
#else
inline const char* present_snapshot_last() noexcept { return std::getenv("GALAXY_PRESENT_SNAPSHOT_LAST"); }
inline constexpr const char* present_snapshot_last_name() noexcept { return "GALAXY_PRESENT_SNAPSHOT_LAST"; }
inline constexpr const wchar_t* present_snapshot_last_wide_name() noexcept { return L"GALAXY_PRESENT_SNAPSHOT_LAST"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* profile_game() noexcept { return nullptr; }
inline constexpr const char* profile_game_name() noexcept { return ""; }
inline constexpr const wchar_t* profile_game_wide_name() noexcept { return L""; }
#else
inline const char* profile_game() noexcept { return std::getenv("GALAXY_PROFILE_GAME"); }
inline constexpr const char* profile_game_name() noexcept { return "GALAXY_PROFILE_GAME"; }
inline constexpr const wchar_t* profile_game_wide_name() noexcept { return L"GALAXY_PROFILE_GAME"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_() noexcept { return nullptr; }
inline constexpr const char* test__name() noexcept { return ""; }
inline constexpr const wchar_t* test__wide_name() noexcept { return L""; }
#else
inline const char* test_() noexcept { return std::getenv("GALAXY_TEST_"); }
inline constexpr const char* test__name() noexcept { return "GALAXY_TEST_"; }
inline constexpr const wchar_t* test__wide_name() noexcept { return L"GALAXY_TEST_"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_arena_fill() noexcept { return nullptr; }
inline constexpr const char* test_arena_fill_name() noexcept { return ""; }
inline constexpr const wchar_t* test_arena_fill_wide_name() noexcept { return L""; }
#else
inline const char* test_arena_fill() noexcept { return std::getenv("GALAXY_TEST_ARENA_FILL"); }
inline constexpr const char* test_arena_fill_name() noexcept { return "GALAXY_TEST_ARENA_FILL"; }
inline constexpr const wchar_t* test_arena_fill_wide_name() noexcept { return L"GALAXY_TEST_ARENA_FILL"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_capture_no_wait() noexcept { return nullptr; }
inline constexpr const char* test_capture_no_wait_name() noexcept { return ""; }
inline constexpr const wchar_t* test_capture_no_wait_wide_name() noexcept { return L""; }
#else
inline const char* test_capture_no_wait() noexcept { return std::getenv("GALAXY_TEST_CAPTURE_NO_WAIT"); }
inline constexpr const char* test_capture_no_wait_name() noexcept { return "GALAXY_TEST_CAPTURE_NO_WAIT"; }
inline constexpr const wchar_t* test_capture_no_wait_wide_name() noexcept { return L"GALAXY_TEST_CAPTURE_NO_WAIT"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_cpu_affinity() noexcept { return nullptr; }
inline constexpr const char* test_cpu_affinity_name() noexcept { return ""; }
inline constexpr const wchar_t* test_cpu_affinity_wide_name() noexcept { return L""; }
#else
inline const char* test_cpu_affinity() noexcept { return std::getenv("GALAXY_TEST_CPU_AFFINITY"); }
inline constexpr const char* test_cpu_affinity_name() noexcept { return "GALAXY_TEST_CPU_AFFINITY"; }
inline constexpr const wchar_t* test_cpu_affinity_wide_name() noexcept { return L"GALAXY_TEST_CPU_AFFINITY"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_deterministic() noexcept { return nullptr; }
inline constexpr const char* test_deterministic_name() noexcept { return ""; }
inline constexpr const wchar_t* test_deterministic_wide_name() noexcept { return L""; }
#else
inline const char* test_deterministic() noexcept { return std::getenv("GALAXY_TEST_DETERMINISTIC"); }
inline constexpr const char* test_deterministic_name() noexcept { return "GALAXY_TEST_DETERMINISTIC"; }
inline constexpr const wchar_t* test_deterministic_wide_name() noexcept { return L"GALAXY_TEST_DETERMINISTIC"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_disc_delay_us() noexcept { return nullptr; }
inline constexpr const char* test_disc_delay_us_name() noexcept { return ""; }
inline constexpr const wchar_t* test_disc_delay_us_wide_name() noexcept { return L""; }
#else
inline const char* test_disc_delay_us() noexcept { return std::getenv("GALAXY_TEST_DISC_DELAY_US"); }
inline constexpr const char* test_disc_delay_us_name() noexcept { return "GALAXY_TEST_DISC_DELAY_US"; }
inline constexpr const wchar_t* test_disc_delay_us_wide_name() noexcept { return L"GALAXY_TEST_DISC_DELAY_US"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_dump_layouts() noexcept { return nullptr; }
inline constexpr const char* test_dump_layouts_name() noexcept { return ""; }
inline constexpr const wchar_t* test_dump_layouts_wide_name() noexcept { return L""; }
#else
inline const char* test_dump_layouts() noexcept { return std::getenv("GALAXY_TEST_DUMP_LAYOUTS"); }
inline constexpr const char* test_dump_layouts_name() noexcept { return "GALAXY_TEST_DUMP_LAYOUTS"; }
inline constexpr const wchar_t* test_dump_layouts_wide_name() noexcept { return L"GALAXY_TEST_DUMP_LAYOUTS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_events() noexcept { return nullptr; }
inline constexpr const char* test_events_name() noexcept { return ""; }
inline constexpr const wchar_t* test_events_wide_name() noexcept { return L""; }
#else
inline const char* test_events() noexcept { return std::getenv("GALAXY_TEST_EVENTS"); }
inline constexpr const char* test_events_name() noexcept { return "GALAXY_TEST_EVENTS"; }
inline constexpr const wchar_t* test_events_wide_name() noexcept { return L"GALAXY_TEST_EVENTS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_event_flags() noexcept { return nullptr; }
inline constexpr const char* test_event_flags_name() noexcept { return ""; }
inline constexpr const wchar_t* test_event_flags_wide_name() noexcept { return L""; }
#else
inline const char* test_event_flags() noexcept { return std::getenv("GALAXY_TEST_EVENT_FLAGS"); }
inline constexpr const char* test_event_flags_name() noexcept { return "GALAXY_TEST_EVENT_FLAGS"; }
inline constexpr const wchar_t* test_event_flags_wide_name() noexcept { return L"GALAXY_TEST_EVENT_FLAGS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_galaxy_tour() noexcept { return nullptr; }
inline constexpr const char* test_galaxy_tour_name() noexcept { return ""; }
inline constexpr const wchar_t* test_galaxy_tour_wide_name() noexcept { return L""; }
#else
inline const char* test_galaxy_tour() noexcept { return std::getenv("GALAXY_TEST_GALAXY_TOUR"); }
inline constexpr const char* test_galaxy_tour_name() noexcept { return "GALAXY_TEST_GALAXY_TOUR"; }
inline constexpr const wchar_t* test_galaxy_tour_wide_name() noexcept { return L"GALAXY_TEST_GALAXY_TOUR"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_galaxy_tour_frames() noexcept { return nullptr; }
inline constexpr const char* test_galaxy_tour_frames_name() noexcept { return ""; }
inline constexpr const wchar_t* test_galaxy_tour_frames_wide_name() noexcept { return L""; }
#else
inline const char* test_galaxy_tour_frames() noexcept { return std::getenv("GALAXY_TEST_GALAXY_TOUR_FRAMES"); }
inline constexpr const char* test_galaxy_tour_frames_name() noexcept { return "GALAXY_TEST_GALAXY_TOUR_FRAMES"; }
inline constexpr const wchar_t* test_galaxy_tour_frames_wide_name() noexcept { return L"GALAXY_TEST_GALAXY_TOUR_FRAMES"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_game_stall_ms() noexcept { return nullptr; }
inline constexpr const char* test_game_stall_ms_name() noexcept { return ""; }
inline constexpr const wchar_t* test_game_stall_ms_wide_name() noexcept { return L""; }
#else
inline const char* test_game_stall_ms() noexcept { return std::getenv("GALAXY_TEST_GAME_STALL_MS"); }
inline constexpr const char* test_game_stall_ms_name() noexcept { return "GALAXY_TEST_GAME_STALL_MS"; }
inline constexpr const wchar_t* test_game_stall_ms_wide_name() noexcept { return L"GALAXY_TEST_GAME_STALL_MS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_gpu_lost_frame() noexcept { return nullptr; }
inline constexpr const char* test_gpu_lost_frame_name() noexcept { return ""; }
inline constexpr const wchar_t* test_gpu_lost_frame_wide_name() noexcept { return L""; }
#else
inline const char* test_gpu_lost_frame() noexcept { return std::getenv("GALAXY_TEST_GPU_LOST_FRAME"); }
inline constexpr const char* test_gpu_lost_frame_name() noexcept { return "GALAXY_TEST_GPU_LOST_FRAME"; }
inline constexpr const wchar_t* test_gpu_lost_frame_wide_name() noexcept { return L"GALAXY_TEST_GPU_LOST_FRAME"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_gpu_lost_times() noexcept { return nullptr; }
inline constexpr const char* test_gpu_lost_times_name() noexcept { return ""; }
inline constexpr const wchar_t* test_gpu_lost_times_wide_name() noexcept { return L""; }
#else
inline const char* test_gpu_lost_times() noexcept { return std::getenv("GALAXY_TEST_GPU_LOST_TIMES"); }
inline constexpr const char* test_gpu_lost_times_name() noexcept { return "GALAXY_TEST_GPU_LOST_TIMES"; }
inline constexpr const wchar_t* test_gpu_lost_times_wide_name() noexcept { return L"GALAXY_TEST_GPU_LOST_TIMES"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_grand_stars() noexcept { return nullptr; }
inline constexpr const char* test_grand_stars_name() noexcept { return ""; }
inline constexpr const wchar_t* test_grand_stars_wide_name() noexcept { return L""; }
#else
inline const char* test_grand_stars() noexcept { return std::getenv("GALAXY_TEST_GRAND_STARS"); }
inline constexpr const char* test_grand_stars_name() noexcept { return "GALAXY_TEST_GRAND_STARS"; }
inline constexpr const wchar_t* test_grand_stars_wide_name() noexcept { return L"GALAXY_TEST_GRAND_STARS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_home_row() noexcept { return nullptr; }
inline constexpr const char* test_home_row_name() noexcept { return ""; }
inline constexpr const wchar_t* test_home_row_wide_name() noexcept { return L""; }
#else
inline const char* test_home_row() noexcept { return std::getenv("GALAXY_TEST_HOME_ROW"); }
inline constexpr const char* test_home_row_name() noexcept { return "GALAXY_TEST_HOME_ROW"; }
inline constexpr const wchar_t* test_home_row_wide_name() noexcept { return L"GALAXY_TEST_HOME_ROW"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_joycon() noexcept { return nullptr; }
inline constexpr const char* test_joycon_name() noexcept { return ""; }
inline constexpr const wchar_t* test_joycon_wide_name() noexcept { return L""; }
#else
inline const char* test_joycon() noexcept { return std::getenv("GALAXY_TEST_JOYCON"); }
inline constexpr const char* test_joycon_name() noexcept { return "GALAXY_TEST_JOYCON"; }
inline constexpr const wchar_t* test_joycon_wide_name() noexcept { return L"GALAXY_TEST_JOYCON"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_key_layout() noexcept { return nullptr; }
inline constexpr const char* test_key_layout_name() noexcept { return ""; }
inline constexpr const wchar_t* test_key_layout_wide_name() noexcept { return L""; }
#else
inline const char* test_key_layout() noexcept { return std::getenv("GALAXY_TEST_KEY_LAYOUT"); }
inline constexpr const char* test_key_layout_name() noexcept { return "GALAXY_TEST_KEY_LAYOUT"; }
inline constexpr const wchar_t* test_key_layout_wide_name() noexcept { return L"GALAXY_TEST_KEY_LAYOUT"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_last_input() noexcept { return nullptr; }
inline constexpr const char* test_last_input_name() noexcept { return ""; }
inline constexpr const wchar_t* test_last_input_wide_name() noexcept { return L""; }
#else
inline const char* test_last_input() noexcept { return std::getenv("GALAXY_TEST_LAST_INPUT"); }
inline constexpr const char* test_last_input_name() noexcept { return "GALAXY_TEST_LAST_INPUT"; }
inline constexpr const wchar_t* test_last_input_wide_name() noexcept { return L"GALAXY_TEST_LAST_INPUT"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_memory_at() noexcept { return nullptr; }
inline constexpr const char* test_memory_at_name() noexcept { return ""; }
inline constexpr const wchar_t* test_memory_at_wide_name() noexcept { return L""; }
#else
inline const char* test_memory_at() noexcept { return std::getenv("GALAXY_TEST_MEMORY_AT"); }
inline constexpr const char* test_memory_at_name() noexcept { return "GALAXY_TEST_MEMORY_AT"; }
inline constexpr const wchar_t* test_memory_at_wide_name() noexcept { return L"GALAXY_TEST_MEMORY_AT"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_native_j3d() noexcept { return nullptr; }
inline constexpr const char* test_native_j3d_name() noexcept { return ""; }
inline constexpr const wchar_t* test_native_j3d_wide_name() noexcept { return L""; }
#else
inline const char* test_native_j3d() noexcept { return std::getenv("GALAXY_TEST_NATIVE_J3D"); }
inline constexpr const char* test_native_j3d_name() noexcept { return "GALAXY_TEST_NATIVE_J3D"; }
inline constexpr const wchar_t* test_native_j3d_wide_name() noexcept { return L"GALAXY_TEST_NATIVE_J3D"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_one_game() noexcept { return nullptr; }
inline constexpr const char* test_one_game_name() noexcept { return ""; }
inline constexpr const wchar_t* test_one_game_wide_name() noexcept { return L""; }
#else
inline const char* test_one_game() noexcept { return std::getenv("GALAXY_TEST_ONE_GAME"); }
inline constexpr const char* test_one_game_name() noexcept { return "GALAXY_TEST_ONE_GAME"; }
inline constexpr const wchar_t* test_one_game_wide_name() noexcept { return L"GALAXY_TEST_ONE_GAME"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_p2() noexcept { return nullptr; }
inline constexpr const char* test_p2_name() noexcept { return ""; }
inline constexpr const wchar_t* test_p2_wide_name() noexcept { return L""; }
#else
inline const char* test_p2() noexcept { return std::getenv("GALAXY_TEST_P2"); }
inline constexpr const char* test_p2_name() noexcept { return "GALAXY_TEST_P2"; }
inline constexpr const wchar_t* test_p2_wide_name() noexcept { return L"GALAXY_TEST_P2"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_post_fault() noexcept { return nullptr; }
inline constexpr const char* test_post_fault_name() noexcept { return ""; }
inline constexpr const wchar_t* test_post_fault_wide_name() noexcept { return L""; }
#else
inline const char* test_post_fault() noexcept { return std::getenv("GALAXY_TEST_POST_FAULT"); }
inline constexpr const char* test_post_fault_name() noexcept { return "GALAXY_TEST_POST_FAULT"; }
inline constexpr const wchar_t* test_post_fault_wide_name() noexcept { return L"GALAXY_TEST_POST_FAULT"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_rt_effect_ab() noexcept { return nullptr; }
inline constexpr const char* test_rt_effect_ab_name() noexcept { return ""; }
inline constexpr const wchar_t* test_rt_effect_ab_wide_name() noexcept { return L""; }
#else
inline const char* test_rt_effect_ab() noexcept { return std::getenv("GALAXY_TEST_RT_EFFECT_AB"); }
inline constexpr const char* test_rt_effect_ab_name() noexcept { return "GALAXY_TEST_RT_EFFECT_AB"; }
inline constexpr const wchar_t* test_rt_effect_ab_wide_name() noexcept { return L"GALAXY_TEST_RT_EFFECT_AB"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_rumble_target() noexcept { return nullptr; }
inline constexpr const char* test_rumble_target_name() noexcept { return ""; }
inline constexpr const wchar_t* test_rumble_target_wide_name() noexcept { return L""; }
#else
inline const char* test_rumble_target() noexcept { return std::getenv("GALAXY_TEST_RUMBLE_TARGET"); }
inline constexpr const char* test_rumble_target_name() noexcept { return "GALAXY_TEST_RUMBLE_TARGET"; }
inline constexpr const wchar_t* test_rumble_target_wide_name() noexcept { return L"GALAXY_TEST_RUMBLE_TARGET"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_script_clock() noexcept { return nullptr; }
inline constexpr const char* test_script_clock_name() noexcept { return ""; }
inline constexpr const wchar_t* test_script_clock_wide_name() noexcept { return L""; }
#else
inline const char* test_script_clock() noexcept { return std::getenv("GALAXY_TEST_SCRIPT_CLOCK"); }
inline constexpr const char* test_script_clock_name() noexcept { return "GALAXY_TEST_SCRIPT_CLOCK"; }
inline constexpr const wchar_t* test_script_clock_wide_name() noexcept { return L"GALAXY_TEST_SCRIPT_CLOCK"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_settings_keys() noexcept { return nullptr; }
inline constexpr const char* test_settings_keys_name() noexcept { return ""; }
inline constexpr const wchar_t* test_settings_keys_wide_name() noexcept { return L""; }
#else
inline const char* test_settings_keys() noexcept { return std::getenv("GALAXY_TEST_SETTINGS_KEYS"); }
inline constexpr const char* test_settings_keys_name() noexcept { return "GALAXY_TEST_SETTINGS_KEYS"; }
inline constexpr const wchar_t* test_settings_keys_wide_name() noexcept { return L"GALAXY_TEST_SETTINGS_KEYS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_settings_open() noexcept { return nullptr; }
inline constexpr const char* test_settings_open_name() noexcept { return ""; }
inline constexpr const wchar_t* test_settings_open_wide_name() noexcept { return L""; }
#else
inline const char* test_settings_open() noexcept { return std::getenv("GALAXY_TEST_SETTINGS_OPEN"); }
inline constexpr const char* test_settings_open_name() noexcept { return "GALAXY_TEST_SETTINGS_OPEN"; }
inline constexpr const wchar_t* test_settings_open_wide_name() noexcept { return L"GALAXY_TEST_SETTINGS_OPEN"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_shown_window() noexcept { return nullptr; }
inline constexpr const char* test_shown_window_name() noexcept { return ""; }
inline constexpr const wchar_t* test_shown_window_wide_name() noexcept { return L""; }
#else
inline const char* test_shown_window() noexcept { return std::getenv("GALAXY_TEST_SHOWN_WINDOW"); }
inline constexpr const char* test_shown_window_name() noexcept { return "GALAXY_TEST_SHOWN_WINDOW"; }
inline constexpr const wchar_t* test_shown_window_wide_name() noexcept { return L"GALAXY_TEST_SHOWN_WINDOW"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_teleport() noexcept { return nullptr; }
inline constexpr const char* test_teleport_name() noexcept { return ""; }
inline constexpr const wchar_t* test_teleport_wide_name() noexcept { return L""; }
#else
inline const char* test_teleport() noexcept { return std::getenv("GALAXY_TEST_TELEPORT"); }
inline constexpr const char* test_teleport_name() noexcept { return "GALAXY_TEST_TELEPORT"; }
inline constexpr const wchar_t* test_teleport_wide_name() noexcept { return L"GALAXY_TEST_TELEPORT"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_walk() noexcept { return nullptr; }
inline constexpr const char* test_walk_name() noexcept { return ""; }
inline constexpr const wchar_t* test_walk_wide_name() noexcept { return L""; }
#else
inline const char* test_walk() noexcept { return std::getenv("GALAXY_TEST_WALK"); }
inline constexpr const char* test_walk_name() noexcept { return "GALAXY_TEST_WALK"; }
inline constexpr const wchar_t* test_walk_wide_name() noexcept { return L"GALAXY_TEST_WALK"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* test_window_sizes() noexcept { return nullptr; }
inline constexpr const char* test_window_sizes_name() noexcept { return ""; }
inline constexpr const wchar_t* test_window_sizes_wide_name() noexcept { return L""; }
#else
inline const char* test_window_sizes() noexcept { return std::getenv("GALAXY_TEST_WINDOW_SIZES"); }
inline constexpr const char* test_window_sizes_name() noexcept { return "GALAXY_TEST_WINDOW_SIZES"; }
inline constexpr const wchar_t* test_window_sizes_wide_name() noexcept { return L"GALAXY_TEST_WINDOW_SIZES"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* trace_file_reads() noexcept { return nullptr; }
inline constexpr const char* trace_file_reads_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_file_reads_wide_name() noexcept { return L""; }
#else
inline const char* trace_file_reads() noexcept { return std::getenv("GALAXY_TRACE_FILE_READS"); }
inline constexpr const char* trace_file_reads_name() noexcept { return "GALAXY_TRACE_FILE_READS"; }
inline constexpr const wchar_t* trace_file_reads_wide_name() noexcept { return L"GALAXY_TRACE_FILE_READS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* trace_gx_stalls() noexcept { return nullptr; }
inline constexpr const char* trace_gx_stalls_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_gx_stalls_wide_name() noexcept { return L""; }
#else
inline const char* trace_gx_stalls() noexcept { return std::getenv("GALAXY_TRACE_GX_STALLS"); }
inline constexpr const char* trace_gx_stalls_name() noexcept { return "GALAXY_TRACE_GX_STALLS"; }
inline constexpr const wchar_t* trace_gx_stalls_wide_name() noexcept { return L"GALAXY_TRACE_GX_STALLS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* trace_lights() noexcept { return nullptr; }
inline constexpr const char* trace_lights_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_lights_wide_name() noexcept { return L""; }
#else
inline const char* trace_lights() noexcept { return std::getenv("GALAXY_TRACE_LIGHTS"); }
inline constexpr const char* trace_lights_name() noexcept { return "GALAXY_TRACE_LIGHTS"; }
inline constexpr const wchar_t* trace_lights_wide_name() noexcept { return L"GALAXY_TRACE_LIGHTS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* trace_loads() noexcept { return nullptr; }
inline constexpr const char* trace_loads_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_loads_wide_name() noexcept { return L""; }
#else
inline const char* trace_loads() noexcept { return std::getenv("GALAXY_TRACE_LOADS"); }
inline constexpr const char* trace_loads_name() noexcept { return "GALAXY_TRACE_LOADS"; }
inline constexpr const wchar_t* trace_loads_wide_name() noexcept { return L"GALAXY_TRACE_LOADS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* trace_memory() noexcept { return nullptr; }
inline constexpr const char* trace_memory_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_memory_wide_name() noexcept { return L""; }
#else
inline const char* trace_memory() noexcept { return std::getenv("GALAXY_TRACE_MEMORY"); }
inline constexpr const char* trace_memory_name() noexcept { return "GALAXY_TRACE_MEMORY"; }
inline constexpr const wchar_t* trace_memory_wide_name() noexcept { return L"GALAXY_TRACE_MEMORY"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* trace_present_stats() noexcept { return nullptr; }
inline constexpr const char* trace_present_stats_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_present_stats_wide_name() noexcept { return L""; }
#else
inline const char* trace_present_stats() noexcept { return std::getenv("GALAXY_TRACE_PRESENT_STATS"); }
inline constexpr const char* trace_present_stats_name() noexcept { return "GALAXY_TRACE_PRESENT_STATS"; }
inline constexpr const wchar_t* trace_present_stats_wide_name() noexcept { return L"GALAXY_TRACE_PRESENT_STATS"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* trace_render_stall_us() noexcept { return nullptr; }
inline constexpr const char* trace_render_stall_us_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_render_stall_us_wide_name() noexcept { return L""; }
#else
inline const char* trace_render_stall_us() noexcept { return std::getenv("GALAXY_TRACE_RENDER_STALL_US"); }
inline constexpr const char* trace_render_stall_us_name() noexcept { return "GALAXY_TRACE_RENDER_STALL_US"; }
inline constexpr const wchar_t* trace_render_stall_us_wide_name() noexcept { return L"GALAXY_TRACE_RENDER_STALL_US"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* trace_thread_cpu() noexcept { return nullptr; }
inline constexpr const char* trace_thread_cpu_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_thread_cpu_wide_name() noexcept { return L""; }
#else
inline const char* trace_thread_cpu() noexcept { return std::getenv("GALAXY_TRACE_THREAD_CPU"); }
inline constexpr const char* trace_thread_cpu_name() noexcept { return "GALAXY_TRACE_THREAD_CPU"; }
inline constexpr const wchar_t* trace_thread_cpu_wide_name() noexcept { return L"GALAXY_TRACE_THREAD_CPU"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* trace_vi_times() noexcept { return nullptr; }
inline constexpr const char* trace_vi_times_name() noexcept { return ""; }
inline constexpr const wchar_t* trace_vi_times_wide_name() noexcept { return L""; }
#else
inline const char* trace_vi_times() noexcept { return std::getenv("GALAXY_TRACE_VI_TIMES"); }
inline constexpr const char* trace_vi_times_name() noexcept { return "GALAXY_TRACE_VI_TIMES"; }
inline constexpr const wchar_t* trace_vi_times_wide_name() noexcept { return L"GALAXY_TRACE_VI_TIMES"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* vk_simulate_driver() noexcept { return nullptr; }
inline constexpr const char* vk_simulate_driver_name() noexcept { return ""; }
inline constexpr const wchar_t* vk_simulate_driver_wide_name() noexcept { return L""; }
#else
inline const char* vk_simulate_driver() noexcept { return std::getenv("GALAXY_VK_SIMULATE_DRIVER"); }
inline constexpr const char* vk_simulate_driver_name() noexcept { return "GALAXY_VK_SIMULATE_DRIVER"; }
inline constexpr const wchar_t* vk_simulate_driver_wide_name() noexcept { return L"GALAXY_VK_SIMULATE_DRIVER"; }
#endif
#if defined(NEBULA_RELEASE)
inline constexpr const char* vk_test_refuse() noexcept { return nullptr; }
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
}  // namespace nebula_switch

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
