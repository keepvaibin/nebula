# AV1 decoding for movies (plan 3.10: a mod's movie cooked to NMOV with AV1 frames, galaxy/render/movie_playback.h):
# dav1d 1.5.1 from third_party/dav1d (BSD-2-Clause, see THIRD-PARTY-NOTICES.md), 8-bit only, built in-tree as the
# static library `nebula_dav1d`. Its C functions only for now: the hand-written x86 (NASM) and Arm (.S) kernels are a
# later speed step and are not vendored yet. config.h is written here from the target, as dav1d's meson would.

set(nebula_dav1d_root "${CMAKE_CURRENT_LIST_DIR}/../third_party/dav1d")
set(nebula_dav1d_gen "${CMAKE_BINARY_DIR}/dav1d-gen")

set(nebula_dav1d_x86 0)
set(nebula_dav1d_x86_64 0)
set(nebula_dav1d_aarch64 0)
set(nebula_dav1d_arm 0)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" nebula_dav1d_cpu)
if(CMAKE_OSX_ARCHITECTURES)
    string(TOLOWER "${CMAKE_OSX_ARCHITECTURES}" nebula_dav1d_cpu)
endif()
if(ANDROID_ABI)
    string(TOLOWER "${ANDROID_ABI}" nebula_dav1d_cpu)
endif()
if(nebula_dav1d_cpu MATCHES "^(x86_64|amd64|x64)$")
    set(nebula_dav1d_x86 1)
    set(nebula_dav1d_x86_64 1)
elseif(nebula_dav1d_cpu MATCHES "^(x86|i[3-6]86)$")
    set(nebula_dav1d_x86 1)
elseif(nebula_dav1d_cpu MATCHES "^(aarch64|arm64|arm64-v8a)$")
    set(nebula_dav1d_aarch64 1)
elseif(nebula_dav1d_cpu MATCHES "^(arm|armv7|armeabi-v7a)")
    set(nebula_dav1d_arm 1)
endif()

set(nebula_dav1d_unix 1)
set(nebula_dav1d_io 0)
set(nebula_dav1d_windows_lines "")
if(WIN32)
    set(nebula_dav1d_unix 0)
    set(nebula_dav1d_io 1)
    set(nebula_dav1d_windows_lines "#define _WIN32_WINNT 0x0601\n#define UNICODE 1\n#define _UNICODE 1\n#define _CRT_DECLARE_NONSTDC_NAMES 1\n")
endif()

file(MAKE_DIRECTORY "${nebula_dav1d_gen}")
set(nebula_dav1d_config "/* written by cmake/dav1d.cmake */
#pragma once
${nebula_dav1d_windows_lines}#define ARCH_AARCH64 ${nebula_dav1d_aarch64}
#define ARCH_ARM ${nebula_dav1d_arm}
#define ARCH_LOONGARCH 0
#define ARCH_LOONGARCH32 0
#define ARCH_LOONGARCH64 0
#define ARCH_PPC64LE 0
#define ARCH_RISCV 0
#define ARCH_RV32 0
#define ARCH_RV64 0
#define ARCH_X86 ${nebula_dav1d_x86}
#define ARCH_X86_32 0
#define ARCH_X86_64 ${nebula_dav1d_x86_64}
#define CONFIG_8BPC 1
#define CONFIG_16BPC 0
#define CONFIG_LOG 0
#define CONFIG_MACOS_KPERF 0
#define ENDIANNESS_BIG 0
#define HAVE_ASM 0
#define HAVE_AS_FUNC 0
#define HAVE_AS_ARCH_DIRECTIVE 0
#define HAVE_C11_GENERIC 1
#define HAVE_CLOCK_GETTIME ${nebula_dav1d_unix}
#define HAVE_DLSYM 0
#define HAVE_POSIX_MEMALIGN ${nebula_dav1d_unix}
#define HAVE_MEMALIGN 0
#define HAVE_ALIGNED_ALLOC 0
#define HAVE_GETAUXVAL 0
#define HAVE_ELF_AUX_INFO 0
#define HAVE_IO_H ${nebula_dav1d_io}
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H ${nebula_dav1d_unix}
#define HAVE_PTHREAD_NP_H 0
#define HAVE_PTHREAD_GETAFFINITY_NP 0
#define HAVE_PTHREAD_SETAFFINITY_NP 0
#define HAVE_PTHREAD_SETNAME_NP 0
#define HAVE_PTHREAD_SET_NAME_NP 0
#define TRIM_DSP_FUNCTIONS 0
")
file(CONFIGURE OUTPUT "${nebula_dav1d_gen}/config.h" CONTENT "${nebula_dav1d_config}")
file(CONFIGURE OUTPUT "${nebula_dav1d_gen}/vcs_version.h" CONTENT "/* written by cmake/dav1d.cmake */\n#define DAV1D_VERSION \"1.5.1\"\n")

set(nebula_dav1d_sources
    cdf.c cpu.c ctx.c data.c decode.c dequant_tables.c getbits.c intra_edge.c itx_1d.c lf_mask.c lib.c log.c mem.c
    msac.c obu.c pal.c picture.c qm.c ref.c refmvs.c scan.c tables.c thread_task.c warpmv.c wedge.c)
set(nebula_dav1d_bitdepth_sources
    cdef_apply_tmpl.c cdef_tmpl.c fg_apply_tmpl.c filmgrain_tmpl.c ipred_prepare_tmpl.c ipred_tmpl.c itx_tmpl.c
    lf_apply_tmpl.c loopfilter_tmpl.c looprestoration_tmpl.c lr_apply_tmpl.c mc_tmpl.c recon_tmpl.c)
list(TRANSFORM nebula_dav1d_sources PREPEND "${nebula_dav1d_root}/src/")
list(TRANSFORM nebula_dav1d_bitdepth_sources PREPEND "${nebula_dav1d_root}/src/")
if(WIN32)
    list(APPEND nebula_dav1d_sources "${nebula_dav1d_root}/src/win32/thread.c")
endif()

set(nebula_dav1d_includes "${nebula_dav1d_gen}" "${nebula_dav1d_root}" "${nebula_dav1d_root}/include")
if(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    list(APPEND nebula_dav1d_includes "${nebula_dav1d_root}/include/compat/msvc")
else()
    list(APPEND nebula_dav1d_includes "${nebula_dav1d_root}/include/compat/gcc")
endif()

function(nebula_dav1d_flags target)
    target_include_directories(${target} PRIVATE ${nebula_dav1d_includes})
    target_compile_definitions(${target} PRIVATE _FILE_OFFSET_BITS=64)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        # dav1d's meson for MSVC: its own warnings off, the CRT's "secure" nags off.
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
        target_compile_options(${target} PRIVATE /W0 /O2)
    else()
        target_compile_options(${target} PRIVATE -w -O2 -fvisibility=hidden)
        if(NOT WIN32)
            target_compile_definitions(${target} PRIVATE _GNU_SOURCE)
        endif()
    endif()
endfunction()

add_library(nebula_dav1d_8bpc OBJECT ${nebula_dav1d_bitdepth_sources})
nebula_dav1d_flags(nebula_dav1d_8bpc)
target_compile_definitions(nebula_dav1d_8bpc PRIVATE BITDEPTH=8)

add_library(nebula_dav1d STATIC ${nebula_dav1d_sources} $<TARGET_OBJECTS:nebula_dav1d_8bpc>)
nebula_dav1d_flags(nebula_dav1d)
target_include_directories(nebula_dav1d SYSTEM INTERFACE "${nebula_dav1d_root}/include")
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(nebula_dav1d PUBLIC Threads::Threads)
endif()
