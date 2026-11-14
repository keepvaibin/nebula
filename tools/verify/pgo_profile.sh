#!/bin/sh
# pgo_profile.sh SOURCE_TREE WORK HOST_BIN PAK: the game's profile from the 102-stop tour, into SOURCE_TREE/native/pgo/game.profdata: the game built with
# -fprofile-instr-generate (WORK/build), each stop 300 frames, llvm-profdata merge. HOST_BIN holds a host of the same API (NebulaNative.exe, tables).
# Run before a release when much of the game has changed; the next -Pgo build takes it.
T=$1
W=$2
H=$3
P=$4
V=$(cd "$(dirname "$0")" && pwd)
LLVM="${LLVM_BIN:-/c/Program Files/LLVM/bin}"
B="$W/bin"
mkdir -p "$B/tables" "$W/raw"
rm -f "$W"/raw/*.profraw
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$T/native/build.ps1" -Petari "$T/native/petari" -Build "$W/build" \
    -ProfileFlags -fprofile-instr-generate -NoBaseline || exit 1
cp "$H/NebulaNative.exe" "$B/"
cp "$H"/tables/* "$B/tables/"
cp "$W/build/smg_game.dll" "$B/"
printf '%s' "$P" > "$B/pak.path"
WW=$(cygpath -w "$W")
[ -d "$W/saves" ] || { echo "WORK/saves: give a test save folder"; exit 1; }
NEBULA_WORK="$W" NEBULA_PAK="$P" NEBULA_SETTINGS="$WW\\settings" NEBULA_SAVES="$WW\\saves" LLVM_PROFILE_FILE="$WW\\raw\\game-%p.profraw" \
    sh "$V/tour.sh" "$B" t GALAXY_TEST_GALAXY_TOUR_FRAMES=300
mkdir -p "$T/native/pgo"
"$LLVM/llvm-profdata.exe" merge -o "$T/native/pgo/game.profdata" "$W"/raw/*.profraw
ls -la "$T/native/pgo/game.profdata"
