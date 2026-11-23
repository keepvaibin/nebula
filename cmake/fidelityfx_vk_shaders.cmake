# FidelityFX's Vulkan shader headers: made once from the SDK's own project by its shader compiler, which runs on Windows only. Where
# the build runs on Windows it makes them itself (cmake/fidelityfx_android.cmake); the Linux package's build takes the ones a
# Windows step of the same workflow run made (cmake/ffx-vk-shaders, .github/actions/ffx-vk-shaders), as the Android app's build
# script can (platform/android/build_apk.ps1 -FfxShaders). nebula-luma16.txt marks the output as made from the SDK with the
# rgba16f luma-history fix (cmake/fidelityfx_vulkan_fixes.cmake).
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/nebula_options.cmake")

# FSR 3 is not in this build: stops the configure when the build makes a package (NEBULA_REQUIRE_FSR3), else says so.
function(nebula_fsr3_left_out reason)
    if(NEBULA_REQUIRE_FSR3)
        message(FATAL_ERROR "FSR 3 is left out of this build and this build makes a package, which must hold it: ${reason}")
    endif()
    message(STATUS "FSR 3 left out: ${reason}")
endfunction()

# The SDK's source (pinned, checked by SHA-256): its `sdk` folder in `result`.
function(nebula_fidelityfx_sdk result)
    include(FetchContent)
    FetchContent_Declare(fidelityfx_sdk
        URL https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/archive/refs/tags/v1.1.4.tar.gz
        URL_HASH SHA256=25c46398a656150397597f78d44bf7cb445e9e177dd116b309bbfdf50d50cc9f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR "nebula-no-cmake")
    FetchContent_MakeAvailable(fidelityfx_sdk)
    set(${result} "${fidelityfx_sdk_SOURCE_DIR}/sdk" PARENT_SCOPE)
endfunction()

# Makes the headers where the SDK's compiler runs (Windows). `shaders` is the folder they land in.
function(nebula_fidelityfx_vk_shaders sdk headers names shaders)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/fidelityfx_vulkan_fixes.cmake")
    nebula_fidelityfx_vulkan_sdk("${sdk}" "${CMAKE_BINARY_DIR}/fidelityfx-sdk-vulkan-fixed" fixed_sdk)
    set(build "${CMAKE_BINARY_DIR}/fidelityfx-vk-luma16-build")
    set(made "${build}/src/backends/shaders/vk")
    if(NOT EXISTS "${made}/nebula-luma16.txt")
        message(STATUS "Compiling the FidelityFX SDK's Vulkan shaders")
        set(no_library "${CMAKE_BINARY_DIR}/fidelityfx-vk-no-library.lib")
        file(TOUCH "${no_library}")
        # The same configuration as the desktop's (cmake/fidelityfx.cmake): the SDK's libraries land in one shared folder.
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -S "${fixed_sdk}" -B "${build}" -G "Visual Studio 17 2022" -A x64
                    -DFFX_API_BACKEND=VK_X64 -DFFX_FSR3UPSCALER=ON -DFFX_AUTO_COMPILE_SHADERS=ON
                    "-DVulkan_INCLUDE_DIR=${headers}" "-DVulkan_LIBRARY=${no_library}" "-DCMAKE_CXX_FLAGS_INIT=/FI\"${names}\""
            RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE errors)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Configuring the FidelityFX SDK for Vulkan failed:\n${errors}")
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release --target ffx_backend_vk_x64
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Compiling the FidelityFX SDK's Vulkan shaders failed:\n${output}\n${errors}")
        endif()
        file(WRITE "${made}/nebula-luma16.txt" "rgba16f\n")
    endif()
    set(${shaders} "${made}" PARENT_SCOPE)
endfunction()
