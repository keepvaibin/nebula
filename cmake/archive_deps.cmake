# Archive reading for the mods importer (zip, 7z, RAR4/RAR5, tar): libarchive with zlib and liblzma, all three from
# third_party/ (see THIRD-PARTY-NOTICES.md). They are built and installed into the build tree at configure time with
# their own build systems, once per build tree: libarchive's configure checks need real library files, which in-tree
# targets are not. The result is the `nebula_archive` target (static libraries, no DLLs).
#
# NEBULA_ARCHIVE_SOURCES names the folder holding zlib/, xz/ and libarchive/ (third_party/archive by default).

set(NEBULA_ARCHIVE_SOURCES "${CMAKE_SOURCE_DIR}/third_party/archive" CACHE PATH
    "zlib, xz and libarchive sources for the mods importer")
set(NEBULA_ARCHIVE_PREFIX "${CMAKE_BINARY_DIR}/archive-deps")

function(nebula_archive_dep name source)
    set(stamp "${NEBULA_ARCHIVE_PREFIX}/${name}.built")
    if(EXISTS "${stamp}")
        return()
    endif()
    set(build "${CMAKE_BINARY_DIR}/archive-deps-build/${name}")
    message(STATUS "Building ${name} for the mods importer")
    set(toolchain)
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND toolchain "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
        list(APPEND toolchain "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND toolchain "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${build}" -G "${CMAKE_GENERATOR}"
                "-DCMAKE_BUILD_TYPE=Release"
                "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
                "-DCMAKE_INSTALL_PREFIX=${NEBULA_ARCHIVE_PREFIX}"
                "-DCMAKE_PREFIX_PATH=${NEBULA_ARCHIVE_PREFIX}"
                "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
                "-DBUILD_SHARED_LIBS=OFF"
                ${toolchain} ${ARGN}
        RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Configuring ${name} failed:\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Building ${name} failed:\n${output}\n${errors}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --install "${build}" --config Release
                    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Installing ${name} failed:\n${errors}")
    endif()
    file(WRITE "${stamp}" "${source}\n")
endfunction()

if(WIN32)
    set(nebula_zlib_library "${NEBULA_ARCHIVE_PREFIX}/lib/zlibstatic.lib")
    set(nebula_lzma_library "${NEBULA_ARCHIVE_PREFIX}/lib/lzma.lib")
    set(nebula_archive_library "${NEBULA_ARCHIVE_PREFIX}/lib/archive.lib")
else()
    set(nebula_zlib_library "${NEBULA_ARCHIVE_PREFIX}/lib/libz.a")
    set(nebula_lzma_library "${NEBULA_ARCHIVE_PREFIX}/lib/liblzma.a")
    set(nebula_archive_library "${NEBULA_ARCHIVE_PREFIX}/lib/libarchive.a")
endif()

nebula_archive_dep(zlib "${NEBULA_ARCHIVE_SOURCES}/zlib" -DZLIB_BUILD_EXAMPLES=OFF)
nebula_archive_dep(xz "${NEBULA_ARCHIVE_SOURCES}/xz"
    -DXZ_NLS=OFF -DXZ_DOC=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF -DXZ_TOOL_LZMAINFO=OFF
    -DXZ_TOOL_SCRIPTS=OFF -DXZ_TOOL_SYMLINKS=OFF -DXZ_TOOL_SYMLINKS_LZMA=OFF -DBUILD_TESTING=OFF)
nebula_archive_dep(libarchive "${NEBULA_ARCHIVE_SOURCES}/libarchive"
    -DENABLE_TEST=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF
    -DENABLE_INSTALL=ON -DENABLE_WERROR=OFF
    -DENABLE_OPENSSL=OFF -DENABLE_MBEDTLS=OFF -DENABLE_NETTLE=OFF -DENABLE_CNG=OFF -DENABLE_LIBB2=OFF
    -DENABLE_LZ4=OFF -DENABLE_LZO=OFF -DENABLE_ZSTD=OFF -DENABLE_BZip2=OFF -DENABLE_LIBXML2=OFF
    -DENABLE_EXPAT=OFF -DENABLE_PCREPOSIX=OFF -DENABLE_PCRE2POSIX=OFF -DENABLE_LIBGCC=OFF -DENABLE_ICONV=OFF
    -DENABLE_ACL=OFF -DENABLE_XATTR=OFF -DENABLE_ZLIB=ON -DENABLE_LZMA=ON -DENABLE_WIN32_XMLLITE=OFF
    -DPOSIX_REGEX_LIB=NONE
    "-DZLIB_INCLUDE_DIR=${NEBULA_ARCHIVE_PREFIX}/include" "-DZLIB_LIBRARY=${nebula_zlib_library}"
    "-DLIBLZMA_INCLUDE_DIR=${NEBULA_ARCHIVE_PREFIX}/include" "-DLIBLZMA_LIBRARY=${nebula_lzma_library}")

add_library(nebula_archive INTERFACE)
target_include_directories(nebula_archive INTERFACE "${NEBULA_ARCHIVE_PREFIX}/include")
target_compile_definitions(nebula_archive INTERFACE LIBARCHIVE_STATIC)
target_link_libraries(nebula_archive INTERFACE
    "${nebula_archive_library}" "${nebula_lzma_library}" "${nebula_zlib_library}")
if(WIN32)
    # libarchive's Windows code: advapi32 (security), bcrypt (random numbers), xmllite is off.
    target_link_libraries(nebula_archive INTERFACE advapi32 bcrypt)
endif()
