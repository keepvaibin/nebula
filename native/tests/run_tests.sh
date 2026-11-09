#!/bin/sh
# Builds and runs the native layer's tests (the math library, the text strings, the UI names, the formatted names
# and the name index) with the game module's flags.
#   native/tests/run_tests.sh <petari checkout> <output dir> [clang]
set -e
P=$1
OUT=$2
CC=${3:-clang}
N=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT"
FL="-std=gnu11 -O2 -g -DVERSION=1 -DNEBULA_GAME_MODULE=1 -fsigned-char -fno-strict-aliasing
    -fno-strict-float-cast-overflow -fno-math-errno -Wno-everything -Werror=return-type
    -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast -Werror=void-pointer-to-int-cast -Werror=pointer-to-enum-cast
    -Werror=implicit-function-declaration -Werror=implicit-int -Werror=int-conversion
    -Werror=incompatible-function-pointer-types -Werror=return-stack-address -Werror=array-bounds
    -include nebula_prelude.h -I$N/math/include
    -I$P/include -I$N/include"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EXE=.exe ;;
    *) EXE= ;;
esac
# The text string test is C++: the C flags but the language.
CXXFL=$(echo "$FL" | sed 's/-std=gnu11/-std=gnu++17/; s/-Werror=implicit-function-declaration//; s/-Werror=implicit-int//;
    s/-Werror=incompatible-function-pointer-types//; s/-Werror=int-conversion//')
$CC $FL "$N/math/tests/math_tests.c" "$N/math/src/nebula_math.c" -o "$OUT/math_tests$EXE"
${CC}++ $CXXFL "$N/tests/text_string_tests.cpp" "$P/src/Game/Util/TextString.cpp" -o "$OUT/text_string_tests$EXE"
# NameText is header-only. StringUtil.cpp is linked with the few game functions its test defines; the linker drops the
# sections nothing in the test reaches.
${CC}++ $CXXFL "$N/tests/name_text_tests.cpp" -o "$OUT/name_text_tests$EXE"
$CC $FL -c "$P/src/Game/System/NebulaFormat.c" -o "$OUT/NebulaFormat.o"
${CC}++ $CXXFL -ffunction-sections -fdata-sections "$N/tests/formatted_name_tests.cpp" "$P/src/Game/Util/StringUtil.cpp" "$P/src/Game/Util/TextString.cpp" "$OUT/NebulaFormat.o" -Wl,--gc-sections -o "$OUT/formatted_name_tests$EXE"
${CC}++ $CXXFL "$N/tests/name_index_tests.cpp" "$P/src/Game/Util/HashUtil.cpp" -o "$OUT/name_index_tests$EXE"
"$OUT/math_tests$EXE"
"$OUT/text_string_tests$EXE"
"$OUT/name_text_tests$EXE"
"$OUT/formatted_name_tests$EXE"
"$OUT/name_index_tests$EXE"
