cmake_minimum_required(VERSION 3.24)
project(nebula_linux_tools LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(nebula_root "${CMAKE_CURRENT_LIST_DIR}/../..")

set(NEBULA_DEPS "" CACHE PATH "Folder of the dependency checkouts")
set(NEBULA_VULKAN_HEADERS_DIR "${NEBULA_DEPS}/Vulkan-Headers-vulkan-sdk-1.4.350.1" CACHE PATH "")
set(NEBULA_VOLK_DIR "${NEBULA_DEPS}/volk-vulkan-sdk-1.4.350.1" CACHE PATH "")
set(NEBULA_VMA_DIR "${NEBULA_DEPS}/VulkanMemoryAllocator-v3.4.0" CACHE PATH "")
set(NEBULA_GLSLANG_SOURCE_DIR "${NEBULA_DEPS}/glslang-src" CACHE PATH "")

# Same glslang wiring as platform/sdl.
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT ON CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory("${NEBULA_GLSLANG_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/glslang" EXCLUDE_FROM_ALL)
file(GLOB nebula_glslang_spirv_headers "${NEBULA_GLSLANG_SOURCE_DIR}/SPIRV/*.h" "${NEBULA_GLSLANG_SOURCE_DIR}/SPIRV/*.hpp")
file(COPY ${nebula_glslang_spirv_headers} DESTINATION "${CMAKE_BINARY_DIR}/glslang-include/glslang/SPIRV")
add_library(nebula_shader_compiler INTERFACE)
target_include_directories(nebula_shader_compiler SYSTEM INTERFACE
    "${CMAKE_BINARY_DIR}/glslang-include" "${NEBULA_GLSLANG_SOURCE_DIR}" "${NEBULA_GLSLANG_SOURCE_DIR}/External/spirv-tools/include")
target_link_libraries(nebula_shader_compiler INTERFACE glslang SPIRV glslang-default-resource-limits SPIRV-Tools-opt SPIRV-Tools-static)

add_library(nebula_vulkan_deps INTERFACE)
target_include_directories(nebula_vulkan_deps INTERFACE
    "${NEBULA_VULKAN_HEADERS_DIR}/include" "${NEBULA_VOLK_DIR}" "${NEBULA_VMA_DIR}/include")
target_compile_definitions(nebula_vulkan_deps INTERFACE NEBULA_GPU_VULKAN VK_NO_PROTOTYPES)

# The tools, from the same sources the top-level Windows Vulkan build uses.
add_executable(galaxy_native_spirv
    "${nebula_root}/tools/dev/native_spirv.cpp"
    "${nebula_root}/runtime/src/render/vk/shader_compile_vk.cpp")
target_include_directories(galaxy_native_spirv PRIVATE
    "${nebula_root}/runtime/include" "${nebula_root}/runtime/src" "${nebula_root}/native/include")
target_compile_features(galaxy_native_spirv PRIVATE cxx_std_20)
target_link_libraries(galaxy_native_spirv PRIVATE nebula_vulkan_deps nebula_shader_compiler)

set(galaxy_post_embed_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/post_shader_embed.cpp")
include("${nebula_root}/cmake/post_shader_sources.cmake")
galaxy_embed_post_sources("${galaxy_post_embed_cpp}" "${nebula_root}")
add_executable(galaxy_utility_spirv
    "${nebula_root}/tools/dev/utility_spirv.cpp"
    "${nebula_root}/runtime/src/render/vk/shader_compile_vk.cpp"
    "${nebula_root}/runtime/src/render/renderer_common.cpp"
    "${galaxy_post_embed_cpp}")
target_include_directories(galaxy_utility_spirv PRIVATE
    "${nebula_root}/runtime/include" "${nebula_root}/runtime/src" "${nebula_root}/native/include")
target_compile_features(galaxy_utility_spirv PRIVATE cxx_std_20)
target_link_libraries(galaxy_utility_spirv PRIVATE nebula_vulkan_deps nebula_shader_compiler)
