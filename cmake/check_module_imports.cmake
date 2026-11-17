# The game module's imports, checked at its link (native/CMakeLists.txt, every platform but Windows, whose module has no
# C runtime to import from): no rounded math function may come from the system's library. The game's math is its own
# (native/math/src/nebula_libm.c), the same bits on every platform; one call answered by the system's library (Android's
# sincosf, which the compiler made of a sinf and a cosf of one angle) gives another platform other results, and the
# game steps of a device no longer match the PC's.
#   cmake -DNM=<nm> -DMODULE=<the linked module> -P check_module_imports.cmake
if(NOT NM OR NOT MODULE)
    message(FATAL_ERROR "check_module_imports.cmake needs -DNM and -DMODULE")
endif()
execute_process(COMMAND "${NM}" -u "${MODULE}" OUTPUT_VARIABLE listed ERROR_VARIABLE said RESULT_VARIABLE code)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "${NM} -u ${MODULE} failed (${code}): ${said}")
endif()
string(REPLACE "\n" ";" lines "${listed}")
set(found "")
foreach(line IN LISTS lines)
    # ELF: "                 U sincosf@LIBC"; Mach-O: "___sincosf_stret".
    string(REGEX REPLACE "^[ \t]*U[ \t]+" "" name "${line}")
    string(STRIP "${name}" name)
    string(REGEX REPLACE "@.*$" "" name "${name}")
    string(REGEX REPLACE "^_+" "" name "${name}")
    # The rounded functions only: an exact one (sqrt, floor, ceil, trunc, round, fmod, fma) gives the same bits from
    # any library.
    if(name MATCHES "^(sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|asinh|acosh|atanh|sincos|sincospi|sinpi|cospi|tanpi|pow|exp|exp2|exp10|expm1|log|log2|log10|log1p|cbrt|hypot|erf|erfc|tgamma|lgamma)(f|l)?(_stret)?$")
        list(APPEND found "${name}")
    endif()
endforeach()
if(found)
    list(JOIN found ", " names)
    message(FATAL_ERROR "${MODULE} takes math from the system's library: ${names}. The game's math is its own "
                        "(native/math/src/nebula_libm.c): name the function in native/CMakeLists.txt's game_math list.")
endif()
