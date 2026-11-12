# The game module (docs/NATIVE_PORT.md): Petari's kept code for RMGE01 and the parts of the RVL SDK it reuses,
# built natively with clang against the host's C and C++ libraries, with the
# native SDK layer, as a shared library apart from the host runtime.
cmake_minimum_required(VERSION 3.24)
project(NebulaGame LANGUAGES C CXX)
# The game's version, from the main project's file.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt" nebula_project_line REGEX "^project\\(Nebula VERSION ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" nebula_version "${nebula_project_line}")
add_compile_definitions(NEBULA_VERSION="${nebula_version}")

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
    "${petari}/src/Game/*.cpp" "${petari}/src/Game/*.c")

# LumaCore's field rows (tools/lumacore) name private fields too: offsetof reads them with no access check.
set_source_files_properties("${petari}/src/Game/System/generated/lumacore_fields.cpp" PROPERTIES COMPILE_OPTIONS "-Xclang;-fno-access-control")

# Every function the game defines carries its LumaCore row and hook test (include/luma_hook.h). Each source is compiled
# from a copy tools/lumacore/instrument.py writes into the build folder; the source itself is never changed. The hook
# runner and the generated tables are compiled as they are.
# OFF builds the module with no rows (no function can be hooked or called by name): only to measure what they cost.
option(NEBULA_LUMA_HOOKS "Every game function carries its LumaCore row and hook test" ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(luma_tool "${CMAKE_CURRENT_SOURCE_DIR}/../tools/lumacore/instrument.py")
# instrument.py runs gen_types.py's unit pass: a copy is out of date when either changes.
set(luma_types_tool "${CMAKE_CURRENT_SOURCE_DIR}/../tools/lumacore/gen_types.py")
# The row generator parses with libclang on the build machine. A compiler of the build machine has it beside itself
# (LLVM's packages); a cross compiler may not (Android's NDK ships none), so a cross build finds the build machine's.
set(NEBULA_LIBCLANG "" CACHE FILEPATH "The build machine's libclang for the row generator (default: beside the compiler)")
set(luma_libclang)
if(NEBULA_LUMA_HOOKS AND NOT NEBULA_LIBCLANG AND CMAKE_CROSSCOMPILING)
    file(GLOB luma_libclang_hints LIST_DIRECTORIES true "/usr/lib/llvm-*/lib")
    file(TO_CMAKE_PATH "$ENV{ProgramFiles}" luma_program_files)
    find_file(luma_libclang_found NAMES libclang.dll libclang.so libclang.so.1 libclang.dylib
              HINTS "${luma_program_files}/LLVM/bin" "$ENV{LLVM_ROOT}/bin" "$ENV{LLVM_ROOT}/lib" ${luma_libclang_hints}
                    /opt/homebrew/opt/llvm/lib /usr/local/opt/llvm/lib /usr/lib /usr/lib64
              NO_CMAKE_FIND_ROOT_PATH NO_DEFAULT_PATH)
    if(NOT luma_libclang_found)
        message(FATAL_ERROR "The row generator needs the build machine's libclang (the cross compiler has none beside it): "
                            "install LLVM or set NEBULA_LIBCLANG to libclang.dll, libclang.so or libclang.dylib")
    endif()
    set(NEBULA_LIBCLANG "${luma_libclang_found}" CACHE FILEPATH "The build machine's libclang for the row generator" FORCE)
endif()
if(NEBULA_LIBCLANG)
    set(luma_libclang --libclang "${NEBULA_LIBCLANG}")
endif()
set(module_sources)
set(luma_defined_lists)
foreach(source IN LISTS game_sources)
    if(NEBULA_LUMA_HOOKS AND source MATCHES "\\.cpp$" AND NOT source MATCHES "/System/(Luma(Hook|Loader|Types|Numbers)\\.cpp$|generated/)")
        file(RELATIVE_PATH relative "${petari}/src" "${source}")
        get_filename_component(folder "${source}" DIRECTORY)
        set(copy "${CMAKE_CURRENT_BINARY_DIR}/luma/${relative}")
        add_custom_command(OUTPUT "${copy}"
            BYPRODUCTS "${copy}.globals" "${copy}.types" "${copy}.rows" "${copy}.functions" "${copy}.numbers"
            COMMAND "${Python3_EXECUTABLE}" "${luma_tool}" "${source}" "${copy}"
                    --commands "${CMAKE_BINARY_DIR}/compile_commands.json" --root "${petari}/src" --depfile "${copy}.d"
                    ${luma_libclang}
            DEPENDS "${source}" "${luma_tool}" "${luma_types_tool}" DEPFILE "${copy}.d" VERBATIM)
        set_source_files_properties("${copy}" PROPERTIES COMPILE_OPTIONS "-iquote${folder}")
        list(APPEND module_sources "${copy}")
        list(APPEND luma_defined_lists "${copy}.globals" "${copy}.types" "${copy}.rows" "${copy}.functions" "${copy}.numbers")
    else()
        list(APPEND module_sources "${source}")
    endif()
endforeach()

# The type rows (contract types, plan 1.2): every record, field, base, alias and global of the game's headers, from one
# libclang parse of all of them with a game unit's command (tools/lumacore/gen_types.py). The numbers are the
# compiler's own: the table is compiled here, with no access check, since offsetof names private fields. A global gets a
# row only when a unit defines it (the copies' .globals lists); what a .cpp defines is in its copy's .types.
if(NEBULA_LUMA_HOOKS)
    set(luma_types "${CMAKE_CURRENT_BINARY_DIR}/luma_types")
    add_custom_command(OUTPUT "${luma_types}/luma_types_table.cpp"
        BYPRODUCTS "${luma_types}/luma_types.json" "${luma_types}/luma_types.left" "${luma_types}/luma_functions.json"
                   "${luma_types}/luma_numbers.json"
        COMMAND "${Python3_EXECUTABLE}" "${luma_types_tool}" --commands "${CMAKE_BINARY_DIR}/compile_commands.json"
                --out "${luma_types}" --defined "${CMAKE_CURRENT_BINARY_DIR}/luma" --depfile "${luma_types}/luma_types_table.d"
                ${luma_libclang}
        DEPENDS "${luma_types_tool}" "${luma_tool}" ${luma_defined_lists} DEPFILE "${luma_types}/luma_types_table.d" VERBATIM)
    set_source_files_properties("${luma_types}/luma_types_table.cpp" PROPERTIES COMPILE_OPTIONS "-Xclang;-fno-access-control")
    list(APPEND module_sources "${luma_types}/luma_types_table.cpp")
    # The module's rows against the generator's, after each link; any difference fails the build, naming the row. A
    # cross build cannot run its module here: the test run compares (tools/lumacore/luma_test.py).
    if(NOT CMAKE_CROSSCOMPILING)
        add_executable(luma_describe tools/luma_describe.c)
        target_include_directories(luma_describe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(luma_describe PRIVATE ${CMAKE_DL_LIBS})
        add_custom_command(OUTPUT "${luma_types}/luma_types_described.json"
            COMMAND luma_describe "$<TARGET_FILE:smg_game>" "${luma_types}/luma_types_described.json"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tools/lumacore/compare_types.py"
                    "${luma_types}/luma_types.json" "${luma_types}/luma_types_described.json"
            DEPENDS smg_game luma_describe "${luma_types}/luma_types.json"
                    "${CMAKE_CURRENT_SOURCE_DIR}/../tools/lumacore/compare_types.py" "${luma_types_tool}"
            VERBATIM)
        add_custom_target(luma_types_check ALL DEPENDS "${luma_types}/luma_types_described.json")
    endif()
endif()

add_library(smg_game SHARED
    ${module_sources}
    math/src/nebula_math.c
    math/src/nebula_libm.c
    math/src/nebula_libc.c
    $<$<PLATFORM_ID:Windows>:math/src/nebula_runtime_win.c>)

if(WIN32)
    enable_language(RC)
    include(../cmake/nebula_file_details.cmake)
    nebula_file_details(smg_game smg_game.dll "Super Mario Galaxy on PC, the game")
endif()

target_include_directories(smg_game BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/math/include"
    "${petari}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(version_ids RMGJ01=0 RMGE01=1 RMGP01=2 RMGK01=3)
string(REGEX MATCH "${NEBULA_GAME_VERSION}=([0-9])" _ "${version_ids}")
target_compile_definitions(smg_game PRIVATE VERSION=${CMAKE_MATCH_1} ${version_ids} NEBULA_GAME_MODULE=1 _HAS_EXCEPTIONS=0)

set_target_properties(smg_game PROPERTIES
    C_STANDARD 11 C_EXTENSIONS ON
    CXX_STANDARD 17 CXX_EXTENSIONS ON
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
    # find_function (a native plugin's hook) finds the game's functions by name.
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
# Only to measure what those exports cost, or to train the profile (its counters cannot be exported): find_function finds nothing in such a build.
option(NEBULA_NO_PLUGIN_EXPORTS "Leave the game's functions unexported" OFF)
if(NEBULA_NO_PLUGIN_EXPORTS OR CMAKE_CXX_FLAGS MATCHES "fprofile-instr-generate")
    set_target_properties(smg_game PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
elseif(NOT WIN32)
    # The same on Linux and Mac: what Windows does with its export list, these do with visibility.
    set_target_properties(smg_game PROPERTIES C_VISIBILITY_PRESET default CXX_VISIBILITY_PRESET default VISIBILITY_INLINES_HIDDEN OFF)
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

# The game builds warning free: a new warning stops the build. The names turned off are the ones still to be cleared
# (rows in helpers/warnings_b11.tsv); offsetof, register, pragmas, constructor initializer order and unused private
# fields are on.
# An audit build replaces all of it, e.g. -Wshorten-64-to-32 for tools/narrowing.py.
set(NEBULA_AUDIT_FLAGS "" CACHE STRING "Diagnostics for an audit build")
if(NEBULA_AUDIT_FLAGS)
    separate_arguments(diagnostics UNIX_COMMAND "${NEBULA_AUDIT_FLAGS}")
else()
    set(diagnostics -Wall -Wextra -Werror -Wconditional-uninitialized
        -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare
        -Wno-ignored-qualifiers -Wno-microsoft-anon-tag
        -Wno-deprecated-copy-with-user-provided-copy -Wno-undefined-var-template -D_CRT_SECURE_NO_WARNINGS)
endif()

# docs/NATIVE_PORT.md, "Build of the game code", has the reason for each, and docs/NATIVE_PERFORMANCE.md the cost:
# -fno-math-errno (the game never reads errno; sqrtf and friends stay single instructions), -fno-stack-protector
# (pinned off for every toolchain: distribution compilers turn it on by default).
# The same language on every platform: clang enables Microsoft extensions by default only when targeting Windows
# (__declspec, a static definition after an extern declaration); glibc's GNU interfaces (dladdr, memfd_create, the
# signal context's registers) are declared only under _GNU_SOURCE, which must precede the prelude's first include.
target_compile_definitions(smg_game PRIVATE $<$<PLATFORM_ID:Linux,Android>:_GNU_SOURCE>
    # Apple's headers turn memcpy, strcpy and the like into checking macros that call the system's library: the module
    # defines those functions itself (math/src/nebula_libc.c) and calls its own.
    $<$<PLATFORM_ID:Darwin,iOS>:_FORTIFY_SOURCE=0>)
target_compile_options(smg_game PRIVATE
    -ffp-contract=off  # the same results on every CPU (A-10)
    $<$<NOT:$<PLATFORM_ID:Windows>>:-fms-extensions>
    -fno-strict-aliasing -fno-strict-float-cast-overflow -fno-math-errno -fno-stack-protector
    $<$<NOT:$<PLATFORM_ID:Windows,Darwin,iOS>>:-fno-plt>
    -fno-ms-compatibility -fno-delayed-template-parsing
    ${diagnostics}
    -Werror=return-type -Werror=pointer-to-int-cast -Werror=void-pointer-to-int-cast -Werror=format
    -Werror=int-to-pointer-cast -Werror=pointer-to-enum-cast
    -Werror=implicit-function-declaration -Werror=implicit-int -Werror=int-conversion
    -Werror=incompatible-function-pointer-types -Werror=return-stack-address -Werror=array-bounds
    # The module has no C runtime: a stack frame of 4 KiB or more makes the compiler call a stack probe (__chkstk on
    # Windows), which nothing defines, and the link fails with no word of where. This stops it at compile, in the
    # function that did it. clang counts the frame without the return address; it probes from 4096, so 4095 is exact.
    -Wframe-larger-than=4095 -Werror=frame-larger-than
    "SHELL:-include nebula_prelude.h"
    # Source paths in the module (assertion messages) relative to the tree, not the build machine.
    "-ffile-prefix-map=${petari}=petari" "-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=native"
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-c++-static-destructors -fno-sized-deallocation -fno-aligned-allocation>
    # The module with rows carries RTTI data, so the kernel finds an object's own class from its vtable (contract objects,
    # plan 1.3); the game never uses typeid on an object or dynamic_cast. The module without rows has none.
    $<$<COMPILE_LANGUAGE:CXX>:$<IF:$<BOOL:${NEBULA_LUMA_HOOKS}>,-frtti,-fno-rtti>>
    # Every new-expression tests the allocation before constructing: the arenas' operator new returns null when the
    # system has no memory left, and the game checks. Standard C++ lets the compiler drop those checks for an operator new that
    # is not noexcept, which would run a constructor on null.
    $<$<COMPILE_LANGUAGE:CXX>:-fcheck-new>
    # The Microsoft C++ ABI sizes a member pointer by its class's inheritance, known only where the class is complete;
    # a unit seeing just a declaration would use another size than its callers.
    $<$<AND:$<PLATFORM_ID:Windows>,$<COMPILE_LANGUAGE:CXX>>:-fcomplete-member-pointers>
    -Wno-c++11-narrowing)

set_source_files_properties(math/src/nebula_libm.c math/src/nebula_libc.c math/src/nebula_runtime_win.c PROPERTIES COMPILE_OPTIONS -fno-builtin)
target_compile_definitions(smg_game PRIVATE _USE_STD_VECTOR_ALGORITHMS=0 _ACRTIMP=)
# No system library: everything the module uses it defines or asks the host for. A sanitizer's runtime and the profile counters' need the C runtime.
if(WIN32 AND NOT NEBULA_SANITIZE AND NOT CMAKE_CXX_FLAGS MATCHES "fprofile-instr-generate")
    target_link_options(smg_game PRIVATE -nostdlib "LINKER:/NOENTRY" "LINKER:/NODEFAULTLIB")
endif()

if(WIN32)
    # The DLL's own operator new/delete are NebulaArena's; the host process keeps its own.
    target_link_options(smg_game PRIVATE -fuse-ld=lld "LINKER:/errorlimit:0")
elseif(APPLE)
    # Mach-O binds a library's calls to its own functions already (two-level names). The allocation operators and the
    # C++ library's own symbols stay inside: dyld would hand an exported operator new to every image that binds one weakly.
    target_link_options(smg_game PRIVATE -Wl,-bind_at_load
        -Wl,-unexported_symbol,__Znw* -Wl,-unexported_symbol,__Zna* -Wl,-unexported_symbol,__Zdl* -Wl,-unexported_symbol,__Zda*
        -Wl,-unexported_symbol,__ZNSt* -Wl,-unexported_symbol,__ZNKSt* -Wl,-unexported_symbol,__ZSt*
        -Wl,-unexported_symbol,__ZT?St* -Wl,-unexported_symbol,__ZT?NSt* -Wl,-unexported_symbol,___cxa* -Wl,-unexported_symbol,___gxx*)
else()
    target_link_options(smg_game PRIVATE -Wl,-Bsymbolic -Wl,-z,now)
    # A symbol the module names and nothing defines (a class's type_info whose key function has no body: contract
    # objects M-2) fails the link here, as the NDK's own flags make it fail on Android, never the game's start. A
    # sanitizer's runtime is the executable's, so its build leaves them to the loader.
    if(NOT ANDROID AND NOT NEBULA_SANITIZE)
        target_link_options(smg_game PRIVATE -Wl,--no-undefined)
    endif()
    if(NOT ANDROID)  # Android's C library holds the threads
        target_link_libraries(smg_game PRIVATE pthread)
    endif()
endif()

# A build for players: ThinLTO with the profile of a tour (native/pgo/game.profdata, tools/verify/pgo_profile.sh): the game's CPU falls about
# 17 % (docs/NATIVE_PERFORMANCE.md). A profile older than the code still applies to what has not changed.
option(NEBULA_PGO "Build the game with ThinLTO and its profile" OFF)
if(NEBULA_PGO)
    set(game_profile "${CMAKE_CURRENT_SOURCE_DIR}/pgo/game.profdata")
    target_compile_options(smg_game PRIVATE -flto=thin "-fprofile-instr-use=${game_profile}"
                           -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-backend-plugin)
    target_link_options(smg_game PRIVATE -flto=thin "-fprofile-instr-use=${game_profile}")
endif()

# CPU level. The Windows package needs AVX2 (Setup checks it; the host is built for it), and every AVX2 CPU also has
# FMA3, BMI1, BMI2, F16C, LZCNT and MOVBE: the module is built for that level, x86-64-v3, not the x86-64 baseline.
# -ffp-contract=off (above) keeps every multiply and add a separate rounding with FMA3 present: the results stay
# those of the baseline build.
if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|amd64)$")
    set(nebula_cpu_level_default "x86-64-v3")
else()
    set(nebula_cpu_level_default "")
endif()
set(NEBULA_CPU_LEVEL "${nebula_cpu_level_default}" CACHE STRING "Game module CPU level (-march): x86-64-v3, x86-64, or empty for the compiler's default")
if(NEBULA_CPU_LEVEL)
    if(NOT NEBULA_CPU_LEVEL MATCHES "^x86-64(-v[234])?$")
        message(FATAL_ERROR "NEBULA_CPU_LEVEL must be x86-64, x86-64-v2, x86-64-v3, x86-64-v4 or empty")
    endif()
    target_compile_options(smg_game PRIVATE -march=${NEBULA_CPU_LEVEL})
endif()
message(STATUS "Nebula game module CPU level: ${NEBULA_CPU_LEVEL}")

# Sanitizer builds (tests only): e.g. -DNEBULA_SANITIZE=address for the module and the host.
set(NEBULA_SANITIZE "" CACHE STRING "Sanitizers for a test build (clang -fsanitize=...)")
if(NEBULA_SANITIZE)
    target_compile_options(smg_game PRIVATE -fsanitize=${NEBULA_SANITIZE} -fno-omit-frame-pointer)
    # The module's operator new/delete are NebulaArena's; the sanitizer runtime's own definitions give way.
    target_link_options(smg_game PRIVATE -fsanitize=${NEBULA_SANITIZE} $<$<PLATFORM_ID:Windows>:LINKER:/force:multiple>)
    if(NEBULA_SANITIZE MATCHES "address")
        # The game's arenas manage their own memory: built uninstrumented, they keep what is free poisoned and
        # what they hand out (NebulaArena.cpp), so overflows and uses after free inside them are reported too.
        set_source_files_properties("${petari}/src/Game/System/NebulaArena.cpp" PROPERTIES COMPILE_OPTIONS "-fno-sanitize=address"
                                    COMPILE_DEFINITIONS NEBULA_ASAN_HEAPS=1)
    endif()
endif()

# The native game's host (host/host_main.cpp): loads the module and serves it. Built with the host's own C++ library.
# The mods' virtual disc (galaxy::mods) serves this host's disc reads as the windowed host's.
# How a plugin replaces a game function on Windows.
set(nebula_minhook_sources)
if(NOT WIN32)
    set(nebula_minhook_sources ../runtime/src/mod_hook_posix.cpp)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        list(APPEND nebula_minhook_sources ../third_party/minhook/src/hde/hde64.c)
        set_source_files_properties(../third_party/minhook/src/hde/hde64.c PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include/galaxy/hde_shim")
    endif()
endif()
if(WIN32)
    set(nebula_minhook_dir "${CMAKE_CURRENT_LIST_DIR}/../third_party/minhook")
    set(nebula_minhook_sources "${nebula_minhook_dir}/src/buffer.c" "${nebula_minhook_dir}/src/hook.c" "${nebula_minhook_dir}/src/trampoline.c" "${nebula_minhook_dir}/src/hde/hde64.c")
endif()
add_executable(nebula_native_host host/host_main.cpp ../runtime/src/host_extension.cpp ../runtime/src/luma_services_host.cpp ../runtime/src/luma_system.cpp ../runtime/src/luma_mods.cpp
    ../runtime/src/mod_log.cpp ../runtime/src/mod_start_check.cpp ../runtime/src/mod_plugins.cpp ${nebula_minhook_sources}
    ../runtime/src/luma.cpp ../runtime/src/mods.cpp ../runtime/src/mod_consent.cpp ../runtime/src/texture_categories.cpp ../runtime/src/dolphin_pack.cpp ../runtime/src/mod_xml.cpp ../runtime/src/atomic_file.cpp ../runtime/src/save_files.cpp ../runtime/src/asset_store.cpp
    ../runtime/src/crash_report.cpp ../runtime/src/discord_presence.cpp ../runtime/src/mii_import.cpp
    ../runtime/src/mii_formats.cpp ../runtime/src/pointer_control.cpp
    # The start check asks whether the compiler of mods' programs is there (mod_start_check.cpp): the same answer as
    # the windowed host's on Windows (dxc beside the program) and Apple (none yet); elsewhere Vulkan's, whose HLSL
    # front end is part of the program (mod_shader_present_vk.cpp, without the front end itself).
    $<$<PLATFORM_ID:Windows>:../runtime/src/render/mod_shader_compile_d3d12.cpp>
    $<$<PLATFORM_ID:Darwin,iOS>:../runtime/src/render/mtl/mod_shader_compile_mtl.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows,Darwin,iOS>>:../runtime/src/render/vk/mod_shader_present_vk.cpp>)
target_include_directories(nebula_native_host PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/minhook/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include")
target_compile_features(nebula_native_host PRIVATE cxx_std_20)
target_compile_definitions(nebula_native_host PRIVATE _CRT_SECURE_NO_WARNINGS)
add_dependencies(nebula_native_host smg_game)
if(NEBULA_SANITIZE)
    target_compile_options(nebula_native_host PRIVATE -fsanitize=${NEBULA_SANITIZE} -fno-omit-frame-pointer)
    target_link_options(nebula_native_host PRIVATE -fsanitize=${NEBULA_SANITIZE})
endif()
