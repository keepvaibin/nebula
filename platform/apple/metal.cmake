# SPDX-License-Identifier: GPL-3.0-only
# The Metal renderer: the renderer's own sources, as platform/sdl lists them for Vulkan, with
# runtime/src/render/mtl in place of the Vulkan backend. Included by CMakeLists.txt after the SDL host's build.

# The renderer's shaders that are not materials, as MSL (tools/utility_msl.cpp over the SPIR-V the SDL build makes).
set(nebula_apple_utility_spirv_dir "${CMAKE_BINARY_DIR}/sdl/generated/utility_spirv")
set(nebula_apple_utility_msl_dir "${CMAKE_BINARY_DIR}/generated/utility_msl")
file(MAKE_DIRECTORY "${nebula_apple_utility_msl_dir}")
if(nebula_apple_builds_tools)
    add_executable(nebula_apple_utility_msl
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/utility_msl.cpp"
        "${nebula_root}/runtime/src/render/vk/utility_spirv.cpp"
        "${nebula_apple_utility_spirv_dir}/utility_spirv_data.h")
    target_include_directories(nebula_apple_utility_msl PRIVATE "${nebula_apple_utility_spirv_dir}")
    target_link_libraries(nebula_apple_utility_msl PRIVATE nebula_msl)
    add_dependencies(nebula_apple_utility_msl galaxy_utility_spirv_header)  # made in platform/sdl
    set_target_properties(nebula_apple_utility_msl PROPERTIES
        OUTPUT_NAME utility_msl RUNTIME_OUTPUT_DIRECTORY "${NEBULA_HOST_TOOLS}")
    set(nebula_apple_utility_msl_tool "$<TARGET_FILE:nebula_apple_utility_msl>")
    set(nebula_apple_utility_msl_dependency nebula_apple_utility_msl)
else()
    set(nebula_apple_utility_msl_tool "${NEBULA_HOST_TOOLS}/utility_msl")
    set(nebula_apple_utility_msl_dependency "${NEBULA_HOST_TOOLS}/utility_msl")
endif()
set(nebula_apple_utility_texts "${nebula_apple_utility_msl_dir}/utility_msl.bin")
add_custom_command(OUTPUT "${nebula_apple_utility_msl_dir}/utility_msl_data.h" "${nebula_apple_utility_texts}"
    COMMAND "${nebula_apple_utility_msl_tool}" "${nebula_apple_utility_msl_dir}/utility_msl_data.h"
            "${nebula_apple_utility_texts}" ${nebula_apple_msl_platform}
    DEPENDS ${nebula_apple_utility_msl_dependency} VERBATIM)
# The texts the tool filed are the texts the runtime makes, each under the name it asks utility.metallib for
# (tools/utility_msl_check.cpp, over runtime/src/render/mtl/utility_msl.cpp): run at every build that makes the tools.
set(nebula_apple_utility_check_stamp)
if(nebula_apple_builds_tools)
    add_executable(nebula_apple_utility_msl_check
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/utility_msl_check.cpp"
        "${nebula_root}/runtime/src/render/mtl/utility_msl.cpp"
        "${nebula_apple_utility_msl_dir}/utility_msl_data.h")
    target_compile_features(nebula_apple_utility_msl_check PRIVATE cxx_std_20)
    target_include_directories(nebula_apple_utility_msl_check PRIVATE "${nebula_root}/runtime/include" "${nebula_apple_utility_msl_dir}")
    set_target_properties(nebula_apple_utility_msl_check PROPERTIES
        OUTPUT_NAME utility_msl_check RUNTIME_OUTPUT_DIRECTORY "${NEBULA_HOST_TOOLS}")
    set(nebula_apple_utility_check_stamp "${nebula_apple_utility_msl_dir}/utility_msl_check.stamp")
    add_custom_command(OUTPUT "${nebula_apple_utility_check_stamp}"
        COMMAND "$<TARGET_FILE:nebula_apple_utility_msl_check>" "${nebula_apple_utility_texts}" "${nebula_apple_utility_check_stamp}"
        DEPENDS nebula_apple_utility_msl_check "${nebula_apple_utility_texts}" VERBATIM)
endif()
# With Apple's Metal compiler (CMakeLists.txt) the texts are compiled here into the app's utility.metallib
# (tools/compile_metallib.py --utility): the renderer takes each function from it, and compiles a text only in a build
# without the compiler.
set(nebula_apple_utility_library)
if(NOT nebula_apple_no_metal_compiler)
    set(nebula_apple_utility_library "${CMAKE_BINARY_DIR}/generated/utility.metallib")
    add_custom_command(OUTPUT "${nebula_apple_utility_library}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_metallib.py"
                --utility "${nebula_apple_utility_texts}"
                --out-dir "${CMAKE_BINARY_DIR}/generated" --work "${CMAKE_BINARY_DIR}/generated/metallib-utility"
                --sdk ${nebula_apple_metal_sdk} --min-os ${nebula_apple_metal_min_os}
                ${nebula_apple_metal_developer_argument}
        DEPENDS "${nebula_apple_utility_texts}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/compile_metallib.py"
        USES_TERMINAL VERBATIM)
    list(APPEND nebula_apple_program_libraries "${nebula_apple_utility_library}")
endif()
add_custom_target(nebula_apple_utility_files DEPENDS "${nebula_apple_utility_msl_dir}/utility_msl_data.h"
    ${nebula_apple_utility_check_stamp} ${nebula_apple_utility_library})

# Every source of the Vulkan library that is not Vulkan's: what other lanes add to the renderer comes along.
get_target_property(nebula_apple_vk_sources galaxy_render_vk SOURCES)
set(nebula_apple_mtl_sources)
foreach(source IN LISTS nebula_apple_vk_sources)
    if(source MATCHES "/render/vk/" OR source MATCHES "_vk\\.cpp$" OR source MATCHES "utility_spirv_data\\.h$")
        continue()
    endif()
    list(APPEND nebula_apple_mtl_sources "${source}")
endforeach()
file(GLOB nebula_apple_mtl_backend CONFIGURE_DEPENDS
    "${nebula_root}/runtime/src/render/mtl/*.mm" "${nebula_root}/runtime/src/render/mtl/*.cpp")
# shader_msl.cpp is nebula_msl's (it needs SPIRV-Cross's headers).
list(FILTER nebula_apple_mtl_backend EXCLUDE REGEX "/shader_msl\\.cpp$")
# A mod's programs are made on the player's machine through the Vulkan renderer's HLSL front end, then SPIRV-Cross
# (mtl/mod_shader_compile_mtl.cpp): that one source of render/vk comes along, with glslang.
add_library(galaxy_render_mtl STATIC ${nebula_apple_mtl_sources} ${nebula_apple_mtl_backend}
    "${nebula_root}/runtime/src/render/vk/shader_compile_vk.cpp"
    "${nebula_apple_utility_msl_dir}/utility_msl_data.h")
target_link_libraries(galaxy_render_mtl PRIVATE nebula_shader_compiler)
target_compile_features(galaxy_render_mtl PUBLIC cxx_std_20)
# The game's version, from the main project's file (as platform/sdl reads it for its own targets).
file(STRINGS "${nebula_root}/CMakeLists.txt" nebula_apple_project_line REGEX "^project\\(Nebula VERSION ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" nebula_apple_version "${nebula_apple_project_line}")
target_compile_definitions(galaxy_render_mtl PUBLIC NEBULA_GPU_METAL NEBULA_VERSION="${nebula_apple_version}${NEBULA_VERSION_SUFFIX}")
if(TARGET nebula_archive)
    target_compile_definitions(galaxy_render_mtl PUBLIC NEBULA_MOD_IMPORT=1)
    target_link_libraries(galaxy_render_mtl PUBLIC nebula_archive)
endif()
target_include_directories(galaxy_render_mtl PUBLIC "${nebula_root}/runtime/src" "${nebula_root}/runtime/include")
target_include_directories(galaxy_render_mtl PRIVATE "${nebula_root}/third_party/smaa" "${nebula_root}/native/include"
    "${nebula_apple_utility_msl_dir}")
target_link_libraries(galaxy_render_mtl PRIVATE nebula_msl)
# As platform/sdl gives galaxy_render_vk: TLS and gzip for the net services (luma_net.cpp), the save import's AES,
# hashes and inflate (save_transfer.cpp), and the game's GameData.bin check (a source property is its folder's, so
# platform/sdl's does not reach this folder's target).
target_link_libraries(galaxy_render_mtl PRIVATE nebula_bssl_ssl nebula_zinflate)
set_property(SOURCE "${nebula_root}/runtime/src/save_transfer.cpp" APPEND PROPERTY INCLUDE_DIRECTORIES
    "${nebula_root}/native/petari/include")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # The settings' UPDATES rows (native_settings.cpp), as galaxy_render_vk has them on macOS: the service is
    # nebula_native's (CMakeLists.txt); iOS updates through its store.
    target_compile_definitions(galaxy_render_mtl PRIVATE NEBULA_UPDATE_CHECK=1)
endif()
target_link_libraries(galaxy_render_mtl PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Foundation"
    "-framework CoreGraphics" "-framework IOSurface")
# MetalFX is asked for at run time (macOS 13, iOS 16): the program starts without it.
target_link_libraries(galaxy_render_mtl PUBLIC "-weak_framework MetalFX")
set_source_files_properties(${nebula_apple_mtl_backend} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
# The programs' file in this library is Metal's (core/program_table.cpp includes it): the texts, or with Apple's
# compiler on the build machine the names of the functions in the app's Metal libraries (CMakeLists.txt).
set_source_files_properties("${nebula_root}/runtime/src/render/core/program_table.cpp" PROPERTIES
    COMPILE_DEFINITIONS "GALAXY_PROGRAM_BLOB=\"${nebula_apple_programs_blob}\"" OBJECT_DEPENDS "${nebula_apple_programs_blob}")
add_dependencies(galaxy_render_mtl nebula_apple_programs_msl_file nebula_apple_utility_files)

# The game on the Metal renderer with the SDL host: a development build until Apple's own host (host/) carries it.
get_target_property(nebula_apple_main_sources main SOURCES)
add_executable(nebula_metal ${nebula_apple_main_sources})
set_target_properties(nebula_metal PROPERTIES OUTPUT_NAME nebula-metal
    BUILD_RPATH "@executable_path;@executable_path/../Frameworks")
target_include_directories(nebula_metal PRIVATE "${nebula_root}/native/include" "${nebula_root}/runtime/include"
    "${nebula_root}/platform/sdl" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(nebula_metal PRIVATE NEBULA_HOST_RENDERER=1 NEBULA_HOST_SDL=1 NEBULA_HOST_APPLE=1)
set_source_files_properties(${nebula_apple_host_sources} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(nebula_metal PRIVATE galaxy_render_mtl SDL3::SDL3
    "-framework AppKit" "-framework CoreAudio" "-framework CoreVideo")

# The game as Apple's systems run it: the Metal renderer and Apple's own host (host_window_apple.mm,
# audio_device_apple.mm, display_clock_apple.mm): AppKit, Game Controller, Core Haptics, Core Audio. No SDL.
add_executable(nebula_native
    "${nebula_root}/native/host/host_main.cpp"
    "${nebula_root}/runtime/src/whole_lines.cpp"
    "${nebula_root}/platform/sdl/native_game_host_sdl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/host_window_apple.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/luma_system_apple.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio_device_apple.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/display_clock_apple.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/apple_host.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/setup_apple.mm")
set_target_properties(nebula_native PROPERTIES OUTPUT_NAME Nebula)
target_include_directories(nebula_native PRIVATE "${nebula_root}/native/include" "${nebula_root}/runtime/include"
    "${nebula_root}/platform/sdl" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(nebula_native PRIVATE NEBULA_HOST_RENDERER=1 NEBULA_HOST_SDL=1 NEBULA_HOST_APPLE=1
    NEBULA_HOST_NATIVE_APPLE=1)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/host_window_apple.mm" "${CMAKE_CURRENT_SOURCE_DIR}/luma_system_apple.mm" "${CMAKE_CURRENT_SOURCE_DIR}/audio_device_apple.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/setup_apple.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

# The importer (crates/nebula-import), linked into each program for the first start's setup, the mods' cooker and
# the refusals' words: its static library for this processor, made by cargo (the crate's own manifest builds a shared
# library for the launcher; this asks for the archive without touching it).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(nebula_apple_rust_triple "aarch64-apple-ios")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(nebula_apple_rust_triple "aarch64-apple-darwin")
else()
    set(nebula_apple_rust_triple "x86_64-apple-darwin")
endif()
find_program(NEBULA_CARGO cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
set(nebula_apple_importer "${nebula_root}/target/${nebula_apple_rust_triple}/dist/libnebula_import.a")
file(GLOB_RECURSE nebula_apple_importer_sources CONFIGURE_DEPENDS "${nebula_root}/crates/nebula-import/src/*.rs")
add_custom_command(OUTPUT "${nebula_apple_importer}"
    COMMAND "${CMAKE_COMMAND}" -E env "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "${NEBULA_CARGO}" rustc --profile dist --target "${nebula_apple_rust_triple}" -p nebula-import --lib --crate-type staticlib
    WORKING_DIRECTORY "${nebula_root}"
    DEPENDS ${nebula_apple_importer_sources} "${nebula_root}/crates/nebula-import/Cargo.toml"
    USES_TERMINAL VERBATIM)
add_custom_target(nebula_apple_importer_file DEPENDS "${nebula_apple_importer}")
# Every program that compiles mod_plugins.cpp (galaxy_render_vk, whose sources galaxy_render_mtl takes) or host_main.cpp
# calls luma_validate_message and nebula_cook_overlay directly on Apple: main (MoltenVK on macOS, UIKit on iOS),
# nebula_metal and nebula_native.
foreach(program IN ITEMS main nebula_metal nebula_native)
    add_dependencies(${program} nebula_apple_importer_file)
    target_link_libraries(${program} PRIVATE "${nebula_apple_importer}" "-liconv" "-lresolv" "-framework Security")
endforeach()
target_link_libraries(nebula_native PRIVATE galaxy_render_mtl
    "-framework AppKit" "-framework GameController" "-framework CoreHaptics" "-framework AudioToolbox"
    "-framework CoreAudio" "-framework CoreVideo" "-framework IOKit")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # The update check and install of the home screen (runtime/include/galaxy/update.h); iOS updates through its store.
    set(nebula_apple_update_objc "${nebula_root}/runtime/src/update/update_transport_apple.mm"
        "${nebula_root}/runtime/src/update/update_install_macos.mm")
    target_sources(nebula_native PRIVATE "${nebula_root}/runtime/src/update/update.cpp"
        "${nebula_root}/runtime/src/update/update_crypto.cpp" "${nebula_root}/runtime/src/update/update_probe_posix.cpp"
        ${nebula_apple_update_objc})
    set_source_files_properties(${nebula_apple_update_objc} TARGET_DIRECTORY nebula_native PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(nebula_native PRIVATE NEBULA_UPDATE_CHECK=1)
    if(TARGET nebula_archive)
        # The diagnostics report modes (diag_report.h): zlib from the archive libraries, the update check's transport.
        target_sources(nebula_native PRIVATE "${nebula_root}/runtime/src/diag_report.cpp"
            "${nebula_root}/runtime/src/diag_seal.cpp")
        target_compile_definitions(nebula_native PRIVATE NEBULA_DIAG_REPORTS=1)
        # The report's seal (diag_seal.cpp): BoringSSL's HPKE.
        include("${nebula_root}/third_party/boringssl.cmake")
        target_link_libraries(nebula_native PRIVATE nebula_bssl_crypto)
    endif()
    target_link_libraries(nebula_native PRIVATE "-framework Foundation" "-framework CFNetwork" "-framework Security")
endif()

# The app's Metal libraries beside the program (a development run reads them there; build_macos.sh puts them in the
# app's Resources).
foreach(library IN LISTS nebula_apple_program_libraries)
    add_custom_command(TARGET nebula_native POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${library}" "$<TARGET_FILE_DIR:nebula_native>" VERBATIM)
endforeach()
