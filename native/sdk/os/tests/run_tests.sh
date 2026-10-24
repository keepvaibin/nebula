#!/bin/sh
# Builds and runs the native layer's tests (context switch, the SDK scheduler on the native core, the MTX library,
# the MSL runtime) with the game module's C flags.
#   native/sdk/os/tests/run_tests.sh <petari checkout> <output dir> [clang]
set -e
P=$1
OUT=$2
CC=${3:-clang}
N=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$OUT"
FL="-std=gnu11 -O2 -g -DTARGET_PC=1 -DVERSION=1 -DNEBULA_GAME_MODULE=1 -fsigned-char -fshort-wchar -fno-strict-aliasing
    -fno-strict-float-cast-overflow -fno-math-errno -Wno-everything -Werror=return-type
    -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast -Werror=void-pointer-to-int-cast -Werror=pointer-to-enum-cast
    -Werror=implicit-function-declaration -Werror=implicit-int -Werror=int-conversion
    -Werror=incompatible-function-pointer-types -Werror=return-stack-address -Werror=array-bounds
    -include nebula_prelude.h -I$N/msl/include -I$P/libs/RVL_SDK/include -I$P/include -I$N/sdk -I$N/include"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) LIBS="-lwinmm"; EXE=.exe ;;
    *) LIBS="-lpthread"; EXE= ;;
esac
OS="$N/sdk/os/os_native.c $N/sdk/os/context_switch.c $N/sdk/os/divide_native.c $N/sdk/memory.c $N/msl/src/msl_runtime.c"
SDK="$P/src/RVL_SDK/os/OSThread.c $P/src/RVL_SDK/os/OSMutex.c $P/src/RVL_SDK/os/OSMessage.c $P/src/RVL_SDK/os/OSAlarm.c
     $P/src/RVL_SDK/os/OSTime.c $P/src/RVL_SDK/os/OSArena.c"
$CC $FL "$N/sdk/os/tests/context_switch_tests.c" "$N/sdk/os/context_switch.c" -o "$OUT/context_switch_tests$EXE"
$CC $FL "$N/sdk/os/tests/os_scheduler_tests.c" $OS $SDK $LIBS -o "$OUT/os_scheduler_tests$EXE"
$CC $FL "$N/sdk/mtx/tests/mtx_tests.c" "$N/sdk/mtx/mtx_native.c" -o "$OUT/mtx_tests$EXE"
$CC $FL "$N/msl/tests/msl_runtime_tests.c" "$N/msl/src/msl_runtime.c" -o "$OUT/msl_runtime_tests$EXE"
"$OUT/context_switch_tests$EXE"
"$OUT/os_scheduler_tests$EXE"
"$OUT/mtx_tests$EXE"
"$OUT/msl_runtime_tests$EXE"
