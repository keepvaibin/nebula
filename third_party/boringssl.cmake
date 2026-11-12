# BoringSSL (third_party/boringssl, pinned in NEBULA-PIN.txt) as two static libraries: nebula_bssl_crypto and
# nebula_bssl_ssl. The net services' TLS (net 1.3) and the loader's mod signatures use them; one copy on every platform.
# Included by each build that links them (the game's, the native host's, the SDL host's); include_guard keeps one set
# of targets a build folder.
#
# Assembly: Windows x86-64 uses the NASM files when NASM is found, else OPENSSL_NO_ASM (C only); every other platform
# assembles the .S files with its C compiler (each file is guarded for its own OS and CPU). The configure log says
# which ("BoringSSL ...: assembly ...").
include_guard(GLOBAL)

set(nebula_bssl_dir "${CMAKE_CURRENT_LIST_DIR}/boringssl")
include("${nebula_bssl_dir}/gen/sources.cmake")
foreach(list_name BCM_SOURCES BCM_SOURCES_ASM BCM_SOURCES_NASM CRYPTO_SOURCES CRYPTO_SOURCES_ASM CRYPTO_SOURCES_NASM
        SSL_SOURCES)
    list(TRANSFORM ${list_name} PREPEND "${nebula_bssl_dir}/")
endforeach()

set(nebula_bssl_asm "")
set(nebula_bssl_asm_kind "none: OPENSSL_NO_ASM")
if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|amd64)$" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        find_program(NEBULA_NASM NAMES nasm
            HINTS "$ENV{ProgramFiles}/NASM" "$ENV{LOCALAPPDATA}/bin/NASM" DOC "NASM for BoringSSL's x86-64 assembly")
        if(NEBULA_NASM)
            set(CMAKE_ASM_NASM_COMPILER "${NEBULA_NASM}")
            enable_language(ASM_NASM)
            set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -gcv8 -I${nebula_bssl_dir}/gen/")
            set(nebula_bssl_asm ${BCM_SOURCES_NASM} ${CRYPTO_SOURCES_NASM})
            set(nebula_bssl_asm_kind "NASM (${NEBULA_NASM})")
        else()
            set(nebula_bssl_asm_kind "none: OPENSSL_NO_ASM (no NASM found)")
        endif()
    endif()
else()
    enable_language(ASM)
    set(nebula_bssl_asm ${BCM_SOURCES_ASM} ${CRYPTO_SOURCES_ASM})
    set(nebula_bssl_asm_kind ".S files (${CMAKE_ASM_COMPILER_ID})")
endif()
message(STATUS "BoringSSL 0.20261020.0: assembly ${nebula_bssl_asm_kind}")

add_library(nebula_bssl_crypto STATIC ${BCM_SOURCES} ${CRYPTO_SOURCES} ${nebula_bssl_asm})
add_library(nebula_bssl_ssl STATIC ${SSL_SOURCES})
target_link_libraries(nebula_bssl_ssl PUBLIC nebula_bssl_crypto)
target_include_directories(nebula_bssl_crypto SYSTEM PUBLIC "${nebula_bssl_dir}/include")
# Its C++ headers (openssl/span.h) need C++17 in every file that includes them.
target_compile_features(nebula_bssl_crypto PUBLIC cxx_std_17)
if(nebula_bssl_asm STREQUAL "")
    # Public: the headers read it too (one setting for the library and its users).
    target_compile_definitions(nebula_bssl_crypto PUBLIC OPENSSL_NO_ASM)
endif()
foreach(target nebula_bssl_crypto nebula_bssl_ssl)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(${target} PRIVATE BORINGSSL_IMPLEMENTATION)
    if(MSVC)
        # Third-party code: its warnings are upstream's (/w); the game's targets keep /W4 /WX.
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/w /utf-8>
            $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
    else()
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:-w -fno-strict-aliasing -fno-common
            -fvisibility=hidden> $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
    endif()
    if(WIN32)
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # pthread_rwlock_t on Linux needs it (upstream sets it for Linux only).
        target_compile_definitions(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:_XOPEN_SOURCE=700>)
    endif()
endforeach()
if(WIN32)
    target_link_libraries(nebula_bssl_crypto PUBLIC ws2_32)
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    find_package(Threads REQUIRED)
    target_link_libraries(nebula_bssl_crypto PUBLIC Threads::Threads)
endif()
if(NOT WIN32 AND NOT APPLE)
    target_compile_options(nebula_bssl_crypto PRIVATE $<$<COMPILE_LANGUAGE:ASM>:-Wa,--noexecstack>)
endif()

# Mozilla's root list (third_party/mozilla-roots/cacert.pem, THIRD-PARTY-NOTICES.md) built into the program, so it
# ships and changes with the game on every platform: nebula_net_roots gives net_roots.inc with
# `static const unsigned char kNetRoots[]`, the file's bytes. Users of nebula_bssl_ssl get it.
set(nebula_roots_pem "${CMAKE_CURRENT_LIST_DIR}/mozilla-roots/cacert.pem")
set(nebula_roots_inc "${CMAKE_BINARY_DIR}/nebula-net/net_roots.inc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${nebula_roots_pem}")
if(NOT EXISTS "${nebula_roots_inc}" OR "${nebula_roots_pem}" IS_NEWER_THAN "${nebula_roots_inc}")
    file(READ "${nebula_roots_pem}" nebula_roots_hex HEX)
    # 16 bytes a line (32 hex digits; CMake's expressions have no {n}), then each byte as 0x.., .
    string(REGEX REPLACE "(................................)" "\\1\n" nebula_roots_hex "${nebula_roots_hex}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," nebula_roots_hex "${nebula_roots_hex}")
    file(WRITE "${nebula_roots_inc}.part"
        "// Generated by third_party/boringssl.cmake from third_party/mozilla-roots/cacert.pem; not edited by hand.\n"
        "static const unsigned char kNetRoots[] = {\n${nebula_roots_hex}};\n")
    file(RENAME "${nebula_roots_inc}.part" "${nebula_roots_inc}")
endif()
add_library(nebula_net_roots INTERFACE)
target_include_directories(nebula_net_roots INTERFACE "${CMAKE_BINARY_DIR}/nebula-net")
target_link_libraries(nebula_bssl_ssl PUBLIC nebula_net_roots)
