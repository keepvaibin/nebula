# SPDX-License-Identifier: GPL-3.0-only
# The two inputs of a release build every top-level project (the root, platform/sdl, platform/apple, native) reads the
# same way. Each is given as a CMake variable (-DNEBULA_VERSION_SUFFIX=-beta.1 -DNEBULA_MODS=OFF) or as the environment
# variable of the same name (a workflow or a script sets it once for every build it starts); the variable wins.
#
#   NEBULA_VERSION_SUFFIX  "" (a release) or "-beta.<n>": the pre-release label after the project's MAJOR.MINOR.PATCH.
#                          NEBULA_VERSION, which the title screen shows and the update check compares, is both.
#   NEBULA_MODS            ON (default): mods are supported. OFF: a build with no mod support (runtime/include/galaxy/
#                          mod_support.h): NEBULA_NO_MODS is defined for every program.
include_guard(GLOBAL)

if(NOT DEFINED NEBULA_VERSION_SUFFIX)
    set(nebula_suffix_default "")
    if(DEFINED ENV{NEBULA_VERSION_SUFFIX})
        set(nebula_suffix_default "$ENV{NEBULA_VERSION_SUFFIX}")
    endif()
    set(NEBULA_VERSION_SUFFIX "${nebula_suffix_default}" CACHE STRING "A pre-release label after the version: empty or -beta.<n>")
endif()
if(NOT NEBULA_VERSION_SUFFIX MATCHES "^(-beta\.[0-9]+)?$")
    message(FATAL_ERROR "NEBULA_VERSION_SUFFIX is empty or -beta.<n>, not '${NEBULA_VERSION_SUFFIX}'")
endif()

if(NOT DEFINED NEBULA_MODS)
    set(nebula_mods_default ON)
    if(DEFINED ENV{NEBULA_MODS})
        set(nebula_mods_default "$ENV{NEBULA_MODS}")
    endif()
    set(NEBULA_MODS "${nebula_mods_default}" CACHE BOOL "Support mods (OFF: a build with no mod support, galaxy/mod_support.h)")
endif()
if(NOT NEBULA_MODS)
    add_compile_definitions(NEBULA_NO_MODS=1)
endif()

# NEBULA_REQUIRE_FSR3 is set by a build that makes a package (the release workflow's and the check workflow's Windows, Linux and
# Android jobs; not the Mac's, whose upscaler is MetalFX). OFF (default): a build may leave AMD's FSR 3 out and says so once.
# ON: leaving it out stops the configure with the reason (cmake/fidelityfx_vk_shaders.cmake, nebula_fsr3_left_out).
if(NOT DEFINED NEBULA_REQUIRE_FSR3)
    set(nebula_require_fsr3_default OFF)
    if(DEFINED ENV{NEBULA_REQUIRE_FSR3})
        set(nebula_require_fsr3_default "$ENV{NEBULA_REQUIRE_FSR3}")
    endif()
    set(NEBULA_REQUIRE_FSR3 "${nebula_require_fsr3_default}" CACHE BOOL "A package build: leaving FSR 3 out is an error")
endif()
