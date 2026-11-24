cmake_minimum_required(VERSION 3.24)
project(Nebula VERSION 2.0.0 LANGUAGES C CXX)
# What the host computes for the game (a stick's position above all) is the same bits on every CPU: no multiply and
# add is fused into one rounding. Clang fuses them by default on CPUs with a fused multiply-add (arm64, and x86-64 at
# AVX2): a tablet's stick then differed from a computer's by one ulp, and its replay from the computer's (N-1064). The
# game module is built the same way (native/CMakeLists.txt). MSVC fuses none at /fp:precise without /fp:contract.
if(MSVC)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/clang:-ffp-contract=off>)
    endif()
else()
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=off>)
endif()

# The programs' debug information (crash dumps, the profiler): one PDB per program (/Zi), or inside each object (/Z7)
# when a compiler cache sits in front of the compiler (the push workflow's sccache), which cannot cache a shared PDB.
# The linker makes the same PDB either way.
if(CMAKE_CXX_COMPILER_LAUNCHER OR "$ENV{CMAKE_CXX_COMPILER_LAUNCHER}")
    set(NEBULA_MSVC_DEBUG_INFO /Z7)
else()
    set(NEBULA_MSVC_DEBUG_INFO /Zi)
endif()
include(cmake/nebula_options.cmake)
add_compile_definitions(NEBULA_VERSION="${PROJECT_VERSION}${NEBULA_VERSION_SUFFIX}")

# Coverage builds (the gate's long run, tools/gate/coverage.py, N-1042): every program, library and test counts the lines
# and functions it runs (clang's source-based coverage), each process writing them where LLVM_PROFILE_FILE says when it
# ends (the host writes its counts itself: native/host/host_main.cpp). Clang-cl only; no optimizing across source files,
# and warnings stay warnings (clang names some that Microsoft's compiler does not; the gate's MSVC build keeps /WX).
option(NEBULA_COVERAGE "Count the lines and functions every program runs (clang-cl)" OFF)
if(NEBULA_COVERAGE)
    if(NOT (MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
        message(FATAL_ERROR "NEBULA_COVERAGE needs clang-cl")
    endif()
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-fprofile-instr-generate> $<$<COMPILE_LANGUAGE:C,CXX>:-fcoverage-mapping>)
    add_compile_definitions(NEBULA_COVERAGE=1)
    # The profile runtime from the compiler's own resource folder.
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /clang:-print-resource-dir OUTPUT_VARIABLE nebula_clang_resources
                    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE nebula_clang_resources_result)
    set(nebula_profile_runtime "${nebula_clang_resources}/lib/windows/clang_rt.profile-x86_64.lib")
    if(NOT nebula_clang_resources_result EQUAL 0 OR NOT EXISTS "${nebula_profile_runtime}")
        message(FATAL_ERROR "NEBULA_COVERAGE: no ${nebula_profile_runtime}")
    endif()
    link_libraries("${nebula_profile_runtime}")
endif()
# /WX beside /W4 in the targets below, except in a coverage build.
set(NEBULA_WARNINGS_AS_ERRORS $<$<NOT:$<BOOL:${NEBULA_COVERAGE}>>:/WX>)

# One build: a release and a test run use the same binaries (tools/release_check.ps1 checks the sources and the release).
# The standard library's bounds checks (array, vector, span, string_view subscripts and the like) in every host build: an
# index past the end stops the program at once, never reading or writing a neighbouring member.
if(MSVC)
    add_compile_definitions(_MSVC_STL_HARDENING=1)
endif()

# The Windows build: Nebula (D3D12) and, with NEBULA_VULKAN, NebulaVk. The game module is
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
    # The host's CPU level, whatever machine builds it. The package carries the host twice, like the game module
    # (native/build.ps1): AVX2 (x86-64-v3), and SSE4.2 (x86-64-v2, which Setup requires) from a second build tree
    # configured with NEBULA_HOST_BASELINE, named <program>-x86-64-v2.exe beside it. The AVX2 program's entry point
    # starts the SSE4.2 one where the CPU lacks x86-64-v3 (nebula_host_cpu_entry, native/host/host_entry_win32.cpp).
    option(NEBULA_HOST_BASELINE "Build the SSE4.2 host the AVX2 host starts on CPUs without x86-64-v3 (<program>-x86-64-v2.exe)" OFF)
    set(GALAXY_NATIVE_ISA "AVX2" CACHE STRING "Host CPU target: SSE2, SSE4.2, or AVX2")
    set_property(CACHE GALAXY_NATIVE_ISA PROPERTY STRINGS SSE2 SSE4.2 AVX2)
    string(TOUPPER "${GALAXY_NATIVE_ISA}" GALAXY_NATIVE_ISA)
    # AUTO: the value a build folder configured before the two builds holds (it chose AVX2 on an AVX2 build machine).
    if(GALAXY_NATIVE_ISA STREQUAL "AUTO")
        set(GALAXY_NATIVE_ISA "AVX2")
    endif()
    if(NEBULA_HOST_BASELINE)
        set(GALAXY_NATIVE_ISA "SSE4.2")
    endif()
    if(NOT GALAXY_NATIVE_ISA MATCHES "^(SSE2|SSE4\\.2|AVX2)$")
        message(FATAL_ERROR "GALAXY_NATIVE_ISA must be SSE2, SSE4.2, or AVX2")
    endif()
    message(STATUS "Nebula host CPU target: ${GALAXY_NATIVE_ISA}")
    # clang-cl takes the GCC spelling.
    if(GALAXY_NATIVE_ISA STREQUAL "AVX2")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-mavx2>)
        else()
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/arch:AVX2>)
        endif()
    elseif(GALAXY_NATIVE_ISA STREQUAL "SSE4.2")
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-msse4.2> $<$<COMPILE_LANGUAGE:C,CXX>:-mpopcnt>)
        else()
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/arch:SSE4.2>)
        endif()
    endif()
endif()

# The host libraries' Release code: no buffer security checks, no RTTI.
function(galaxy_host_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /GS- /GR- /Oi $<$<CXX_COMPILER_ID:MSVC>:/favor:INTEL64>)
    endif()
endfunction()

# Libraries only a movie (Media Foundation, D3D11 for its decoder), a download (WinHTTP) and the XeSS library's signature
# check (WinTrust) call: loaded at that first call instead of before main. A library a back end does not import is
# skipped by the linker (LNK4199).
function(nebula_host_delay_loads target)
    if(MSVC)
        target_link_options(${target} PRIVATE /DELAYLOAD:MFPlat.dll /DELAYLOAD:MFReadWrite.dll /DELAYLOAD:d3d11.dll
            /DELAYLOAD:winhttp.dll /DELAYLOAD:wintrust.dll /IGNORE:4199)
        target_link_libraries(${target} PRIVATE delayimp)
    endif()
endfunction()

# The Windows x64 host's two builds (GALAXY_NATIVE_ISA above). The AVX2 program enters at nebula_host_entry, compiled
# without the AVX2 option and without buffer security checks, which tests the CPU before the C runtime starts and runs
# <program>-x86-64-v2.exe where the CPU lacks x86-64-v3. The SSE4.2 tree names its program so; it needs no test.
# Every x64 host links the file: its nebula_run_sibling is also the Direct3D 12 host's hand-off to NebulaD11.
# Every Windows host is a window program: a start from Explorer, a shortcut or the starter opens no console window
# (host_main.cpp use_console). main stays the program's own entry, past the C runtime's start (mainCRTStartup).
function(nebula_host_cpu_entry target)
    if(MSVC)
        target_link_options(${target} PRIVATE /SUBSYSTEM:WINDOWS)
    endif()
    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64)$")
        if(MSVC)
            target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
        endif()
        return()
    endif()
    if(NOT TARGET nebula_host_entry)
        add_library(nebula_host_entry OBJECT native/host/host_entry_win32.cpp)
        target_include_directories(nebula_host_entry PRIVATE runtime/include)
        get_target_property(entry_options nebula_host_entry COMPILE_OPTIONS)
        list(FILTER entry_options EXCLUDE REGEX "arch:AVX2|-mavx2")
        list(APPEND entry_options /GS- ${NEBULA_MSVC_DEBUG_INFO})
        set_property(TARGET nebula_host_entry PROPERTY COMPILE_OPTIONS "${entry_options}")
    endif()
    target_link_libraries(${target} PRIVATE nebula_host_entry)
    if(NEBULA_HOST_BASELINE)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME "${target}-x86-64-v2")
        get_target_property(link_options ${target} LINK_OPTIONS)
        list(TRANSFORM link_options REPLACE "^/PDBALTPATH:.*$" "/PDBALTPATH:${target}-x86-64-v2.pdb")
        set_property(TARGET ${target} PROPERTY LINK_OPTIONS "${link_options}")
        target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
    elseif(GALAXY_NATIVE_ISA STREQUAL "AVX2")
        target_link_options(${target} PRIVATE /ENTRY:nebula_host_entry)
    else()
        target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
    endif()
endfunction()

include_directories("${CMAKE_CURRENT_LIST_DIR}/native/include")

# The runtime's small libraries (shared with the unit tests' own project, tests/unit).
include(cmake/runtime_libs.cmake)

add_library(galaxy_native_host STATIC
    runtime/src/game_paths.cpp
    runtime/src/hidden_start.cpp
    runtime/src/whole_lines.cpp
    runtime/src/input/host_input.cpp
    runtime/src/input/controls_engine.cpp
    runtime/src/input/live_controls_engine.cpp
    runtime/src/input/co_star.cpp
    runtime/src/input/key_positions_win32.cpp
    runtime/src/input/raw_mouse_win32.cpp
    runtime/src/input/rumble_route.cpp
    runtime/src/native_audio.cpp
    runtime/src/audio_device_wasapi.cpp
    runtime/src/mii_import.cpp
    runtime/src/mod_xml.cpp
    runtime/src/luma.cpp
    runtime/src/mod_log.cpp
    runtime/src/mod_check.cpp
    runtime/src/luma_game_store.cpp
    runtime/src/mod_screens.cpp
    runtime/src/mod_start_check.cpp
    runtime/src/luma_system.cpp
    runtime/src/luma_system_win32.cpp
    runtime/src/luma_mods.cpp
    runtime/src/luma_fs.cpp
    runtime/src/luma_net.cpp
    runtime/src/luma_net_reach.cpp
    runtime/src/luma_assets.cpp
    runtime/src/luma_audio.cpp
    runtime/src/luma_capture.cpp
    runtime/src/luma_input.cpp
    runtime/src/mod_plugins.cpp
    runtime/src/game_module.cpp
    native/host/luma_guard.cpp
    native/host/crash_watch.cpp
    runtime/src/mods.cpp
    runtime/src/mod_consent.cpp
    runtime/src/texture_categories.cpp
    runtime/src/dolphin_pack.cpp
    runtime/src/mod_import.cpp
    runtime/src/vendor_download.cpp
    runtime/src/diag_report.cpp
    runtime/src/strict_json.cpp
    runtime/src/diag_seal.cpp
    runtime/src/crash_report.cpp
    runtime/src/discord_presence.cpp
    runtime/src/controller_hid.cpp
    runtime/src/switch2_ble_win.cpp
    runtime/src/wiimote_hid.cpp
    runtime/src/wiimote_link_win32.cpp
    # The home screen's update check (galaxy/update.h) over WinHTTP; Nebula Setup installs.
    runtime/src/update/update.cpp
    runtime/src/update/update_crypto.cpp
    runtime/src/update/update_transport_winhttp.cpp
    runtime/src/update/update_install_windows.cpp)
target_link_libraries(galaxy_native_host
    PUBLIC
        galaxy_runtime_abi
        galaxy_asset_store
        galaxy_runtime_settings
        galaxy_native_input
        galaxy_audio_mixer
        galaxy_atomic_file
        galaxy_mii_formats
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
# The host's diagnostics report modes (diag_report.h).
target_compile_definitions(galaxy_native_host PUBLIC NEBULA_DIAG_REPORTS=1 NEBULA_UPDATE_CHECK=1)

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
galaxy_post_shader(post_copy_ps PASS_COPY - none ps_5_0 PSMain)
galaxy_post_shader(post_fxaa_ps PASS_FXAA - none ps_5_0 PSMain)
galaxy_post_shader(post_fsr_easu_ps PASS_FSR_EASU - fsr ps_5_0 PSMain)
galaxy_post_shader(post_fsr_easu_half_ps PASS_FSR_EASU_HALF - fsr ps_5_0 PSMain)
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
    runtime/src/render/device_memory.cpp
    runtime/src/render/renderer_choice.cpp
    runtime/src/render/renderer_check_win32.cpp
runtime/src/render/host_window_win32.cpp
    runtime/src/ui/native_settings.cpp
    runtime/src/ui/launcher.cpp
    runtime/src/ui/settings_model.cpp
    runtime/src/ui/settings_system_page.cpp
    runtime/src/ui/settings_benchmark_page.cpp
    runtime/src/ui/benchmark_menu.cpp
    runtime/src/ui/hdr_calibration.cpp
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
    runtime/src/benchmark_recommend.cpp
    runtime/src/benchmark.cpp
    runtime/src/calibration_scene.cpp
    runtime/src/benchmark_stats.cpp
    runtime/src/benchmark_results.cpp
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
    runtime/src/render/streamline_bridge.cpp
    runtime/src/render/fsr_frame_generation.cpp
    runtime/src/render/frame_interpolation.cpp
    runtime/src/render/display_modes.cpp
    runtime/src/render/os_hdr.cpp
    runtime/src/render/render_core.cpp
    runtime/src/render/frame_schedule.cpp
    runtime/src/render/present_slots.cpp
    runtime/src/render/gpu_pass_times.cpp
    runtime/src/render/affine.cpp
    runtime/src/render/core/compose.cpp
    runtime/src/render/core/draw_walker.cpp
    runtime/src/render/core/pairing.cpp
    runtime/src/render/core/pipelines.cpp
    runtime/src/render/core/program_table.cpp
    runtime/src/render/core/registries.cpp
    runtime/src/render/core/pipelines_d3d12.cpp
    runtime/src/frame_telemetry.cpp
    runtime/src/perf_hint.cpp
    runtime/src/pacing.cpp
    runtime/src/pacing_recorder.cpp
)
target_include_directories(galaxy_render PUBLIC runtime/src)
target_include_directories(galaxy_render PRIVATE "${galaxy_utility_shader_dir}" runtime/src/render)
# AMD FSR 3 (cmake/fidelityfx.cmake).
include(cmake/fidelityfx.cmake)
target_link_libraries(galaxy_render PRIVATE nebula_fidelityfx)
# The renderer check reads the Vulkan headers in every Windows build (cmake/vulkan_headers.cmake).
include(cmake/vulkan_headers.cmake)
target_link_libraries(galaxy_render PRIVATE nebula_vulkan_headers)
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
# The game window's cloak until its first picture (window_owner.cpp).
target_link_libraries(galaxy_render PRIVATE dwmapi)
target_link_libraries(galaxy_render
    PUBLIC
        galaxy_save_transfer
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
    target_compile_options(galaxy_render PRIVATE /GS- /GR- /Oi $<$<CXX_COMPILER_ID:MSVC>:/favor:INTEL64>)
endif()

# The native game's windowed host (native/host): runs the game module that native/CMakeLists.txt builds with clang
# (smg_game.dll, beside the executable or --module) on Nebula's renderer.
add_executable(Nebula native/host/host_main.cpp runtime/src/frame_phase.cpp)
if(WIN32)
    enable_language(RC)
endif()
include(cmake/nebula_file_details.cmake)
nebula_file_details(Nebula Nebula.exe "Nebula" ICON "${CMAKE_CURRENT_SOURCE_DIR}/installer/assets/nebula.ico" DPI_MANIFEST)
target_sources(Nebula PRIVATE ${galaxy_programs_dxil})
target_include_directories(Nebula PRIVATE native/include runtime/include)
target_compile_definitions(Nebula PRIVATE NEBULA_HOST_RENDERER=1 NEBULA_HOST_D3D12=1 _CRT_SECURE_NO_WARNINGS)
target_compile_features(Nebula PRIVATE cxx_std_20)
target_link_libraries(Nebula PRIVATE galaxy_render galaxy_save_files galaxy_asset_store dwmapi)
nebula_agility_runtime(Nebula)
if(MSVC)
    # The program's identity and the Windows versions it supports, merged into the linker's manifest (which names the
    # execution level, asInvoker); the DPI and path settings are nebula_game.manifest's.
    target_sources(Nebula PRIVATE native/host/nebula_host.manifest)
    # Symbols for crash dumps and the thread profiler; code generation is unchanged.
    target_compile_options(Nebula PRIVATE ${NEBULA_MSVC_DEBUG_INFO})
    target_link_options(Nebula PRIVATE /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:Nebula.pdb)
endif()
nebula_host_cpu_entry(Nebula)
nebula_host_delay_loads(Nebula)

option(NEBULA_HOST_IPO "Optimize the host libraries across source files in Release builds" ON)
# Settings > ADVANCED (any galaxy, the Grand Stars) in every run: test builds only. A release shows it in dev mode only.
option(NEBULA_TEST_TOOLS "Show the testing tools in Settings without dev mode (test builds, never a release)" OFF)
if(NEBULA_TEST_TOOLS)
    add_compile_definitions(NEBULA_TEST_TOOLS=1)
endif()
set(nebula_host_ipo_effective OFF)
if(NEBULA_HOST_IPO AND NOT NEBULA_COVERAGE)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT nebula_host_ipo_supported OUTPUT nebula_host_ipo_error LANGUAGES CXX)
    if(nebula_host_ipo_supported)
        foreach(target galaxy_native_host galaxy_render galaxy_native_input galaxy_host_pointer_events
                galaxy_runtime_settings galaxy_atomic_file galaxy_audio_mixer galaxy_mii_formats)
            set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endforeach()
        set(nebula_host_ipo_effective ON)
    else()
        message(STATUS "Host IPO unavailable: ${nebula_host_ipo_error}")
    endif()
endif()
message(STATUS "Host Release IPO: ${nebula_host_ipo_effective}")

# The unit tests every desktop system runs (cmake/unit_tests.cmake); the tests of Windows code and of the renderers
# follow.
include(cmake/unit_tests.cmake)

set(rt runtime/src)
set(render_src runtime/src/render)
# The update core of the desktop hosts (runtime/src/update), with a stand-in transport and platform.
galaxy_test(galaxy_update_tests SOURCES ${rt}/update/update.cpp ${rt}/update/update_crypto.cpp ${rt}/update/update_tests.cpp
    LIBS galaxy_atomic_file shell32 ARGS "${CMAKE_CURRENT_SOURCE_DIR}/installer/release-public-key.xml")
galaxy_test(galaxy_live_controls_engine_tests SOURCES ${rt}/input/live_controls_engine_tests.cpp LIBS galaxy_native_host)
galaxy_test(galaxy_input_hotplug_tests SOURCES ${rt}/input/input_hotplug_tests.cpp LIBS galaxy_native_host)
galaxy_test(galaxy_raw_mouse_win32_tests SOURCES ${rt}/input/raw_mouse_win32_tests.cpp LIBS galaxy_native_host)
galaxy_test(galaxy_key_positions_win32_tests SOURCES ${rt}/input/key_positions_win32_tests.cpp
    ${rt}/input/key_positions_win32.cpp LIBS user32)
galaxy_test(galaxy_windows_power_tests SOURCES ${rt}/windows_power_tests.cpp)
galaxy_test(galaxy_windows_child_tests SOURCES ${rt}/windows_child_tests.cpp)
galaxy_test(galaxy_settings_graphics_page_tests SOURCES ${rt}/ui/settings_graphics_page_tests.cpp
    ${rt}/ui/settings_graphics_page.cpp ${rt}/ui/settings_model.cpp LIBS galaxy_render)
# The HDR calibration's steps, with the renderer library's levels and patterns.
galaxy_test(galaxy_hdr_calibration_tests SOURCES ${rt}/ui/hdr_calibration_tests.cpp LIBS galaxy_render)
galaxy_test(galaxy_calibration_scene_tests SOURCES ${rt}/calibration_scene_tests.cpp LIBS galaxy_render)
galaxy_test(galaxy_os_hdr_tests SOURCES ${rt}/render/os_hdr_tests.cpp LIBS galaxy_render)
galaxy_test(galaxy_window_owner_tests SOURCES ${render_src}/window_owner_tests.cpp ${render_src}/window_owner.cpp)
target_link_libraries(galaxy_window_owner_tests PRIVATE dwmapi)
set_tests_properties(galaxy_window_owner_tests PROPERTIES TIMEOUT 20)
# The frame counts' shared memory for outside readers (frame_telemetry.cpp: Windows only; elsewhere it publishes nothing).
galaxy_test(galaxy_frame_telemetry_tests SOURCES ${rt}/frame_telemetry_tests.cpp ${rt}/frame_telemetry.cpp)
# Renderer core code that reads the GPU types (galaxy/render/gpu_types.h: Direct3D 12's here, with no backend chosen).
galaxy_test(galaxy_core_pairing_tests SOURCES ${render_src}/core/pairing_tests.cpp ${render_src}/core/pairing.cpp
    ${render_src}/frame_interpolation.cpp ${render_src}/affine.cpp)
target_include_directories(galaxy_core_pairing_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src" "${CMAKE_CURRENT_SOURCE_DIR}/native/include")

# Additions a build asks for at configure, as a list of CMake files (-DNEBULA_EXTRA_CMAKE=<file>;...), each included
# here after the product's targets and galaxy_test. A build that passes none builds the product and its unit tests only.
set(NEBULA_EXTRA_CMAKE "" CACHE STRING "CMake files included after the product's targets")
foreach(extra IN LISTS NEBULA_EXTRA_CMAKE)
    get_filename_component(extra "${extra}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    include("${extra}")
endforeach()

# Tests of the renderer library.
foreach(name render_config gpu_sampler texture_pack movie_playback frame_pass post_process render_core_host)
    galaxy_test(galaxy_${name}_tests SOURCES ${render_src}/${name}_tests.cpp LIBS galaxy_render)
endforeach()
# The pictures the mod check reads with the same answers (crates/nebula-import cook/pack_texture.rs).
target_compile_definitions(galaxy_texture_pack_tests PRIVATE
    NEBULA_PACK_PICTURES="${CMAKE_CURRENT_SOURCE_DIR}/crates/nebula-import/data/pack-pictures")
# The import check of P-059 (R2.3): every texture record of a store judged by the runtime against the cook's bit. It
# needs a disc's game.pak, so it is built with the tests and run by hand, not by ctest.
add_executable(galaxy_authored_levels_check ${render_src}/authored_levels_check.cpp)
target_include_directories(galaxy_authored_levels_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include")
target_compile_features(galaxy_authored_levels_check PRIVATE cxx_std_20)
target_link_libraries(galaxy_authored_levels_check PRIVATE galaxy_runtime_abi galaxy_render)
if(MSVC)
    target_compile_options(galaxy_authored_levels_check PRIVATE /W4 ${NEBULA_WARNINGS_AS_ERRORS} /permissive- /EHsc)
endif()
# The tests compile their own small shaders; the program itself never compiles any.
target_link_libraries(galaxy_frame_pass_tests PRIVATE d3dcompiler)

if(MSVC)
    foreach(target galaxy_render galaxy_runtime_settings galaxy_atomic_file
            galaxy_host_pointer_events galaxy_native_input galaxy_native_host galaxy_audio_mixer)
        target_compile_options(${target} PRIVATE /W4 ${NEBULA_WARNINGS_AS_ERRORS} /permissive- /EHsc)
    endforeach()
endif()

# The Vulkan backend: NebulaVk, its library and tests; dependencies from
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
            runtime/src/render/device_memory.cpp
            runtime/src/render/renderer_choice.cpp
            runtime/src/render/renderer_check_win32.cpp
runtime/src/render/host_window_win32.cpp
    runtime/src/ui/native_settings.cpp
    runtime/src/ui/launcher.cpp
    runtime/src/ui/settings_model.cpp
    runtime/src/ui/settings_system_page.cpp
    runtime/src/ui/settings_benchmark_page.cpp
    runtime/src/ui/benchmark_menu.cpp
    runtime/src/ui/hdr_calibration.cpp
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
        runtime/src/render/os_hdr.cpp
            runtime/src/render/render_core.cpp
            runtime/src/render/frame_schedule.cpp
            runtime/src/render/gpu_pass_times.cpp
        runtime/src/render/affine.cpp
        runtime/src/render/core/compose.cpp
        runtime/src/render/core/draw_walker.cpp
        runtime/src/render/core/pairing.cpp
        runtime/src/render/core/pipelines.cpp
        runtime/src/render/core/program_table.cpp
        runtime/src/render/core/registries.cpp
        runtime/src/render/core/pipelines_vk.cpp
        runtime/src/render/core/encoder_vk.cpp
        runtime/src/render/galaxy_select.cpp
        runtime/src/benchmark_recommend.cpp
        runtime/src/benchmark.cpp
        runtime/src/calibration_scene.cpp
        runtime/src/benchmark_stats.cpp
        runtime/src/benchmark_results.cpp
        runtime/src/render/frame_interpolation.cpp
        runtime/src/render/streamline_bridge.cpp
        runtime/src/frame_telemetry.cpp
        runtime/src/perf_hint.cpp
        runtime/src/pacing.cpp
        runtime/src/pacing_recorder.cpp
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
        runtime/src/render/present_slots.cpp
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
            galaxy_save_transfer
            galaxy_native_host
            galaxy_runtime_abi
            galaxy_runtime_settings
            galaxy_host_pointer_events)
    if(MSVC)
        target_compile_options(galaxy_render_vk PRIVATE /GS- /GR- /Oi $<$<CXX_COMPILER_ID:MSVC>:/favor:INTEL64>)
    endif()
    if(WIN32)
        target_link_libraries(galaxy_render_vk PRIVATE mfplat mfreadwrite mfuuid d3d11 dwmapi)
    endif()

    add_executable(NebulaVk native/host/host_main.cpp runtime/src/frame_phase.cpp)
    nebula_file_details(NebulaVk Nebula.exe "Nebula" ICON "${CMAKE_CURRENT_SOURCE_DIR}/installer/assets/nebula.ico" DPI_MANIFEST)
    target_sources(NebulaVk PRIVATE ${galaxy_programs_spirv})
    target_include_directories(NebulaVk PRIVATE native/include runtime/include)
    target_compile_definitions(NebulaVk PRIVATE NEBULA_HOST_RENDERER=1 _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(NebulaVk PRIVATE dwmapi galaxy_render_vk galaxy_save_files galaxy_asset_store)
    if(MSVC)
        target_sources(NebulaVk PRIVATE native/host/nebula_host.manifest)
        target_compile_options(NebulaVk PRIVATE ${NEBULA_MSVC_DEBUG_INFO})
        target_link_options(NebulaVk PRIVATE /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:NebulaVk.pdb)
    endif()
    nebula_host_cpu_entry(NebulaVk)
    nebula_host_delay_loads(NebulaVk)

    # The Vulkan pipeline cache file name (device_quirks_vk.cpp driver_key).
    galaxy_test(galaxy_vk_driver_key_tests SOURCES runtime/src/render/vk/driver_key_tests.cpp
        runtime/src/render/vk/device_quirks_vk.cpp runtime/src/render/vk/vk_libraries.cpp
        LIBS nebula_vulkan_deps)
endif()

# The Direct3D 11 backend (runtime/include/galaxy/render/d3d11/renderer_d3d11.h): NebulaD11, for the Windows
# adapters Direct3D 12 passes over. The API-neutral renderer sources (the Vulkan library's), the Direct3D 11 renderer and
# its parts, the programs for shader model 5.0 (compile_programs.py dxbc) and the utility shaders (utility_shaders sm50).
option(NEBULA_D3D11 "Build the Direct3D 11 backend" OFF)
if(NEBULA_D3D11)
    galaxy_program_blob(galaxy_programs_dxbc dxbc)
    set(galaxy_utility_sm50_dir "${galaxy_utility_shader_dir}/sm50")
    file(MAKE_DIRECTORY "${galaxy_utility_sm50_dir}")
    add_custom_command(OUTPUT "${galaxy_utility_sm50_dir}/utility_shaders_dxbc.h"
        COMMAND galaxy_utility_shaders "${galaxy_utility_sm50_dir}/utility_shaders_dxbc.h" sm50
        DEPENDS galaxy_utility_shaders VERBATIM)
    file(GLOB galaxy_render_d3d11_backend CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/render/d3d11/*.cpp")
    add_library(galaxy_render_d3d11 STATIC
        runtime/src/render/render_api.cpp
        runtime/src/render/render_config.cpp
        runtime/src/render/device_memory.cpp
        runtime/src/render/renderer_choice.cpp
        runtime/src/render/renderer_check_win32.cpp
        runtime/src/render/host_window_win32.cpp
        runtime/src/ui/native_settings.cpp
        runtime/src/ui/settings_model.cpp
        runtime/src/ui/settings_system_page.cpp
        runtime/src/ui/settings_benchmark_page.cpp
        runtime/src/ui/benchmark_menu.cpp
        runtime/src/ui/hdr_calibration.cpp
        runtime/src/ui/settings_advanced_page.cpp
        runtime/src/ui/settings_display_page.cpp
        runtime/src/ui/settings_general_page.cpp
        runtime/src/ui/settings_graphics_page.cpp
        runtime/src/ui/settings_save_model.cpp
        runtime/src/ui/launcher.cpp
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
        runtime/src/render/os_hdr.cpp
        runtime/src/render/render_core.cpp
        runtime/src/render/frame_schedule.cpp
        runtime/src/render/present_slots.cpp
        runtime/src/render/gpu_pass_times.cpp
        runtime/src/render/affine.cpp
        runtime/src/render/core/compose.cpp
        runtime/src/render/core/draw_walker.cpp
        runtime/src/render/core/pairing.cpp
        runtime/src/render/core/pipelines.cpp
        runtime/src/render/core/program_table.cpp
        runtime/src/render/core/registries.cpp
        runtime/src/render/core/pipelines_d3d11.cpp
        runtime/src/render/galaxy_select.cpp
        runtime/src/benchmark_recommend.cpp
        runtime/src/benchmark.cpp
        runtime/src/calibration_scene.cpp
        runtime/src/benchmark_stats.cpp
        runtime/src/benchmark_results.cpp
        runtime/src/render/frame_interpolation.cpp
        runtime/src/render/streamline_bridge.cpp
        runtime/src/render/xess_library.cpp
        runtime/src/frame_telemetry.cpp
        runtime/src/perf_hint.cpp
        runtime/src/pacing.cpp
        runtime/src/pacing_recorder.cpp
        runtime/src/render/mod_shaders.cpp
        runtime/src/render/mod_shader_build.cpp
        runtime/src/render/mod_post.cpp
        ${galaxy_render_d3d11_backend}
        "${galaxy_utility_sm50_dir}/utility_shaders_dxbc.h"
        ${galaxy_post_headers}
        ${galaxy_ao_headers}
        ${galaxy_camera_headers}
        ${galaxy_bloom_headers})
    target_include_directories(galaxy_render_d3d11 PUBLIC runtime/include runtime/src)
    target_include_directories(galaxy_render_d3d11 PRIVATE "${galaxy_utility_shader_dir}" runtime/src/render "${galaxy_post_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/smaa" "${CMAKE_CURRENT_SOURCE_DIR}/native/include")
    target_include_directories(galaxy_render_d3d11 SYSTEM PRIVATE third_party/xess/include "${GALAXY_STREAMLINE_SDK}/include")
    target_compile_features(galaxy_render_d3d11 PUBLIC cxx_std_20)
    target_compile_definitions(galaxy_render_d3d11 PUBLIC NEBULA_GPU_D3D11=1)
    target_link_libraries(galaxy_render_d3d11 PRIVATE nebula_vulkan_headers)
    target_link_libraries(galaxy_render_d3d11
        PUBLIC
            galaxy_asset_store
            galaxy_save_transfer
            galaxy_native_host
            galaxy_runtime_abi
            galaxy_runtime_settings
            galaxy_host_pointer_events
            d3d11
            dxgi
            dxguid
        PRIVATE
            mfplat mfreadwrite mfuuid dwmapi)
    if(MSVC)
        target_compile_options(galaxy_render_d3d11 PRIVATE /GS- /GR- /Oi $<$<CXX_COMPILER_ID:MSVC>:/favor:INTEL64> /W4 ${NEBULA_WARNINGS_AS_ERRORS}
                               /permissive- /EHsc)
    endif()

    add_executable(NebulaD11 native/host/host_main.cpp runtime/src/frame_phase.cpp)
    nebula_file_details(NebulaD11 Nebula.exe "Nebula" ICON "${CMAKE_CURRENT_SOURCE_DIR}/installer/assets/nebula.ico" DPI_MANIFEST)
    target_sources(NebulaD11 PRIVATE ${galaxy_programs_dxbc})
    target_include_directories(NebulaD11 PRIVATE native/include runtime/include)
    target_compile_definitions(NebulaD11 PRIVATE NEBULA_HOST_RENDERER=1 _CRT_SECURE_NO_WARNINGS)
    target_compile_features(NebulaD11 PRIVATE cxx_std_20)
    target_link_libraries(NebulaD11 PRIVATE dwmapi galaxy_render_d3d11 galaxy_save_files galaxy_asset_store)
    if(MSVC)
        target_sources(NebulaD11 PRIVATE native/host/nebula_host.manifest)
        target_compile_options(NebulaD11 PRIVATE ${NEBULA_MSVC_DEBUG_INFO})
        target_link_options(NebulaD11 PRIVATE /DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:NebulaD11.pdb)
    endif()
    # Both CPU levels like the other hosts (NebulaD11-x86-64-v2.exe from the baseline tree).
    nebula_host_cpu_entry(NebulaD11)
    nebula_host_delay_loads(NebulaD11)
    # The constant ring on the machine's own device (P-242); the test compiles its own small shaders.
    galaxy_test(galaxy_d3d11_constant_ring_tests SOURCES runtime/src/render/d3d11_constant_ring_tests.cpp
        LIBS galaxy_render_d3d11 d3dcompiler)
    # The constants watch (P-332): a wrong binding prints its line, the right ones nothing.
    galaxy_test(galaxy_d3d11_constants_watch_tests SOURCES runtime/src/render/d3d11_constants_watch_tests.cpp
        LIBS galaxy_render_d3d11 d3dcompiler)
endif()

# AV1 movies (a mod's movie, NMOV codec 1): dav1d 1.5.1 built in-tree (cmake/dav1d.cmake), for every renderer.
option(NEBULA_AV1 "Decode AV1 movies with dav1d" ON)
if(NEBULA_AV1)
    include(cmake/dav1d.cmake)
    foreach(target galaxy_render galaxy_render_vk galaxy_render_d3d11)
        if(TARGET ${target})
            target_link_libraries(${target} PRIVATE nebula_dav1d)
            target_compile_definitions(${target} PRIVATE NEBULA_AV1=1)
        endif()
    endforeach()
endif()
