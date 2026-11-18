# The unit tests on Linux and the Mac (cmake/unit_tests.cmake, the same list Windows builds in the root CMakeLists.txt),
# in a project of their own: the tests need no SDL, renderer or dependency checkout beyond the tree's third_party.
#
#   cmake -S tests/unit -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build <build>
#   ctest --test-dir <build> --output-on-failure
cmake_minimum_required(VERSION 3.22)
project(NebulaUnitTests LANGUAGES C CXX ASM)
# What the host computes for the game (a stick's position above all) is the same bits on every CPU: no multiply and
# add is fused into one rounding. Clang fuses them by default on CPUs with a fused multiply-add (arm64, and x86-64 at
# AVX2): a tablet's stick then differed from a computer's by one ulp, and its replay from the computer's (N-1064). The
# game module is built the same way (native/CMakeLists.txt). MSVC fuses none at /fp:precise without /fp:contract.
if(MSVC)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/clang:-ffp-contract=off>)
    endif()
else()
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=off>)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(nebula_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include_directories("${nebula_root}/native/include")
# The mods importer's archive libraries (zlib, liblzma, libarchive), BoringSSL and zlib's inflate, from third_party.
set(NEBULA_ARCHIVE_SOURCES "${nebula_root}/third_party/archive" CACHE PATH "zlib, xz and libarchive sources")
include("${nebula_root}/cmake/archive_deps.cmake")
include("${nebula_root}/third_party/boringssl.cmake")
include("${nebula_root}/third_party/zlib_inflate.cmake")

include("${nebula_root}/cmake/unit_tests.cmake")
