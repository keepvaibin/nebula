# The game module (docs/NATIVE_PORT.md): Petari's kept code for RMGE01 and the parts of the RVL SDK it reuses,
# built natively with clang against the game's own C++ library (MSL_C++) and Nebula's MSL interface, with the
# native SDK layer, as a shared library apart from the host runtime.
cmake_minimum_required(VERSION 3.24)
project(NebulaGame LANGUAGES C CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    message(FATAL_ERROR "The game module is built with clang's GNU driver (clang++, not clang-cl).")
endif()

set(NEBULA_PETARI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/petari" CACHE PATH "The game core: Nebula's Petari fork (native/petari)")
if(NOT EXISTS "${NEBULA_PETARI_DIR}/src/Game/System/GameSystem.cpp")
    message(FATAL_ERROR "NEBULA_PETARI_DIR must point at a Petari checkout (src/Game not found in '${NEBULA_PETARI_DIR}')")
endif()
set(NEBULA_GAME_VERSION "RMGE01" CACHE STRING "Game release")
set_property(CACHE NEBULA_GAME_VERSION PROPERTY STRINGS RMGE01)

set(petari "${NEBULA_PETARI_DIR}")
file(GLOB_RECURSE game_sources CONFIGURE_DEPENDS
    "${petari}/src/Game/*.cpp" "${petari}/src/Game/*.c"
    "${petari}/src/JSystem/*.cpp" "${petari}/src/JSystem/*.c"
    "${petari}/src/nw4r/*.cpp" "${petari}/src/nw4r/*.c"
    "${petari}/src/RVLFaceLib/*.c" "${petari}/src/RVLFaceLib/*.cpp")
# The SDK code the native layer reuses (docs/NATIVE_PORT.md, "Platform boundary"): GX but its FIFO (sdk/gx/gx_fifo.c),
# display-list building, the MEM heaps, KPAD's processing, the disc's file system (dvdfs.c), U8 archives (arc.c), the
# Wii Remote speaker's encoder (wenc.c), and the OS's scheduler, alarms, synchronisation and arenas on the native OS
# core (sdk/os/os_native.c).
file(GLOB sdk_sources CONFIGURE_DEPENDS
    "${petari}/src/RVL_SDK/gx/*.c"
    "${petari}/src/RVL_SDK/gd/*.c"
    "${petari}/src/RVL_SDK/mem/*.c"
    "${petari}/src/RVL_SDK/kpad/*.c"
    "${petari}/src/RVL_SDK/dvd/dvdfs.c"
    "${petari}/src/RVL_SDK/arc/arc.c"
    "${petari}/src/RVL_SDK/wenc/wenc.c")
list(REMOVE_ITEM sdk_sources "${petari}/src/RVL_SDK/gx/GXFifo.c")
foreach(unit OSThread OSMutex OSMessage OSAlarm OSTime OSArena OSAlloc)
    list(APPEND sdk_sources "${petari}/src/RVL_SDK/os/${unit}.c")
endforeach()
file(GLOB_RECURSE layer_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sdk/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/sdk/*.c")
list(FILTER layer_sources EXCLUDE REGEX "/tests/")

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
    "${CMAKE_CURRENT_SOURCE_DIR}/sdk"
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(version_ids RMGJ01=0 RMGE01=1 RMGP01=2 RMGK01=3)
string(REGEX MATCH "${NEBULA_GAME_VERSION}=([0-9])" _ "${version_ids}")
target_compile_definitions(smg_game PRIVATE VERSION=${CMAKE_MATCH_1} ${version_ids} TARGET_PC=1 NEBULA_GAME_MODULE=1)

set_target_properties(smg_game PROPERTIES
    C_STANDARD 11 C_EXTENSIONS ON
    CXX_STANDARD 17 CXX_EXTENSIONS ON
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# No native-built line may test a host pointer against the console's memory map (tools/console_address_audit.py);
# checked before every link of the module.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_custom_command(TARGET smg_game PRE_LINK
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/console_address_audit.py"
            "${CMAKE_CURRENT_SOURCE_DIR}" "${petari}"
        VERBATIM)
else()
    message(WARNING "Python 3 not found: tools/console_address_audit.py does not run with this build")
endif()

# Every float-to-integer conversion into a type narrower than 32 bits goes through s32, as the Wii converts
# (tools/narrow_float_audit.py; clang-query over the whole module, minutes): after each link with this option.
option(NEBULA_AUDIT_FLOAT_CASTS "Fail the module build on narrow float-to-integer conversions" OFF)
if(NEBULA_AUDIT_FLOAT_CASTS AND Python3_Interpreter_FOUND)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    add_custom_command(TARGET smg_game POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/narrow_float_audit.py" "${CMAKE_BINARY_DIR}"
        VERBATIM)
endif()

# Audit builds (docs/NATIVE_PORT.md, "Audits"), not for shipping: these diagnostics in place of -Wno-everything
# (which no later -W option undoes), e.g. -Wshorten-64-to-32 for tools/narrowing.py.
set(NEBULA_AUDIT_FLAGS "" CACHE STRING "Diagnostics for an audit build")
if(NEBULA_AUDIT_FLAGS)
    separate_arguments(diagnostics UNIX_COMMAND "${NEBULA_AUDIT_FLAGS}")
else()
    set(diagnostics -Wno-everything)
endif()

# docs/NATIVE_PORT.md, "Build of the game code", has the reason for each, and docs/NATIVE_PERFORMANCE.md the cost:
# -fno-math-errno (the game never reads errno; sqrtf and friends stay single instructions), -fno-stack-protector
# (pinned off for every toolchain: distribution compilers turn it on by default).
# The same language on every platform: clang enables Microsoft extensions by default only when targeting Windows
# (__declspec, a static definition after an extern declaration); glibc's GNU interfaces (dladdr, memfd_create, the
# signal context's registers) are declared only under _GNU_SOURCE, which must precede the prelude's first include.
target_compile_definitions(smg_game PRIVATE $<$<PLATFORM_ID:Linux,Android>:_GNU_SOURCE>)
target_compile_options(smg_game PRIVATE
    $<$<NOT:$<PLATFORM_ID:Windows>>:-fms-extensions>
    -fsigned-char -fshort-wchar
    -fno-strict-aliasing -fno-strict-float-cast-overflow -fno-math-errno -fno-stack-protector
    $<$<NOT:$<PLATFORM_ID:Windows>>:-fno-plt>
    -fno-ms-compatibility -fno-delayed-template-parsing
    ${diagnostics}
    -Werror=return-type -Werror=pointer-to-int-cast -Werror=void-pointer-to-int-cast
    -Werror=int-to-pointer-cast -Werror=pointer-to-enum-cast
    -Werror=implicit-function-declaration -Werror=implicit-int -Werror=int-conversion
    -Werror=incompatible-function-pointer-types -Werror=return-stack-address -Werror=array-bounds
    "SHELL:-include nebula_prelude.h"
    # Source paths in the module (assertion messages) relative to the tree, not the build machine.
    "-ffile-prefix-map=${petari}=petari" "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=native"
    $<$<COMPILE_LANGUAGE:CXX>:-nostdinc++ -fno-rtti -fno-exceptions -fno-sized-deallocation -fno-aligned-allocation
        -fno-threadsafe-statics>
    # Every new-expression tests the allocation before constructing, as the console's compiler does: JKRHeap's
    # operator new and the sound pools' (JASPoolAllocObject) return null when full, and the game checks
    # (JAISeMgr::newSe_). Standard C++ lets the compiler drop those checks for an operator new that is not
    # noexcept, which would run JAISe's constructor on null when a sound pool is full.
    $<$<COMPILE_LANGUAGE:CXX>:-fcheck-new>
    # The Microsoft C++ ABI sizes a member pointer by its class's inheritance, known only where the class is complete;
    # a unit seeing just a declaration would use another size than its callers.
    $<$<AND:$<PLATFORM_ID:Windows>,$<COMPILE_LANGUAGE:CXX>>:-fcomplete-member-pointers>
    -Wno-register -Wno-c++11-narrowing)

if(WIN32)
    # The DLL's own operator new/delete are JKRHeap's; the host process keeps its own.
    target_link_options(smg_game PRIVATE -fuse-ld=lld "LINKER:/errorlimit:0")
    target_link_libraries(smg_game PRIVATE winmm)
else()
    target_link_options(smg_game PRIVATE -Wl,-Bsymbolic -Wl,-z,now)
    if(NOT ANDROID)  # Android's C library holds the threads
        target_link_libraries(smg_game PRIVATE pthread)
    endif()
endif()

# Sanitizer builds (tests only): e.g. -DNEBULA_SANITIZE=address for the module and the host.
set(NEBULA_SANITIZE "" CACHE STRING "Sanitizers for a test build (clang -fsanitize=...)")
if(NEBULA_SANITIZE)
    target_compile_options(smg_game PRIVATE -fsanitize=${NEBULA_SANITIZE} -fno-omit-frame-pointer)
    # The module's operator new/delete are JKRHeap's; the sanitizer runtime's own definitions give way.
    target_link_options(smg_game PRIVATE -fsanitize=${NEBULA_SANITIZE} $<$<PLATFORM_ID:Windows>:LINKER:/force:multiple>)
    if(NEBULA_SANITIZE MATCHES "address")
        # The game's heaps manage their own memory: built uninstrumented, they keep what is free poisoned and unpoison
        # what they hand out (JKRHeap.cpp), so overflows and uses after free inside them are reported too.
        set(heaps "${petari}/src/JSystem/JKernel")
        set_source_files_properties("${heaps}/JKRHeap.cpp" "${heaps}/JKRExpHeap.cpp" "${heaps}/JKRSolidHeap.cpp"
                                    "${heaps}/JKRUnitHeap.cpp" PROPERTIES COMPILE_OPTIONS "-fno-sanitize=address")
        set_source_files_properties("${heaps}/JKRHeap.cpp" PROPERTIES COMPILE_DEFINITIONS NEBULA_ASAN_HEAPS=1)
    endif()
endif()

# The native game's host (host/host_main.cpp): loads the module and serves it. Built with the host's own C++ library.
# The mods' virtual disc (galaxy::mods) serves this host's disc reads as the windowed host's.
add_executable(nebula_native_host host/host_main.cpp
    ../runtime/src/mods.cpp ../runtime/src/mod_xml.cpp ../runtime/src/virtual_disc.cpp ../runtime/src/atomic_file.cpp
    ../runtime/src/crash_report.cpp ../runtime/src/discord_presence.cpp)
target_include_directories(nebula_native_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include")
target_compile_features(nebula_native_host PRIVATE cxx_std_20)
target_compile_definitions(nebula_native_host PRIVATE _CRT_SECURE_NO_WARNINGS)
add_dependencies(nebula_native_host smg_game)
if(NEBULA_SANITIZE)
    target_compile_options(nebula_native_host PRIVATE -fsanitize=${NEBULA_SANITIZE} -fno-omit-frame-pointer)
    target_link_options(nebula_native_host PRIVATE -fsanitize=${NEBULA_SANITIZE})
endif()
