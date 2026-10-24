# The Vulkan renderer's dependencies, downloaded at configure time from pinned archives (checked by SHA-256):
# Vulkan-Headers, volk and VulkanMemoryAllocator (headers), and glslang with SPIRV-Tools built from source.
# Defines `nebula_vulkan_deps`. Used by the Windows build (NEBULA_VULKAN) and platform/sdl.

include(FetchContent)

# A pinned archive, unpacked without adding its own CMake project; sets <name>_SOURCE_DIR here.
macro(nebula_fetch_sources name url sha256)
    FetchContent_Declare(${name} URL "${url}" URL_HASH SHA256=${sha256} DOWNLOAD_EXTRACT_TIMESTAMP TRUE
                         SOURCE_SUBDIR "nebula-no-cmake")
    FetchContent_MakeAvailable(${name})
endmacro()

nebula_fetch_sources(vulkan_headers
    https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.350.1.tar.gz
    6d1bb65e49520344cc0a48af3dc02e993781efff14c7ebdcb8ae9fa23ddf7e83)
nebula_fetch_sources(volk
    https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.350.1.tar.gz
    078a9411298e4e0f60f5f5398c890783427c25a414619294ca8e69587bbd5eae)
nebula_fetch_sources(vma
    https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.4.0.tar.gz
    822aa850c6ce77346ae96a8a1d351d52e77e85929f35363849a0a4e638e0a2a1)

# glslang 16.6.0 with the SPIRV-Tools and SPIRV-Headers commits its known_good.json names, placed where its build
# looks for them (External/spirv-tools, External/spirv-tools/external/spirv-headers).
nebula_fetch_sources(glslang
    https://github.com/KhronosGroup/glslang/archive/refs/tags/16.6.0.tar.gz
    9c09b901149c729df745057dafa815278aaa101b84d2b6e14f16a42de52f97f2)
FetchContent_Declare(spirv_tools
    URL https://github.com/KhronosGroup/SPIRV-Tools/archive/ef96ed763b43b59b33b31b362f09a02b729fa1c9.tar.gz
    URL_HASH SHA256=82c62146083fd558735a3171cf97cfc47903ca7d368482e87f94bd44883c0f00
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR "nebula-no-cmake"
    SOURCE_DIR "${glslang_SOURCE_DIR}/External/spirv-tools")
FetchContent_MakeAvailable(spirv_tools)
FetchContent_Declare(spirv_headers
    URL https://github.com/KhronosGroup/SPIRV-Headers/archive/496543121ce6419f23d6fa5d7194ba66c36212d2.tar.gz
    URL_HASH SHA256=a9bb9c48713245eacf97cc539b6f1d45405a92d8813f8b82e635f0a085ee9898
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR "nebula-no-cmake"
    SOURCE_DIR "${glslang_SOURCE_DIR}/External/spirv-tools/external/spirv-headers")
FetchContent_MakeAvailable(spirv_headers)

# glslang with its HLSL front end and the SPIR-V optimizer; no tools, tests or install rules.
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(ENABLE_OPT ON CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
if(NOT EXISTS "${glslang_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "glslang's sources are missing (${glslang_SOURCE_DIR})")
endif()
add_subdirectory("${glslang_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/glslang" EXCLUDE_FROM_ALL)
# The renderer includes glslang's SPIR-V headers in their installed layout (glslang/SPIRV/...).
file(GLOB nebula_glslang_spirv_headers "${glslang_SOURCE_DIR}/SPIRV/*.h" "${glslang_SOURCE_DIR}/SPIRV/*.hpp")
file(COPY ${nebula_glslang_spirv_headers} DESTINATION "${CMAKE_BINARY_DIR}/glslang-include/glslang/SPIRV")

add_library(nebula_vulkan_deps INTERFACE)
target_include_directories(nebula_vulkan_deps SYSTEM INTERFACE
    "${vulkan_headers_SOURCE_DIR}/include" "${volk_SOURCE_DIR}" "${vma_SOURCE_DIR}/include"
    "${CMAKE_BINARY_DIR}/glslang-include" "${glslang_SOURCE_DIR}" "${glslang_SOURCE_DIR}/External/spirv-tools/include")
target_compile_definitions(nebula_vulkan_deps INTERFACE NEBULA_GPU_VULKAN VK_NO_PROTOTYPES
    $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR NOMINMAX>
    $<$<PLATFORM_ID:Android>:VK_USE_PLATFORM_ANDROID_KHR>)
target_link_libraries(nebula_vulkan_deps INTERFACE glslang SPIRV glslang-default-resource-limits SPIRV-Tools-opt
    SPIRV-Tools-static)
