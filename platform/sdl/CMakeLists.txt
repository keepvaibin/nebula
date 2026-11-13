# The native game on SDL3 platforms (Android, Linux, SteamOS; docs/NATIVE_PORT.md "Platforms"): the shared render layer
# with the Vulkan renderer, the host's runtime pieces, and the SDL host. Dependencies are source checkouts named by
# the cache paths below; the game module is native/CMakeLists.txt.
cmake_minimum_required(VERSION 3.22)
project(NebulaSDL LANGUAGES C CXX)
# The game's version, from the main project's file.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../../CMakeLists.txt" nebula_project_line REGEX "^project\\(Nebula VERSION ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" nebula_version "${nebula_project_line}")
add_compile_definitions(NEBULA_VERSION="${nebula_version}")

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

# The shader compiler, for mods' materials: glslang's HLSL front end and the SPIR-V optimizer.
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT ON CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory("${NEBULA_GLSLANG_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/glslang" EXCLUDE_FROM_ALL)
file(GLOB nebula_glslang_spirv_headers "${NEBULA_GLSLANG_SOURCE_DIR}/SPIRV/*.h" "${NEBULA_GLSLANG_SOURCE_DIR}/SPIRV/*.hpp")
file(COPY ${nebula_glslang_spirv_headers} DESTINATION "${CMAKE_BINARY_DIR}/glslang-include/glslang/SPIRV")
add_library(nebula_shader_compiler INTERFACE)
target_include_directories(nebula_shader_compiler SYSTEM INTERFACE
    "${CMAKE_BINARY_DIR}/glslang-include" "${NEBULA_GLSLANG_SOURCE_DIR}" "${NEBULA_GLSLANG_SOURCE_DIR}/External/spirv-tools/include")
target_link_libraries(nebula_shader_compiler INTERFACE glslang SPIRV glslang-default-resource-limits SPIRV-Tools-opt SPIRV-Tools-static)

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
galaxy_program_blob(galaxy_programs_spirv spirv)
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
    ${rt}/render/movie_playback.cpp
    ${rt}/movie_video.cpp
    ${rt}/render/frame_pass.cpp
    ${rt}/render/post_process_settings.cpp
    ${rt}/render/render_core.cpp
    ${rt}/render/affine.cpp
    ${rt}/render/core/compose.cpp
    ${rt}/render/core/draw_walker.cpp
    ${rt}/render/core/rt_walk.cpp
    ${rt}/render/core/rt_bounce.cpp
    ${rt}/render/core/rt_pass_indirect.cpp
    ${rt}/render/core/rt_pass_reflect.cpp
    ${rt}/render/core/rt_pass_transmit.cpp
    ${rt}/render/core/rt_pass_volume.cpp
    ${rt}/render/core/pairing.cpp
    ${rt}/render/core/pipelines.cpp
    ${rt}/render/core/program_table.cpp
    ${rt}/render/core/registries.cpp
    ${rt}/render/core/world_lights.cpp
    ${rt}/render/core/pipelines_vk.cpp
    ${rt}/render/core/encoder_vk.cpp
    ${rt}/render/galaxy_select.cpp
    ${rt}/render/frame_interpolation.cpp
    ${rt}/render/rt_materials.cpp
    ${rt}/render/rt_caps.cpp
    ${rt}/render/rt/rt_shaders.cpp
    ${rt}/render/vk/rt_backend_vk.cpp
    ${rt}/render/rt/rt_geometry.cpp
    ${rt}/render/rt/rt_scene.cpp
    ${rt}/frame_telemetry.cpp
    ${rt}/perf_hint.cpp
    ${rt}/render/vk/vk_context.cpp
    ${rt}/render/vk/device_quirks_vk.cpp
    ${rt}/render/vk/vk_libraries.cpp
    ${rt}/render/vk/utility_spirv.cpp
    ${rt}/render/vk/texture_backend_vk.cpp
    ${rt}/render/mod_shaders.cpp
    ${rt}/render/mod_shader_build.cpp
    ${rt}/render/vk/mod_shader_compile_vk.cpp
    ${rt}/render/mod_post.cpp
    ${rt}/render/vk/mod_post_vk.cpp
    ${rt}/render/vk/mod_shader_present_vk.cpp
    ${rt}/render/vk/shader_compile_vk.cpp
    ${rt}/render/vk/compile_journal.cpp
    ${rt}/render/vk/renderer_vk.cpp
    ${rt}/render/vk/ambient_occlusion_vk.cpp
    ${rt}/render/vk/temporal_upscaler_vk.cpp
    ${rt}/render/vk/sgsr2_vk.cpp
    ${rt}/render/vk/ffx_vk_forward.cpp
    ${rt}/render/vk/fsr_frame_generation_vk.cpp
    ${rt}/render/vk/scene_camera_vk.cpp
    ${rt}/render/vk/renderer_vk_frame.cpp
    ${rt}/render/vk/post_process_vk.cpp
    ${rt}/render/vk/game_bloom_vk.cpp
    "${galaxy_utility_spirv_dir}/utility_spirv_data.h"
    ${rt}/runtime_settings.cpp
    ${rt}/host_extension.cpp
    ${rt}/luma_services_host.cpp
    ${rt}/luma_system.cpp
    ${rt}/luma_mods.cpp
    ${rt}/luma_fs.cpp
    ${rt}/luma_net.cpp
    ${rt}/luma_assets.cpp
    ${rt}/luma_audio.cpp
    ${rt}/luma_capture.cpp
    ${rt}/luma_input.cpp
    ${rt}/achievements.cpp
    ${rt}/stats.cpp
    ${rt}/input_bindings.cpp
    ${rt}/input/mod_actions.cpp
    ${rt}/host_pointer_events.cpp
    ${rt}/input/wiimote_motion.cpp
    ${rt}/input/controls_engine.cpp
    ${rt}/input/live_controls_engine.cpp
    ${rt}/native_input.cpp
    ${rt}/pointer_control.cpp
    ${rt}/rumble.cpp
    ${rt}/switch_controller.cpp
    ${rt}/tilt.cpp
    ${rt}/audio_mixer.cpp
    ${rt}/native_audio.cpp
    ${rt}/movie_audio.cpp
    ${rt}/atomic_file.cpp
    ${rt}/asset_store.cpp
    ${rt}/save_files.cpp
    ${rt}/mii_import.cpp
    ${rt}/mii_formats.cpp
    ${rt}/mod_xml.cpp
    ${rt}/luma.cpp
    ${rt}/mod_log.cpp
    ${rt}/mod_screens.cpp
    ${rt}/mod_start_check.cpp
    ${rt}/mod_plugins.cpp
    ${rt}/game_module.cpp
    ${nebula_root}/native/host/luma_guard.cpp
    ${nebula_root}/native/host/crash_watch.cpp
    ${rt}/mods.cpp
    ${rt}/mod_consent.cpp
    ${rt}/texture_categories.cpp
    ${rt}/dolphin_pack.cpp
    ${rt}/crash_report.cpp
    ${rt}/discord_presence.cpp
    ${rt}/controller_roster.cpp
    ${rt}/wiimote_speaker.cpp)
# Mod import (folders and archives).
if(NOT IOS)
    set(NEBULA_ARCHIVE_SOURCES "${nebula_root}/third_party/archive" CACHE PATH "zlib, xz and libarchive sources")
    include("${nebula_root}/cmake/archive_deps.cmake")
    target_sources(galaxy_render_vk PRIVATE ${rt}/mod_import.cpp)
    target_link_libraries(galaxy_render_vk PUBLIC nebula_archive)
    # The host's stand-in importer (host_window_sdl.cpp) is for the systems without this one.
    target_compile_definitions(galaxy_render_vk PUBLIC NEBULA_MOD_IMPORT=1)
endif()
# TLS (BoringSSL) and gzip answers for the net services (luma_net.cpp, net 1.3), on every platform this builds.
include("${nebula_root}/third_party/boringssl.cmake")
include("${nebula_root}/third_party/zlib_inflate.cmake")
target_link_libraries(galaxy_render_vk PRIVATE nebula_bssl_ssl nebula_zinflate)
target_include_directories(galaxy_render_vk PUBLIC "${nebula_root}/runtime/src")
target_include_directories(galaxy_render_vk PUBLIC "${nebula_root}/runtime/include")
target_include_directories(galaxy_render_vk PRIVATE "${nebula_root}/third_party/smaa" "${nebula_root}/native/include" "${galaxy_utility_spirv_dir}")
# The tracer's SPIR-V, where a dxc that writes it is named (cmake/rt_shaders.cmake).
include("${nebula_root}/cmake/rt_shaders.cmake")
galaxy_find_dxc_spirv(galaxy_dxc_spirv)
if(galaxy_dxc_spirv)
    galaxy_rt_shaders(galaxy_rt_spirv spirv "${galaxy_dxc_spirv}")
    target_sources(galaxy_render_vk PRIVATE "${galaxy_rt_spirv}")
    target_include_directories(galaxy_render_vk PRIVATE "${galaxy_rt_shader_dir}")
    target_compile_definitions(galaxy_render_vk PRIVATE GALAXY_RT_SPIRV=1)
endif()
# vk_mem_alloc.h leaves pointer nullability unannotated.
target_compile_options(galaxy_render_vk PRIVATE -Wno-nullability-completeness)
target_link_libraries(galaxy_render_vk PUBLIC nebula_vulkan_deps)
target_link_libraries(galaxy_render_vk PRIVATE nebula_shader_compiler)
# AV1 movies (plan 3.10): dav1d 1.5.1 built in-tree.
option(NEBULA_AV1 "Decode AV1 movies with dav1d (plan 3.10)" ON)
if(NEBULA_AV1)
    include("${nebula_root}/cmake/dav1d.cmake")
    target_link_libraries(galaxy_render_vk PRIVATE nebula_dav1d)
    target_compile_definitions(galaxy_render_vk PRIVATE NEBULA_AV1=1)
endif()

# AMD FSR 3 (cmake/fidelityfx_android.cmake).
include("${nebula_root}/cmake/fidelityfx_android.cmake")
nebula_fidelityfx_android("${NEBULA_VULKAN_HEADERS_DIR}/include" "${rt}/render/vk/ffx_portable.h" "${rt}/render/vk/ffx_vk_names.h")
if(TARGET nebula_fidelityfx_vk)
    target_link_libraries(galaxy_render_vk PRIVATE nebula_fidelityfx_vk)
    set_source_files_properties(${rt}/render/vk/temporal_upscaler_vk.cpp PROPERTIES
        COMPILE_OPTIONS "-include;${rt}/render/vk/ffx_portable.h")
endif()

# SDL3: the window, Vulkan surface, audio, gamepads and touch. Android loads libSDL3.so from SDL's Java activity.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${NEBULA_SDL_DIR}" SDL3 EXCLUDE_FROM_ALL)
if(ANDROID)
    # luma_capture.cpp asks the player for a microphone or camera through SDL (SDL_RequestAndroidPermission, plan
    # 8.2 c): SDL's headers here; the function comes with libSDL3.so, which main links.
    target_link_libraries(galaxy_render_vk PRIVATE SDL3::Headers)
endif()

# The native game's host on SDL (native/host/host_main.cpp with NEBULA_HOST_SDL): libmain.so on Android (SDL's
# activity calls its SDL_main), an executable elsewhere.
set(host_sources
    "${nebula_root}/native/host/host_main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/host_window_sdl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/luma_system_sdl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio_device_sdl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/display_clock.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/thread_sampler.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/native_game_host_sdl.cpp")
if(ANDROID)
    # A real Wii Remote on a DolphinBar (wiimote_hid.h over wiimote_link_android.cpp).
    list(APPEND host_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/wiimote_link_android.cpp"
        ${rt}/wiimote_hid.cpp
        ${rt}/wiimote_protocol.cpp
        ${rt}/input/wiimote_input.cpp
        ${rt}/input/wiimote_motion_pointer.cpp)
    add_library(main SHARED ${host_sources})
    # aaudio, camera2ndk, mediandk: LumaCore's capture services (plan 8.2 c, runtime/src/luma_capture.cpp).
    target_link_libraries(main PRIVATE android log nativewindow aaudio camera2ndk mediandk)
else()
    add_executable(main ${host_sources})
    set_target_properties(main PROPERTIES OUTPUT_NAME nebula)
endif()
target_sources(main PRIVATE ${galaxy_programs_spirv})
target_include_directories(main PRIVATE "${nebula_root}/native/include" "${nebula_root}/runtime/include")
target_compile_definitions(main PRIVATE NEBULA_HOST_RENDERER=1 NEBULA_HOST_SDL=1)
target_include_directories(main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(main PRIVATE galaxy_render_vk SDL3::SDL3)

# The launcher's mod and Mii import on Android (libnebula_mods.so): the game's own importers, in the launcher's process.
if(ANDROID)
    add_library(nebula_mods SHARED
        "${CMAKE_CURRENT_SOURCE_DIR}/mod_import_jni.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/mii_import_jni.cpp"
        ${rt}/mii_import.cpp
        ${rt}/mii_formats.cpp
        ${rt}/mod_import.cpp
        ${rt}/mods.cpp
        ${rt}/mod_consent.cpp
        ${rt}/mod_xml.cpp
        ${rt}/luma.cpp
        ${rt}/texture_categories.cpp
        ${rt}/dolphin_pack.cpp
        ${rt}/asset_store.cpp
        ${rt}/atomic_file.cpp)
    target_include_directories(nebula_mods PRIVATE "${nebula_root}/runtime/include" "${nebula_root}/native/include")
    target_link_libraries(nebula_mods PRIVATE nebula_archive log)
    target_link_options(nebula_mods PRIVATE "-Wl,--gc-sections")
endif()

