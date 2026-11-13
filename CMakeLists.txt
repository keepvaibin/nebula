cmake_minimum_required(VERSION 3.24)
project(Nebula VERSION 2.0.1 LANGUAGES C CXX)
add_compile_definitions(NEBULA_VERSION="${PROJECT_VERSION}")

# One build: a release and a test run use the same binaries (tools/release_check.ps1 checks the sources and the release).
# The standard library's bounds checks (array, vector, span, string_view subscripts and the like) in every host build: an
# index past the end stops the program at once, never reading or writing a neighbouring member.
if(MSVC)
    add_compile_definitions(_MSVC_STL_HARDENING=1)
endif()

# The Windows build: NebulaNative (D3D12) and, with NEBULA_VULKAN, NebulaNativeVk. The game module is
# native/CMakeLists.txt; Android and Linux build from platform/sdl.
if(NOT WIN32)
    message(FATAL_ERROR "This build targets Windows x64; other platforms build from platform/sdl.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Nebula requires a 64-bit build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The mods importer's archive libraries (zlib, liblzma, libarchive), built once per build tree.
# The DirectX 12 Agility SDK's headers before the Windows SDK's, for every target (cmake/agility_sdk.cmake).
include(cmake/agility_sdk.cmake)
include(cmake/archive_deps.cmake)
# TLS (BoringSSL) and gzip answers for the net services (runtime/src/luma_net.cpp, net 1.3).
include(third_party/boringssl.cmake)
include(third_party/zlib_inflate.cmake)

# NVIDIA's runtime binaries (Streamline, DLSS) never enter the source tree: players install them themselves.
file(GLOB_RECURSE GALAXY_VENDOR_BINARIES "${CMAKE_SOURCE_DIR}/runtime/*nvngx*.dll"
     "${CMAKE_SOURCE_DIR}/runtime/*sl.*.dll" "${CMAKE_SOURCE_DIR}/third_party/*nvngx*.dll"
     "${CMAKE_SOURCE_DIR}/third_party/*sl.*.dll")
if(GALAXY_VENDOR_BINARIES)
    message(FATAL_ERROR "NVIDIA runtime binaries must not be in the source tree: ${GALAXY_VENDOR_BINARIES}")
endif()

if(MSVC)
    foreach(lang C CXX)
        string(REGEX REPLACE "(^| )/Ob[0-9]" "\\1/Ob3" CMAKE_${lang}_FLAGS_RELEASE "${CMAKE_${lang}_FLAGS_RELEASE}")
    endforeach()
    # Sources and narrow literals are UTF-8 whatever the build machine's code page.
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>)
    # AUTO builds for AVX2 when the build machine and its OS run it (Setup refuses CPUs without it), else SSE2.
    set(GALAXY_NATIVE_ISA "AUTO" CACHE STRING "Native CPU target: AUTO, SSE2, or AVX2")
    set_property(CACHE GALAXY_NATIVE_ISA PROPERTY STRINGS AUTO SSE2 AVX2)
    string(TOUPPER "${GALAXY_NATIVE_ISA}" GALAXY_NATIVE_ISA)
    if(NOT GALAXY_NATIVE_ISA MATCHES "^(AUTO|SSE2|AVX2)$")
        message(FATAL_ERROR "GALAXY_NATIVE_ISA must be AUTO, SSE2, or AVX2")
    endif()
    set(galaxy_native_isa_effective "${GALAXY_NATIVE_ISA}")
    if(GALAXY_NATIVE_ISA STREQUAL "AUTO")
        if(CMAKE_CROSSCOMPILING)
            set(galaxy_native_isa_effective "SSE2")
        else()
            include(CheckCXXSourceRuns)
            check_cxx_source_runs(
                "#include <intrin.h>
                int main() {
                    int cpu[4] = {};
                    __cpuid(cpu, 0);
                    if (cpu[0] < 7) return 1;
                    __cpuidex(cpu, 1, 0);
                    constexpr int required_ecx = (1 << 12) | (1 << 27) | (1 << 28);
                    if ((cpu[2] & required_ecx) != required_ecx) return 2;
                    if ((_xgetbv(0) & 0x6u) != 0x6u) return 3;
                    __cpuidex(cpu, 7, 0);
                    return (cpu[1] & (1 << 5)) != 0 ? 0 : 4;
                }"
                GALAXY_HOST_CAN_RUN_AVX2_FMA
            )
            if(GALAXY_HOST_CAN_RUN_AVX2_FMA)
                set(galaxy_native_isa_effective "AVX2")
            else()
                set(galaxy_native_isa_effective "SSE2")
            endif()
        endif()
    endif()
    message(STATUS "Nebula CPU target: ${galaxy_native_isa_effective} (requested ${GALAXY_NATIVE_ISA})")
    if(galaxy_native_isa_effective STREQUAL "AVX2")
        # clang-cl takes the GCC spelling.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-mavx2>)
        else()
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/arch:AVX2>)
        endif()
    endif()
endif()

# The host libraries' Release code: no buffer security checks, no RTTI.
function(galaxy_host_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /GS- /GR- /Oi /favor:INTEL64)
    endif()
endfunction()

include_directories("${CMAKE_CURRENT_LIST_DIR}/native/include")

add_library(galaxy_runtime_abi INTERFACE)
target_include_directories(galaxy_runtime_abi INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")

add_library(galaxy_atomic_file STATIC runtime/src/atomic_file.cpp)
target_link_libraries(galaxy_atomic_file PUBLIC galaxy_runtime_abi)

add_library(galaxy_asset_store STATIC runtime/src/asset_store.cpp)
target_link_libraries(galaxy_asset_store PUBLIC galaxy_runtime_abi)

add_library(galaxy_save_files STATIC runtime/src/save_files.cpp)
target_link_libraries(galaxy_save_files PUBLIC galaxy_runtime_abi galaxy_atomic_file)

# The host's extensions (host_extension.h, --extension).
add_library(galaxy_host_extension STATIC runtime/src/host_extension.cpp runtime/src/luma_services_host.cpp)
target_link_libraries(galaxy_host_extension PUBLIC galaxy_runtime_abi)
galaxy_host_options(galaxy_host_extension)
# The test runs' extension (run.sh: --extension): scripted controls, pictures, sound, menus and device losses from the
# run's GALAXY_* environment. Never in an install.
add_library(nebula_test_extension MODULE tools/test_driver/extension/test_extension.cpp
    tools/test_driver/extension/harness.cpp tools/test_driver/extension/test_services.cpp tools/test_driver/extension/test_plugin.cpp tools/test_driver/extension/data_guard.cpp runtime/src/hidden_start.cpp)
target_link_libraries(nebula_test_extension PRIVATE galaxy_runtime_abi)
if(WIN32)
    target_link_libraries(nebula_test_extension PRIVATE shell32)
else()
    target_link_libraries(nebula_test_extension PRIVATE ${CMAKE_DL_LIBS})
endif()
target_compile_features(nebula_test_extension PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(nebula_test_extension PRIVATE /W4 /WX /permissive- /EHsc)
endif()

add_library(galaxy_runtime_settings STATIC
    runtime/src/runtime_settings.cpp
    runtime/src/input_bindings.cpp
    runtime/src/input/mod_actions.cpp
    runtime/src/achievements.cpp
    runtime/src/stats.cpp)
target_link_libraries(galaxy_runtime_settings PUBLIC galaxy_runtime_abi galaxy_atomic_file galaxy_host_extension)
galaxy_host_options(galaxy_runtime_settings)

add_library(galaxy_host_pointer_events STATIC runtime/src/host_pointer_events.cpp)
target_link_libraries(galaxy_host_pointer_events PUBLIC galaxy_runtime_abi)
galaxy_host_options(galaxy_host_pointer_events)

add_library(galaxy_native_input STATIC
    runtime/src/controller_roster.cpp
    runtime/src/generic_hid.cpp
    runtime/src/input/wiimote_input.cpp
    runtime/src/input/wiimote_motion.cpp
    runtime/src/input/wiimote_motion_pointer.cpp
    runtime/src/native_input.cpp
    runtime/src/playstation_controller.cpp
    runtime/src/simple_pads.cpp
    runtime/src/switch2_ble.cpp
    runtime/src/pointer_control.cpp
    runtime/src/rumble.cpp
    runtime/src/steam_controller.cpp
    runtime/src/switch_controller.cpp
    runtime/src/tilt.cpp
    runtime/src/wiimote_protocol.cpp
    runtime/src/wiimote_speaker.cpp)
target_link_libraries(galaxy_native_input PUBLIC galaxy_runtime_abi)
galaxy_host_options(galaxy_native_input)

# The game's sound mixed natively (audio_mixer.h).
add_library(galaxy_audio_mixer STATIC runtime/src/audio_mixer.cpp runtime/src/movie_audio.cpp)
target_include_directories(galaxy_audio_mixer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
                           "${CMAKE_CURRENT_SOURCE_DIR}/native/include")
galaxy_host_options(galaxy_audio_mixer)

add_library(galaxy_native_host STATIC
    runtime/src/game_paths.cpp
    runtime/src/hidden_start.cpp
    runtime/src/input/host_input.cpp
    runtime/src/input/controls_engine.cpp
    runtime/src/input/live_controls_engine.cpp
    runtime/src/input/key_positions_win32.cpp
    runtime/src/input/raw_mouse_win32.cpp
    runtime/src/input/rumble_route.cpp
    runtime/src/native_audio.cpp
    runtime/src/audio_device_wasapi.cpp
    runtime/src/mii_import.cpp
    runtime/src/mii_formats.cpp
    runtime/src/mod_xml.cpp
    runtime/src/luma.cpp
    runtime/src/mod_log.cpp
    runtime/src/mod_screens.cpp
    runtime/src/mod_start_check.cpp
    runtime/src/luma_system.cpp
    runtime/src/luma_system_win32.cpp
    runtime/src/luma_mods.cpp
    runtime/src/luma_fs.cpp
    runtime/src/luma_net.cpp
    runtime/src/luma_assets.cpp
    runtime/src/luma_audio.cpp
    runtime/src/luma_capture.cpp
    runtime/src/luma_input.cpp
    runtime/src/mod_plugins.cpp
    runtime/src/game_module.cpp
    native/host/luma_guard.cpp
    runtime/src/mods.cpp
    runtime/src/mod_consent.cpp
    runtime/src/texture_categories.cpp
    runtime/src/dolphin_pack.cpp
    runtime/src/mod_import.cpp
    runtime/src/vendor_download.cpp
    runtime/src/crash_report.cpp
    runtime/src/discord_presence.cpp
    runtime/src/controller_hid.cpp
    runtime/src/switch2_ble_win.cpp
    runtime/src/wiimote_hid.cpp
    runtime/src/wiimote_link_win32.cpp)
target_link_libraries(galaxy_native_host
    PUBLIC
        galaxy_runtime_abi
        galaxy_runtime_settings
        galaxy_native_input
        galaxy_audio_mixer
        galaxy_atomic_file
        nebula_archive
        nebula_bssl_ssl
        nebula_zinflate
        shell32
        bcrypt
        runtimeobject
        hid
        ole32
        setupapi
        cfgmgr32
        xinput9_1_0
)
galaxy_host_options(galaxy_native_host)

# The same passes for the Vulkan renderer, compiled on the device (cmake/post_shader_sources.cmake).
include(cmake/post_shader_sources.cmake)
set(galaxy_post_embed_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/post_shader_embed.cpp")
galaxy_embed_post_sources("${galaxy_post_embed_cpp}" "${CMAKE_CURRENT_SOURCE_DIR}")

# Presentation post-processing passes (runtime/src/render/post_process.hlsl with
# the vendored SMAA and FidelityFX FSR 1 sources) are compiled by fxc at build
# time into bytecode headers, so the runtime never compiles them at startup.
find_program(GALAXY_FXC fxc
    HINTS "$ENV{WindowsSdkVerBinPath}x64" "$ENV{WindowsSdkBinPath}x64"
    REQUIRED)
set(galaxy_post_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/post_shaders")
file(MAKE_DIRECTORY "${galaxy_post_dir}")
set(galaxy_post_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/post_process.hlsl")
set(galaxy_smaa_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl")
set(galaxy_ffx_a_h "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fidelityfx-fsr1/ffx_a.h")
set(galaxy_ffx_fsr1_h "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fidelityfx-fsr1/ffx_fsr1.h")
foreach(galaxy_post_input
        "${galaxy_post_hlsl}" "${galaxy_smaa_hlsl}" "${galaxy_ffx_a_h}" "${galaxy_ffx_fsr1_h}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${galaxy_post_input}")
endforeach()
file(READ "${galaxy_post_hlsl}" galaxy_post_src)
file(READ "${galaxy_smaa_hlsl}" galaxy_smaa_src)
file(READ "${galaxy_ffx_a_h}" galaxy_ffx_a_src)
file(READ "${galaxy_ffx_fsr1_h}" galaxy_ffx_fsr1_src)
set(galaxy_post_headers "")
# One composed source and one bytecode header (g_<name>) per pass variant;
# `preset` is an SMAA_PRESET_* macro or "-", `library` smaa, fsr or none.
function(galaxy_post_shader name pass preset library stage entry)
    set(text "#define ${pass} 1\n")
    if(NOT preset STREQUAL "-")
        string(APPEND text "#define ${preset} 1\n")
    endif()
    string(APPEND text "#define POST_SECTION 1\n${galaxy_post_src}\n")
    if(library STREQUAL "smaa")
        string(APPEND text "${galaxy_smaa_src}\n")
    elseif(library STREQUAL "fsr")
        string(APPEND text "${galaxy_ffx_a_src}\n#undef POST_SECTION\n#define POST_SECTION 2\n"
            "${galaxy_post_src}\n${galaxy_ffx_fsr1_src}\n")
    endif()
    string(APPEND text "#undef POST_SECTION\n#define POST_SECTION 3\n${galaxy_post_src}\n")
    set(source "${galaxy_post_dir}/${name}.hlsl")
    file(WRITE "${source}.tmp" "${text}")
    configure_file("${source}.tmp" "${source}" COPYONLY)
    set(header "${galaxy_post_dir}/${name}.h")
    add_custom_command(OUTPUT "${header}"
        COMMAND "${GALAXY_FXC}" /nologo /T ${stage} /E ${entry} /O3 /Ges
            /Vn g_${name} /Fh "${header}" "${source}"
        DEPENDS "${source}"
        VERBATIM)
    set(galaxy_post_headers ${galaxy_post_headers} "${header}" PARENT_SCOPE)
endfunction()
galaxy_post_shader(post_fullscreen_vs PASS_RESAMPLE - none vs_5_0 VSMain)
galaxy_post_shader(post_resample_ps PASS_RESAMPLE - none ps_5_0 PSMain)
galaxy_post_shader(post_fxaa_ps PASS_FXAA - none ps_5_0 PSMain)
galaxy_post_shader(post_fsr_easu_ps PASS_FSR_EASU - fsr ps_5_0 PSMain)
galaxy_post_shader(post_fsr_rcas_ps PASS_FSR_RCAS - fsr ps_5_0 PSMain)
galaxy_post_shader(post_fsr_rcas_scaled_ps PASS_FSR_RCAS_SCALED - fsr ps_5_0 PSMain)
galaxy_post_shader(post_smaa_edges_vs PASS_SMAA_EDGES SMAA_PRESET_HIGH smaa vs_5_0 VSMain)
galaxy_post_shader(post_smaa_blend_vs PASS_SMAA_BLEND SMAA_PRESET_HIGH smaa vs_5_0 VSMain)
galaxy_post_shader(post_smaa_blend_ps PASS_SMAA_BLEND SMAA_PRESET_HIGH smaa ps_5_0 PSMain)
galaxy_post_shader(post_smaa_blend_keep_ps PASS_SMAA_BLEND_KEEP SMAA_PRESET_HIGH smaa ps_5_0 PSMain)
foreach(galaxy_smaa_preset LOW MEDIUM HIGH ULTRA)
    string(TOLOWER "${galaxy_smaa_preset}" galaxy_smaa_suffix)
    galaxy_post_shader(post_smaa_edges_ps_${galaxy_smaa_suffix}
        PASS_SMAA_EDGES SMAA_PRESET_${galaxy_smaa_preset} smaa ps_5_0 PSMain)
    galaxy_post_shader(post_smaa_weights_vs_${galaxy_smaa_suffix}
        PASS_SMAA_WEIGHTS SMAA_PRESET_${galaxy_smaa_preset} smaa vs_5_0 VSMain)
    galaxy_post_shader(post_smaa_weights_ps_${galaxy_smaa_suffix}
        PASS_SMAA_WEIGHTS SMAA_PRESET_${galaxy_smaa_preset} smaa ps_5_0 PSMain)
endforeach()

# Ambient occlusion (runtime/src/render/ambient_occlusion.hlsl), compiled by fxc like the post passes.
set(galaxy_ao_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/ambient_occlusion.hlsl")
set(galaxy_ao_headers "")
foreach(galaxy_ao_pass distance_cs:DepthCS:cs_5_0 occlusion_cs:AoCS:cs_5_0 ssao_cs:SsaoCS:cs_5_0 denoise_cs:DenoiseCS:cs_5_0
        upsample_cs:UpsampleCS:cs_5_0 apply_vs:ApplyVS:vs_5_0 apply_ps:ApplyPS:ps_5_0)
    string(REPLACE ":" ";" galaxy_ao_pair "${galaxy_ao_pass}")
    list(GET galaxy_ao_pair 0 galaxy_ao_name)
    list(GET galaxy_ao_pair 1 galaxy_ao_entry)
    list(GET galaxy_ao_pair 2 galaxy_ao_profile)
    set(galaxy_ao_header "${galaxy_post_dir}/ao_${galaxy_ao_name}.h")
    set(galaxy_ao_stage /DAO_GRAPHICS=1)
    if(galaxy_ao_profile MATCHES "^cs_")
        set(galaxy_ao_stage /DAO_COMPUTE=1)
    endif()
    add_custom_command(OUTPUT "${galaxy_ao_header}"
        COMMAND "${GALAXY_FXC}" /nologo /T ${galaxy_ao_profile} /E ${galaxy_ao_entry} /O3 /Ges ${galaxy_ao_stage}
            /DAO_PASS_${galaxy_ao_name}=1 /Vn g_ao_${galaxy_ao_name} /Fh "${galaxy_ao_header}" "${galaxy_ao_hlsl}"
        DEPENDS "${galaxy_ao_hlsl}"
        VERBATIM)
    list(APPEND galaxy_ao_headers "${galaxy_ao_header}")
endforeach()

# The wide scene's camera (runtime/src/render/scene_camera.hlsl), compiled by fxc like the post passes.
set(galaxy_camera_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/scene_camera.hlsl")
set(galaxy_camera_headers "")
foreach(galaxy_camera_pass meter_cs:MeterCS:cs_5_0:CAMERA_METER vs:CameraVS:vs_5_0:CAMERA_PLAIN ps:CameraPS:ps_5_0:CAMERA_PLAIN
        samples_ps:CameraPS:ps_5_0:CAMERA_SAMPLES resolve_ps:CameraResolvePS:ps_5_0:CAMERA_RESOLVE
        widen_ps:WidenPS:ps_5_0:CAMERA_PLAIN)
    string(REPLACE ":" ";" galaxy_camera_pair "${galaxy_camera_pass}")
    list(GET galaxy_camera_pair 0 galaxy_camera_name)
    list(GET galaxy_camera_pair 1 galaxy_camera_entry)
    list(GET galaxy_camera_pair 2 galaxy_camera_profile)
    list(GET galaxy_camera_pair 3 galaxy_camera_define)
    set(galaxy_camera_header "${galaxy_post_dir}/camera_${galaxy_camera_name}.h")
    add_custom_command(OUTPUT "${galaxy_camera_header}"
        COMMAND "${GALAXY_FXC}" /nologo /T ${galaxy_camera_profile} /E ${galaxy_camera_entry} /O3 /Ges
            /D${galaxy_camera_define}=1 /Vn g_camera_${galaxy_camera_name} /Fh "${galaxy_camera_header}" "${galaxy_camera_hlsl}"
        DEPENDS "${galaxy_camera_hlsl}"
        VERBATIM)
    list(APPEND galaxy_camera_headers "${galaxy_camera_header}")
endforeach()

# The game's bloom (runtime/src/render/game_bloom.hlsl), compiled by fxc like the post passes.
set(galaxy_bloom_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/game_bloom.hlsl")
set(galaxy_bloom_headers "")
foreach(galaxy_bloom_pass down_cs:DownCS:cs_5_0 box_cs:BoxCS:cs_5_0 blur_cs:BlurCS:cs_5_0
        composite_vs:CompositeVS:vs_5_0 composite_ps:CompositePS:ps_5_0)
    string(REPLACE ":" ";" galaxy_bloom_pair "${galaxy_bloom_pass}")
    list(GET galaxy_bloom_pair 0 galaxy_bloom_name)
    list(GET galaxy_bloom_pair 1 galaxy_bloom_entry)
    list(GET galaxy_bloom_pair 2 galaxy_bloom_profile)
    set(galaxy_bloom_header "${galaxy_post_dir}/bloom_${galaxy_bloom_name}.h")
    add_custom_command(OUTPUT "${galaxy_bloom_header}"
        COMMAND "${GALAXY_FXC}" /nologo /T ${galaxy_bloom_profile} /E ${galaxy_bloom_entry} /O3 /Ges
            /Vn g_bloom_${galaxy_bloom_name} /Fh "${galaxy_bloom_header}" "${galaxy_bloom_hlsl}"
        DEPENDS "${galaxy_bloom_hlsl}"
        VERBATIM)
    list(APPEND galaxy_bloom_headers "${galaxy_bloom_header}")
endforeach()

# dxc (the Windows SDK's), for the shaders compiled at build.
find_program(GALAXY_DXC dxc
    HINTS "$ENV{WindowsSdkVerBinPath}x64" "$ENV{WindowsSdkBinPath}x64"
    NO_DEFAULT_PATH)
if(NOT GALAXY_DXC)
    find_program(GALAXY_DXC dxc REQUIRED)
endif()

# The programs, compiled by dxc at build into one file (cmake/native_materials.cmake).
include(cmake/native_materials.cmake)
galaxy_program_blob(galaxy_programs_dxil dxil)

# The tracer's shaders, compiled by dxc at build (cmake/rt_shaders.cmake).
include(cmake/rt_shaders.cmake)
galaxy_rt_shaders(galaxy_rt_dxil dxil "${GALAXY_DXC}")

# The renderer's utility shaders, compiled at build (E-19): the player's machine compiles nothing.
add_executable(galaxy_utility_shaders tools/dev/utility_shaders.cpp runtime/src/render/renderer_common.cpp)
target_include_directories(galaxy_utility_shaders PRIVATE runtime/include)
target_compile_features(galaxy_utility_shaders PRIVATE cxx_std_20)
target_link_libraries(galaxy_utility_shaders PRIVATE d3dcompiler)
set(galaxy_utility_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/utility_shaders")
file(MAKE_DIRECTORY "${galaxy_utility_shader_dir}")
add_custom_command(OUTPUT "${galaxy_utility_shader_dir}/utility_shaders_dxbc.h"
    COMMAND galaxy_utility_shaders "${galaxy_utility_shader_dir}/utility_shaders_dxbc.h"
    DEPENDS galaxy_utility_shaders VERBATIM)

add_library(galaxy_render STATIC
    runtime/src/render/utility_shaders.cpp
    "${galaxy_utility_shader_dir}/utility_shaders_dxbc.h"
    runtime/src/render/render_api.cpp
    runtime/src/render/render_config.cpp
runtime/src/render/host_window_win32.cpp
    runtime/src/ui/native_settings.cpp
    runtime/src/ui/settings_model.cpp
    runtime/src/ui/settings_system_page.cpp
    runtime/src/ui/settings_advanced_page.cpp
    runtime/src/ui/settings_display_page.cpp
    runtime/src/ui/settings_general_page.cpp
    runtime/src/ui/settings_graphics_page.cpp
    runtime/src/ui/settings_save_model.cpp
    runtime/src/render/vblank_clock_win32.cpp
    runtime/src/render/renderer_d3d12.cpp
    runtime/src/render/renderer_common.cpp
    runtime/src/render/gpu_upload_ring.cpp
    runtime/src/render/galaxy_select.cpp
    runtime/src/render/window_owner.cpp
    runtime/src/render/texture_store.cpp
    runtime/src/render/texture_pack.cpp
    runtime/src/render/texture_pack_codecs.cpp
    runtime/src/render/movie_playback.cpp
    runtime/src/movie_video.cpp
    runtime/src/render/gpu_texture_backend_d3d12.cpp
    runtime/src/render/gpu_placed_textures.cpp
    runtime/src/render/frame_pass.cpp
    runtime/src/render/mod_shaders.cpp
    runtime/src/render/mod_shader_build.cpp
    runtime/src/render/mod_shader_compile_d3d12.cpp
    runtime/src/render/mod_post.cpp
    runtime/src/render/mod_post_d3d12.cpp
    runtime/src/render/post_process.cpp
    runtime/src/render/post_process_settings.cpp
    ${galaxy_post_headers}
    runtime/src/render/ambient_occlusion.cpp
    ${galaxy_ao_headers}
    runtime/src/render/scene_camera.cpp
    ${galaxy_camera_headers}
    runtime/src/render/game_bloom.cpp
    ${galaxy_bloom_headers}
    runtime/src/render/temporal_upscaler.cpp
    runtime/src/render/xess_library.cpp
    runtime/src/render/xess_upscaler.cpp
    runtime/src/render/rt_materials.cpp
    runtime/src/render/rt_caps.cpp
    runtime/src/render/rt/rt_shaders.cpp
    runtime/src/render/rt/rt_backend_d3d12.cpp
    runtime/src/render/rt/rt_geometry.cpp
    runtime/src/render/rt/rt_scene.cpp
    ${galaxy_rt_dxil}
    runtime/src/render/streamline_bridge.cpp
    runtime/src/render/fsr_frame_generation.cpp
    runtime/src/render/frame_interpolation.cpp
    runtime/src/render/display_modes.cpp
    runtime/src/render/render_core.cpp
    runtime/src/render/affine.cpp
    runtime/src/render/core/compose.cpp
    runtime/src/render/core/draw_walker.cpp
    runtime/src/render/core/rt_walk.cpp
    runtime/src/render/core/rt_bounce.cpp
    runtime/src/render/core/rt_pass_indirect.cpp
    runtime/src/render/core/rt_pass_reflect.cpp
    runtime/src/render/core/rt_pass_transmit.cpp
    runtime/src/render/core/rt_pass_volume.cpp
    runtime/src/render/core/pairing.cpp
    runtime/src/render/core/pipelines.cpp
    runtime/src/render/core/program_table.cpp
    runtime/src/render/core/registries.cpp
    runtime/src/render/core/world_lights.cpp
    runtime/src/render/core/pipelines_d3d12.cpp
    runtime/src/frame_telemetry.cpp
    runtime/src/perf_hint.cpp
)
target_include_directories(galaxy_render PUBLIC runtime/src)
target_include_directories(galaxy_render PRIVATE "${galaxy_utility_shader_dir}" "${galaxy_rt_shader_dir}" runtime/src/render)
# AMD FSR 3 (cmake/fidelityfx.cmake).
include(cmake/fidelityfx.cmake)
target_link_libraries(galaxy_render PRIVATE nebula_fidelityfx)
target_link_libraries(galaxy_render PUBLIC galaxy_asset_store)
# Intel XeSS headers (MIT, third_party/xess): the runtime loads the signed library from a vendor folder at run time.
target_include_directories(galaxy_render SYSTEM PRIVATE third_party/xess/include)
# NVIDIA Streamline 2.14.1 headers (MIT, third_party/streamline) for DLSS: the
# runtime loads the signed binaries from the user's vendor folder at run time.
# Empty builds the runtime without DLSS.
set(GALAXY_STREAMLINE_SDK "${CMAKE_SOURCE_DIR}/third_party/streamline" CACHE PATH "Streamline SDK 2.14.1 directory (headers in include/)")
if(GALAXY_STREAMLINE_SDK)
    target_include_directories(galaxy_render SYSTEM PRIVATE "${GALAXY_STREAMLINE_SDK}/include")
    target_compile_definitions(galaxy_render PRIVATE GALAXY_WITH_STREAMLINE=1)
endif()
# HD movie replacements decode through Media Foundation (movie_playback.cpp).
target_link_libraries(galaxy_render PRIVATE mfplat mfreadwrite mfuuid d3d11)
target_link_libraries(galaxy_render
    PUBLIC
        galaxy_native_host
        galaxy_runtime_abi
        galaxy_runtime_settings
        galaxy_host_pointer_events
        d3d12
        dxgi
)
target_compile_definitions(
    galaxy_render
    PRIVATE
        GALAXY_WITH_STREAMLINE_BRIDGE=$<BOOL:${GALAXY_WITH_STREAMLINE_BRIDGE}>
)
target_include_directories(galaxy_render
    PRIVATE
        "${galaxy_post_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/include"
)
if(MSVC)
    target_compile_options(galaxy_render PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

# The native game's windowed host (native/host): runs the game module that native/CMakeLists.txt builds with clang
# (smg_game.dll, beside the executable or --module) on Nebula's renderer.
add_executable(NebulaNative native/host/host_main.cpp runtime/src/frame_phase.cpp)
if(WIN32)
    enable_language(RC)
endif()
include(cmake/nebula_file_details.cmake)
nebula_file_details(NebulaNative NebulaNative.exe "Super Mario Galaxy on PC" ICON "${CMAKE_CURRENT_SOURCE_DIR}/installer/assets/nebula.ico" DPI_MANIFEST)
target_sources(NebulaNative PRIVATE ${galaxy_programs_dxil})
target_include_directories(NebulaNative PRIVATE native/include runtime/include)
target_compile_definitions(NebulaNative PRIVATE NEBULA_HOST_RENDERER=1 NEBULA_HOST_D3D12=1 _CRT_SECURE_NO_WARNINGS)
target_compile_features(NebulaNative PRIVATE cxx_std_20)
target_link_libraries(NebulaNative PRIVATE galaxy_render galaxy_save_files galaxy_asset_store dwmapi)
nebula_agility_runtime(NebulaNative)
if(MSVC)
    # Paths past MAX_PATH (CMake merges a .manifest source into the program's manifest).
    target_sources(NebulaNative PRIVATE native/host/nebula_host.manifest)
    # Symbols for crash dumps and the thread profiler; code generation is unchanged.
    target_compile_options(NebulaNative PRIVATE /Zi)
    target_link_options(NebulaNative PRIVATE /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:NebulaNative.pdb)
endif()

option(NEBULA_HOST_IPO "Optimize the host libraries across source files in Release builds" ON)
set(nebula_host_ipo_effective OFF)
if(NEBULA_HOST_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT nebula_host_ipo_supported OUTPUT nebula_host_ipo_error LANGUAGES CXX)
    if(nebula_host_ipo_supported)
        foreach(target galaxy_native_host galaxy_render galaxy_native_input galaxy_host_pointer_events
                galaxy_runtime_settings galaxy_atomic_file galaxy_audio_mixer)
            set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endforeach()
        set(nebula_host_ipo_effective ON)
    else()
        message(STATUS "Host IPO unavailable: ${nebula_host_ipo_error}")
    endif()
endif()
message(STATUS "Host Release IPO: ${nebula_host_ipo_effective}")

enable_testing()
# A unit test: an executable from SOURCES linked with LIBS, registered with ctest (run with ARGS).
function(galaxy_test name)
    cmake_parse_arguments(arg "" "" "SOURCES;LIBS;ARGS" ${ARGN})
    add_executable(${name} ${arg_SOURCES})
    target_include_directories(${name} PRIVATE runtime/include)
    target_compile_features(${name} PRIVATE cxx_std_20)
    target_link_libraries(${name} PRIVATE galaxy_runtime_abi ${arg_LIBS})
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /WX /permissive- /EHsc)
    endif()
    add_test(NAME ${name} COMMAND ${name} ${arg_ARGS})
endfunction()

set(rt runtime/src)
galaxy_test(galaxy_atomic_file_tests SOURCES ${rt}/atomic_file_tests.cpp LIBS galaxy_atomic_file)
galaxy_test(galaxy_save_files_tests SOURCES ${rt}/save_files_tests.cpp LIBS galaxy_save_files)
galaxy_test(galaxy_save_format_tests SOURCES ${rt}/save_format_tests.cpp)
galaxy_test(galaxy_launch_args_tests SOURCES ${rt}/launch_args_tests.cpp)
target_include_directories(galaxy_save_format_tests PRIVATE native/petari/include)
galaxy_test(galaxy_asset_store_tests SOURCES ${rt}/asset_store_tests.cpp ${rt}/luma_game_store.cpp LIBS galaxy_asset_store)
target_include_directories(galaxy_asset_store_tests PRIVATE crates/nebula-import/include)
# D-07: an older install's store and saves under the current layout.
galaxy_test(galaxy_install_migration_tests SOURCES ${rt}/install_migration_tests.cpp LIBS galaxy_save_files galaxy_asset_store)
# The game's calendar (native/petari), compiled against a host the test provides.
galaxy_test(galaxy_native_calendar_tests SOURCES ${rt}/native_calendar_tests.cpp
    native/petari/src/Game/System/NebulaCalendar.cpp)
target_include_directories(galaxy_native_calendar_tests PRIVATE native/include native/petari/include)
galaxy_test(galaxy_runtime_settings_tests SOURCES ${rt}/runtime_settings_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_achievements_tests SOURCES ${rt}/achievements_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_stats_tests SOURCES ${rt}/stats_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_controls_tests SOURCES ${rt}/controls_tests.cpp LIBS galaxy_runtime_settings galaxy_native_input)
galaxy_test(galaxy_live_controls_engine_tests SOURCES ${rt}/input/live_controls_engine_tests.cpp LIBS galaxy_native_host)
galaxy_test(galaxy_input_hotplug_tests SOURCES ${rt}/input/input_hotplug_tests.cpp LIBS galaxy_native_host)
galaxy_test(galaxy_keyboard_layout_tests SOURCES ${rt}/input/keyboard_layout_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_keyboard_state_tests SOURCES ${rt}/input/keyboard_state_tests.cpp)
galaxy_test(galaxy_raw_mouse_tests SOURCES ${rt}/input/raw_mouse_tests.cpp)
galaxy_test(galaxy_rumble_route_tests SOURCES ${rt}/input/rumble_route_tests.cpp ${rt}/input/rumble_route.cpp)
galaxy_test(galaxy_wiimote_motion_tests SOURCES ${rt}/input/wiimote_motion_tests.cpp ${rt}/input/wiimote_motion.cpp)
galaxy_test(galaxy_wiimote_motion_pointer_tests SOURCES ${rt}/input/wiimote_motion_pointer_tests.cpp
    LIBS galaxy_native_input)
galaxy_test(galaxy_mouse_projection_tests SOURCES ${rt}/input/mouse_projection_tests.cpp LIBS galaxy_native_input)
galaxy_test(galaxy_raw_mouse_win32_tests SOURCES ${rt}/input/raw_mouse_win32_tests.cpp LIBS galaxy_native_host)
galaxy_test(galaxy_key_positions_win32_tests SOURCES ${rt}/input/key_positions_win32_tests.cpp
    ${rt}/input/key_positions_win32.cpp LIBS user32)
galaxy_test(galaxy_pad_controller_tests SOURCES ${rt}/pad_controller_tests.cpp
    LIBS galaxy_runtime_settings galaxy_native_input)
galaxy_test(galaxy_native_input_tests SOURCES ${rt}/native_input_tests.cpp LIBS galaxy_native_input)
galaxy_test(galaxy_wiimote_speaker_tests SOURCES ${rt}/wiimote_speaker_tests.cpp LIBS galaxy_native_input)
# The extension's work-folder policy holds for this test too: its folders are in the build's test-work.
galaxy_test(galaxy_player_input_tests SOURCES ${rt}/input/player_input_tests.cpp LIBS galaxy_render
    ARGS --settings "${CMAKE_CURRENT_BINARY_DIR}/test-work/settings" --crash-dumps "${CMAKE_CURRENT_BINARY_DIR}/test-work/crashes")
target_compile_definitions(galaxy_player_input_tests PRIVATE NEBULA_TEST_EXTENSION_FILE="$<TARGET_FILE:nebula_test_extension>"
    NEBULA_TEST_WORK="${CMAKE_CURRENT_BINARY_DIR}/test-work")
add_dependencies(galaxy_player_input_tests nebula_test_extension)
galaxy_test(galaxy_host_pointer_events_tests SOURCES ${rt}/host_pointer_events_tests.cpp
    LIBS galaxy_host_pointer_events)
galaxy_test(galaxy_audio_mixer_tests SOURCES ${rt}/audio_mixer_tests.cpp LIBS galaxy_audio_mixer)
galaxy_test(galaxy_movie_video_tests SOURCES ${rt}/movie_video_tests.cpp ${rt}/movie_video.cpp)
# draw.post's registry (mod_post.cpp) without a device.
galaxy_test(galaxy_mod_post_tests SOURCES ${rt}/render/mod_post_tests.cpp ${rt}/render/mod_post.cpp)
galaxy_test(galaxy_windows_power_tests SOURCES ${rt}/windows_power_tests.cpp)
galaxy_test(galaxy_power_policy_tests SOURCES ${rt}/power_policy_tests.cpp)
galaxy_test(galaxy_frame_phase_tests SOURCES ${rt}/frame_phase_tests.cpp ${rt}/frame_phase.cpp)
galaxy_test(galaxy_frame_telemetry_tests SOURCES ${rt}/frame_telemetry_tests.cpp ${rt}/frame_telemetry.cpp)
galaxy_test(galaxy_mii_import_tests SOURCES ${rt}/mii_import_tests.cpp ${rt}/mii_import.cpp ${rt}/mii_formats.cpp
    LIBS galaxy_atomic_file)
galaxy_test(galaxy_luma_tests SOURCES ${rt}/luma_tests.cpp ${rt}/luma.cpp)
galaxy_test(galaxy_mods_tests SOURCES ${rt}/mods_tests.cpp ${rt}/mod_xml.cpp ${rt}/mods.cpp ${rt}/mod_consent.cpp ${rt}/luma.cpp
    ${rt}/mod_import.cpp ${rt}/texture_categories.cpp ${rt}/dolphin_pack.cpp ${rt}/asset_store.cpp LIBS galaxy_atomic_file nebula_archive)
# The consent store (contracts/consent.md 1.5): answers, digests and their round trip through the mod settings.
galaxy_test(galaxy_mod_consent_tests SOURCES ${rt}/mod_consent_tests.cpp ${rt}/mod_consent.cpp ${rt}/mods.cpp ${rt}/mod_xml.cpp
    ${rt}/luma.cpp ${rt}/mod_import.cpp ${rt}/texture_categories.cpp ${rt}/dolphin_pack.cpp ${rt}/asset_store.cpp
    LIBS galaxy_atomic_file nebula_archive)
# Console mods the tests read as files (runtime/tests).
target_compile_definitions(galaxy_mods_tests PRIVATE MODS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/runtime/tests")
galaxy_test(galaxy_crash_report_tests SOURCES ${rt}/crash_report_tests.cpp ${rt}/crash_report.cpp)

galaxy_test(galaxy_test_data_paths_tests SOURCES ${rt}/test_data_paths_tests.cpp ${rt}/hidden_start.cpp
    tools/test_driver/extension/data_guard.cpp LIBS shell32)
target_include_directories(galaxy_test_data_paths_tests PRIVATE tools/test_driver/extension)

galaxy_test(galaxy_home_menu_model_tests SOURCES ${rt}/home_menu_model_tests.cpp)

galaxy_test(galaxy_settings_model_tests SOURCES ${rt}/ui/settings_model_tests.cpp ${rt}/ui/settings_model.cpp)

galaxy_test(galaxy_settings_save_model_tests SOURCES ${rt}/ui/settings_save_model_tests.cpp ${rt}/ui/settings_save_model.cpp)

galaxy_test(galaxy_settings_graphics_page_tests SOURCES ${rt}/ui/settings_graphics_page_tests.cpp
    ${rt}/ui/settings_graphics_page.cpp ${rt}/ui/settings_model.cpp)

galaxy_test(galaxy_settings_general_page_tests SOURCES ${rt}/ui/settings_general_page_tests.cpp
    ${rt}/ui/settings_general_page.cpp ${rt}/ui/settings_model.cpp)

galaxy_test(galaxy_settings_display_page_tests SOURCES ${rt}/ui/settings_display_page_tests.cpp
    ${rt}/ui/settings_display_page.cpp ${rt}/ui/settings_model.cpp)

galaxy_test(galaxy_settings_advanced_page_tests SOURCES ${rt}/ui/settings_advanced_page_tests.cpp
    ${rt}/ui/settings_advanced_page.cpp ${rt}/ui/settings_model.cpp runtime/src/render/galaxy_select.cpp)
galaxy_test(galaxy_settings_system_page_tests SOURCES ${rt}/ui/settings_system_page_tests.cpp
    ${rt}/ui/settings_system_page.cpp ${rt}/ui/settings_model.cpp)
galaxy_test(galaxy_discord_presence_tests SOURCES ${rt}/discord_presence_tests.cpp ${rt}/discord_presence.cpp)

set(render_src runtime/src/render)
galaxy_test(galaxy_frame_interpolation_tests SOURCES ${render_src}/frame_interpolation_tests.cpp
    ${render_src}/frame_interpolation.cpp ${render_src}/affine.cpp)
galaxy_test(galaxy_core_pairing_tests SOURCES ${render_src}/core/pairing_tests.cpp ${render_src}/core/pairing.cpp
    ${render_src}/frame_interpolation.cpp ${render_src}/affine.cpp)
target_include_directories(galaxy_core_pairing_tests PRIVATE runtime/src native/include)
galaxy_test(galaxy_window_owner_tests SOURCES ${render_src}/window_owner_tests.cpp ${render_src}/window_owner.cpp)
galaxy_test(galaxy_core_world_lights_tests SOURCES ${render_src}/core/world_lights_tests.cpp ${render_src}/core/world_lights.cpp)
galaxy_test(galaxy_rt_geometry_tests SOURCES ${render_src}/rt/rt_geometry_tests.cpp ${render_src}/rt/rt_geometry.cpp
    ${render_src}/rt/rt_scene.cpp ${render_src}/rt/rt_backend_fake.cpp)
target_include_directories(galaxy_rt_geometry_tests PRIVATE runtime/src)
target_include_directories(galaxy_core_world_lights_tests PRIVATE runtime/src native/include)
# The pipeline cache files' headers (header-only; both backends' caches).
galaxy_test(galaxy_pipeline_cache_file_tests SOURCES ${render_src}/core/pipeline_cache_file_tests.cpp)
target_include_directories(galaxy_pipeline_cache_file_tests PRIVATE runtime/src)
set_tests_properties(galaxy_window_owner_tests PROPERTIES TIMEOUT 20)
# Tests of the renderer library.
foreach(name render_config gpu_sampler rt_materials texture_pack movie_playback frame_pass post_process
        render_core_host rt_caps)
    galaxy_test(galaxy_${name}_tests SOURCES ${render_src}/${name}_tests.cpp LIBS galaxy_render)
endforeach()
# The tests compile their own small shaders; the program itself never compiles any.
target_link_libraries(galaxy_frame_pass_tests PRIVATE d3dcompiler)

if(MSVC)
    foreach(target galaxy_render galaxy_runtime_settings galaxy_atomic_file
            galaxy_host_pointer_events galaxy_native_input galaxy_native_host galaxy_audio_mixer)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
    endforeach()
endif()

# The Vulkan backend (docs/VULKAN.md): NebulaNativeVk, its library and tests; dependencies from
# cmake/vulkan_deps.cmake.
option(NEBULA_VULKAN "Build the Vulkan backend" OFF)
if(NEBULA_VULKAN)
    include(cmake/vulkan_deps.cmake)

    # The native materials' SPIR-V (cmake/native_materials.cmake): the renderer's HLSL front end (shader_compile_vk.cpp)
    # run at build by this tool.
    add_executable(galaxy_native_spirv tools/dev/native_spirv.cpp runtime/src/render/vk/shader_compile_vk.cpp)
    target_include_directories(galaxy_native_spirv PRIVATE runtime/include)
    target_compile_features(galaxy_native_spirv PRIVATE cxx_std_20)
    target_link_libraries(galaxy_native_spirv PRIVATE nebula_vulkan_deps nebula_shader_compiler)
    galaxy_program_blob(galaxy_programs_spirv spirv)

    # The renderer's other shaders' SPIR-V (E-19; tools/dev/utility_spirv.cpp).
    add_executable(galaxy_utility_spirv tools/dev/utility_spirv.cpp runtime/src/render/vk/shader_compile_vk.cpp
        runtime/src/render/renderer_common.cpp ${galaxy_post_embed_cpp})
    target_include_directories(galaxy_utility_spirv PRIVATE runtime/include)
    target_compile_features(galaxy_utility_spirv PRIVATE cxx_std_20)
    target_link_libraries(galaxy_utility_spirv PRIVATE nebula_vulkan_deps nebula_shader_compiler)
    set(galaxy_utility_spirv_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/utility_spirv")
    file(MAKE_DIRECTORY "${galaxy_utility_spirv_dir}")
    add_custom_command(OUTPUT "${galaxy_utility_spirv_dir}/utility_spirv_data.h"
        COMMAND galaxy_utility_spirv "${galaxy_utility_spirv_dir}/utility_spirv_data.h"
        DEPENDS galaxy_utility_spirv VERBATIM)


    # galaxy_render on Vulkan: the API-neutral renderer sources with the Vulkan renderer, pipeline cache and texture backend.
    add_library(galaxy_render_vk STATIC
        runtime/src/render/render_api.cpp
            runtime/src/render/render_config.cpp
runtime/src/render/host_window_win32.cpp
    runtime/src/ui/native_settings.cpp
    runtime/src/ui/settings_model.cpp
    runtime/src/ui/settings_system_page.cpp
    runtime/src/ui/settings_advanced_page.cpp
    runtime/src/ui/settings_display_page.cpp
    runtime/src/ui/settings_general_page.cpp
    runtime/src/ui/settings_graphics_page.cpp
    runtime/src/ui/settings_save_model.cpp
        runtime/src/render/vblank_clock_win32.cpp
        runtime/src/render/renderer_common.cpp
        runtime/src/render/gpu_upload_ring.cpp
        runtime/src/render/window_owner.cpp
        runtime/src/render/texture_store.cpp
        runtime/src/render/texture_pack.cpp
        runtime/src/render/texture_pack_codecs.cpp
        runtime/src/render/movie_playback.cpp
        runtime/src/movie_video.cpp
        runtime/src/render/frame_pass.cpp
        runtime/src/render/post_process_settings.cpp
        runtime/src/render/display_modes.cpp
            runtime/src/render/render_core.cpp
        runtime/src/render/affine.cpp
        runtime/src/render/core/compose.cpp
        runtime/src/render/core/draw_walker.cpp
        runtime/src/render/core/rt_walk.cpp
        runtime/src/render/core/rt_bounce.cpp
        runtime/src/render/core/rt_pass_indirect.cpp
        runtime/src/render/core/rt_pass_reflect.cpp
        runtime/src/render/core/rt_pass_transmit.cpp
        runtime/src/render/core/rt_pass_volume.cpp
        runtime/src/render/core/pairing.cpp
        runtime/src/render/core/pipelines.cpp
        runtime/src/render/core/program_table.cpp
        runtime/src/render/core/registries.cpp
        runtime/src/render/core/world_lights.cpp
        runtime/src/render/core/pipelines_vk.cpp
        runtime/src/render/core/encoder_vk.cpp
        runtime/src/render/galaxy_select.cpp
        runtime/src/render/frame_interpolation.cpp
        runtime/src/render/rt_materials.cpp
        runtime/src/render/rt_caps.cpp
        runtime/src/render/rt/rt_shaders.cpp
        runtime/src/render/vk/rt_backend_vk.cpp
        runtime/src/render/rt/rt_geometry.cpp
        runtime/src/render/rt/rt_scene.cpp
        runtime/src/render/streamline_bridge.cpp
        runtime/src/frame_telemetry.cpp
        runtime/src/perf_hint.cpp
        runtime/src/render/vk/vk_context.cpp
        runtime/src/render/vk/device_quirks_vk.cpp
        runtime/src/render/vk/vk_libraries.cpp
        runtime/src/render/vk/utility_spirv.cpp
        "${galaxy_utility_spirv_dir}/utility_spirv_data.h"
        runtime/src/render/vk/texture_backend_vk.cpp
        runtime/src/render/mod_shaders.cpp
        runtime/src/render/mod_shader_build.cpp
        runtime/src/render/vk/mod_shader_compile_vk.cpp
        runtime/src/render/mod_post.cpp
        runtime/src/render/vk/mod_post_vk.cpp
        runtime/src/render/vk/mod_shader_present_vk.cpp
        runtime/src/render/vk/shader_compile_vk.cpp
        runtime/src/render/vk/compile_journal.cpp
        runtime/src/render/vk/renderer_vk.cpp
        runtime/src/render/vk/renderer_vk_frame.cpp
        runtime/src/render/vk/post_process_vk.cpp
        runtime/src/render/vk/game_bloom_vk.cpp
        runtime/src/render/vk/ambient_occlusion_vk.cpp
        runtime/src/render/vk/temporal_upscaler_vk.cpp
        runtime/src/render/vk/sgsr2_vk.cpp
        runtime/src/render/vk/streamline_vk.cpp
        runtime/src/render/vk/ffx_vk_forward.cpp
        runtime/src/render/vk/fsr_frame_generation_vk.cpp
        runtime/src/render/xess_library.cpp
        runtime/src/render/vk/scene_camera_vk.cpp
        runtime/src/render/vk/surface_win32.cpp)
    target_include_directories(galaxy_render_vk PUBLIC runtime/include)
    target_include_directories(galaxy_render_vk PUBLIC runtime/src)
    target_include_directories(galaxy_render_vk PRIVATE "${galaxy_utility_spirv_dir}")
    # The tracer's SPIR-V, where a dxc that writes it is named (cmake/rt_shaders.cmake).
    galaxy_find_dxc_spirv(galaxy_dxc_spirv)
    if(galaxy_dxc_spirv)
        galaxy_rt_shaders(galaxy_rt_spirv spirv "${galaxy_dxc_spirv}")
        target_sources(galaxy_render_vk PRIVATE "${galaxy_rt_spirv}")
        target_include_directories(galaxy_render_vk PRIVATE "${galaxy_rt_shader_dir}")
        target_compile_definitions(galaxy_render_vk PRIVATE GALAXY_RT_SPIRV=1)
    endif()
    target_include_directories(galaxy_render_vk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa"
        "${CMAKE_CURRENT_SOURCE_DIR}/native/include")
    target_compile_features(galaxy_render_vk PUBLIC cxx_std_20)
    target_link_libraries(galaxy_render_vk PUBLIC galaxy_asset_store)
    target_include_directories(galaxy_render_vk SYSTEM PRIVATE third_party/xess/include "${GALAXY_STREAMLINE_SDK}/include")
    target_compile_definitions(galaxy_render_vk PRIVATE GALAXY_STREAMLINE_VK=1)
    if(NEBULA_FSR3)
        nebula_fidelityfx_vulkan("${vulkan_headers_SOURCE_DIR}/include")
        target_link_libraries(galaxy_render_vk PRIVATE nebula_fidelityfx_vk)
    endif()
    target_link_libraries(galaxy_render_vk PRIVATE nebula_shader_compiler)
    target_link_libraries(galaxy_render_vk
        PUBLIC
            nebula_vulkan_deps
            galaxy_native_host
            galaxy_runtime_abi
            galaxy_runtime_settings
            galaxy_host_pointer_events)
    if(MSVC)
        target_compile_options(galaxy_render_vk PRIVATE /GS- /GR- /Oi /favor:INTEL64)
    endif()
    if(WIN32)
        target_link_libraries(galaxy_render_vk PRIVATE mfplat mfreadwrite mfuuid d3d11)
    endif()

    add_executable(NebulaNativeVk native/host/host_main.cpp runtime/src/frame_phase.cpp)
    nebula_file_details(NebulaNativeVk NebulaNative.exe "Super Mario Galaxy on PC" ICON "${CMAKE_CURRENT_SOURCE_DIR}/installer/assets/nebula.ico" DPI_MANIFEST)
    target_sources(NebulaNativeVk PRIVATE ${galaxy_programs_spirv})
    target_include_directories(NebulaNativeVk PRIVATE native/include runtime/include)
    target_compile_definitions(NebulaNativeVk PRIVATE NEBULA_HOST_RENDERER=1 _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(NebulaNativeVk PRIVATE dwmapi galaxy_render_vk galaxy_save_files galaxy_asset_store)
    if(MSVC)
        target_sources(NebulaNativeVk PRIVATE native/host/nebula_host.manifest)
        target_compile_options(NebulaNativeVk PRIVATE /Zi)
        target_link_options(NebulaNativeVk PRIVATE /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:NebulaNativeVk.pdb)
    endif()

    # The Vulkan pipeline cache file name (device_quirks_vk.cpp driver_key).
    galaxy_test(galaxy_vk_driver_key_tests SOURCES runtime/src/render/vk/driver_key_tests.cpp
        runtime/src/render/vk/device_quirks_vk.cpp runtime/src/render/vk/vk_libraries.cpp
        LIBS nebula_vulkan_deps)
endif()

# AV1 movies (plan 3.10: a mod's movie, NMOV codec 1): dav1d 1.5.1 built in-tree (cmake/dav1d.cmake), for both renderers.
option(NEBULA_AV1 "Decode AV1 movies with dav1d (plan 3.10)" ON)
if(NEBULA_AV1)
    include(cmake/dav1d.cmake)
    foreach(target galaxy_render galaxy_render_vk)
        if(TARGET ${target})
            target_link_libraries(${target} PRIVATE nebula_dav1d)
            target_compile_definitions(${target} PRIVATE NEBULA_AV1=1)
        endif()
    endforeach()
endif()
