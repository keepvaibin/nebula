# The native game on SDL3 platforms (Android, Linux, SteamOS; docs/NATIVE_PORT.md "Platforms"): the shared render layer
# with the Vulkan renderer, the host's runtime pieces, and the SDL host. Dependencies are source checkouts named by
# the cache paths below; the game module is native/CMakeLists.txt.
cmake_minimum_required(VERSION 3.22)
project(NebulaSDL LANGUAGES C CXX)
# The game's version, from the main project's file.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../../CMakeLists.txt" nebula_project_line REGEX "^project\\(Nebula VERSION ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" nebula_version "${nebula_project_line}")
add_compile_definitions(NEBULA_VERSION="${nebula_version}")

option(NEBULA_RELEASE "Leave the test hooks out" OFF)
if(NEBULA_RELEASE)
    add_compile_definitions(NEBULA_RELEASE=1)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(nebula_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(NEBULA_DEPS "" CACHE PATH "Folder of the dependency checkouts")
if(NOT IS_DIRECTORY "${NEBULA_DEPS}")
    message(FATAL_ERROR "Set NEBULA_DEPS to the folder of the dependency checkouts (platform/android/build_apk.ps1 -Deps)")
endif()
set(NEBULA_VULKAN_HEADERS_DIR "${NEBULA_DEPS}/Vulkan-Headers-vulkan-sdk-1.4.350.1" CACHE PATH "Vulkan-Headers")
set(NEBULA_VOLK_DIR "${NEBULA_DEPS}/volk-vulkan-sdk-1.4.350.1" CACHE PATH "volk")
set(NEBULA_VMA_DIR "${NEBULA_DEPS}/VulkanMemoryAllocator-v3.4.0" CACHE PATH "VulkanMemoryAllocator")
set(NEBULA_GLSLANG_SOURCE_DIR "${NEBULA_DEPS}/glslang-src" CACHE PATH "glslang source (External/spirv-tools inside)")
set(NEBULA_SDL_DIR "${NEBULA_DEPS}/SDL3-3.4.18" CACHE PATH "SDL3 source")

add_library(nebula_vulkan_deps INTERFACE)
target_include_directories(nebula_vulkan_deps INTERFACE
    "${NEBULA_VULKAN_HEADERS_DIR}/include" "${NEBULA_VOLK_DIR}" "${NEBULA_VMA_DIR}/include")
target_compile_definitions(nebula_vulkan_deps INTERFACE NEBULA_GPU_VULKAN VK_NO_PROTOTYPES
    $<$<PLATFORM_ID:Android>:VK_USE_PLATFORM_ANDROID_KHR>)

# Shaders are compiled at build time by the PC's tools (the Windows Vulkan build's galaxy_native_spirv and
# galaxy_utility_spirv): a device compiles nothing.
set(NEBULA_HOST_TOOLS "" CACHE PATH "Folder of galaxy_native_spirv and galaxy_utility_spirv for the build machine")
if(NOT IS_DIRECTORY "${NEBULA_HOST_TOOLS}")
    message(FATAL_ERROR "Set NEBULA_HOST_TOOLS to the Windows Vulkan build folder")
endif()
add_executable(galaxy_native_spirv IMPORTED)
set_target_properties(galaxy_native_spirv PROPERTIES IMPORTED_LOCATION "${NEBULA_HOST_TOOLS}/galaxy_native_spirv${CMAKE_HOST_EXECUTABLE_SUFFIX}")
add_executable(galaxy_utility_spirv IMPORTED)
set_target_properties(galaxy_utility_spirv PROPERTIES IMPORTED_LOCATION "${NEBULA_HOST_TOOLS}/galaxy_utility_spirv${CMAKE_HOST_EXECUTABLE_SUFFIX}")
include("${nebula_root}/cmake/native_materials.cmake")
galaxy_native_material_sources(galaxy_native_spirv_sources spirv)
set(galaxy_utility_spirv_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/utility_spirv")
file(MAKE_DIRECTORY "${galaxy_utility_spirv_dir}")
add_custom_command(OUTPUT "${galaxy_utility_spirv_dir}/utility_spirv_data.h"
    COMMAND galaxy_utility_spirv "${galaxy_utility_spirv_dir}/utility_spirv_data.h"
    DEPENDS galaxy_utility_spirv VERBATIM)

set(rt "${nebula_root}/runtime/src")
add_library(galaxy_render_vk STATIC
    ${rt}/render/render_api.cpp
    ${rt}/render/render_config.cpp
    ${rt}/ui/native_settings.cpp
    ${rt}/ui/settings_model.cpp
    ${rt}/ui/settings_system_page.cpp
    ${rt}/ui/settings_advanced_page.cpp
    ${rt}/ui/settings_display_page.cpp
    ${rt}/ui/settings_general_page.cpp
    ${rt}/ui/settings_graphics_page.cpp
    ${rt}/ui/settings_save_model.cpp
    ${rt}/render/renderer_common.cpp
    ${rt}/render/gpu_upload_ring.cpp
    ${rt}/render/texture_store.cpp
    ${rt}/render/texture_pack.cpp
    ${rt}/render/texture_pack_codecs.cpp
    ${rt}/render/movie_replacement.cpp
    ${rt}/render/curved_surfaces.cpp
    ${rt}/render/native_shapes.cpp
    ${rt}/render/native_material.cpp
    ${rt}/render/drawing_state.cpp
    ${rt}/render/draw_matrices.cpp
    ${rt}/render/render_state_key.cpp
    ${rt}/render/frame_pass.cpp
    ${rt}/render/line_point_params.cpp
    ${rt}/render/post_process_settings.cpp
    ${rt}/render/transform_history.cpp
    ${rt}/render/render_core.cpp
    ${rt}/render/game_lights.cpp
    ${rt}/render/galaxy_select.cpp
    ${rt}/render/frame_interpolation.cpp
    ${rt}/render/interp_gpu.cpp
    ${rt}/render/rt_materials.cpp
    ${rt}/frame_telemetry.cpp
    ${rt}/perf_hint.cpp
    ${rt}/render/game_asset_extract.cpp
    ${rt}/render/vk/vk_context.cpp
    ${rt}/render/vk/device_quirks_vk.cpp
    ${rt}/render/vk/vk_libraries.cpp
    ${rt}/render/vk/utility_spirv.cpp
    ${rt}/render/vk/texture_backend_vk.cpp
    ${rt}/render/vk/pipeline_cache_vk.cpp
    ${rt}/render/mod_shaders.cpp
    ${rt}/render/pipeline_seed.cpp
    ${rt}/render/vk/compile_journal.cpp
    ${rt}/render/vk/renderer_vk.cpp
    ${rt}/render/vk/ambient_occlusion_vk.cpp
    ${rt}/render/vk/scene_camera_vk.cpp
    ${rt}/render/vk/renderer_vk_scene.cpp
    ${rt}/render/vk/renderer_vk_replay.cpp
    ${rt}/render/vk/post_process_vk.cpp
    ${rt}/render/vk/game_bloom_vk.cpp
    "${galaxy_utility_spirv_dir}/utility_spirv_data.h"
    ${galaxy_native_spirv_sources}
    ${rt}/frame_cadence_diagnostics.cpp
    ${rt}/runtime_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/test_driver/run_options.cpp
    ${rt}/achievements.cpp
    ${rt}/stats.cpp
    ${rt}/input_bindings.cpp
    ${rt}/host_pointer_events.cpp
    ${rt}/native_input.cpp
    ${rt}/pointer_control.cpp
    ${rt}/rumble.cpp
    ${rt}/switch_controller.cpp
    ${rt}/tilt.cpp
    ${rt}/audio_mixer.cpp
    ${rt}/native_audio.cpp
    ${rt}/movie_audio.cpp
    ${rt}/atomic_file.cpp
    ${rt}/disc_files.cpp
    ${rt}/save_files.cpp
    ${rt}/mii_import.cpp
    ${rt}/mii_formats.cpp
    ${rt}/mod_xml.cpp
    ${rt}/luma.cpp
    ${rt}/mods.cpp
    ${rt}/texture_categories.cpp
    ${rt}/crash_report.cpp
    ${rt}/discord_presence.cpp
    ${rt}/controller_roster.cpp
    ${rt}/wiimote_speaker.cpp)
# Mod import (archives) on the desktop systems; Android and iOS take no mods.
if(NOT ANDROID AND NOT IOS)
    set(NEBULA_ARCHIVE_SOURCES "${nebula_root}/third_party/archive" CACHE PATH "zlib, xz and libarchive sources")
    include("${nebula_root}/cmake/archive_deps.cmake")
    target_sources(galaxy_render_vk PRIVATE ${rt}/mod_import.cpp)
    target_link_libraries(galaxy_render_vk PUBLIC nebula_archive)
endif()
target_include_directories(galaxy_render_vk PUBLIC "${nebula_root}/runtime/include")
target_include_directories(galaxy_render_vk PRIVATE "${nebula_root}/third_party/smaa" "${nebula_root}/native/include" "${galaxy_native_include_dir_spirv}" "${galaxy_utility_spirv_dir}")
# vk_mem_alloc.h leaves pointer nullability unannotated.
target_compile_options(galaxy_render_vk PRIVATE -Wno-nullability-completeness)
target_link_libraries(galaxy_render_vk PUBLIC nebula_vulkan_deps)

# SDL3: the window, Vulkan surface, audio, gamepads and touch. Android loads libSDL3.so from SDL's Java activity.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${NEBULA_SDL_DIR}" SDL3 EXCLUDE_FROM_ALL)

# The native game's host on SDL (native/host/host_main.cpp with NEBULA_HOST_SDL): libmain.so on Android (SDL's
# activity calls its SDL_main), an executable elsewhere.
set(host_sources
    "${nebula_root}/native/host/host_main.cpp"
    "${nebula_root}/runtime/src/movie_video.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/host_window_sdl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio_device_sdl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/display_clock.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/thread_sampler.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/native_game_host_sdl.cpp")
if(ANDROID)
    add_library(main SHARED ${host_sources})
    target_link_libraries(main PRIVATE android log nativewindow)
else()
    add_executable(main ${host_sources})
    set_target_properties(main PROPERTIES OUTPUT_NAME nebula)
endif()
target_include_directories(main PRIVATE "${nebula_root}/native/include" "${nebula_root}/runtime/include")
target_compile_definitions(main PRIVATE NEBULA_HOST_RENDERER=1 NEBULA_HOST_SDL=1)
target_include_directories(main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(main PRIVATE galaxy_render_vk SDL3::SDL3)

