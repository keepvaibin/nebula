cmake_minimum_required(VERSION 3.24)
project(NebulaRuntime VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The first runtime milestone targets Windows x64 only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Nebula requires a 64-bit build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    GALAXY_WITH_STREAMLINE_BRIDGE
    "Build the SDK-backed NVIDIA Streamline bridge. Hidden/off until real bridge sources and SDK deployment are present."
    OFF
)
mark_as_advanced(GALAXY_WITH_STREAMLINE_BRIDGE)
if(GALAXY_WITH_STREAMLINE_BRIDGE)
    message(
        FATAL_ERROR
        "GALAXY_WITH_STREAMLINE_BRIDGE requires a real SDK-backed Streamline "
        "bridge target. Do not enable it with stub sources, checked-in SDK "
        "binaries, or renderer hot-path placeholders."
    )
endif()

if(MSVC)
    foreach(lang C CXX)
        string(REGEX REPLACE "(^| )/Ob[0-9]" "\\1/Ob3" CMAKE_${lang}_FLAGS_RELEASE "${CMAKE_${lang}_FLAGS_RELEASE}")
    endforeach()
    # Select the native ISA at install time. AUTO enables AVX2/FMA only when
    # the build host and its OS can execute it; older x64 hosts retain MSVC's
    # SSE2 baseline. Keep /fp:precise: guest results must remain bit-identical
    # to the Berkeley SoftFloat reference.
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
    message(STATUS "Galaxy native CPU target: ${galaxy_native_isa_effective} (requested ${GALAXY_NATIVE_ISA})")
    if(galaxy_native_isa_effective STREQUAL "AVX2")
        # /arch:AVX2 also makes FMA3 available to the code generator. Under
        # /fp:precise that is only safe because Visual Studio 2022 changed the
        # /fp:precise default to fp_contract(off) (documented in
        # learn.microsoft.com/cpp/build/reference/fp-specify-floating-point-behavior).
        # Before VS2022, /fp:precise defaulted to fp_contract(on), so an FMA3-
        # capable target would let the compiler fuse a*b+c and round once.
        # galaxy_ppc_float's TwoSum/TwoProduct error terms and every guest
        # single-precision result are only exact because that cannot happen, so
        # refuse the combination rather than emit a guest-visible FP difference.
        if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC_VERSION LESS 1930)
            message(FATAL_ERROR
                "GALAXY_NATIVE_ISA=AVX2 with ${CMAKE_CXX_COMPILER_ID} "
                "${MSVC_VERSION} would allow floating-point contraction under "
                "/fp:precise and change guest FP results. Use a Visual Studio "
                "2022 (MSVC 19.30+) toolset, clang-cl, or GALAXY_NATIVE_ISA=SSE2.")
        endif()
        # /arch:AVX2 is an MSVC cl.exe spelling. Setup and build-release both
        # drive this project with clang-cl (installer/toolchain.json pins it and
        # InstallEngine passes -DCMAKE_CXX_COMPILER=<clang-cl>), where
        # CMAKE_CXX_COMPILER_ID is "Clang" but MSVC is still true -- so the
        # message above reported AVX2 while every translation unit was compiled
        # for the SSE2 baseline and the runtime lost VEX encoding, the wider
        # register file and vector integer compares. clang-cl accepts the
        # GCC-style -mavx2. Deliberately NOT -mfma: guest float results must stay
        # bit-identical to the SoftFloat reference and /fp:precise is mandatory.
        # (clang's own -ffp-contract default is on, so withholding the FMA
        # feature is what keeps contraction impossible on that path.)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-mavx2>)
        else()
            add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/arch:AVX2>)
        endif()
    endif()
    # NOTE: deliberately NOT enabling LTCG/IPO. The recompiled game module is
    # ~51 MB; whole-program link on a weak install-time host can take very long
    # or exhaust memory. Per-translation-unit /O2 keeps install cost bounded.
endif()

set(SOFTFLOAT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/berkeley-softfloat-3")
set(
    SOFTFLOAT_COMMON_NAMES
    s_eq128
    s_le128
    s_lt128
    s_shortShiftLeft128
    s_shortShiftRight128
    s_shortShiftRightJam64
    s_shortShiftRightJam64Extra
    s_shortShiftRightJam128
    s_shiftRightJam128
    s_shortShiftRightJam128Extra
    s_shiftRightJam32
    s_shiftRightJam64
    s_shiftRightJam64Extra
    s_countLeadingZeros8
    s_countLeadingZeros16
    s_countLeadingZeros32
    s_countLeadingZeros64
    s_add128
    s_sub128
    s_mul64ByShifted32To128
    s_mul64To128
    s_approxRecip_1Ks
    s_approxRecip32_1
    s_approxRecipSqrt_1Ks
    s_approxRecipSqrt32_1
    s_roundToUI32
    s_roundToUI64
    s_roundToI32
    s_roundToI64
    s_normSubnormalF32Sig
    s_roundPackToF32
    s_normRoundPackToF32
    s_addMagsF32
    s_subMagsF32
    s_mulAddF32
    s_normSubnormalF64Sig
    s_roundPackToF64
    s_normRoundPackToF64
    s_addMagsF64
    s_subMagsF64
    s_mulAddF64
    softfloat_state
    i32_to_f32
    i32_to_f64
    f32_to_i32
    f32_to_i32_r_minMag
    f32_to_f64
    f64_to_f32
    f64_to_i32
    f64_to_i32_r_minMag
    f32_roundToInt
    f32_add
    f32_sub
    f32_mul
    f32_mulAdd
    f32_div
    f32_rem
    f32_sqrt
    f32_eq
    f32_le
    f32_lt
    f32_eq_signaling
    f32_le_quiet
    f32_lt_quiet
    f32_isSignalingNaN
    f64_roundToInt
    f64_add
    f64_sub
    f64_mul
    f64_mulAdd
    f64_div
    f64_rem
    f64_sqrt
    f64_eq
    f64_le
    f64_lt
    f64_eq_signaling
    f64_le_quiet
    f64_lt_quiet
    f64_isSignalingNaN
)
list(TRANSFORM SOFTFLOAT_COMMON_NAMES PREPEND "${SOFTFLOAT_ROOT}/source/")
list(TRANSFORM SOFTFLOAT_COMMON_NAMES APPEND ".c")
set(
    SOFTFLOAT_SPECIALIZE_NAMES
    softfloat_raiseFlags
    s_f32UIToCommonNaN
    s_commonNaNToF32UI
    s_propagateNaNF32UI
    s_f64UIToCommonNaN
    s_commonNaNToF64UI
    s_propagateNaNF64UI
)
list(
    TRANSFORM
    SOFTFLOAT_SPECIALIZE_NAMES
    PREPEND
    "${SOFTFLOAT_ROOT}/source/8086-SSE/"
)
list(TRANSFORM SOFTFLOAT_SPECIALIZE_NAMES APPEND ".c")
add_library(
    galaxy_softfloat
    STATIC
    ${SOFTFLOAT_COMMON_NAMES}
    ${SOFTFLOAT_SPECIALIZE_NAMES}
)
target_include_directories(
    galaxy_softfloat
    PUBLIC
        "${SOFTFLOAT_ROOT}/source/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/softfloat"
        "${SOFTFLOAT_ROOT}/source/8086-SSE"
)
target_compile_definitions(
    galaxy_softfloat
    PRIVATE
        SOFTFLOAT_FAST_INT64=1
        SOFTFLOAT_FAST_DIV32TO16=1
        SOFTFLOAT_FAST_DIV64TO32=1
        INLINE_LEVEL=5
)
if(MSVC)
    target_compile_options(galaxy_softfloat PRIVATE /W0 /GS- /Oi /favor:INTEL64)
endif()

add_library(galaxy_ppc_float STATIC runtime/src/ppc_float.cpp)
target_include_directories(
    galaxy_ppc_float
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/softfloat"
)
target_link_libraries(galaxy_ppc_float PUBLIC galaxy_softfloat)
if(MSVC)
    target_compile_options(galaxy_ppc_float PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

add_library(galaxy_dsp_alu STATIC runtime/src/dsp_alu.cpp)
target_include_directories(
    galaxy_dsp_alu
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
)
if(MSVC)
    target_compile_options(galaxy_dsp_alu PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

add_library(galaxy_runtime_abi INTERFACE)
target_include_directories(
    galaxy_runtime_abi
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
)

add_library(galaxy_native_dsp STATIC runtime/src/native_dsp.cpp)
target_link_libraries(galaxy_native_dsp PUBLIC galaxy_dsp_alu galaxy_runtime_abi)
if(MSVC)
    target_compile_options(galaxy_native_dsp PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

# Install-time generated DSP microcode. Like the recompiled game module, the
# lowered RMGE01 ucode is produced on the end-user machine by
# `nebula-recomp generate-dsp <game>` and is never committed. When present it
# is compiled into the native DSP library and GALAXY_HAVE_NATIVE_DSP is set so
# the host runs the real coprocessor (gated by GALAXY_DSP_NATIVE).
set(NEBULA_GENERATED_DSP_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/dsp/rmge01_dsp.cpp"
    CACHE FILEPATH "Locally generated RMGE01 DSP source")
set(galaxy_generated_dsp_source "${NEBULA_GENERATED_DSP_SOURCE}")
# Release builds load the DSP from RMGE01_dsp.dll, which Setup compiles,
# so the shipped runtime contains nothing derived from the game.
option(NEBULA_DSP_MODULE "Load the generated DSP from RMGE01_dsp.dll" OFF)
if(NEBULA_DSP_MODULE)
    target_compile_definitions(galaxy_native_dsp PUBLIC GALAXY_HAVE_NATIVE_DSP=1 GALAXY_NATIVE_DSP_MODULE=1)
    message(STATUS "Nebula: native DSP loaded from RMGE01_dsp.dll")
elseif(EXISTS "${galaxy_generated_dsp_source}")
    target_sources(galaxy_native_dsp PRIVATE "${galaxy_generated_dsp_source}")
    target_compile_definitions(galaxy_native_dsp PUBLIC GALAXY_HAVE_NATIVE_DSP=1)
    message(STATUS
        "Nebula: native DSP ucode present — GALAXY_HAVE_NATIVE_DSP=1")
else()
    message(STATUS
        "Nebula: no generated DSP ucode "
        "(run `nebula-recomp generate-dsp <game>`); "
        "GALAXY_DSP_NATIVE launches will hard-fail")
endif()

add_library(galaxy_test_module SHARED runtime/src/test_module.cpp)
target_link_libraries(galaxy_test_module PRIVATE galaxy_runtime_abi)
target_compile_definitions(galaxy_test_module PRIVATE GALAXY_BUILDING_GAME_MODULE=1)
set_target_properties(galaxy_test_module PROPERTIES PREFIX "")

add_executable(galaxy_runtime_smoke runtime/src/runtime_smoke.cpp)
target_link_libraries(galaxy_runtime_smoke PRIVATE galaxy_runtime_abi)

add_library(galaxy_atomic_file STATIC runtime/src/atomic_file.cpp)
target_link_libraries(galaxy_atomic_file PUBLIC galaxy_runtime_abi)
add_executable(galaxy_atomic_file_tests runtime/src/atomic_file_tests.cpp)
target_link_libraries(galaxy_atomic_file_tests PRIVATE galaxy_atomic_file)

add_library(galaxy_runtime_instance STATIC runtime/src/runtime_instance.cpp)
target_link_libraries(galaxy_runtime_instance PUBLIC galaxy_runtime_abi)
add_executable(
    galaxy_runtime_instance_tests
    runtime/src/runtime_instance_tests.cpp)
target_link_libraries(
    galaxy_runtime_instance_tests
    PRIVATE galaxy_runtime_instance)

add_library(galaxy_runtime_settings STATIC runtime/src/runtime_settings.cpp)
target_link_libraries(
    galaxy_runtime_settings
    PUBLIC galaxy_runtime_abi galaxy_atomic_file)
if(MSVC)
    target_compile_options(galaxy_runtime_settings PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

add_executable(galaxy_runtime_settings_tests runtime/src/runtime_settings_tests.cpp)
target_link_libraries(galaxy_runtime_settings_tests PRIVATE galaxy_runtime_settings)

add_library(galaxy_runtime_timeline STATIC runtime/src/runtime_timeline.cpp)
target_link_libraries(galaxy_runtime_timeline PUBLIC galaxy_runtime_abi)
if(MSVC)
    target_compile_options(galaxy_runtime_timeline PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

add_executable(galaxy_runtime_timeline_tests runtime/src/runtime_timeline_tests.cpp)
target_link_libraries(galaxy_runtime_timeline_tests PRIVATE galaxy_runtime_timeline)

add_library(
    galaxy_decrementer_deadline
    STATIC
    runtime/src/decrementer_deadline.cpp)
target_link_libraries(galaxy_decrementer_deadline PUBLIC galaxy_runtime_abi)
if(MSVC)
    target_compile_options(
        galaxy_decrementer_deadline
        PRIVATE
            /GS-
            /GR-
            /Oi
            /favor:INTEL64)
endif()

add_executable(
    galaxy_decrementer_deadline_tests
    runtime/src/decrementer_deadline_tests.cpp)
target_link_libraries(
    galaxy_decrementer_deadline_tests
    PRIVATE
        galaxy_decrementer_deadline)

add_library(
    galaxy_frame_cadence_diagnostics
    STATIC
    runtime/src/frame_cadence_diagnostics.cpp)
target_link_libraries(
    galaxy_frame_cadence_diagnostics
    PUBLIC
        galaxy_runtime_abi)
if(MSVC)
    target_compile_options(
        galaxy_frame_cadence_diagnostics
        PRIVATE
            /GS-
            /GR-
            /Oi
            /favor:INTEL64)
endif()

add_executable(
    galaxy_frame_cadence_diagnostics_tests
    runtime/src/frame_cadence_diagnostics_tests.cpp)
target_link_libraries(
    galaxy_frame_cadence_diagnostics_tests
    PRIVATE
        galaxy_frame_cadence_diagnostics)

add_library(galaxy_checkpoint_gate STATIC runtime/src/checkpoint_gate.cpp)
target_link_libraries(galaxy_checkpoint_gate PUBLIC galaxy_runtime_abi)
if(MSVC)
    target_compile_options(galaxy_checkpoint_gate PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

add_executable(galaxy_checkpoint_gate_tests runtime/src/checkpoint_gate_tests.cpp)
target_link_libraries(
    galaxy_checkpoint_gate_tests
    PRIVATE
        galaxy_checkpoint_gate
        galaxy_runtime_abi)

add_executable(galaxy_flat_dispatch_tests runtime/src/flat_dispatch_tests.cpp)
target_link_libraries(galaxy_flat_dispatch_tests PRIVATE galaxy_runtime_abi)

add_executable(
    galaxy_opening_route_marker_tests
    runtime/src/opening_route_marker_tests.cpp)
target_link_libraries(galaxy_opening_route_marker_tests PRIVATE galaxy_runtime_abi)

# Isolated WiiCompiled-derived VA mapping test; the native host also links the
# mapping owner for its explicit opt-in RAM backing path.
add_executable(
    galaxy_flat_guest_memory_tests
    runtime/src/flat_guest_memory.cpp
    runtime/src/flat_guest_memory_tests.cpp)
target_link_libraries(galaxy_flat_guest_memory_tests PRIVATE galaxy_runtime_abi)

add_executable(
    galaxy_function_async_flight_tests
    runtime/src/function_async_flight_tests.cpp)
target_link_libraries(
    galaxy_function_async_flight_tests
    PRIVATE
        galaxy_runtime_abi)

add_executable(
    galaxy_external_dispatch_tests
    runtime/src/external_dispatch_tests.cpp)
target_link_libraries(galaxy_external_dispatch_tests PRIVATE galaxy_runtime_abi)

add_executable(galaxy_vi_boundary_tests runtime/src/vi_boundary_tests.cpp)
target_link_libraries(galaxy_vi_boundary_tests PRIVATE galaxy_runtime_abi)

add_library(galaxy_host_pointer_events STATIC runtime/src/host_pointer_events.cpp)
target_link_libraries(galaxy_host_pointer_events PUBLIC galaxy_runtime_abi)
if(MSVC)
    target_compile_options(galaxy_host_pointer_events PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

add_executable(
    galaxy_host_pointer_events_tests
    runtime/src/host_pointer_events_tests.cpp)
target_link_libraries(
    galaxy_host_pointer_events_tests
    PRIVATE
        galaxy_host_pointer_events)

add_library(galaxy_native_input STATIC runtime/src/native_input.cpp)
target_link_libraries(galaxy_native_input PUBLIC galaxy_runtime_abi)
if(MSVC)
    target_compile_options(galaxy_native_input PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

add_library(
    galaxy_native_input_causality
    STATIC
    runtime/src/native_input_causality.cpp)
target_link_libraries(
    galaxy_native_input_causality
    PUBLIC
        galaxy_runtime_abi)
if(MSVC)
    target_compile_options(
        galaxy_native_input_causality
        PRIVATE
            /GS-
            /GR-
            /Oi
            /favor:INTEL64)
endif()

add_library(galaxy_ai_pcm_identity STATIC runtime/src/ai_pcm_identity.cpp)
target_link_libraries(galaxy_ai_pcm_identity PUBLIC galaxy_runtime_abi)
if(MSVC)
    target_compile_options(galaxy_ai_pcm_identity PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

# Tiny, dependency-free (only <atomic>) shared flag: the file-select scene
# hint written by galaxy_native_host (native_host.cpp, observing DVDLowRead)
# and read by galaxy_gx (renderer_d3d12.cpp's file-select UI button). Kept
# out of both of those heavier libraries so neither has to link the other
# just for this one process-wide flag.
add_library(galaxy_scene_hint STATIC runtime/src/host/scene_hint.cpp)
target_include_directories(
    galaxy_scene_hint
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
)
if(MSVC)
    target_compile_options(galaxy_scene_hint PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

add_library(galaxy_native_host STATIC
    runtime/src/native_host.cpp
    runtime/src/flat_guest_memory.cpp)
target_link_libraries(
    galaxy_native_host
    PUBLIC
        galaxy_runtime_abi
        galaxy_runtime_settings
        galaxy_native_input
        galaxy_native_dsp
        galaxy_frame_cadence_diagnostics
        galaxy_ai_pcm_identity
        galaxy_scene_hint
        bcrypt
        hid
        ole32
        setupapi
        xinput9_1_0
        xaudio2
)
if(MSVC)
    target_compile_options(galaxy_native_host PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

# Some legacy unit tests still exercise the retired DSP mixer helpers directly
# as behavior-level fixtures. Build a separate test-support variant so the
# product runtime cannot re-enable that mixer with an environment variable.
add_library(galaxy_native_host_test_support STATIC
    runtime/src/native_host.cpp
    runtime/src/flat_guest_memory.cpp)
target_link_libraries(
    galaxy_native_host_test_support
    PUBLIC
        galaxy_runtime_abi
        galaxy_runtime_settings
        galaxy_native_input
        galaxy_native_dsp
        galaxy_frame_cadence_diagnostics
        galaxy_ai_pcm_identity
        galaxy_scene_hint
        bcrypt
        hid
        ole32
        setupapi
        xinput9_1_0
        xaudio2
)
target_compile_definitions(
    galaxy_native_host_test_support
    PRIVATE
        GALAXY_TEST_ALLOW_RETIRED_DSP_MIXER=1
        GALAXY_NATIVE_HOST_TEST_SUPPORT=1
)
if(MSVC)
    target_compile_options(
        galaxy_native_host_test_support
        PRIVATE
            /GS-
            /GR-
            /Oi
            /favor:INTEL64)
endif()

# galaxy_gx: static lib used by NebulaRuntime and the renderer tests.
# The public API (gx_d3d12.h) is implemented by gx_shim.cpp over GxBackend.
add_library(galaxy_gx STATIC
    runtime/src/gx/gx_shim.cpp
    runtime/src/gx/gx_headers_compile.cpp
    runtime/src/gx/gx_state.cpp
    runtime/src/gx/frame_completion.cpp
    runtime/src/gx/render_config.cpp
    runtime/src/gx/renderer_d3d12.cpp
    runtime/src/gx/window_owner.cpp
    runtime/src/gx/texture_cache.cpp
    runtime/src/gx/vertex_loader.cpp
    runtime/src/gx/shader_keys.cpp
    runtime/src/gx/efb_copy.cpp
    runtime/src/gx/line_point_raster.cpp
    runtime/src/gx/pipeline_cache.cpp
    runtime/src/gx/shader_gen.cpp
    runtime/src/gx/fifo_parser.cpp
    runtime/src/gx/gx_backend.cpp
    runtime/src/frame_telemetry.cpp
    runtime/src/gx/game_asset_extract.cpp
)
target_link_libraries(galaxy_gx
    PUBLIC
        # Renderer window events use the real native input bridge.
        galaxy_native_host
        galaxy_frame_cadence_diagnostics
        galaxy_runtime_abi
        galaxy_runtime_settings
        galaxy_host_pointer_events
        galaxy_scene_hint
        d3d12
        dxgi
        d3dcompiler
)
target_compile_definitions(
    galaxy_gx
    PRIVATE
        GALAXY_WITH_STREAMLINE_BRIDGE=$<BOOL:${GALAXY_WITH_STREAMLINE_BRIDGE}>
)
if(MSVC)
    target_compile_options(galaxy_gx PRIVATE /GS- /GR- /Oi /favor:INTEL64)
endif()

# Throwaway manual verification tool (not a ctest) for
# game_asset_extract.cpp against a real game dump -- see tools/dev/.
add_executable(galaxy_verify_asset_extract tools/dev/verify_asset_extract.cpp)
target_link_libraries(galaxy_verify_asset_extract PRIVATE galaxy_gx)

add_executable(galaxy_game_asset_extract_tests
    runtime/src/gx/game_asset_extract_tests.cpp
    runtime/src/gx/game_asset_extract.cpp)
target_include_directories(galaxy_game_asset_extract_tests PRIVATE runtime/include)
target_compile_features(galaxy_game_asset_extract_tests PRIVATE cxx_std_20)

add_executable(NebulaRuntime
    runtime/src/native_runtime.cpp
    runtime/src/native_thp_video.cpp
)
# Exported releases pass their source identity; working checkouts report Git
# identity/dirty state at build time. This is runtime provenance, not a label
# inferred from the launcher or a module compatibility key.
set(NEBULA_SOURCE_REVISION "" CACHE STRING "Revision of exported source")
set(NEBULA_SOURCE_REVISION_KIND "unknown" CACHE STRING "Exported revision object type (commit or tree)")
set(NEBULA_SOURCE_GIT_TREE "unknown" CACHE STRING "Git tree of exported source")
set(NEBULA_SOURCE_TREE_SHA256 "" CACHE STRING "Exported source manifest tree SHA256")
set(NEBULA_SOURCE_DIRTY "unknown" CACHE STRING "Exported source dirty state")
mark_as_advanced(NEBULA_SOURCE_REVISION NEBULA_SOURCE_REVISION_KIND NEBULA_SOURCE_GIT_TREE NEBULA_SOURCE_TREE_SHA256 NEBULA_SOURCE_DIRTY)
add_custom_target(nebula_runtime_identity
    COMMAND "${CMAKE_COMMAND}"
        "-DCONFIG_FILE=${CMAKE_CURRENT_BINARY_DIR}/runtime_identity_config.cmake"
        "-DBUILD_CONFIG=$<CONFIG>"
        "-DOUTPUT_HEADER=${CMAKE_CURRENT_BINARY_DIR}/nebula_build_identity.h"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/runtime_build_identity.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/nebula_build_identity.h"
    VERBATIM)
add_dependencies(NebulaRuntime nebula_runtime_identity)
target_include_directories(NebulaRuntime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_executable(galaxy_native_thp_video_tests runtime/src/native_thp_video_tests.cpp runtime/src/native_thp_video.cpp)
target_link_libraries(galaxy_native_thp_video_tests PRIVATE galaxy_runtime_abi)
add_executable(galaxy_verify_thp_native tools/dev/verify_thp_native.cpp runtime/src/native_thp_video.cpp)
target_link_libraries(galaxy_verify_thp_native PRIVATE galaxy_runtime_abi)
target_link_libraries(
    NebulaRuntime
    PRIVATE
        galaxy_native_host
        galaxy_native_input_causality
        galaxy_gx
        galaxy_ppc_float
        galaxy_checkpoint_gate
        galaxy_decrementer_deadline
        galaxy_runtime_timeline)
target_link_libraries(NebulaRuntime PRIVATE galaxy_runtime_instance)
target_link_libraries(NebulaRuntime PRIVATE xinput9_1_0)
target_link_libraries(NebulaRuntime PRIVATE dbghelp)
if(MSVC)
    # Generated DSP probe/entry functions are referenced through the native
    # host, but the production link proof also requires archive retention to
    # be explicit in the final executable graph.  WHOLE_ARCHIVE prevents a
    # future reference-shape change from silently orphaning generated ucode.
    set_property(
        TARGET NebulaRuntime
        PROPERTY LINK_LIBRARY_OVERRIDE_galaxy_native_dsp WHOLE_ARCHIVE)
    # Cooperative guest threads nest on the host stack (one native frame tree
    # per suspended thread), so the default 1 MiB reserve overflows once
    # mid-function resume lets every thread actually run.  Reserve is virtual
    # address space only; pages commit on demand.
    target_compile_options(NebulaRuntime PRIVATE /GS- /GR- /Oi /favor:INTEL64)
    target_link_options(NebulaRuntime PRIVATE /STACK:268435456,1048576)
endif()
get_target_property(NEBULA_ID_TARGET_OPTIONS NebulaRuntime COMPILE_OPTIONS)
if(NOT NEBULA_ID_TARGET_OPTIONS)
    set(NEBULA_ID_TARGET_OPTIONS "")
endif()
get_directory_property(NEBULA_ID_DIRECTORY_OPTIONS COMPILE_OPTIONS)
configure_file(cmake/runtime_identity_config.cmake.in runtime_identity_config.cmake @ONLY)

add_executable(galaxy_native_api_tests runtime/src/native_api_tests.cpp)
target_link_libraries(galaxy_native_api_tests PRIVATE galaxy_runtime_abi galaxy_ppc_float)
if(MSVC)
    # This exhaustive helper test keeps many independent PpcContext fixtures
    # alive in one function; optimized MSVC currently reserves about 1.3 MiB
    # for that frame. Give the test a measured fixed reserve instead of
    # depending on the PE default 1 MiB stack (which fails before main runs).
    target_link_options(galaxy_native_api_tests PRIVATE /STACK:4194304,65536)
endif()

add_executable(galaxy_native_host_tests runtime/src/native_host_tests.cpp)
target_link_libraries(
    galaxy_native_host_tests
    PRIVATE
        galaxy_native_host_test_support
        galaxy_gx
        galaxy_host_pointer_events
        galaxy_native_input_causality)

add_executable(
    galaxy_native_audio_queue_tests
    runtime/src/native_audio_queue_tests.cpp)
target_link_libraries(
    galaxy_native_audio_queue_tests
    PRIVATE
        galaxy_runtime_abi)

add_executable(galaxy_thp_audio_worker_tests
    runtime/src/thp_audio_worker_tests.cpp)
target_link_libraries(galaxy_thp_audio_worker_tests PRIVATE galaxy_runtime_abi)

add_executable(galaxy_owned_fifo_packet_tests
    runtime/src/gx/owned_fifo_packet_tests.cpp)
target_link_libraries(galaxy_owned_fifo_packet_tests PRIVATE galaxy_runtime_abi)

add_executable(galaxy_diagnostic_stick_lease_tests runtime/src/diagnostic_stick_lease_tests.cpp)
target_link_libraries(galaxy_diagnostic_stick_lease_tests PRIVATE galaxy_runtime_abi)
add_executable(galaxy_simulation_cache_affinity_tests runtime/src/simulation_cache_affinity_tests.cpp)
target_link_libraries(galaxy_simulation_cache_affinity_tests PRIVATE galaxy_runtime_abi)

add_executable(galaxy_native_input_tests runtime/src/native_input_tests.cpp)
target_link_libraries(galaxy_native_input_tests PRIVATE galaxy_native_input)

add_executable(galaxy_layout_aspect_tests runtime/src/layout_aspect_tests.cpp)
target_link_libraries(galaxy_layout_aspect_tests PRIVATE galaxy_runtime_abi)

add_executable(
    galaxy_native_input_causality_tests
    runtime/src/native_input_causality_tests.cpp)
target_link_libraries(
    galaxy_native_input_causality_tests
    PRIVATE
        galaxy_native_input_causality)

add_executable(galaxy_ai_pcm_identity_tests runtime/src/ai_pcm_identity_tests.cpp)
target_link_libraries(
    galaxy_ai_pcm_identity_tests
    PRIVATE
        galaxy_ai_pcm_identity)

add_executable(galaxy_ppc_float_tests runtime/src/ppc_float_tests.cpp)
target_link_libraries(galaxy_ppc_float_tests PRIVATE galaxy_ppc_float)
target_include_directories(galaxy_ppc_float_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/runtime/softfloat")

add_executable(galaxy_dsp_alu_tests runtime/src/dsp_alu_tests.cpp)
target_link_libraries(galaxy_dsp_alu_tests PRIVATE galaxy_dsp_alu)

add_executable(galaxy_native_dsp_tests runtime/src/native_dsp_tests.cpp)
target_link_libraries(
    galaxy_native_dsp_tests
    PRIVATE
        galaxy_native_dsp
        galaxy_runtime_timeline)

# Offline FXC validation of every ShaderGenerator codegen path (texgen,
# lighting, TEV compare, swap tables, fog, ztex) — catches HLSL slips in the
# gate instead of as invisible geometry mid-boot.
add_executable(galaxy_shader_gen_tests runtime/src/gx/shader_gen_tests.cpp)
target_link_libraries(galaxy_shader_gen_tests PRIVATE galaxy_gx)

add_executable(galaxy_fifo_parser_tests runtime/src/gx/fifo_parser_tests.cpp)
target_link_libraries(galaxy_fifo_parser_tests PRIVATE galaxy_gx)

add_executable(galaxy_vertex_loader_tests
    runtime/src/gx/vertex_loader_tests.cpp)
target_link_libraries(galaxy_vertex_loader_tests PRIVATE galaxy_gx)

add_executable(galaxy_render_config_tests runtime/src/gx/render_config_tests.cpp)
target_link_libraries(galaxy_render_config_tests PRIVATE galaxy_gx)

add_executable(galaxy_frame_completion_tests
    runtime/src/gx/frame_completion_tests.cpp)
target_link_libraries(galaxy_frame_completion_tests PRIVATE galaxy_gx)

add_executable(galaxy_window_owner_tests
    runtime/src/gx/window_owner_tests.cpp
    runtime/src/gx/window_owner.cpp)
target_include_directories(galaxy_window_owner_tests PRIVATE runtime/include)

add_executable(galaxy_memory_snapshot_tests
    runtime/src/gx/memory_snapshot_tests.cpp)
target_link_libraries(galaxy_memory_snapshot_tests PRIVATE galaxy_gx)

add_executable(galaxy_sampler_key_tests runtime/src/gx/sampler_key_tests.cpp)
target_link_libraries(galaxy_sampler_key_tests PRIVATE galaxy_gx)

add_executable(galaxy_xfb_present_selection_tests
    runtime/src/gx/xfb_present_selection_tests.cpp)
target_link_libraries(galaxy_xfb_present_selection_tests PRIVATE galaxy_gx)

add_executable(galaxy_efb_conversion_tests
    runtime/src/gx/efb_conversion_tests.cpp)
target_link_libraries(galaxy_efb_conversion_tests PRIVATE galaxy_gx)

add_executable(galaxy_line_point_raster_tests
    runtime/src/gx/line_point_raster_tests.cpp)
target_link_libraries(galaxy_line_point_raster_tests PRIVATE galaxy_gx)

add_executable(galaxy_sorted_dirty_ranges_tests
    runtime/src/gx/sorted_dirty_ranges_tests.cpp)
target_link_libraries(galaxy_sorted_dirty_ranges_tests PRIVATE galaxy_gx)

add_executable(galaxy_same_context_rfi_tests runtime/src/same_context_rfi_tests.cpp)
target_link_libraries(galaxy_same_context_rfi_tests PRIVATE galaxy_runtime_abi)

# Cache file recovery exercises the production loader/writer without a GPU.
# Fault injection exists only in this target's own pipeline_cache.cpp object.
add_executable(galaxy_pipeline_cache_persistence_tests
    runtime/src/gx/pipeline_cache_persistence_tests.cpp
    runtime/src/gx/pipeline_cache.cpp)
target_compile_definitions(galaxy_pipeline_cache_persistence_tests
    PRIVATE GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO=1)
target_link_libraries(galaxy_pipeline_cache_persistence_tests PRIVATE galaxy_gx)

add_executable(galaxy_native_settings_overlay_tests
    runtime/src/gx/native_settings_overlay_tests.cpp)
target_link_libraries(galaxy_native_settings_overlay_tests PRIVATE galaxy_gx)

if(MSVC)
    foreach(
        target
        galaxy_gx
        galaxy_test_module
        galaxy_runtime_smoke
        galaxy_runtime_settings
        galaxy_runtime_settings_tests
        galaxy_atomic_file
        galaxy_atomic_file_tests
        galaxy_runtime_instance
        galaxy_runtime_instance_tests
        galaxy_runtime_timeline
        galaxy_runtime_timeline_tests
        galaxy_decrementer_deadline
        galaxy_decrementer_deadline_tests
        galaxy_frame_cadence_diagnostics
        galaxy_frame_cadence_diagnostics_tests
        galaxy_checkpoint_gate
        galaxy_checkpoint_gate_tests
        galaxy_flat_dispatch_tests
        galaxy_opening_route_marker_tests
        galaxy_flat_guest_memory_tests
        galaxy_function_async_flight_tests
        galaxy_external_dispatch_tests
        galaxy_vi_boundary_tests
        galaxy_host_pointer_events
        galaxy_host_pointer_events_tests
        galaxy_native_input
        galaxy_native_input_causality
        galaxy_native_input_causality_tests
        galaxy_ai_pcm_identity
        galaxy_ai_pcm_identity_tests
        galaxy_native_input_tests
        galaxy_diagnostic_stick_lease_tests
        galaxy_simulation_cache_affinity_tests
        galaxy_native_host
        galaxy_native_host_test_support
        galaxy_native_audio_queue_tests
        galaxy_native_thp_video_tests
        galaxy_thp_audio_worker_tests
        galaxy_owned_fifo_packet_tests
        galaxy_verify_asset_extract
        galaxy_verify_thp_native
        galaxy_game_asset_extract_tests
        NebulaRuntime
        galaxy_native_api_tests
        galaxy_native_host_tests
        galaxy_ppc_float
        galaxy_ppc_float_tests
        galaxy_dsp_alu
        galaxy_dsp_alu_tests
        galaxy_native_dsp
        galaxy_native_dsp_tests
        galaxy_shader_gen_tests
        galaxy_fifo_parser_tests
        galaxy_vertex_loader_tests
        galaxy_render_config_tests
        galaxy_frame_completion_tests
        galaxy_window_owner_tests
        galaxy_memory_snapshot_tests
        galaxy_sampler_key_tests
        galaxy_xfb_present_selection_tests
        galaxy_efb_conversion_tests
        galaxy_line_point_raster_tests
        galaxy_sorted_dirty_ranges_tests
        galaxy_same_context_rfi_tests
        galaxy_pipeline_cache_persistence_tests
        galaxy_native_settings_overlay_tests
    )
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
    endforeach()
endif()

enable_testing()

function(galaxy_add_native_test name target)
    add_test(NAME ${name} COMMAND ${target} ${ARGN})
endfunction()

galaxy_add_native_test(native_api_helpers galaxy_native_api_tests)
galaxy_add_native_test(atomic_settings_file galaxy_atomic_file_tests)
galaxy_add_native_test(runtime_instance_lease galaxy_runtime_instance_tests)
galaxy_add_native_test(runtime_settings_helpers galaxy_runtime_settings_tests)
galaxy_add_native_test(runtime_timeline_helpers galaxy_runtime_timeline_tests)
galaxy_add_native_test(
    decrementer_deadline_policy
    galaxy_decrementer_deadline_tests)
galaxy_add_native_test(
    frame_cadence_diagnostics
    galaxy_frame_cadence_diagnostics_tests)
galaxy_add_native_test(checkpoint_gate_policy galaxy_checkpoint_gate_tests)
galaxy_add_native_test(flat_scheduler_dispatch galaxy_flat_dispatch_tests)
galaxy_add_native_test(opening_route_marker_guard galaxy_opening_route_marker_tests)
galaxy_add_native_test(flat_guest_memory_aliases galaxy_flat_guest_memory_tests)
galaxy_add_native_test(
    function_async_flight
    galaxy_function_async_flight_tests)
galaxy_add_native_test(external_interrupt_dispatch galaxy_external_dispatch_tests)
galaxy_add_native_test(persistent_vi_boundary galaxy_vi_boundary_tests)
galaxy_add_native_test(
    host_pointer_events
    galaxy_host_pointer_events_tests)
galaxy_add_native_test(native_input_reports galaxy_native_input_tests)
galaxy_add_native_test(layout_aspect_geometry galaxy_layout_aspect_tests)
galaxy_add_native_test(diagnostic_stick_lease galaxy_diagnostic_stick_lease_tests)
galaxy_add_native_test(simulation_cache_affinity galaxy_simulation_cache_affinity_tests)
galaxy_add_native_test(
    native_input_causality
    galaxy_native_input_causality_tests)
galaxy_add_native_test(ai_pcm_identity galaxy_ai_pcm_identity_tests)
galaxy_add_native_test(native_host_loader galaxy_native_host_tests)
galaxy_add_native_test(
    native_audio_queue_ownership
    galaxy_native_audio_queue_tests)
galaxy_add_native_test(native_thp_video
    galaxy_native_thp_video_tests)
galaxy_add_native_test(thp_audio_worker_ownership
    galaxy_thp_audio_worker_tests)
galaxy_add_native_test(owned_fifo_packet_ownership
    galaxy_owned_fifo_packet_tests)
galaxy_add_native_test(ppc_float_helpers galaxy_ppc_float_tests)
galaxy_add_native_test(dsp_alu_helpers galaxy_dsp_alu_tests)
galaxy_add_native_test(native_dsp_coprocessor galaxy_native_dsp_tests)
galaxy_add_native_test(render_config_gates galaxy_render_config_tests)
galaxy_add_native_test(frame_pe_completion galaxy_frame_completion_tests)
galaxy_add_native_test(native_window_owner galaxy_window_owner_tests)
set_tests_properties(native_window_owner PROPERTIES TIMEOUT 20)
galaxy_add_native_test(compact_memory_snapshot galaxy_memory_snapshot_tests)
galaxy_add_native_test(sampler_key_packing galaxy_sampler_key_tests)
galaxy_add_native_test(xfb_present_selection galaxy_xfb_present_selection_tests)
galaxy_add_native_test(efb_content_key_identity galaxy_efb_conversion_tests --content-key-hash)
galaxy_add_native_test(efb_conversion_pipeline galaxy_efb_conversion_tests)
galaxy_add_native_test(efb_oversized_content_retirement galaxy_efb_conversion_tests --oversized-content-cache)
galaxy_add_native_test(line_point_raster galaxy_line_point_raster_tests)
galaxy_add_native_test(cosmetic_game_asset_extract galaxy_game_asset_extract_tests)
galaxy_add_native_test(sorted_dirty_ranges galaxy_sorted_dirty_ranges_tests)
galaxy_add_native_test(same_context_rfi galaxy_same_context_rfi_tests)
galaxy_add_native_test(pipeline_cache_persistence galaxy_pipeline_cache_persistence_tests)
galaxy_add_native_test(
    native_settings_overlay
    galaxy_native_settings_overlay_tests)
galaxy_add_native_test(shader_gen_compiles galaxy_shader_gen_tests)
galaxy_add_native_test(fifo_parser_streaming galaxy_fifo_parser_tests)
galaxy_add_native_test(vertex_loader_defaults galaxy_vertex_loader_tests)
galaxy_add_native_test(
    native_module_smoke
    galaxy_runtime_smoke
    "$<TARGET_FILE:galaxy_test_module>")
add_executable(galaxy_frame_telemetry_tests
    runtime/src/frame_telemetry_tests.cpp runtime/src/frame_telemetry.cpp)
target_include_directories(galaxy_frame_telemetry_tests PRIVATE runtime/include)
target_compile_features(galaxy_frame_telemetry_tests PRIVATE cxx_std_20)
add_test(NAME galaxy_frame_telemetry_tests COMMAND galaxy_frame_telemetry_tests)
