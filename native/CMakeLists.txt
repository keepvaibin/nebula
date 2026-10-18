# The game module (docs/NATIVE_PORT.md): Petari's kept code for RMGE01 and the parts of the RVL SDK it reuses,
# built natively with clang against the game's own C++ library (MSL_C++) and Nebula's MSL interface, with the
# native SDK layer, as a shared library apart from the host runtime.
cmake_minimum_required(VERSION 3.24)
project(NebulaGame LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    message(FATAL_ERROR "The game module is built with clang's GNU driver (clang++, not clang-cl).")
endif()

set(NEBULA_PETARI_DIR "" CACHE PATH "Petari checkout on the nebula branch")
if(NOT EXISTS "${NEBULA_PETARI_DIR}/src/Game/System/GameSystem.cpp")
    message(FATAL_ERROR "NEBULA_PETARI_DIR must point at a Petari checkout (src/Game not found in '${NEBULA_PETARI_DIR}')")
endif()
set(NEBULA_GAME_VERSION "RMGE01" CACHE STRING "Game release")
set_property(CACHE NEBULA_GAME_VERSION PROPERTY STRINGS RMGE01)
if(NOT EXISTS "${NEBULA_PETARI_DIR}/build/${NEBULA_GAME_VERSION}/include/assets")
    message(FATAL_ERROR "Run Petari's configure.py and `ninja build/${NEBULA_GAME_VERSION}/config.json` with the "
                        "user's main.dol first: the DOL's data tables are extracted into build/${NEBULA_GAME_VERSION}/include.")
endif()

set(petari "${NEBULA_PETARI_DIR}")
file(GLOB_RECURSE game_sources CONFIGURE_DEPENDS
    "${petari}/src/Game/*.cpp" "${petari}/src/Game/*.c"
    "${petari}/src/JSystem/*.cpp" "${petari}/src/JSystem/*.c"
    "${petari}/src/nw4r/*.cpp" "${petari}/src/nw4r/*.c"
    "${petari}/src/RVLFaceLib/*.c" "${petari}/src/RVLFaceLib/*.cpp")
# The SDK code the native layer reuses as it is: display-list building, the MEM heaps, KPAD's processing.
file(GLOB sdk_sources CONFIGURE_DEPENDS
    "${petari}/src/RVL_SDK/gd/*.c"
    "${petari}/src/RVL_SDK/mem/*.c"
    "${petari}/src/RVL_SDK/kpad/*.c")
file(GLOB_RECURSE layer_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sdk/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/sdk/*.c")

add_library(smg_game SHARED
    ${game_sources}
    ${sdk_sources}
    ${layer_sources}
    msl/src/msl_runtime.c)

target_include_directories(smg_game BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/msl/include"
    "${petari}/include"
    "${petari}/libs/JSystem/include"
    "${petari}/libs/MSL_C++/include"
    "${petari}/libs/RVLFaceLib/include"
    "${petari}/libs/RVL_SDK/include"
    "${petari}/libs/nw4r/include"
    "${petari}/build/${NEBULA_GAME_VERSION}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk")

set(version_ids RMGJ01=0 RMGE01=1 RMGP01=2 RMGK01=3)
string(REGEX MATCH "${NEBULA_GAME_VERSION}=([0-9])" _ "${version_ids}")
target_compile_definitions(smg_game PRIVATE VERSION=${CMAKE_MATCH_1} ${version_ids} TARGET_PC=1)

set_target_properties(smg_game PROPERTIES
    C_STANDARD 11 C_EXTENSIONS ON
    CXX_STANDARD 17 CXX_EXTENSIONS ON
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# docs/NATIVE_PORT.md, "Build of the game code", has the reason for each.
target_compile_options(smg_game PRIVATE
    -fsigned-char -fshort-wchar
    -fno-strict-aliasing -fno-strict-float-cast-overflow
    -fno-ms-compatibility -fno-delayed-template-parsing
    -Wno-everything
    -Werror=return-type -Werror=pointer-to-int-cast -Werror=void-pointer-to-int-cast
    -Werror=int-to-pointer-cast -Werror=pointer-to-enum-cast
    "SHELL:-include nebula_prelude.h"
    $<$<COMPILE_LANGUAGE:CXX>:-nostdinc++ -fno-rtti -fno-exceptions -fno-sized-deallocation -fno-aligned-allocation>
    -Wno-register -Wno-c++11-narrowing)

if(WIN32)
    # The DLL's own operator new/delete are JKRHeap's; the host process keeps its own.
    target_link_options(smg_game PRIVATE -fuse-ld=lld)
else()
    target_link_options(smg_game PRIVATE -Wl,-Bsymbolic)
endif()
