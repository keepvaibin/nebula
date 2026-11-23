# SPDX-License-Identifier: GPL-3.0-only
# Makes FidelityFX's Vulkan shader headers on a Windows machine and copies them to one folder, for a build that cannot make them
# (the Linux package's: the SDK's shader compiler runs on Windows only). It is the same maker the Android build runs through
# cmake/fidelityfx_android.cmake (cmake/fidelityfx_vk_shaders.cmake), so the headers are the same ones.
#
#   cmake -S cmake/ffx-vk-shaders -B <build> -DNEBULA_VULKAN_HEADERS=<Vulkan-Headers' include folder> -DNEBULA_FFX_OUT=<folder>
#
# The folder then holds the headers and nebula-luma16.txt; a build names it as NEBULA_FFX_VK_SHADERS. Run by the workflows'
# step .github/actions/ffx-vk-shaders.
cmake_minimum_required(VERSION 3.25)
project(nebula_ffx_vk_shaders LANGUAGES NONE)

set(nebula_root "${CMAKE_CURRENT_LIST_DIR}/../..")
include("${nebula_root}/cmake/fidelityfx_vk_shaders.cmake")

set(NEBULA_VULKAN_HEADERS "" CACHE PATH "The Vulkan headers' include folder")
set(NEBULA_FFX_OUT "" CACHE PATH "Where the shader headers are copied")
if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "The FidelityFX SDK's shader compiler runs on Windows only")
endif()
if(NOT EXISTS "${NEBULA_VULKAN_HEADERS}/vulkan/vulkan.h" OR NOT NEBULA_FFX_OUT)
    message(FATAL_ERROR "Set NEBULA_VULKAN_HEADERS (the folder holding vulkan/vulkan.h) and NEBULA_FFX_OUT")
endif()

nebula_fidelityfx_sdk(sdk)
nebula_fidelityfx_vk_shaders("${sdk}" "${NEBULA_VULKAN_HEADERS}" "${nebula_root}/runtime/src/render/vk/ffx_vk_names.h" shaders)
file(GLOB made "${shaders}/*.h" "${shaders}/nebula-luma16.txt")
list(LENGTH made count)
if(count LESS 100)
    message(FATAL_ERROR "The FidelityFX shader compiler made only ${count} files in ${shaders}")
endif()
file(MAKE_DIRECTORY "${NEBULA_FFX_OUT}")
file(COPY ${made} DESTINATION "${NEBULA_FFX_OUT}")
message(STATUS "FidelityFX Vulkan shaders: ${count} files in ${NEBULA_FFX_OUT}")
