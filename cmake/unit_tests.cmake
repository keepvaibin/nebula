# The unit tests every desktop system builds and runs: Windows in the root CMakeLists.txt (which adds its own tests of
# Windows code and of the renderers after these), Linux and the Mac in the tests' own project (tests/unit). The small
# libraries they link are cmake/runtime_libs.cmake's; the archive libraries and BoringSSL come from the including build.
#
# galaxy_test(name SOURCES ... [LIBS ...] [ARGS ...]): an executable from SOURCES linked with LIBS, registered with
# ctest (run with ARGS). The tests compile warning-free with every warning the compiler gives an error.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/runtime_libs.cmake")

enable_testing()
function(galaxy_test name)
    cmake_parse_arguments(arg "" "" "SOURCES;LIBS;ARGS" ${ARGN})
    add_executable(${name} ${arg_SOURCES})
    target_include_directories(${name} PRIVATE "${galaxy_root}/runtime/include")
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_link_libraries(${name} PRIVATE galaxy_runtime_abi ${arg_LIBS})
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 ${NEBULA_WARNINGS_AS_ERRORS} /permissive- /EHsc)
    endif()
    if(NOT WIN32)
        find_package(Threads REQUIRED)
        target_link_libraries(${name} PRIVATE Threads::Threads)
    endif()
    add_test(NAME ${name} COMMAND ${name} ${arg_ARGS})
endfunction()

set(rt "${galaxy_rt}")
set(render_src "${galaxy_rt}/render")

# The unit tests' wait for another thread (galaxy/test_wait.h): the event itself, a deadline that only reports (N-1073).
galaxy_test(galaxy_test_wait_tests SOURCES ${rt}/test_wait_tests.cpp)
# The SDL host's waits (galaxy/host_wait.h, P-281): the event pump's block a wake ends, the display clock's sleep.
galaxy_test(galaxy_host_wait_tests SOURCES ${rt}/host_wait_tests.cpp)
# The game's own frame rates by the step rate (galaxy/game_step.h own_frame_rate; Android's vote and display mode).
galaxy_test(galaxy_game_step_tests SOURCES ${rt}/game_step_tests.cpp)
# The process's output: every line whole, whichever threads write.
galaxy_test(galaxy_frame_due_tests SOURCES ${rt}/frame_due_tests.cpp)
galaxy_test(galaxy_start_watch_tests SOURCES ${rt}/start_watch_tests.cpp)
galaxy_test(galaxy_whole_lines_tests SOURCES ${rt}/whole_lines_tests.cpp ${rt}/whole_lines.cpp)
# Its writer's thread: no caller waits on a stuck output; order kept; flush bounded.
galaxy_test(galaxy_line_writer_tests SOURCES ${rt}/line_writer_tests.cpp)
# The pacing measures (file 00 4.3) on synthetic runs, and the recorder that feeds them.
galaxy_test(galaxy_pacing_tests SOURCES ${rt}/pacing_tests.cpp ${rt}/pacing.cpp ${rt}/pacing_recorder.cpp)
# The frame threads' CPU time by thread (the pacing line's old figure). Windows reads no thread's CPU time there.
if(NOT WIN32)
    galaxy_test(galaxy_perf_hint_tests SOURCES ${rt}/perf_hint_tests.cpp ${rt}/perf_hint.cpp)
endif()
# The frame threads' processor time spent working, between their marks (perf_hint_work_cpu: the cost the speed measure judges),
# on every platform.
galaxy_test(galaxy_work_cpu_tests SOURCES ${rt}/work_cpu_tests.cpp ${rt}/perf_hint.cpp)
# The frame timeline (--diagnostics timeline): its clock, threads and file on every platform.
galaxy_test(galaxy_frame_timeline_tests SOURCES ${rt}/frame_timeline_tests.cpp)
# A frame thread's work as wall time between its marks (P-069), on every platform.
galaxy_test(galaxy_busy_window_tests SOURCES ${rt}/busy_window_tests.cpp)

# Files and saves.
galaxy_test(galaxy_atomic_file_tests SOURCES ${rt}/atomic_file_tests.cpp LIBS galaxy_atomic_file)
galaxy_test(galaxy_save_files_tests SOURCES ${rt}/save_files_tests.cpp LIBS galaxy_save_files)
# The bounded call of a process's last work (galaxy/bounded_call.h, header only).
galaxy_test(galaxy_bounded_call_tests SOURCES ${rt}/bounded_call_tests.cpp)
galaxy_test(galaxy_save_format_tests SOURCES ${rt}/save_format_tests.cpp)
target_include_directories(galaxy_save_format_tests PRIVATE "${galaxy_root}/native/petari/include")
galaxy_test(galaxy_launch_args_tests SOURCES ${rt}/launch_args_tests.cpp)
# The guard's word scan (native/host/luma_guard_words.h) on stacks laid out as luma_guard_call lays them out (M-027).
galaxy_test(galaxy_luma_guard_words_tests SOURCES "${galaxy_root}/native/host/luma_guard_words_tests.cpp")
target_include_directories(galaxy_luma_guard_words_tests PRIVATE "${galaxy_root}/native/host" "${galaxy_root}/native/include")
# The guard's need walk (native/include/luma_guard_walk.h), its growing table of mods' code (luma_guard_spans.h), its
# reading of a mod's unwind tables (luma_guard_unwind.h) and the kernel's sets of needed mods (LumaNeeds.hpp), M-001.
galaxy_test(galaxy_luma_guard_walk_tests SOURCES "${galaxy_root}/native/host/luma_guard_walk_tests.cpp")
target_include_directories(galaxy_luma_guard_walk_tests PRIVATE "${galaxy_root}/native/host" "${galaxy_root}/native/include"
                                                               "${galaxy_root}/native/petari/include")
galaxy_test(galaxy_save_transfer_tests SOURCES ${rt}/save_transfer_tests.cpp LIBS galaxy_save_transfer galaxy_mii_formats)
galaxy_test(galaxy_asset_store_tests SOURCES ${rt}/asset_store_tests.cpp ${rt}/luma_game_store.cpp LIBS galaxy_asset_store)
target_include_directories(galaxy_asset_store_tests PRIVATE "${galaxy_root}/crates/nebula-import/include")
# D-07: an older install's store and saves under the current layout.
galaxy_test(galaxy_install_migration_tests SOURCES ${rt}/install_migration_tests.cpp LIBS galaxy_save_files galaxy_asset_store)
# The game's calendar (native/petari), compiled against a host the test provides.
galaxy_test(galaxy_native_calendar_tests SOURCES ${rt}/native_calendar_tests.cpp
    "${galaxy_root}/native/petari/src/Game/System/NebulaCalendar.cpp")
target_include_directories(galaxy_native_calendar_tests PRIVATE "${galaxy_root}/native/include"
    "${galaxy_root}/native/petari/include")
galaxy_test(galaxy_mii_import_tests SOURCES ${rt}/mii_import_tests.cpp ${rt}/mii_import.cpp
    LIBS galaxy_atomic_file galaxy_mii_formats)
galaxy_test(galaxy_crash_report_tests SOURCES ${rt}/crash_report_tests.cpp ${rt}/crash_report.cpp)

# Settings, achievements, statistics and input.
galaxy_test(galaxy_runtime_settings_tests SOURCES ${rt}/runtime_settings_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_achievements_tests SOURCES ${rt}/achievements_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_stats_tests SOURCES ${rt}/stats_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_controls_tests SOURCES ${rt}/controls_tests.cpp LIBS galaxy_runtime_settings galaxy_native_input)
galaxy_test(galaxy_keyboard_layout_tests SOURCES ${rt}/input/keyboard_layout_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_keyboard_state_tests SOURCES ${rt}/input/keyboard_state_tests.cpp)
# A key event of a host whose keys are physical places (SDL) against the default layout: W A S D, Space and Control.
galaxy_test(galaxy_key_event_tests SOURCES ${rt}/input/key_event_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_raw_mouse_tests SOURCES ${rt}/input/raw_mouse_tests.cpp)
galaxy_test(galaxy_rumble_route_tests SOURCES ${rt}/input/rumble_route_tests.cpp ${rt}/input/rumble_route.cpp)
galaxy_test(galaxy_wiimote_motion_tests SOURCES ${rt}/input/wiimote_motion_tests.cpp ${rt}/input/wiimote_motion.cpp)
galaxy_test(galaxy_wiimote_motion_pointer_tests SOURCES ${rt}/input/wiimote_motion_pointer_tests.cpp
    LIBS galaxy_native_input)
galaxy_test(galaxy_mouse_projection_tests SOURCES ${rt}/input/mouse_projection_tests.cpp LIBS galaxy_native_input)
galaxy_test(galaxy_pad_controller_tests SOURCES ${rt}/pad_controller_tests.cpp
    LIBS galaxy_runtime_settings galaxy_native_input)
galaxy_test(galaxy_native_input_tests SOURCES ${rt}/native_input_tests.cpp LIBS galaxy_native_input)
galaxy_test(galaxy_wiimote_speaker_tests SOURCES ${rt}/wiimote_speaker_tests.cpp LIBS galaxy_native_input)
galaxy_test(galaxy_host_pointer_events_tests SOURCES ${rt}/host_pointer_events_tests.cpp
    LIBS galaxy_host_pointer_events)

# Sound, movies and timing.
galaxy_test(galaxy_audio_mixer_tests SOURCES ${rt}/audio_mixer_tests.cpp LIBS galaxy_audio_mixer)
galaxy_test(galaxy_movie_video_tests SOURCES ${rt}/movie_video_tests.cpp ${rt}/movie_video.cpp)
galaxy_test(galaxy_power_policy_tests SOURCES ${rt}/power_policy_tests.cpp)
galaxy_test(galaxy_frame_phase_tests SOURCES ${rt}/frame_phase_tests.cpp ${rt}/frame_phase.cpp)
galaxy_test(galaxy_precise_wait_tests SOURCES ${rt}/precise_wait_tests.cpp)

# Mods.
galaxy_test(galaxy_luma_tests SOURCES ${rt}/luma_tests.cpp ${rt}/luma.cpp)
galaxy_test(galaxy_luma_net_reach_tests SOURCES ${rt}/luma_net_reach_tests.cpp ${rt}/luma_net_reach.cpp)
# A swap chain's size where the surface sets none (Wayland): follows the window (galaxy/render/vk/swapchain_extent.h).
galaxy_test(galaxy_vk_swapchain_extent_tests SOURCES ${rt}/render/vk/swapchain_extent_tests.cpp)
# The desktop SDL host's displays and the window's choice and placement (galaxy/render/desktop_display.h).
galaxy_test(galaxy_desktop_display_tests SOURCES ${render_src}/desktop_display_tests.cpp)
# Mod support in a build, and the settings pages it takes away (galaxy/mod_support.h).
galaxy_test(galaxy_mod_support_tests SOURCES ${rt}/mod_support_tests.cpp)
galaxy_test(galaxy_mods_tests SOURCES ${rt}/mods_tests.cpp ${rt}/game_module.cpp ${rt}/mod_xml.cpp ${rt}/mods.cpp ${rt}/mod_consent.cpp ${rt}/luma.cpp
    ${rt}/mod_import.cpp ${rt}/texture_categories.cpp ${rt}/dolphin_pack.cpp ${rt}/asset_store.cpp LIBS galaxy_atomic_file nebula_archive)
# The consent store (docs/modding/concepts/permissions.md): answers, digests and their round trip through the mod settings.
galaxy_test(galaxy_mod_consent_tests SOURCES ${rt}/mod_consent_tests.cpp ${rt}/mod_consent.cpp ${rt}/mods.cpp ${rt}/mod_xml.cpp
    ${rt}/luma.cpp ${rt}/mod_import.cpp ${rt}/texture_categories.cpp ${rt}/dolphin_pack.cpp ${rt}/asset_store.cpp
    LIBS galaxy_atomic_file nebula_archive)
# Console mods the tests read as files (runtime/tests).
target_compile_definitions(galaxy_mods_tests PRIVATE MODS_FIXTURES_DIR="${galaxy_root}/runtime/tests")
# draw.post's registry (mod_post.cpp) without a device.
galaxy_test(galaxy_mod_post_tests SOURCES ${render_src}/mod_post_tests.cpp ${render_src}/mod_post.cpp)

# Menus, the benchmark and reports.
galaxy_test(galaxy_home_menu_model_tests SOURCES ${rt}/home_menu_model_tests.cpp)
galaxy_test(galaxy_launcher_model_tests SOURCES ${rt}/ui/launcher_model_tests.cpp)
galaxy_test(galaxy_resolution_fit_tests SOURCES ${render_src}/resolution_fit_tests.cpp)
galaxy_test(galaxy_benchmark_recommend_tests SOURCES ${rt}/benchmark_recommend_tests.cpp ${rt}/benchmark_recommend.cpp
    ${rt}/strict_json.cpp)
galaxy_test(galaxy_benchmark_stats_tests SOURCES ${rt}/benchmark_stats_tests.cpp ${rt}/benchmark_stats.cpp ${rt}/strict_json.cpp)
galaxy_test(galaxy_diag_report_tests SOURCES ${rt}/diag_report_tests.cpp ${rt}/diag_report.cpp ${rt}/diag_seal.cpp ${rt}/strict_json.cpp
    LIBS nebula_archive nebula_bssl_crypto)
target_compile_definitions(galaxy_diag_report_tests PRIVATE
    NEBULA_BENCHMARK_FIXTURE="${galaxy_root}/tools/telemetry/testdata/benchmark.json")
galaxy_test(galaxy_settings_model_tests SOURCES ${rt}/ui/settings_model_tests.cpp ${rt}/ui/settings_model.cpp)
galaxy_test(galaxy_settings_save_model_tests SOURCES ${rt}/ui/settings_save_model_tests.cpp ${rt}/ui/settings_save_model.cpp)
galaxy_test(galaxy_settings_general_page_tests SOURCES ${rt}/ui/settings_general_page_tests.cpp
    ${rt}/ui/settings_general_page.cpp ${rt}/ui/settings_model.cpp)
galaxy_test(galaxy_settings_display_page_tests SOURCES ${rt}/ui/settings_display_page_tests.cpp
    ${rt}/ui/settings_display_page.cpp ${rt}/ui/settings_model.cpp)
galaxy_test(galaxy_settings_advanced_page_tests SOURCES ${rt}/ui/settings_advanced_page_tests.cpp
    ${rt}/ui/settings_advanced_page.cpp ${rt}/ui/settings_model.cpp ${render_src}/galaxy_select.cpp)
galaxy_test(galaxy_settings_system_page_tests SOURCES ${rt}/ui/settings_system_page_tests.cpp
    ${rt}/ui/settings_system_page.cpp ${rt}/ui/settings_model.cpp ${render_src}/renderer_choice.cpp)
galaxy_test(galaxy_renderer_choice_tests SOURCES ${render_src}/renderer_choice_tests.cpp ${render_src}/renderer_choice.cpp)
galaxy_test(galaxy_settings_benchmark_page_tests SOURCES ${rt}/ui/settings_benchmark_page_tests.cpp
    ${rt}/ui/settings_benchmark_page.cpp ${rt}/benchmark_results.cpp ${rt}/benchmark_stats.cpp ${rt}/strict_json.cpp)
if(WIN32)
    # The crash watcher (native/host/crash_watch.cpp): the test program is its own watcher process.
    galaxy_test(galaxy_crash_watch_tests SOURCES "${galaxy_root}/native/host/crash_watch.cpp"
        "${galaxy_root}/native/host/crash_watch_tests.cpp")
    target_link_libraries(galaxy_settings_benchmark_page_tests PRIVATE shell32)
endif()
galaxy_test(galaxy_discord_presence_tests SOURCES ${rt}/discord_presence_tests.cpp ${rt}/discord_presence.cpp)

# The renderer's parts that need no device.
galaxy_test(galaxy_frame_interpolation_tests SOURCES ${render_src}/frame_interpolation_tests.cpp
    ${render_src}/frame_interpolation.cpp ${render_src}/affine.cpp)
galaxy_test(galaxy_present_slots_tests SOURCES ${render_src}/present_slots_tests.cpp ${render_src}/present_slots.cpp)
# When each picture between two game frames is shown, on simulated displays (I-016).
galaxy_test(galaxy_frame_schedule_tests SOURCES ${render_src}/frame_schedule_tests.cpp ${render_src}/frame_schedule.cpp
            ${render_src}/present_slots.cpp ${rt}/frame_phase.cpp)
# The GPU's time by pass, one copy for the four renderers (P-022).
galaxy_test(galaxy_gpu_pass_times_tests SOURCES ${render_src}/gpu_pass_times_tests.cpp ${render_src}/gpu_pass_times.cpp)
# The temporal upscaler's hold on generated frames ends (header-only).
galaxy_test(galaxy_temporal_hold_tests SOURCES ${render_src}/temporal_hold_tests.cpp)
# The renderers' video memory read: at most every 100 ms, at once when the budget moved (P-011).
galaxy_test(galaxy_video_memory_reading_tests SOURCES ${render_src}/video_memory_reading_tests.cpp)
# The game thread's place in the render queue (header-only).
galaxy_test(galaxy_render_queue_tests SOURCES ${render_src}/render_queue_tests.cpp)
# The vsync grid the Android and Apple display clocks run the game's frames on (header-only).
galaxy_test(galaxy_vsync_grid_tests SOURCES ${rt}/vsync_grid_tests.cpp)
# The draw walker's plans for a list (header-only).
galaxy_test(galaxy_walk_plan_tests SOURCES ${render_src}/core/walk_plan_tests.cpp)
target_include_directories(galaxy_walk_plan_tests PRIVATE "${galaxy_root}/runtime/src" "${galaxy_root}/native/include")
# Frame lists recorded from tours (--diagnostics frame-plans): their plans must not change unseen (P-044).
target_compile_definitions(galaxy_walk_plan_tests PRIVATE WALK_PLAN_FIXTURES="${galaxy_root}/runtime/src/render/core/testdata/frame-plans")
# A model's section table as the game and the host read it (nebula_model.h, header-only; FRAME-MODEL 2.8).
# Texture lists that live as long as their table (texture_lists.h, header-only; FRAME-MODEL 2.10).
galaxy_test(galaxy_texture_lists_tests SOURCES ${render_src}/core/texture_lists_tests.cpp)
target_include_directories(galaxy_texture_lists_tests PRIVATE "${galaxy_root}/runtime/src")
# What a renderer gives up while frames in flight read it, destroyed after its slot's fence (retire_ring.h, header-only).
galaxy_test(galaxy_retire_ring_tests SOURCES ${render_src}/core/retire_ring_tests.cpp)
target_include_directories(galaxy_retire_ring_tests PRIVATE "${galaxy_root}/runtime/src")
galaxy_test(galaxy_model_sections_tests SOURCES ${render_src}/core/model_sections_tests.cpp)
target_include_directories(galaxy_model_sections_tests PRIVATE "${galaxy_root}/native/include")
# A frame list let go of with release and taken back with acquire (nebula_frame_list.h, P-005): two threads, here so
# the Linux thread sanitizer job runs it (the root list is Windows' alone).
galaxy_test(galaxy_frame_list_release_tests SOURCES ${render_src}/core/frame_list_release_tests.cpp)
target_include_directories(galaxy_frame_list_release_tests PRIVATE "${galaxy_root}/native/include")
# The pipeline cache files' headers (header-only; both backends' caches).
galaxy_test(galaxy_pipeline_cache_file_tests SOURCES ${render_src}/core/pipeline_cache_file_tests.cpp)
target_include_directories(galaxy_pipeline_cache_file_tests PRIVATE "${galaxy_root}/runtime/src")
