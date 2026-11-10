# SPDX-License-Identifier: GPL-3.0-only
# The tracer's shaders (runtime/src/render/rt, tools/dev/compile_rt_shaders.py), compiled at build into one header per
# back end: the player's machine compiles nothing. galaxy_rt_shaders(OUT_VAR BACKEND DXC) adds the rule and sets OUT_VAR
# to the header; galaxy_rt_shader_dir is an include directory of whatever compiles runtime/src/render/rt/rt_shaders.cpp.
#
# Direct3D 12 takes DXIL from the build's dxc, as the material programs do. Vulkan takes SPIR-V from a dxc built with
# SPIR-V, which the Windows SDK's is not: galaxy_find_dxc_spirv(OUT_VAR) gives GALAXY_DXC_SPIRV when it is set, else the
# DirectX Shader Compiler's own release, downloaded at configure time from a pinned archive (checked by SHA-256, as
# cmake/vulkan_deps.cmake's are) and tried once. It is a build tool only: nothing of it is in what players get. Where
# there is none for the build machine, OUT_VAR is empty, and a Vulkan build holds no tracer shaders and reports ray
# tracing as not available (rt_caps.h).

include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(galaxy_rt_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(galaxy_rt_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/rt_shaders")
set(GALAXY_DXC_SPIRV "" CACHE FILEPATH "A dxc that writes SPIR-V, for the Vulkan tracer's shaders; empty: the pinned release")

function(galaxy_find_dxc_spirv out_var)
    if(GALAXY_DXC_SPIRV)
        set(${out_var} "${GALAXY_DXC_SPIRV}" PARENT_SCOPE)
        return()
    endif()
    set(tool "")
    # DirectX Shader Compiler 1.9.2609 (University of Illinois/NCSA licence).
    if(CMAKE_HOST_WIN32)
        FetchContent_Declare(nebula_dxc
            URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip"
            URL_HASH SHA256=ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR "nebula-no-cmake")
        FetchContent_MakeAvailable(nebula_dxc)
        if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(tool "${nebula_dxc_SOURCE_DIR}/bin/arm64/dxc.exe")
        else()
            set(tool "${nebula_dxc_SOURCE_DIR}/bin/x64/dxc.exe")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        FetchContent_Declare(nebula_dxc
            URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/linux_dxc_2026_09_28.x86_x64.tar.gz"
            URL_HASH SHA256=96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR "nebula-no-cmake")
        FetchContent_MakeAvailable(nebula_dxc)
        set(tool "${nebula_dxc_SOURCE_DIR}/bin/dxc")
    endif()
    # One that does not run here (a missing library, another system) is none.
    if(tool AND EXISTS "${tool}")
        execute_process(COMMAND "${tool}" --version RESULT_VARIABLE ran OUTPUT_QUIET ERROR_QUIET)
        if(NOT ran EQUAL 0)
            set(tool "")
        endif()
    else()
        set(tool "")
    endif()
    if(NOT tool)
        message(STATUS "No dxc with SPIR-V for this build machine: the Vulkan build holds no ray tracing shaders (set GALAXY_DXC_SPIRV)")
    endif()
    set(${out_var} "${tool}" PARENT_SCOPE)
endfunction()

function(galaxy_rt_shaders out_var backend dxc)
    file(MAKE_DIRECTORY "${galaxy_rt_shader_dir}")
    set(header "${galaxy_rt_shader_dir}/rt_shaders_${backend}.h")
    file(GLOB sources CONFIGURE_DEPENDS
        "${galaxy_rt_root}/runtime/src/render/rt/*.hlsl"
        "${galaxy_rt_root}/runtime/src/render/rt/*.hlsli")
    add_custom_command(OUTPUT "${header}"
        COMMAND "${Python3_EXECUTABLE}" "${galaxy_rt_root}/tools/dev/compile_rt_shaders.py" ${backend} "${dxc}" "${header}"
        DEPENDS ${sources} "${galaxy_rt_root}/runtime/src/render/rt/shaders.tsv"
            "${galaxy_rt_root}/tools/dev/compile_rt_shaders.py"
        VERBATIM)
    set(${out_var} "${header}" PARENT_SCOPE)
endfunction()
