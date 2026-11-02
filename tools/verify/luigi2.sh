#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_NAND (an empty NAND folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_NAND:?NEBULA_TEST_NAND is not set}"
# luigi2.sh BIN TAG: Luigi, back to Mario, Luigi again, each with a death so the stage reloads; both ways through the stationed heap.
V=$(cd "$(dirname "$0")" && pwd)
B=$NEBULA_BINS/$1; T=$2
W0=$NEBULA_RUNS/lg_$T; rm -rf "$W0"; mkdir -p "$W0"; W=$(cygpath -w "$W0"); cp -r $NEBULA_TEST_NAND "$W0/nand"
export VERIFY_DIR=$V NEBULA_WORK="$W0" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_NAND="$W\nand"
. $V/common.sh; VERIFY=$V
F=$(cygpath -w $(cd "$(dirname "$0")" && pwd)/tours/tour_one_EggStarGalaxy.txt)
run_build "$B" r 120 GALAXY_TEST_GALAXY_TOUR="$F" GALAXY_TEST_GALAXY_TOUR_FRAMES=2400 GALAXY_TEST_EVENTS=luigi@60/kill@240/mario@1500/kill@1700/luigi@3000/kill@3200 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000 >/dev/null 2>&1
L="$RUNS/r/run.log"
echo "$T $1: crash $(grep -ci 'crash:' "$L") $(grep -a 'NEB-' "$L" | tail -1 | cut -c10-60) | $(grep -a 'out of memory:' "$RUNS"/r/*.txt 2>/dev/null | tail -1 | cut -c50-130) | $(tail -1 "$L" | cut -c1-50)"
