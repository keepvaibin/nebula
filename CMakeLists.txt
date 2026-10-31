cmake_minimum_required(VERSION 3.24)
project(Nebula VERSION 2.0.1 LANGUAGES C CXX)

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
include(cmake/archive_deps.cmake)

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

add_library(galaxy_runtime_abi INTERFACE)
target_include_directories(galaxy_runtime_abi INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")

add_library(galaxy_atomic_file STATIC runtime/src/atomic_file.cpp)
target_link_libraries(galaxy_atomic_file PUBLIC galaxy_runtime_abi)

add_library(galaxy_disc_files STATIC runtime/src/disc_files.cpp)
target_link_libraries(galaxy_disc_files PUBLIC galaxy_runtime_abi)

add_library(galaxy_save_files STATIC runtime/src/save_files.cpp)
target_link_libraries(galaxy_save_files PUBLIC galaxy_runtime_abi galaxy_atomic_file)

add_library(galaxy_runtime_settings STATIC
    runtime/src/runtime_settings.cpp
    runtime/src/input_bindings.cpp
    runtime/src/achievements.cpp
    runtime/src/stats.cpp)
target_link_libraries(galaxy_runtime_settings PUBLIC galaxy_runtime_abi galaxy_atomic_file)
galaxy_host_options(galaxy_runtime_settings)

add_library(galaxy_frame_cadence_diagnostics STATIC runtime/src/frame_cadence_diagnostics.cpp)
target_link_libraries(galaxy_frame_cadence_diagnostics PUBLIC galaxy_runtime_abi)
galaxy_host_options(galaxy_frame_cadence_diagnostics)

add_library(galaxy_host_pointer_events STATIC runtime/src/host_pointer_events.cpp)
target_link_libraries(galaxy_host_pointer_events PUBLIC galaxy_runtime_abi)
galaxy_host_options(galaxy_host_pointer_events)

add_library(galaxy_native_input STATIC
    runtime/src/controller_roster.cpp
    runtime/src/generic_hid.cpp
    runtime/src/input/wiimote_input.cpp
    runtime/src/native_input.cpp
    runtime/src/playstation_controller.cpp
    runtime/src/pointer_control.cpp
    runtime/src/rumble.cpp
    runtime/src/steam_controller.cpp
    runtime/src/switch_controller.cpp
    runtime/src/tilt.cpp
    runtime/src/wiimote_protocol.cpp
    runtime/src/wiimote_speaker.cpp)
target_link_libraries(galaxy_native_input PUBLIC galaxy_runtime_abi)
galaxy_host_options(galaxy_native_input)

# The game's audio commands mixed on the host (jaudio_host_mixer.h).
add_library(galaxy_jaudio_mixer STATIC runtime/src/jaudio_host_mixer.cpp)
target_include_directories(galaxy_jaudio_mixer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
                           "${CMAKE_CURRENT_SOURCE_DIR}/native/include")
galaxy_host_options(galaxy_jaudio_mixer)

add_library(galaxy_native_host STATIC
    runtime/src/native_host.cpp
    runtime/src/host_env.cpp
    runtime/src/input/host_input.cpp
    runtime/src/mii_import.cpp
    runtime/src/mii_formats.cpp
    runtime/src/mod_xml.cpp
    runtime/src/virtual_disc.cpp
    runtime/src/mods.cpp
    runtime/src/mod_import.cpp
    runtime/src/crash_report.cpp
    runtime/src/discord_presence.cpp
    runtime/src/controller_hid.cpp
    runtime/src/wiimote_hid.cpp)
target_link_libraries(galaxy_native_host
    PUBLIC
        galaxy_runtime_abi
        galaxy_runtime_settings
        galaxy_native_input
        galaxy_jaudio_mixer
        galaxy_frame_cadence_diagnostics
        galaxy_atomic_file
        nebula_archive
        shell32
        bcrypt
        hid
        ole32
        setupapi
        cfgmgr32
        xinput9_1_0
        xaudio2
)
galaxy_host_options(galaxy_native_host)

# Uber shader sources (runtime/src/render/uber_*.hlsl) become NUL-terminated char
# arrays in a generated translation unit. Editing a source re-runs configure.
set(galaxy_uber_embed_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/uber_shader_embed.cpp")
set(galaxy_uber_embed_body "")
foreach(galaxy_uber_stage vs ps)
    set(galaxy_uber_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/uber_${galaxy_uber_stage}.hlsl")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${galaxy_uber_hlsl}")
    file(READ "${galaxy_uber_hlsl}" galaxy_uber_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," galaxy_uber_hex "${galaxy_uber_hex}")
    string(APPEND galaxy_uber_embed_body
        "extern const char galaxy_uber_${galaxy_uber_stage}_hlsl[] = {${galaxy_uber_hex}0x00};\n")
endforeach()
file(CONFIGURE OUTPUT "${galaxy_uber_embed_cpp}"
    CONTENT "namespace galaxy::render {\n${galaxy_uber_embed_body}}\n"
    @ONLY)

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
galaxy_post_shader(post_deband_ps PASS_DEBAND - none ps_5_0 PSMain)
galaxy_post_shader(post_deband_reduce_ps PASS_DEBAND_REDUCE - none ps_5_0 PSMain)
galaxy_post_shader(post_deband_smooth_ps PASS_DEBAND_SMOOTH - none ps_5_0 PSMain)
galaxy_post_shader(post_deband_apply_ps PASS_DEBAND_APPLY - none ps_5_0 PSMain)
galaxy_post_shader(post_fxaa_ps PASS_FXAA - none ps_5_0 PSMain)
galaxy_post_shader(post_fsr_easu_ps PASS_FSR_EASU - fsr ps_5_0 PSMain)
galaxy_post_shader(post_fsr_rcas_ps PASS_FSR_RCAS - fsr ps_5_0 PSMain)
galaxy_post_shader(post_fsr_rcas_scaled_ps PASS_FSR_RCAS_SCALED - fsr ps_5_0 PSMain)
# The same passes reading a picture held in a taller texture (post_process.hlsl SOURCE_BOX).
galaxy_post_shader(post_resample_box_ps PASS_RESAMPLE SOURCE_BOX none ps_5_0 PSMain)
galaxy_post_shader(post_deband_box_ps PASS_DEBAND SOURCE_BOX none ps_5_0 PSMain)
galaxy_post_shader(post_deband_reduce_box_ps PASS_DEBAND_REDUCE SOURCE_BOX none ps_5_0 PSMain)
galaxy_post_shader(post_deband_apply_box_ps PASS_DEBAND_APPLY SOURCE_BOX none ps_5_0 PSMain)
galaxy_post_shader(post_fsr_easu_box_ps PASS_FSR_EASU SOURCE_BOX fsr ps_5_0 PSMain)
galaxy_post_shader(post_fsr_rcas_scaled_box_ps PASS_FSR_RCAS_SCALED SOURCE_BOX fsr ps_5_0 PSMain)
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
foreach(galaxy_ao_pass distance_cs:DepthCS:cs_5_0 occlusion_cs:AoCS:cs_5_0 denoise_cs:DenoiseCS:cs_5_0
        apply_vs:ApplyVS:vs_5_0 apply_ps:ApplyPS:ps_5_0)
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
            /Vn g_ao_${galaxy_ao_name} /Fh "${galaxy_ao_header}" "${galaxy_ao_hlsl}"
        DEPENDS "${galaxy_ao_hlsl}"
        VERBATIM)
    list(APPEND galaxy_ao_headers "${galaxy_ao_header}")
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

# Ray-tracing compute shaders (runtime/src/render/ray_tracing.hlsl: DXR 1.1 inline ray queries,
# shader model 6.5) are compiled by the Windows SDK's DXC at build time into signed DXIL headers.
find_program(GALAXY_DXC dxc
    HINTS "$ENV{WindowsSdkVerBinPath}x64" "$ENV{WindowsSdkBinPath}x64"
    NO_DEFAULT_PATH)
if(NOT GALAXY_DXC)
    find_program(GALAXY_DXC dxc REQUIRED)
endif()
set(galaxy_rt_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/rt_shaders")
file(MAKE_DIRECTORY "${galaxy_rt_dir}")
set(galaxy_rt_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/ray_tracing.hlsl")
set(galaxy_rt_headers "")
foreach(galaxy_rt_pass geometry:GeometryCS validate:ValidateCS)
    string(REPLACE ":" ";" galaxy_rt_pair "${galaxy_rt_pass}")
    list(GET galaxy_rt_pair 0 galaxy_rt_name)
    list(GET galaxy_rt_pair 1 galaxy_rt_entry)
    set(galaxy_rt_header "${galaxy_rt_dir}/rt_${galaxy_rt_name}_cs.h")
    add_custom_command(OUTPUT "${galaxy_rt_header}" "${galaxy_rt_header}.dxil"
        COMMAND "${GALAXY_DXC}" -nologo -T cs_6_5 -E ${galaxy_rt_entry} -O3 -WX -Qstrip_debug -Qstrip_reflect
            -Vn g_rt_${galaxy_rt_name}_cs -Fh "${galaxy_rt_header}" -Fo "${galaxy_rt_header}.dxil" "${galaxy_rt_hlsl}"
        DEPENDS "${galaxy_rt_hlsl}"
        VERBATIM)
    list(APPEND galaxy_rt_headers "${galaxy_rt_header}")
endforeach()
# Ray-traced lighting passes, the picture capture and the EFB composite
# (runtime/src/render/rt_lighting.hlsl).
set(galaxy_rt_light_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_lighting.hlsl")
foreach(galaxy_rt_pass capture_cs:CaptureCS:cs_6_5 sky_state_cs:SkyStateCS:cs_6_5 glow_base_cs:GlowBaseCS:cs_6_5
        sky_light_map_cs:SkyLightMapCS:cs_6_5 sky_light_sh_cs:SkyLightShCS:cs_6_5
        surface_emit_cs:SurfaceEmitCS:cs_6_5
        glow_emit_cs:GlowEmitCS:cs_6_5 authored_emit_cs:AuthoredEmitCS:cs_6_5 emit_merge_cs:EmitMergeCS:cs_6_5
        star_sphere_cs:StarSphereCS:cs_6_5 sky_trace_sh_cs:SkyTraceShCS:cs_6_5
        composite_vs:CompositeVS:vs_6_0
        composite_ps:CompositePS:ps_6_0 clear_composite_ps:ClearCompositePS:ps_6_0
        mist_composite_ps:MistCompositePS:ps_6_0 light_texture_ps:LightTexturePS:ps_6_0)
    string(REPLACE ":" ";" galaxy_rt_pair "${galaxy_rt_pass}")
    list(GET galaxy_rt_pair 0 galaxy_rt_name)
    list(GET galaxy_rt_pair 1 galaxy_rt_entry)
    list(GET galaxy_rt_pair 2 galaxy_rt_profile)
    set(galaxy_rt_header "${galaxy_rt_dir}/rt_${galaxy_rt_name}.h")
    add_custom_command(OUTPUT "${galaxy_rt_header}" "${galaxy_rt_header}.dxil"
        COMMAND "${GALAXY_DXC}" -nologo -T ${galaxy_rt_profile} -E ${galaxy_rt_entry} -O3 -WX -Qstrip_debug
            -Qstrip_reflect -Vn g_rt_${galaxy_rt_name} -Fh "${galaxy_rt_header}" -Fo "${galaxy_rt_header}.dxil" "${galaxy_rt_light_hlsl}"
        DEPENDS "${galaxy_rt_light_hlsl}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_tev.hlsli"
        VERBATIM)
    list(APPEND galaxy_rt_headers "${galaxy_rt_header}")
endforeach()
# The camera of the ray-traced picture: bloom and the tone curve from the HDR scene EFB (runtime/src/render/rt_camera.hlsl).
set(galaxy_rt_camera_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_camera.hlsl")
foreach(galaxy_rt_pass camera_down_cs:CameraDownCS:cs_6_5 camera_up_cs:CameraUpCS:cs_6_5 camera_vs:CameraVS:vs_6_0
        camera_ps:CameraPS:ps_6_0 camera_widen_ps:CameraWidenPS:ps_6_0 camera_shafts_cs:CameraShaftsCS:cs_6_0)
    string(REPLACE ":" ";" galaxy_rt_pair "${galaxy_rt_pass}")
    list(GET galaxy_rt_pair 0 galaxy_rt_name)
    list(GET galaxy_rt_pair 1 galaxy_rt_entry)
    list(GET galaxy_rt_pair 2 galaxy_rt_profile)
    set(galaxy_rt_header "${galaxy_rt_dir}/rt_${galaxy_rt_name}.h")
    add_custom_command(OUTPUT "${galaxy_rt_header}" "${galaxy_rt_header}.dxil"
        COMMAND "${GALAXY_DXC}" -nologo -T ${galaxy_rt_profile} -E ${galaxy_rt_entry} -O3 -WX -Qstrip_debug
            -Qstrip_reflect -Vn g_rt_${galaxy_rt_name} -Fh "${galaxy_rt_header}" -Fo "${galaxy_rt_header}.dxil" "${galaxy_rt_camera_hlsl}"
        DEPENDS "${galaxy_rt_camera_hlsl}"
        VERBATIM)
    list(APPEND galaxy_rt_headers "${galaxy_rt_header}")
endforeach()
# Ray-traced reflections and their EFB composite (runtime/src/render/rt_reflections.hlsl).
set(galaxy_rt_reflect_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_reflections.hlsl")
foreach(galaxy_rt_pass reflect_memory_cs:ReflectMemoryCS:cs_6_5 reflect_trace_cs:ReflectTraceCS:cs_6_5
        reflect_sky_cs:SkyCaptureCS:cs_6_5 reflect_draw_clear_cs:DrawLightClearCS:cs_6_5
        reflect_draw_cs:DrawLightCS:cs_6_5
        reflect_temporal_cs:ReflectTemporalCS:cs_6_5 reflect_filter_a_cs:ReflectFilterACS:cs_6_5
        reflect_filter_cs:ReflectFilterCS:cs_6_5
        reflect_composite_vs:ReflectCompositeVS:vs_6_0 reflect_composite_ps:ReflectCompositePS:ps_6_0
        water_trace_cs:WaterTraceCS:cs_6_5 water_composite_ps:WaterCompositePS:ps_6_0
        underwater_ps:UnderwaterPS:ps_6_0)
    string(REPLACE ":" ";" galaxy_rt_pair "${galaxy_rt_pass}")
    list(GET galaxy_rt_pair 0 galaxy_rt_name)
    list(GET galaxy_rt_pair 1 galaxy_rt_entry)
    list(GET galaxy_rt_pair 2 galaxy_rt_profile)
    set(galaxy_rt_header "${galaxy_rt_dir}/rt_${galaxy_rt_name}.h")
    add_custom_command(OUTPUT "${galaxy_rt_header}" "${galaxy_rt_header}.dxil"
        COMMAND "${GALAXY_DXC}" -nologo -T ${galaxy_rt_profile} -E ${galaxy_rt_entry} -O3 -WX -Qstrip_debug
            -Qstrip_reflect -Vn g_rt_${galaxy_rt_name} -Fh "${galaxy_rt_header}" -Fo "${galaxy_rt_header}.dxil" "${galaxy_rt_reflect_hlsl}"
        DEPENDS "${galaxy_rt_reflect_hlsl}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_tev.hlsli"
        VERBATIM)
    list(APPEND galaxy_rt_headers "${galaxy_rt_header}")
endforeach()
# High quality's traces: the hits' textures straight from the descriptor heap (shader model 6.6).
foreach(galaxy_rt_pass reflect_trace_tex_cs:ReflectTraceCS water_trace_tex_cs:WaterTraceCS)
    string(REPLACE ":" ";" galaxy_rt_pair "${galaxy_rt_pass}")
    list(GET galaxy_rt_pair 0 galaxy_rt_name)
    list(GET galaxy_rt_pair 1 galaxy_rt_entry)
    set(galaxy_rt_header "${galaxy_rt_dir}/rt_${galaxy_rt_name}.h")
    add_custom_command(OUTPUT "${galaxy_rt_header}" "${galaxy_rt_header}.dxil"
        COMMAND "${GALAXY_DXC}" -nologo -T cs_6_6 -E ${galaxy_rt_entry} -D RT_TEXTURED=1 -O3 -WX -Qstrip_debug
            -Qstrip_reflect -Vn g_rt_${galaxy_rt_name} -Fh "${galaxy_rt_header}" -Fo "${galaxy_rt_header}.dxil" "${galaxy_rt_reflect_hlsl}"
        DEPENDS "${galaxy_rt_reflect_hlsl}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_tev.hlsli"
        VERBATIM)
    list(APPEND galaxy_rt_headers "${galaxy_rt_header}")
endforeach()

# The passes reading textures and TEV records straight from the descriptor heap (shader model 6.6): the traced sky,
# the emitting triangles and the glow light sources.
foreach(galaxy_rt_pass sky_probe_tex_cs:SkyProbeCS emit_triangle_cs:EmitTriangleCS emit_scan_cs:EmitScanCS
        glow_emitter_cs:GlowEmitterCS)
    string(REPLACE ":" ";" galaxy_rt_pair "${galaxy_rt_pass}")
    list(GET galaxy_rt_pair 0 galaxy_rt_name)
    list(GET galaxy_rt_pair 1 galaxy_rt_entry)
    set(galaxy_rt_header "${galaxy_rt_dir}/rt_${galaxy_rt_name}.h")
    add_custom_command(OUTPUT "${galaxy_rt_header}" "${galaxy_rt_header}.dxil"
        COMMAND "${GALAXY_DXC}" -nologo -T cs_6_6 -E ${galaxy_rt_entry} -D RT_TEXTURED=1 -O3 -WX -Qstrip_debug
            -Qstrip_reflect -Vn g_rt_${galaxy_rt_name} -Fh "${galaxy_rt_header}" -Fo "${galaxy_rt_header}.dxil" "${galaxy_rt_light_hlsl}"
        DEPENDS "${galaxy_rt_light_hlsl}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_tev.hlsli"
        VERBATIM)
    list(APPEND galaxy_rt_headers "${galaxy_rt_header}")
endforeach()

# The ray tracing engine (runtime/src/render/rt_engine.hlsl, including rt_lighting.hlsl; shader model 6.6).
set(galaxy_rt_engine_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_engine.hlsl")
foreach(galaxy_rt_pass engine_paint_clear_cs:EnginePaintClearCS engine_paint_cs:EnginePaintCS
        engine_gbuffer_cs:EngineGBufferCS engine_direct_cs:EngineDirectCS
        engine_light_grid_cs:EngineLightGridCS engine_cache_cs:EngineCacheCS
        engine_direct_spatial_cs:EngineDirectSpatialCS
        engine_shadow_blur_cs:EngineShadowBlurCS
        engine_shadow_post_cs:EngineShadowPostCS engine_shadow_stabilize_cs:EngineShadowStabilizeCS
        engine_indirect_cs:EngineIndirectCS
        engine_indirect_spatial_cs:EngineIndirectSpatialCS engine_specular_cs:EngineSpecularCS
        engine_temporal_cs:EngineTemporalCS engine_atrous_cs:EngineAtrousCS engine_final_cs:EngineFinalCS
        engine_clear_cs:EngineClearCS engine_mist_cs:MistLightCS engine_reference_cs:EngineReferenceCS)
    string(REPLACE ":" ";" galaxy_rt_pair "${galaxy_rt_pass}")
    list(GET galaxy_rt_pair 0 galaxy_rt_name)
    list(GET galaxy_rt_pair 1 galaxy_rt_entry)
    set(galaxy_rt_header "${galaxy_rt_dir}/rt_${galaxy_rt_name}.h")
    add_custom_command(OUTPUT "${galaxy_rt_header}" "${galaxy_rt_header}.dxil"
        COMMAND "${GALAXY_DXC}" -nologo -T cs_6_6 -E ${galaxy_rt_entry} -D RT_TEXTURED=1 -O3 -WX -Qstrip_debug
            -Qstrip_reflect -Vn g_rt_${galaxy_rt_name} -Fh "${galaxy_rt_header}" -Fo "${galaxy_rt_header}.dxil" "${galaxy_rt_engine_hlsl}"
        DEPENDS "${galaxy_rt_engine_hlsl}" "${galaxy_rt_light_hlsl}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_tev.hlsli"
        VERBATIM)
    list(APPEND galaxy_rt_headers "${galaxy_rt_header}")
endforeach()

# Texture de-lighting for the physical lighting (runtime/src/render/rt_delight.hlsl; textures from the descriptor heap).
set(galaxy_rt_delight_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/rt_delight.hlsl")
foreach(galaxy_rt_pass delight_cs:DelightCS delight_mip_cs:DelightMipCS)
    string(REPLACE ":" ";" galaxy_rt_pair "${galaxy_rt_pass}")
    list(GET galaxy_rt_pair 0 galaxy_rt_name)
    list(GET galaxy_rt_pair 1 galaxy_rt_entry)
    set(galaxy_rt_header "${galaxy_rt_dir}/rt_${galaxy_rt_name}.h")
    add_custom_command(OUTPUT "${galaxy_rt_header}" "${galaxy_rt_header}.dxil"
        COMMAND "${GALAXY_DXC}" -nologo -T cs_6_6 -E ${galaxy_rt_entry} -O3 -WX -Qstrip_debug
            -Qstrip_reflect -Vn g_rt_${galaxy_rt_name} -Fh "${galaxy_rt_header}" -Fo "${galaxy_rt_header}.dxil" "${galaxy_rt_delight_hlsl}"
        DEPENDS "${galaxy_rt_delight_hlsl}"
        VERBATIM)
    list(APPEND galaxy_rt_headers "${galaxy_rt_header}")
endforeach()
# The shaders' manifest (tools/rt_shader_manifest.cmake: each one's size and DXIL digest, the set's fingerprint) for the
# start's shader preparation (runtime/src/render/rt_prepare.cpp), from the binaries dxc writes beside the headers.
set(galaxy_rt_manifest "${galaxy_rt_dir}/rt_shader_manifest.h")
set(galaxy_rt_binaries "")
foreach(galaxy_rt_header IN LISTS galaxy_rt_headers)
    list(APPEND galaxy_rt_binaries "${galaxy_rt_header}.dxil")
endforeach()
string(REPLACE ";" "|" galaxy_rt_binary_arg "${galaxy_rt_binaries}")
add_custom_command(OUTPUT "${galaxy_rt_manifest}"
    COMMAND "${CMAKE_COMMAND}" "-DOUT=${galaxy_rt_manifest}" "-DBINARIES=${galaxy_rt_binary_arg}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/rt_shader_manifest.cmake"
    DEPENDS ${galaxy_rt_binaries} "${CMAKE_CURRENT_SOURCE_DIR}/tools/rt_shader_manifest.cmake"
    VERBATIM)
list(APPEND galaxy_rt_headers "${galaxy_rt_manifest}")

add_library(galaxy_gx STATIC
    runtime/src/render/renderer_api.cpp
    runtime/src/render/render_config.cpp
    runtime/src/render/host_window_win32.cpp
    runtime/src/render/vblank_clock_win32.cpp
    runtime/src/render/renderer_d3d12.cpp
    runtime/src/render/renderer_common.cpp
    runtime/src/render/upload_ring.cpp
    runtime/src/render/galaxy_select.cpp
    runtime/src/render/window_owner.cpp
    runtime/src/render/texture_cache.cpp
    runtime/src/render/texture_pack.cpp
    runtime/src/render/texture_pack_codecs.cpp
    runtime/src/render/movie_replacement.cpp
    runtime/src/render/texture_backend_d3d12.cpp
    runtime/src/render/placed_texture_allocator.cpp
    runtime/src/render/curved_surfaces.cpp
    runtime/src/render/native_shapes.cpp
    runtime/src/render/native_material.cpp
    runtime/src/render/draw_state.cpp
    runtime/src/render/matrix_memory.cpp
    runtime/src/render/shader_keys.cpp
    runtime/src/render/efb_copy.cpp
    runtime/src/render/line_point_raster.cpp
    runtime/src/render/pipeline_cache.cpp
    runtime/src/render/shader_gen.cpp
    runtime/src/render/uber_shaders.cpp
    ${galaxy_uber_embed_cpp}
    runtime/src/render/post_process.cpp
    runtime/src/render/post_process_settings.cpp
    ${galaxy_post_headers}
    runtime/src/render/ambient_occlusion.cpp
    ${galaxy_ao_headers}
    runtime/src/render/game_bloom.cpp
    ${galaxy_bloom_headers}
    runtime/src/render/temporal_upscaler.cpp
    runtime/src/render/ray_tracing.cpp
    runtime/src/render/game_lights.cpp
    runtime/src/render/game_objects.cpp
    runtime/src/render/rt_tev_eval.cpp
    runtime/src/render/ray_tracing_camera.cpp
    runtime/src/render/ray_tracing_engine.cpp
    runtime/src/render/ray_tracing_reflections.cpp
    runtime/src/render/ray_tracing_water.cpp
    runtime/src/render/rt_prepare.cpp
    runtime/src/render/rt_surfaces.cpp
    runtime/src/render/rt_materials.cpp
    runtime/src/render/enhanced_lighting.cpp
    ${galaxy_rt_headers}
    runtime/src/render/streamline_bridge.cpp
    runtime/src/render/transform_history.cpp
    runtime/src/render/frame_interpolation.cpp
    runtime/src/render/interp_gpu.cpp
    runtime/src/render/display_modes.cpp
    runtime/src/render/render_core.cpp
    runtime/src/frame_telemetry.cpp
    runtime/src/perf_hint.cpp
    runtime/src/render/game_asset_extract.cpp
)
# AMD FSR 3 (cmake/fidelityfx.cmake).
include(cmake/fidelityfx.cmake)
target_link_libraries(galaxy_gx PRIVATE nebula_fidelityfx)
# NVIDIA Streamline 2.14.1 headers (MIT, third_party/streamline) for DLSS: the
# runtime loads the signed binaries from the user's vendor folder at run time.
# Empty builds the runtime without DLSS.
set(GALAXY_STREAMLINE_SDK "${CMAKE_SOURCE_DIR}/third_party/streamline" CACHE PATH "Streamline SDK 2.14.1 directory (headers in include/)")
if(GALAXY_STREAMLINE_SDK)
    target_include_directories(galaxy_gx SYSTEM PRIVATE "${GALAXY_STREAMLINE_SDK}/include")
    target_compile_definitions(galaxy_gx PRIVATE GALAXY_WITH_STREAMLINE=1)
endif()
# HD movie replacements decode through Media Foundation (movie_replacement.cpp).
target_link_libraries(galaxy_gx PRIVATE mfplat mfreadwrite mfuuid d3d11)
target_link_libraries(galaxy_gx
    PUBLIC
        galaxy_native_host
        galaxy_frame_cadence_diagnostics
        galaxy_runtime_abi
        galaxy_runtime_settings
        galaxy_host_pointer_events
        d3d12
        dxgi
        d3dcompiler
)
target_compile_definitions(
    galaxy_gx
    PRIVATE
        GALAXY_WITH_STREAMLINE_BRIDGE=$<BOOL:${GALAXY_WITH_STREAMLINE_BRIDGE}>
)
target_include_directories(galaxy_gx
    PRIVATE
        "${galaxy_post_dir}"
        "${galaxy_rt_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa"
)
if(MSVC)
    target_compile_options(galaxy_gx PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

# The native game's windowed host (native/host): runs the game module that native/CMakeLists.txt builds with clang
# (smg_game.dll, beside the executable or --module) on Nebula's renderer.
add_executable(NebulaNative native/host/host_main.cpp runtime/src/native_thp_video.cpp)
target_include_directories(NebulaNative PRIVATE native/include runtime/include)
target_compile_definitions(NebulaNative PRIVATE NEBULA_HOST_RENDERER=1 NEBULA_HOST_D3D12=1 _CRT_SECURE_NO_WARNINGS)
target_compile_features(NebulaNative PRIVATE cxx_std_20)
target_link_libraries(NebulaNative PRIVATE galaxy_gx galaxy_save_files galaxy_disc_files)
if(MSVC)
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
        foreach(target galaxy_native_host galaxy_gx galaxy_native_input galaxy_host_pointer_events
                galaxy_frame_cadence_diagnostics galaxy_runtime_settings galaxy_atomic_file galaxy_jaudio_mixer)
            set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endforeach()
        set(nebula_host_ipo_effective ON)
    else()
        message(STATUS "Host IPO unavailable: ${nebula_host_ipo_error}")
    endif()
endif()
message(STATUS "Host Release IPO: ${nebula_host_ipo_effective}")

# Developer tool (tools/dev): the cosmetic asset extractor run against real game files.
add_executable(galaxy_verify_asset_extract tools/dev/verify_asset_extract.cpp)
target_link_libraries(galaxy_verify_asset_extract PRIVATE galaxy_gx)

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
galaxy_test(galaxy_disc_files_tests SOURCES ${rt}/disc_files_tests.cpp LIBS galaxy_disc_files)
# The game's calendar (native/petari), compiled against a host the test provides.
galaxy_test(galaxy_native_calendar_tests SOURCES ${rt}/native_calendar_tests.cpp
    native/petari/src/Game/System/NebulaCalendar.cpp)
target_include_directories(galaxy_native_calendar_tests PRIVATE native/include native/petari/include
    native/petari/libs/RVL_SDK/include)
# The game's types.h defines nullptr as 0 unless it is a macro already: here it names the keyword itself.
target_compile_definitions(galaxy_native_calendar_tests PRIVATE _ALLOW_KEYWORD_MACROS nullptr=nullptr)
galaxy_test(galaxy_runtime_settings_tests SOURCES ${rt}/runtime_settings_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_achievements_tests SOURCES ${rt}/achievements_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_stats_tests SOURCES ${rt}/stats_tests.cpp LIBS galaxy_runtime_settings)
galaxy_test(galaxy_controls_tests SOURCES ${rt}/controls_tests.cpp LIBS galaxy_runtime_settings galaxy_native_input)
galaxy_test(galaxy_pad_controller_tests SOURCES ${rt}/pad_controller_tests.cpp
    LIBS galaxy_runtime_settings galaxy_native_input)
galaxy_test(galaxy_native_input_tests SOURCES ${rt}/native_input_tests.cpp LIBS galaxy_native_input)
galaxy_test(galaxy_player_input_tests SOURCES ${rt}/input/player_input_tests.cpp LIBS galaxy_gx)
galaxy_test(galaxy_host_pointer_events_tests SOURCES ${rt}/host_pointer_events_tests.cpp
    LIBS galaxy_host_pointer_events)
galaxy_test(galaxy_diagnostic_stick_lease_tests SOURCES ${rt}/diagnostic_stick_lease_tests.cpp)
galaxy_test(galaxy_jaudio_mixer_tests SOURCES ${rt}/jaudio_host_mixer_tests.cpp LIBS galaxy_jaudio_mixer)
galaxy_test(galaxy_native_thp_video_tests SOURCES ${rt}/native_thp_video_tests.cpp ${rt}/native_thp_video.cpp)
galaxy_test(galaxy_windows_power_tests SOURCES ${rt}/windows_power_tests.cpp)
galaxy_test(galaxy_motion_signals_tests SOURCES ${rt}/motion_signals_tests.cpp)
galaxy_test(galaxy_frame_telemetry_tests SOURCES ${rt}/frame_telemetry_tests.cpp ${rt}/frame_telemetry.cpp)
galaxy_test(galaxy_mii_import_tests SOURCES ${rt}/mii_import_tests.cpp ${rt}/mii_import.cpp ${rt}/mii_formats.cpp
    LIBS galaxy_atomic_file)
galaxy_test(galaxy_mods_tests SOURCES ${rt}/mods_tests.cpp ${rt}/mod_xml.cpp ${rt}/virtual_disc.cpp ${rt}/mods.cpp
    ${rt}/mod_import.cpp LIBS galaxy_atomic_file nebula_archive)
galaxy_test(galaxy_crash_report_tests SOURCES ${rt}/crash_report_tests.cpp ${rt}/crash_report.cpp)
galaxy_test(galaxy_discord_presence_tests SOURCES ${rt}/discord_presence_tests.cpp ${rt}/discord_presence.cpp)

set(gxs runtime/src/render)
galaxy_test(galaxy_game_asset_extract_tests SOURCES ${gxs}/game_asset_extract_tests.cpp ${gxs}/game_asset_extract.cpp)
galaxy_test(galaxy_transform_history_tests SOURCES ${gxs}/transform_history_tests.cpp ${gxs}/transform_history.cpp)
galaxy_test(galaxy_frame_interpolation_tests SOURCES ${gxs}/frame_interpolation_tests.cpp
    ${gxs}/frame_interpolation.cpp ${gxs}/transform_history.cpp)
galaxy_test(galaxy_interp_gpu_tests SOURCES ${gxs}/interp_gpu_tests.cpp ${gxs}/interp_gpu.cpp
    ${gxs}/frame_interpolation.cpp ${gxs}/transform_history.cpp)
galaxy_test(galaxy_window_owner_tests SOURCES ${gxs}/window_owner_tests.cpp ${gxs}/window_owner.cpp)
set_tests_properties(galaxy_window_owner_tests PROPERTIES TIMEOUT 20)
# Tests of the renderer library.
foreach(name shader_gen curved_surfaces native_shapes simd_kernel vs_constants rt_tev_eval render_config
        sampler_key rt_surfaces rt_materials rt_prepare texture_pack movie_replacement
        efb_copy line_point_raster xf_lighting native_settings_overlay post_process
        efb_conversion native_material)
    galaxy_test(galaxy_${name}_tests SOURCES ${gxs}/${name}_tests.cpp LIBS galaxy_gx)
endforeach()
# The game's text table: every text the settings menu shows has its entry.
target_compile_definitions(galaxy_native_settings_overlay_tests PRIVATE
    NEBULA_TEXT_TABLE="${CMAKE_CURRENT_SOURCE_DIR}/localization/NebulaText.tsv")
add_test(NAME galaxy_post_process_tests_held_no_deband COMMAND galaxy_post_process_tests --held-no-deband)
foreach(mode content-key-hash oversized-content-cache decoded-allocation-budget)
    add_test(NAME galaxy_efb_conversion_tests_${mode} COMMAND galaxy_efb_conversion_tests --${mode})
endforeach()
# Cache file recovery through the real loader and writer, with fault injection compiled into this test's own copy.
galaxy_test(galaxy_pipeline_cache_persistence_tests SOURCES ${gxs}/pipeline_cache_persistence_tests.cpp
    ${gxs}/pipeline_cache.cpp LIBS galaxy_gx)
target_compile_definitions(galaxy_pipeline_cache_persistence_tests PRIVATE GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO=1)

if(MSVC)
    foreach(target galaxy_gx galaxy_runtime_settings galaxy_atomic_file galaxy_frame_cadence_diagnostics
            galaxy_host_pointer_events galaxy_native_input galaxy_native_host galaxy_jaudio_mixer
            galaxy_verify_asset_extract)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
    endforeach()
endif()

# The Vulkan GX backend (docs/VULKAN.md): NebulaNativeVk, its library and tests; dependencies from
# cmake/vulkan_deps.cmake.
option(NEBULA_VULKAN "Build the Vulkan GX backend" OFF)
if(NEBULA_VULKAN)
    include(cmake/vulkan_deps.cmake)

    add_executable(galaxy_vk_shader_tests
        runtime/src/render/vk/shader_compile_vk_tests.cpp
        runtime/src/render/vk/shader_compile_vk.cpp
        runtime/src/render/vk/vk_context.cpp
        runtime/src/render/vk/device_quirks_vk.cpp
        runtime/src/render/vk/vk_libraries.cpp
        runtime/src/render/shader_gen.cpp
        runtime/src/render/enhanced_lighting.cpp
        runtime/src/render/rt_materials.cpp
        runtime/src/render/rt_surfaces.cpp
        runtime/src/render/uber_shaders.cpp
        ${galaxy_uber_embed_cpp}
        ${galaxy_post_embed_cpp}
        runtime/src/render/renderer_common.cpp
        runtime/src/render/efb_copy.cpp
        runtime/src/render/render_config.cpp)
    target_include_directories(galaxy_vk_shader_tests PRIVATE runtime/include)
    target_compile_features(galaxy_vk_shader_tests PRIVATE cxx_std_20)
    target_link_libraries(galaxy_vk_shader_tests PRIVATE nebula_vulkan_deps galaxy_runtime_abi galaxy_runtime_settings)
    add_test(NAME galaxy_vk_shader_tests COMMAND galaxy_vk_shader_tests)

    # galaxy_gx on Vulkan: the API-neutral GX sources with the Vulkan renderer, pipeline cache and texture backend.
    add_library(galaxy_gx_vk STATIC
        runtime/src/render/renderer_api.cpp
            runtime/src/render/render_config.cpp
        runtime/src/render/host_window_win32.cpp
        runtime/src/render/vblank_clock_win32.cpp
        runtime/src/render/renderer_common.cpp
        runtime/src/render/upload_ring.cpp
        runtime/src/render/window_owner.cpp
        runtime/src/render/texture_cache.cpp
        runtime/src/render/texture_pack.cpp
        runtime/src/render/texture_pack_codecs.cpp
        runtime/src/render/movie_replacement.cpp
        runtime/src/render/curved_surfaces.cpp
        runtime/src/render/native_shapes.cpp
        runtime/src/render/native_material.cpp
        runtime/src/render/draw_state.cpp
        runtime/src/render/matrix_memory.cpp
        runtime/src/render/shader_keys.cpp
        runtime/src/render/efb_copy.cpp
        runtime/src/render/line_point_raster.cpp
        runtime/src/render/shader_gen.cpp
        runtime/src/render/uber_shaders.cpp
        ${galaxy_uber_embed_cpp}
        runtime/src/render/post_process_settings.cpp
        runtime/src/render/transform_history.cpp
        runtime/src/render/display_modes.cpp
            runtime/src/render/render_core.cpp
        runtime/src/render/game_lights.cpp
        runtime/src/render/game_objects.cpp
    runtime/src/render/rt_tev_eval.cpp
        runtime/src/render/galaxy_select.cpp
        runtime/src/render/frame_interpolation.cpp
        runtime/src/render/interp_gpu.cpp
        runtime/src/render/rt_surfaces.cpp
        runtime/src/render/rt_materials.cpp
        runtime/src/render/enhanced_lighting.cpp
        runtime/src/render/streamline_bridge.cpp
        runtime/src/frame_telemetry.cpp
        runtime/src/perf_hint.cpp
        runtime/src/render/game_asset_extract.cpp
        runtime/src/render/vk/vk_context.cpp
        runtime/src/render/vk/device_quirks_vk.cpp
        runtime/src/render/vk/vk_libraries.cpp
        runtime/src/render/vk/shader_compile_vk.cpp
        runtime/src/render/vk/texture_backend_vk.cpp
        runtime/src/render/vk/pipeline_cache_vk.cpp
        runtime/src/render/vk/compile_journal.cpp
        runtime/src/render/vk/renderer_vk.cpp
        runtime/src/render/vk/renderer_vk_efb.cpp
        runtime/src/render/vk/renderer_vk_replay.cpp
        runtime/src/render/vk/post_process_vk.cpp
        runtime/src/render/vk/game_bloom_vk.cpp
        ${galaxy_post_embed_cpp}
        runtime/src/render/vk/surface_win32.cpp)
    target_include_directories(galaxy_gx_vk PUBLIC runtime/include)
    target_include_directories(galaxy_gx_vk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa")
    target_compile_features(galaxy_gx_vk PUBLIC cxx_std_20)
    target_link_libraries(galaxy_gx_vk
        PUBLIC
            nebula_vulkan_deps
            galaxy_native_host
            galaxy_frame_cadence_diagnostics
            galaxy_runtime_abi
            galaxy_runtime_settings
            galaxy_host_pointer_events)
    if(MSVC)
        target_compile_options(galaxy_gx_vk PRIVATE /GS- /GR- /Oi /favor:INTEL64)
    endif()
    if(WIN32)
        target_link_libraries(galaxy_gx_vk PRIVATE mfplat mfreadwrite mfuuid d3d11)
    endif()

    add_executable(NebulaNativeVk native/host/host_main.cpp runtime/src/native_thp_video.cpp)
    target_include_directories(NebulaNativeVk PRIVATE native/include runtime/include)
    target_compile_definitions(NebulaNativeVk PRIVATE NEBULA_HOST_RENDERER=1 _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(NebulaNativeVk PRIVATE galaxy_gx_vk galaxy_save_files galaxy_disc_files)
    if(MSVC)
        target_compile_options(NebulaNativeVk PRIVATE /Zi)
        target_link_options(NebulaNativeVk PRIVATE /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:NebulaNativeVk.pdb)
    endif()

    # The Android app's precompiled shaders (platform/android/vk_seed_shaders.bin) from its seed keys.
    add_executable(vk_shader_seed tools/dev/vk_shader_seed.cpp)
    target_link_libraries(vk_shader_seed PRIVATE galaxy_gx_vk)
endif()
