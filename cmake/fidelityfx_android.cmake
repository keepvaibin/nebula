# FSR 3.1 in the Vulkan renderer on Android: the SDK's FSR 3 upscaler and Vulkan backend sources built by the NDK over the SDK's
# Vulkan shaders. FidelityFX's shader compiler is a Windows tool: NEBULA_FFX_VK_SHADERS names its output when it is made already,
# otherwise it runs here through the SDK's own project, on Windows. Defines `nebula_fidelityfx_vk`, or nothing where neither applies.

option(NEBULA_FSR3 "Build AMD FSR 3 (FidelityFX SDK 1.1.4) into the Vulkan renderer" ON)
set(NEBULA_FFX_VK_SHADERS "" CACHE PATH "FidelityFX's Vulkan shader headers, when made already")

function(nebula_fidelityfx_android headers portable names)
    if(NOT NEBULA_FSR3)
        return()
    endif()
    include(FetchContent)
    FetchContent_Declare(fidelityfx_sdk
        URL https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/archive/refs/tags/v1.1.4.tar.gz
        URL_HASH SHA256=25c46398a656150397597f78d44bf7cb445e9e177dd116b309bbfdf50d50cc9f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR "nebula-no-cmake")
    FetchContent_MakeAvailable(fidelityfx_sdk)
    set(sdk "${fidelityfx_sdk_SOURCE_DIR}/sdk")

    set(shaders "${NEBULA_FFX_VK_SHADERS}")
    if(shaders AND NOT EXISTS "${shaders}/nebula-luma16.txt")
        message(FATAL_ERROR "FidelityFX Vulkan shaders need the rgba16f luma-history fix. Use the desktop fidelityfx-vk-luma16-build shader output or clear NEBULA_FFX_VK_SHADERS to regenerate.")
    endif()
    if(NOT shaders)
        if(NOT CMAKE_HOST_WIN32)
            message(STATUS "FSR 3 left out: its shader compiler runs on Windows (or set NEBULA_FFX_VK_SHADERS)")
            return()
        endif()
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/fidelityfx_vulkan_fixes.cmake")
        nebula_fidelityfx_vulkan_sdk("${sdk}" "${CMAKE_BINARY_DIR}/fidelityfx-sdk-vulkan-fixed" fixed_sdk)
        set(build "${CMAKE_BINARY_DIR}/fidelityfx-vk-luma16-build")
        set(shaders "${build}/src/backends/shaders/vk")
        if(NOT EXISTS "${shaders}/nebula-luma16.txt")
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
            file(WRITE "${shaders}/nebula-luma16.txt" "rgba16f\n")
        endif()
    endif()

    # The backend promises alignas(32) for its effect contexts, then places them at any offset of its scratch memory.
    # Only MSVC's code gets away with that; nothing in them needs it.
    file(READ "${sdk}/src/backends/vk/ffx_vk.cpp" backend_source)
    string(REPLACE "typedef struct alignas(32) EffectContext" "typedef struct EffectContext" backend_source "${backend_source}")
    set(backend_copy "${CMAKE_CURRENT_BINARY_DIR}/ffx_vk_unaligned.cpp")
    file(WRITE "${backend_copy}.new" "${backend_source}")
    configure_file("${backend_copy}.new" "${backend_copy}" COPYONLY)
    add_library(nebula_fidelityfx_vk STATIC
        "${sdk}/src/shared/ffx_assert.cpp"
        "${sdk}/src/shared/ffx_message.cpp"
        "${sdk}/src/shared/ffx_object_management.cpp"
        "${sdk}/src/shared/ffx_breadcrumbs_list.cpp"
        "${sdk}/src/backends/shared/ffx_shader_blobs.cpp"
        "${sdk}/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp"
        "${sdk}/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp"
        "${backend_copy}"
        "${CMAKE_SOURCE_DIR}/../../runtime/src/render/vk/ffx_no_swapchain.cpp")
    target_include_directories(nebula_fidelityfx_vk SYSTEM PUBLIC "${sdk}/include")
    target_include_directories(nebula_fidelityfx_vk SYSTEM PRIVATE
        "${sdk}/src/shared" "${sdk}/src/backends/shared" "${sdk}/src/backends/shared/blob_accessors"
        "${sdk}/src/components" "${sdk}/src" "${shaders}" "${headers}")
    target_compile_definitions(nebula_fidelityfx_vk PRIVATE FFX_FSR3UPSCALER)
    target_compile_definitions(nebula_fidelityfx_vk PUBLIC GALAXY_WITH_FSR3_VK=1 GALAXY_WITH_FSR3=1)
    target_compile_options(nebula_fidelityfx_vk PRIVATE -w "SHELL:-include \"${names}\"" "SHELL:-include \"${portable}\"")
endfunction()
