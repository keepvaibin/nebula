# FSR 3.1 in the Vulkan renderer on Android and Linux: the SDK's FSR 3 upscaler and Vulkan backend sources built by the NDK or
# the Linux compiler over the SDK's Vulkan shaders. FidelityFX's shader compiler is a Windows tool: NEBULA_FFX_VK_SHADERS names its
# output when it is made already (Linux), otherwise it runs here through the SDK's own project, on Windows (Android). Defines
# `nebula_fidelityfx_vk`, or nothing where neither applies; a build that makes a package (NEBULA_REQUIRE_FSR3) stops instead.
include("${CMAKE_CURRENT_LIST_DIR}/fidelityfx_vk_shaders.cmake")

option(NEBULA_FSR3 "Build AMD FSR 3 (FidelityFX SDK 1.1.4) into the Vulkan renderer" ON)
set(NEBULA_FFX_VK_SHADERS "" CACHE PATH "FidelityFX's Vulkan shader headers, when made already")

function(nebula_fidelityfx_android headers portable names)
    if(NOT NEBULA_FSR3)
        nebula_fsr3_left_out("NEBULA_FSR3 is OFF")
        return()
    endif()
    nebula_fidelityfx_sdk(sdk)

    set(shaders "${NEBULA_FFX_VK_SHADERS}")
    if(shaders AND NOT EXISTS "${shaders}/nebula-luma16.txt")
        message(FATAL_ERROR "FidelityFX Vulkan shaders need the rgba16f luma-history fix. Use the desktop fidelityfx-vk-luma16-build shader output or clear NEBULA_FFX_VK_SHADERS to regenerate.")
    endif()
    if(NOT shaders)
        if(NOT CMAKE_HOST_WIN32)
            nebula_fsr3_left_out("its shader compiler runs on Windows: set NEBULA_FFX_VK_SHADERS to the shader headers a Windows build made (cmake/ffx-vk-shaders)")
            return()
        endif()
        nebula_fidelityfx_vk_shaders("${sdk}" "${headers}" "${names}" shaders)
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
