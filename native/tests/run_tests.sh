#!/bin/sh
# Builds and runs the native layer's tests (the game's threads, the math library, the text strings) with the game
# module's flags.
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
    MINGW*|MSYS*|CYGWIN*) LIBS="-lwinmm"; EXE=.exe ;;
    *) LIBS="-lpthread"; EXE= ;;
esac
OS="$P/src/Game/System/GameHostThreads.c $P/src/Game/System/NebulaMemory.c $P/src/Game/System/NebulaModule.c $P/src/Game/System/NebulaFormat.c"
# The kernel and its test are C++: the C flags but the language.
CXXFL=$(echo "$FL" | sed 's/-std=gnu11/-std=gnu++17/; s/-Werror=implicit-function-declaration//; s/-Werror=implicit-int//;
    s/-Werror=incompatible-function-pointer-types//; s/-Werror=int-conversion//')
mkdir -p "$OUT/obj"
for f in $OS; do
    $CC $FL -c "$f" -o "$OUT/obj/$(basename "$f").o"
done
${CC}++ $CXXFL -c "$P/src/Game/System/GameThreads.cpp" -o "$OUT/obj/nebula_kernel.o"
${CC}++ $CXXFL "$N/tests/thread_tests.cpp" "$OUT"/obj/*.o $LIBS -o "$OUT/thread_tests$EXE"
$CC $FL "$N/math/tests/math_tests.c" "$N/math/src/nebula_math.c" -o "$OUT/math_tests$EXE"
${CC}++ $CXXFL "$N/tests/text_string_tests.cpp" "$P/src/Game/Util/TextString.cpp" -o "$OUT/text_string_tests$EXE"
"$OUT/thread_tests$EXE"
"$OUT/math_tests$EXE"
"$OUT/text_string_tests$EXE"
