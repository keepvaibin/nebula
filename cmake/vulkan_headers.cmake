# Vulkan-Headers alone, at the pin cmake/vulkan_deps.cmake builds the Vulkan renderer with: every Windows build's
# renderer check (runtime/src/render/renderer_check_win32.cpp) reads them, whether it renders with Vulkan or not.
# Sets vulkan_headers_SOURCE_DIR and defines `nebula_vulkan_headers` (the include folder, nothing else).

include(FetchContent)

FetchContent_Declare(vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.350.1.tar.gz
    URL_HASH SHA256=6d1bb65e49520344cc0a48af3dc02e993781efff14c7ebdcb8ae9fa23ddf7e83
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR "nebula-no-cmake")
FetchContent_MakeAvailable(vulkan_headers)

if(NOT TARGET nebula_vulkan_headers)
    add_library(nebula_vulkan_headers INTERFACE)
    target_include_directories(nebula_vulkan_headers SYSTEM INTERFACE "${vulkan_headers_SOURCE_DIR}/include")
endif()
