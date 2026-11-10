# SPDX-License-Identifier: GPL-3.0-only
# The toolchain of every Apple build (docs/APPLE.md, "Toolchain"): LLVM's clang (the game module needs clang 19, as on
# every platform), Apple's SDK and linker, and LLVM's libc++ built for the target (build_libcxx.sh), linked statically.
#
#   -DCMAKE_TOOLCHAIN_FILE=platform/apple/toolchain.cmake
#   -DNEBULA_APPLE_LLVM=<clang root>          bin/clang, bin/clang++
#   -DNEBULA_APPLE_LIBCXX=<build_libcxx out>  <platform>-<arch>/{include,lib}
#   -DNEBULA_APPLE_PLATFORM=macos|ios|ios-simulator
#   -DNEBULA_APPLE_ARCH=x86_64|arm64
#   [-DNEBULA_APPLE_SDK=<sdk path>]            default: xcrun's for the platform
#
# One architecture a build; a universal Mac app is two builds joined with lipo (build_macos.sh).

# try_compile projects read the toolchain file again without the cache: carry the choices in the environment.
foreach(name LLVM LIBCXX PLATFORM ARCH SDK)
    if(DEFINED NEBULA_APPLE_${name})
        set(ENV{NEBULA_APPLE_${name}} "${NEBULA_APPLE_${name}}")
    elseif(DEFINED ENV{NEBULA_APPLE_${name}})
        set(NEBULA_APPLE_${name} "$ENV{NEBULA_APPLE_${name}}")
    endif()
endforeach()

if(NOT NEBULA_APPLE_PLATFORM)
    set(NEBULA_APPLE_PLATFORM macos)
endif()
if(NOT NEBULA_APPLE_ARCH)
    set(NEBULA_APPLE_ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}")
endif()
if(NOT NEBULA_APPLE_ARCH MATCHES "^(x86_64|arm64)$")
    message(FATAL_ERROR "NEBULA_APPLE_ARCH is x86_64 or arm64, not '${NEBULA_APPLE_ARCH}'")
endif()
if(NOT EXISTS "${NEBULA_APPLE_LLVM}/bin/clang++")
    message(FATAL_ERROR "Set NEBULA_APPLE_LLVM to the root of an LLVM 19 (or newer) package: bin/clang++ is not in '${NEBULA_APPLE_LLVM}'")
endif()

if(NEBULA_APPLE_PLATFORM STREQUAL "macos")
    set(CMAKE_SYSTEM_NAME Darwin)
    set(nebula_apple_sdk_name macosx)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0 CACHE STRING "" FORCE)
    set(nebula_apple_triple "${NEBULA_APPLE_ARCH}-apple-macosx12.0")
elseif(NEBULA_APPLE_PLATFORM STREQUAL "ios")
    set(CMAKE_SYSTEM_NAME iOS)
    set(nebula_apple_sdk_name iphoneos)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "" FORCE)
    set(nebula_apple_triple "${NEBULA_APPLE_ARCH}-apple-ios15.0")
elseif(NEBULA_APPLE_PLATFORM STREQUAL "ios-simulator")
    set(CMAKE_SYSTEM_NAME iOS)
    set(nebula_apple_sdk_name iphonesimulator)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "" FORCE)
    set(nebula_apple_triple "${NEBULA_APPLE_ARCH}-apple-ios15.0-simulator")
else()
    message(FATAL_ERROR "NEBULA_APPLE_PLATFORM is macos, ios or ios-simulator, not '${NEBULA_APPLE_PLATFORM}'")
endif()
set(CMAKE_SYSTEM_PROCESSOR "${NEBULA_APPLE_ARCH}")
set(CMAKE_OSX_ARCHITECTURES "${NEBULA_APPLE_ARCH}" CACHE STRING "" FORCE)

if(NOT NEBULA_APPLE_SDK)
    execute_process(COMMAND xcrun --sdk ${nebula_apple_sdk_name} --show-sdk-path
                    OUTPUT_VARIABLE NEBULA_APPLE_SDK OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(ENV{NEBULA_APPLE_SDK} "${NEBULA_APPLE_SDK}")
endif()
if(NOT IS_DIRECTORY "${NEBULA_APPLE_SDK}")
    message(FATAL_ERROR "No ${nebula_apple_sdk_name} SDK (an iPhone or iPad build needs Xcode); or set NEBULA_APPLE_SDK")
endif()
set(CMAKE_OSX_SYSROOT "${NEBULA_APPLE_SDK}" CACHE PATH "" FORCE)

set(CMAKE_C_COMPILER "${NEBULA_APPLE_LLVM}/bin/clang")
set(CMAKE_CXX_COMPILER "${NEBULA_APPLE_LLVM}/bin/clang++")
set(CMAKE_OBJC_COMPILER "${NEBULA_APPLE_LLVM}/bin/clang")
set(CMAKE_OBJCXX_COMPILER "${NEBULA_APPLE_LLVM}/bin/clang++")
foreach(language C CXX OBJC OBJCXX)
    set(CMAKE_${language}_COMPILER_TARGET "${nebula_apple_triple}")
endforeach()

# libc++: the target's own, its headers before the compiler's and its archive in place of the system's library.
set(nebula_apple_libcxx "${NEBULA_APPLE_LIBCXX}/${NEBULA_APPLE_PLATFORM}-${NEBULA_APPLE_ARCH}")
if(NOT EXISTS "${nebula_apple_libcxx}/lib/libc++.a")
    message(FATAL_ERROR "No libc++ for ${NEBULA_APPLE_PLATFORM} ${NEBULA_APPLE_ARCH} in '${NEBULA_APPLE_LIBCXX}': platform/apple/build_libcxx.sh makes it")
endif()
# Nothing of it is exported from a module that holds it: the system's own libc++ is in the process too (the frameworks
# use it), and dyld joins weak symbols of one name across images.
set(nebula_apple_cxx "-nostdinc++ -isystem ${nebula_apple_libcxx}/include/c++/v1 -fvisibility=hidden -fvisibility-inlines-hidden -D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS -D_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS")
set(CMAKE_CXX_FLAGS_INIT "${nebula_apple_cxx}")
set(CMAKE_OBJCXX_FLAGS_INIT "${nebula_apple_cxx}")
set(CMAKE_CXX_STANDARD_LIBRARIES "-nostdlib++ ${nebula_apple_libcxx}/lib/libc++.a")
set(CMAKE_OBJCXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES}")

# Headers Apple's SDKs lack (uchar.h), for the game module's C units.
include_directories(SYSTEM "${CMAKE_CURRENT_LIST_DIR}/compat")

# Dependencies' packages are never the build machine's; programs are.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The vendored libarchive installs its pkg-config file under this name, which nothing defines for it here.
set(CMAKE_INSTALL_LIBDIR lib CACHE STRING "")
