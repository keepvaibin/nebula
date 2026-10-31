# The native game on SDL3 platforms (Android, Linux, SteamOS; docs/NATIVE_PORT.md "Platforms"): the shared GX layer
# with the Vulkan renderer, the host's runtime pieces, and the SDL host. Dependencies are source checkouts named by
# the cache paths below; the game module is native/CMakeLists.txt.
cmake_minimum_required(VERSION 3.22)
project(NebulaSDL LANGUAGES C CXX)

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

# glslang with its HLSL front end and the SPIR-V optimizer, as the Windows build links it.
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT ON CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory("${NEBULA_GLSLANG_SOURCE_DIR}" glslang EXCLUDE_FROM_ALL)
# The renderer includes glslang's headers in their installed layout (glslang/SPIRV/...).
file(GLOB glslang_spirv_headers "${NEBULA_GLSLANG_SOURCE_DIR}/SPIRV/*.h" "${NEBULA_GLSLANG_SOURCE_DIR}/SPIRV/*.hpp")
file(COPY ${glslang_spirv_headers} DESTINATION "${CMAKE_BINARY_DIR}/glslang-include/glslang/SPIRV")

add_library(nebula_vulkan_deps INTERFACE)
target_include_directories(nebula_vulkan_deps INTERFACE
    "${NEBULA_VULKAN_HEADERS_DIR}/include" "${NEBULA_VOLK_DIR}" "${NEBULA_VMA_DIR}/include"
    "${CMAKE_BINARY_DIR}/glslang-include" "${NEBULA_GLSLANG_SOURCE_DIR}"
    "${NEBULA_GLSLANG_SOURCE_DIR}/External/spirv-tools/include")
target_compile_definitions(nebula_vulkan_deps INTERFACE NEBULA_GPU_VULKAN VK_NO_PROTOTYPES
    $<$<PLATFORM_ID:Android>:VK_USE_PLATFORM_ANDROID_KHR>)
target_link_libraries(nebula_vulkan_deps INTERFACE glslang SPIRV glslang-default-resource-limits SPIRV-Tools-opt
    SPIRV-Tools-static)

# Uber shader sources become NUL-terminated char arrays, as in the root build.
set(galaxy_uber_embed_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/uber_shader_embed.cpp")
set(galaxy_uber_embed_body "")
foreach(stage vs ps)
    set(hlsl "${nebula_root}/runtime/src/render/uber_${stage}.hlsl")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${hlsl}")
    file(READ "${hlsl}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
    string(APPEND galaxy_uber_embed_body "extern const char galaxy_uber_${stage}_hlsl[] = {${hex}0x00};\n")
endforeach()
file(CONFIGURE OUTPUT "${galaxy_uber_embed_cpp}" CONTENT "namespace galaxy::render {\n${galaxy_uber_embed_body}}\n" @ONLY)

# The post-processing passes' sources (post_process_vk.cpp compiles them on the device).
include("${nebula_root}/cmake/post_shader_sources.cmake")
set(galaxy_post_embed_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/post_shader_embed.cpp")
galaxy_embed_post_sources("${galaxy_post_embed_cpp}" "${nebula_root}")

set(rt "${nebula_root}/runtime/src")
add_library(galaxy_gx_vk STATIC
    ${rt}/gx/renderer_api.cpp
    ${rt}/gx/gx_state.cpp
    ${rt}/gx/frame_completion.cpp
    ${rt}/gx/render_config.cpp
    ${rt}/gx/renderer_common.cpp
    ${rt}/gx/upload_ring.cpp
    ${rt}/gx/texture_cache.cpp
    ${rt}/gx/texture_pack.cpp
    ${rt}/gx/texture_pack_codecs.cpp
    ${rt}/gx/movie_replacement.cpp
    ${rt}/gx/vertex_loader.cpp
    ${rt}/gx/curved_surfaces.cpp
    ${rt}/gx/native_shapes.cpp
    ${rt}/gx/shader_keys.cpp
    ${rt}/gx/efb_copy.cpp
    ${rt}/gx/line_point_raster.cpp
    ${rt}/gx/shader_gen.cpp
    ${rt}/gx/uber_shaders.cpp
    ${galaxy_uber_embed_cpp}
    ${rt}/gx/post_process_settings.cpp
    ${rt}/gx/transform_history.cpp
    ${rt}/gx/fifo_parser.cpp
    ${rt}/gx/render_core.cpp
    ${rt}/gx/game_lights.cpp
    ${rt}/gx/game_objects.cpp
    ${rt}/gx/rt_tev_eval.cpp
    ${rt}/gx/galaxy_select.cpp
    ${rt}/gx/frame_interpolation.cpp
    ${rt}/gx/interp_gpu.cpp
    ${rt}/gx/rt_surfaces.cpp
    ${rt}/gx/rt_materials.cpp
    ${rt}/gx/enhanced_lighting.cpp
    ${rt}/frame_telemetry.cpp
    ${rt}/perf_hint.cpp
    ${rt}/gx/game_asset_extract.cpp
    ${rt}/gx/vk/vk_context.cpp
    ${rt}/gx/vk/device_quirks_vk.cpp
    ${rt}/gx/vk/vk_libraries.cpp
    ${rt}/gx/vk/shader_compile_vk.cpp
    ${rt}/gx/vk/texture_backend_vk.cpp
    ${rt}/gx/vk/pipeline_cache_vk.cpp
    ${rt}/gx/vk/compile_journal.cpp
    ${rt}/gx/vk/renderer_vk.cpp
    ${rt}/gx/vk/renderer_vk_efb.cpp
    ${rt}/gx/vk/renderer_vk_replay.cpp
    ${rt}/gx/vk/post_process_vk.cpp
    ${rt}/gx/vk/game_bloom_vk.cpp
    ${galaxy_post_embed_cpp}
    ${rt}/frame_cadence_diagnostics.cpp
    ${rt}/runtime_settings.cpp
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
    ${rt}/atomic_file.cpp
    ${rt}/mii_import.cpp
    ${rt}/mii_formats.cpp
    ${rt}/mod_xml.cpp
    ${rt}/virtual_disc.cpp
    ${rt}/mods.cpp
    ${rt}/crash_report.cpp
    ${rt}/discord_presence.cpp)
# Mod import (archives) on the desktop systems; Android and iOS take no mods.
if(NOT ANDROID AND NOT IOS)
    set(NEBULA_ARCHIVE_SOURCES "${nebula_root}/third_party/archive" CACHE PATH "zlib, xz and libarchive sources")
    include("${nebula_root}/cmake/archive_deps.cmake")
    target_sources(galaxy_gx_vk PRIVATE ${rt}/mod_import.cpp)
    target_link_libraries(galaxy_gx_vk PUBLIC nebula_archive)
endif()
target_include_directories(galaxy_gx_vk PUBLIC "${nebula_root}/runtime/include")
target_include_directories(galaxy_gx_vk PRIVATE "${nebula_root}/third_party/smaa")
target_compile_options(galaxy_gx_vk PUBLIC "SHELL:-include ${nebula_root}/runtime/include/galaxy/posix_compat.h")
# vk_mem_alloc.h leaves pointer nullability unannotated.
target_compile_options(galaxy_gx_vk PRIVATE -Wno-nullability-completeness)
target_link_libraries(galaxy_gx_vk PUBLIC nebula_vulkan_deps)

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
    "${nebula_root}/runtime/src/native_thp_video.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/host_window_sdl.cpp"
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
target_link_libraries(main PRIVATE galaxy_gx_vk SDL3::SDL3)

# The vector kernels against their references on this platform's CPU (NEON on AArch64).
add_executable(nebula_simd_kernel_tests "${nebula_root}/runtime/src/render/simd_kernel_tests.cpp")
target_link_libraries(nebula_simd_kernel_tests PRIVATE galaxy_gx_vk)
