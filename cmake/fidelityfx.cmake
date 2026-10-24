# AMD FidelityFX FSR 3.1's upscaler (MIT) for the D3D12 renderer: FidelityFX SDK 1.1.4 downloaded at configure time
# (pinned, checked by SHA-256) and its FSR 3 upscaler and DX12 backend libraries built once per build tree with the
# SDK's own build and shader compiler. Defines `nebula_fidelityfx` (empty when NEBULA_FSR3 is off).

option(NEBULA_FSR3 "Build AMD FSR 3 (FidelityFX SDK 1.1.4) into the D3D12 renderer" ON)
add_library(nebula_fidelityfx INTERFACE)
if(NOT NEBULA_FSR3)
    return()
endif()

include(FetchContent)
FetchContent_Declare(fidelityfx_sdk
    URL https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/archive/refs/tags/v1.1.4.tar.gz
    URL_HASH SHA256=25c46398a656150397597f78d44bf7cb445e9e177dd116b309bbfdf50d50cc9f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR "nebula-no-cmake")
FetchContent_MakeAvailable(fidelityfx_sdk)

set(nebula_ffx_sdk "${fidelityfx_sdk_SOURCE_DIR}/sdk")
set(nebula_ffx_libs "${nebula_ffx_sdk}/bin/ffx_sdk")
set(nebula_ffx_stamp "${CMAKE_BINARY_DIR}/fidelityfx.built")
if(NOT EXISTS "${nebula_ffx_stamp}")
    message(STATUS "Building the FidelityFX SDK's FSR 3 upscaler")
    set(build "${CMAKE_BINARY_DIR}/fidelityfx-build")
    # The SDK's projects are Visual Studio ones (its shader permutations are custom build steps).
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${nebula_ffx_sdk}" -B "${build}" -G "Visual Studio 17 2022" -A x64
                -DFFX_API_BACKEND=DX12_X64 -DFFX_FSR3UPSCALER=ON -DFFX_AUTO_COMPILE_SHADERS=ON
        RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Configuring the FidelityFX SDK failed:\n${errors}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release
                --target ffx_fsr3upscaler_x64 ffx_backend_dx12_x64
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Building the FidelityFX SDK failed:\n${output}\n${errors}")
    endif()
    file(WRITE "${nebula_ffx_stamp}" "${nebula_ffx_sdk}\n")
endif()

target_include_directories(nebula_fidelityfx SYSTEM INTERFACE "${nebula_ffx_sdk}/include")
target_link_libraries(nebula_fidelityfx INTERFACE
    "${nebula_ffx_libs}/ffx_fsr3upscaler_x64.lib" "${nebula_ffx_libs}/ffx_backend_dx12_x64.lib")
target_compile_definitions(nebula_fidelityfx INTERFACE GALAXY_WITH_FSR3=1)
