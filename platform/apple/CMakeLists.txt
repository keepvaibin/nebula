# SPDX-License-Identifier: GPL-3.0-only
# Nebula on Apple's systems (docs/APPLE.md): macOS on Intel and Apple silicon, iOS and iPadOS. The host is the SDL
# host (platform/sdl), whose build this one wraps: the same sources, with the shader tools built here for the build
# machine, Apple's own pieces added (this folder), and the program packed as an app.
#
#   cmake -G Ninja -S platform/apple -B <build> -DCMAKE_TOOLCHAIN_FILE=platform/apple/toolchain.cmake
#         -DNEBULA_APPLE_LLVM=... -DNEBULA_APPLE_LIBCXX=... -DNEBULA_APPLE_PLATFORM=macos -DNEBULA_APPLE_ARCH=x86_64
#         -DNEBULA_DEPS=<dependency checkouts>
#
# NEBULA_DEPS holds what platform/sdl names (SDL3, glslang-src, Vulkan-Headers, volk, VulkanMemoryAllocator) and
# MoltenVK (the release's MoltenVK folder: MoltenVK/dylib, MoltenVK/static).
cmake_minimum_required(VERSION 3.24)
project(NebulaApple LANGUAGES C CXX OBJC OBJCXX)

get_filename_component(nebula_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(NEBULA_DEPS "" CACHE PATH "Folder of the dependency checkouts")
set(NEBULA_MOLTENVK_DIR "${NEBULA_DEPS}/MoltenVK/MoltenVK/MoltenVK" CACHE PATH "MoltenVK's release folder (dylib, static, include)")

# The shader tools run on the build machine. A build for the machine's own system and processor makes them itself
# (below, after the SDL host's build has defined the shader compiler); any other takes them from such a build's
# host-tools folder.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_SYSTEM_PROCESSOR STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
    set(nebula_apple_builds_tools ON)
    set(NEBULA_HOST_TOOLS "${CMAKE_BINARY_DIR}/host-tools" CACHE PATH "" FORCE)
    file(MAKE_DIRECTORY "${NEBULA_HOST_TOOLS}")
else()
    set(nebula_apple_builds_tools OFF)
    set(NEBULA_HOST_TOOLS "" CACHE PATH "The host-tools folder of a build for this machine (galaxy_native_spirv, galaxy_utility_spirv)")
    if(NOT EXISTS "${NEBULA_HOST_TOOLS}/galaxy_native_spirv")
        message(FATAL_ERROR "This build is for another system or processor than the build machine's: set NEBULA_HOST_TOOLS to the host-tools folder of a macOS build made on this machine")
    endif()
endif()

# FSR 3's shaders come from FidelityFX's compiler, a Windows program (cmake/fidelityfx_android.cmake).
if(NOT NEBULA_FFX_VK_SHADERS)
    set(NEBULA_FSR3 OFF CACHE BOOL "" FORCE)
endif()

# SDL is part of the app: a static library, so an iPhone app has no loose dylib and a Mac app one file less.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)

add_subdirectory("${nebula_root}/platform/sdl" sdl)

if(nebula_apple_builds_tools)
    # As the Windows build makes them (CMakeLists.txt, NEBULA_VULKAN): the renderer's HLSL front end run at build.
    include("${nebula_root}/cmake/post_shader_sources.cmake")
    set(nebula_apple_post_embed "${CMAKE_CURRENT_BINARY_DIR}/generated/post_shader_embed.cpp")
    galaxy_embed_post_sources("${nebula_apple_post_embed}" "${nebula_root}")
    add_executable(nebula_apple_native_spirv
        "${nebula_root}/tools/dev/native_spirv.cpp"
        "${nebula_root}/runtime/src/render/vk/shader_compile_vk.cpp")
    add_executable(nebula_apple_utility_spirv
        "${nebula_root}/tools/dev/utility_spirv.cpp"
        "${nebula_root}/runtime/src/render/vk/shader_compile_vk.cpp"
        "${nebula_root}/runtime/src/render/renderer_common.cpp"
        "${nebula_apple_post_embed}")
    foreach(tool native_spirv utility_spirv)
        target_include_directories(nebula_apple_${tool} PRIVATE "${nebula_root}/runtime/include" "${nebula_root}/native/include")
        target_compile_features(nebula_apple_${tool} PRIVATE cxx_std_20)
        target_link_libraries(nebula_apple_${tool} PRIVATE nebula_vulkan_deps nebula_shader_compiler)
        # Under the names and in the folder the SDL host's build imports them from.
        set_target_properties(nebula_apple_${tool} PROPERTIES
            OUTPUT_NAME galaxy_${tool} RUNTIME_OUTPUT_DIRECTORY "${NEBULA_HOST_TOOLS}")
    endforeach()
endif()

# MoltenVK: Vulkan over Metal. A Mac app carries its dylib in Contents/Frameworks; an iPhone app links it in.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(nebula_moltenvk "${NEBULA_MOLTENVK_DIR}/dynamic/dylib/macOS/libMoltenVK.dylib")
    if(NOT EXISTS "${nebula_moltenvk}")
        message(FATAL_ERROR "MoltenVK's dylib is not at ${nebula_moltenvk}: set NEBULA_MOLTENVK_DIR")
    endif()
endif()

set(nebula_apple_host_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/apple_host.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/display_clock_apple.mm")
target_sources(main PRIVATE ${nebula_apple_host_sources})
target_compile_definitions(main PRIVATE NEBULA_HOST_APPLE=1)
target_include_directories(main PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Objective-C++ with automatic reference counting (the property is read where the target was made).
set_source_files_properties(${nebula_apple_host_sources} TARGET_DIRECTORY main PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(main PRIVATE "-framework Foundation" "-framework QuartzCore" "-framework Metal" "-framework CoreVideo")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(main PRIVATE "-framework AppKit" "-framework CoreAudio")
    # Beside the program until it is packed as an app (build_macos.sh), where it lies in Contents/Frameworks.
    set_target_properties(main PROPERTIES
        BUILD_RPATH "@executable_path;@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/sdl/libMoltenVK.dylib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${nebula_moltenvk}" "${CMAKE_BINARY_DIR}/sdl/libMoltenVK.dylib"
        DEPENDS "${nebula_moltenvk}" VERBATIM)
    add_custom_target(nebula_apple_moltenvk DEPENDS "${CMAKE_BINARY_DIR}/sdl/libMoltenVK.dylib")
    add_dependencies(main nebula_apple_moltenvk)
else()
    target_link_libraries(main PRIVATE "-framework UIKit")
endif()

# The Metal renderer's shaders (docs/APPLE.md, "Shaders"): the programs' SPIR-V turned to MSL by SPIRV-Cross at build.
set(NEBULA_SPIRV_CROSS_DIR "${NEBULA_DEPS}/SPIRV-Cross-vulkan-sdk-1.4.350.1" CACHE PATH "SPIRV-Cross")
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${NEBULA_SPIRV_CROSS_DIR}" spirv-cross EXCLUDE_FROM_ALL)
add_library(nebula_msl STATIC "${nebula_root}/runtime/src/render/mtl/shader_msl.cpp")
target_compile_features(nebula_msl PUBLIC cxx_std_20)
target_include_directories(nebula_msl PUBLIC "${nebula_root}/runtime/include")
target_include_directories(nebula_msl SYSTEM PRIVATE "${NEBULA_SPIRV_CROSS_DIR}")
target_link_libraries(nebula_msl PRIVATE spirv-cross-msl)

if(nebula_apple_builds_tools)
    add_executable(nebula_apple_programs_msl "${CMAKE_CURRENT_SOURCE_DIR}/tools/programs_msl.cpp")
    target_link_libraries(nebula_apple_programs_msl PRIVATE nebula_msl)
    set_target_properties(nebula_apple_programs_msl PROPERTIES
        OUTPUT_NAME programs_msl RUNTIME_OUTPUT_DIRECTORY "${NEBULA_HOST_TOOLS}")
    # Every stage through Metal's own compiler, on this machine's GPUs.
    add_executable(nebula_apple_msl_check "${CMAKE_CURRENT_SOURCE_DIR}/tools/msl_check.mm")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/tools/msl_check.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_features(nebula_apple_msl_check PRIVATE cxx_std_20)
    target_link_libraries(nebula_apple_msl_check PRIVATE "-framework Foundation" "-framework Metal" "-framework CoreGraphics")
    set_target_properties(nebula_apple_msl_check PROPERTIES
        OUTPUT_NAME msl_check RUNTIME_OUTPUT_DIRECTORY "${NEBULA_HOST_TOOLS}")
    set(nebula_apple_programs_msl_tool "$<TARGET_FILE:nebula_apple_programs_msl>")
    set(nebula_apple_programs_msl_dependency nebula_apple_programs_msl)
else()
    set(nebula_apple_programs_msl_tool "${NEBULA_HOST_TOOLS}/programs_msl")
    set(nebula_apple_programs_msl_dependency "${NEBULA_HOST_TOOLS}/programs_msl")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(nebula_apple_msl_platform macos)
else()
    set(nebula_apple_msl_platform ios)
endif()
set(nebula_apple_programs_spirv "${CMAKE_BINARY_DIR}/sdl/generated/programs_spirv.bin")
set(nebula_apple_programs_msl "${CMAKE_BINARY_DIR}/generated/programs_msl.bin")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${nebula_apple_programs_msl}"
    COMMAND "${nebula_apple_programs_msl_tool}" "${nebula_apple_programs_spirv}" "${nebula_apple_programs_msl}"
            ${nebula_apple_msl_platform}
    DEPENDS "${nebula_apple_programs_spirv}" ${nebula_apple_programs_msl_dependency} VERBATIM)

# With Apple's Metal compiler on the build machine (it comes with Xcode; the command line tools alone do not have it) the
# programs are compiled here, into the app's two Metal libraries, and the programs' file names each one's function
# (tools/compile_metallib.py): no player's machine then compiles a program's text. Without the compiler the app carries
# the texts, and Metal compiles each the first time it is asked for and keeps it in the system's cache.
if(NEBULA_APPLE_PLATFORM STREQUAL "ios")
    set(nebula_apple_metal_sdk iphoneos)
    set(nebula_apple_metal_min_os 16.0)
elseif(NEBULA_APPLE_PLATFORM STREQUAL "ios-simulator")
    set(nebula_apple_metal_sdk iphonesimulator)
    set(nebula_apple_metal_min_os 16.0)
else()
    set(nebula_apple_metal_sdk macosx)
    set(nebula_apple_metal_min_os 12.0)
endif()
execute_process(COMMAND xcrun -sdk ${nebula_apple_metal_sdk} -f metal
    RESULT_VARIABLE nebula_apple_no_metal_compiler OUTPUT_QUIET ERROR_QUIET)
set(nebula_apple_program_libraries)
if(nebula_apple_no_metal_compiler)
    message(STATUS "Nebula: no Metal compiler on this machine: the app carries its programs as text")
    set(nebula_apple_programs_blob "${nebula_apple_programs_msl}")
else()
    message(STATUS "Nebula: programs compiled into Metal libraries with Apple's compiler")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(nebula_apple_programs_blob "${CMAKE_BINARY_DIR}/generated/programs_metal.bin")
    set(nebula_apple_program_libraries
        "${CMAKE_BINARY_DIR}/generated/programs_2_4.metallib" "${CMAKE_BINARY_DIR}/generated/programs_3_0.metallib")
    add_custom_command(OUTPUT "${nebula_apple_programs_blob}" ${nebula_apple_program_libraries}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_metallib.py"
                --programs "${nebula_apple_programs_msl}" --out-blob "${nebula_apple_programs_blob}"
                --out-dir "${CMAKE_BINARY_DIR}/generated" --work "${CMAKE_BINARY_DIR}/generated/metallib"
                --sdk ${nebula_apple_metal_sdk} --min-os ${nebula_apple_metal_min_os}
        DEPENDS "${nebula_apple_programs_msl}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_metallib.py"
        USES_TERMINAL VERBATIM)
endif()
add_custom_target(nebula_apple_programs_msl_file DEPENDS "${nebula_apple_programs_blob}")

include("${CMAKE_CURRENT_SOURCE_DIR}/metal.cmake")
