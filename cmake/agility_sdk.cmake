# Microsoft's DirectX 12 Agility SDK 1.620.2 for the Direct3D 12 renderer: DXR 1.2 (opacity micromaps, 5.3 G-2), and
# the opt-in that lets the tracer's ray queries (Shader Model 6.6) use them. Downloaded at configure time (pinned,
# checked by SHA-256). Its headers come before the Windows SDK's for every target here; its runtime (D3D12Core.dll,
# d3d12SDKLayers.dll for the debug layer, and its license) goes into D3D12\ beside a program that asks for it
# (nebula_agility_runtime: native/host/d3d12_agility.cpp names the version and the folder). Without the folder Windows'
# own runtime runs, and what that one lacks stays off.

if(NOT WIN32)
    function(nebula_agility_runtime target)
    endfunction()
    return()
endif()

include(FetchContent)
FetchContent_Declare(agility_sdk
    URL https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/1.620.2/microsoft.direct3d.d3d12.1.620.2.nupkg
    URL_HASH SHA256=70489669c2bbeaf318b62d72b0ac3e753d521387bfc848c9763791c4e920bfcc
    DOWNLOAD_NAME agility_sdk.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR "nebula-no-cmake")
FetchContent_MakeAvailable(agility_sdk)

set(nebula_agility_bin "${agility_sdk_SOURCE_DIR}/build/native/bin/x64")
include_directories(BEFORE SYSTEM "${agility_sdk_SOURCE_DIR}/build/native/include")

# `target` asks Windows for this runtime, which is copied beside it after each link.
function(nebula_agility_runtime target)
    target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/native/host/d3d12_agility.cpp")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/D3D12"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${nebula_agility_bin}/D3D12Core.dll" "${nebula_agility_bin}/d3d12SDKLayers.dll"
                "${agility_sdk_SOURCE_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:${target}>/D3D12/"
        VERBATIM)
endfunction()
