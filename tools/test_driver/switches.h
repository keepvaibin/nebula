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
inline const char* test_() noexcept { return nullptr; }
inline constexpr const wchar_t* test__wide_name() noexcept { return L""; }
#else
inline const char* test_() noexcept { return std::getenv("GALAXY_TEST_"); }
inline constexpr const wchar_t* test__wide_name() noexcept { return L"GALAXY_TEST_"; }
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
