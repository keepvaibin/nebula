# zlib's inflate alone (third_party/archive/zlib, 1.3.1) as the static library nebula_zinflate: the net services'
# gzip answers (net 1.3 http). Built with Z_PREFIX, so its names are z_inflate... and never meet the mods importer's
# full zlib (archive_deps.cmake) in one program; a user defines Z_PREFIX before it includes zlib.h. Included by each
# build that links it; include_guard keeps one target a build folder.
include_guard(GLOBAL)

set(nebula_zlib_dir "${CMAKE_CURRENT_LIST_DIR}/archive/zlib")
# zlib's own zconf.h (the source tree keeps it as zconf.h.included), beside nothing else of the build's.
configure_file("${nebula_zlib_dir}/zconf.h.included" "${CMAKE_BINARY_DIR}/nebula-zinflate/zconf.h" COPYONLY)
add_library(nebula_zinflate STATIC
    "${nebula_zlib_dir}/adler32.c" "${nebula_zlib_dir}/crc32.c" "${nebula_zlib_dir}/inffast.c"
    "${nebula_zlib_dir}/inflate.c" "${nebula_zlib_dir}/inftrees.c" "${nebula_zlib_dir}/zutil.c")
target_include_directories(nebula_zinflate SYSTEM PUBLIC "${CMAKE_BINARY_DIR}/nebula-zinflate" "${nebula_zlib_dir}")
# Z_PREFIX renames zlib's functions and types, but not its one data symbol: zutil.c's z_errmsg (zconf.h has no line
# for it), which then met the importer's zlibstatic.lib in the host's link (int0400: LNK2005 z_errmsg already
# defined). Renamed here for this library's own sources; no user of zlib.h names it (zutil.h is zlib's own).
target_compile_definitions(nebula_zinflate PRIVATE Z_PREFIX z_errmsg=nebula_zinflate_errmsg)
set_target_properties(nebula_zinflate PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(nebula_zinflate PRIVATE /w)
    target_compile_definitions(nebula_zinflate PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # No HAVE_UNISTD_H: its users' zlib.h sees none either, so z_off_t is one type on both sides.
    target_compile_options(nebula_zinflate PRIVATE -w)
endif()
