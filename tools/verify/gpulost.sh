#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_SAVES (an empty saves folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_SAVES:?NEBULA_TEST_SAVES is not set}"
# gpulost.sh BIN TAG TIMES SECONDS: the card lost at frame 900 and every 300 after; the game goes on with a new device twice, a third time ends in the report.
V=$(cd "$(dirname "$0")" && pwd)
B=$NEBULA_BINS/$1; T=$2
OUT=$NEBULA_RUNS/gl_$T; rm -rf "$OUT"; mkdir -p "$OUT"; W=$(cygpath -w "$OUT")
cp -r $NEBULA_TEST_SAVES "$OUT/saves"
export VERIFY_DIR=$V NEBULA_WORK="$OUT" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_SAVES="$W\saves"
. $V/common.sh; VERIFY=$V
run_build "$B" g ${4:-40} GALAXY_TEST_GPU_LOST_FRAME=900 GALAXY_TEST_GPU_LOST_TIMES=$3 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000 >/dev/null 2>&1
L="$RUNS/g/run.log"
echo "$T: lost $(grep -c 'graphics card was reset' "$L"), new device $(grep -c 'a new device in' "$L"), crash lines $(grep -ci 'crash:' "$L"), ended $(grep -c 'the game ended' "$L"), codes $(grep -o 'NEB-[A-Z][0-9]*' "$L" | sort -u | tr '\n' ' ')"
grep -E 'graphics card was reset|a new device in|DEVICE REMOVED' "$L" | cut -c1-150 | head -8
