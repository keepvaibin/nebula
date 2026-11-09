#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_SAVES (an empty saves folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_SAVES:?NEBULA_TEST_SAVES is not set}"
# luigi.sh BIN TAG [SECONDS]: the stage reloads as Luigi; crash lines and the memory line.
V=$(cd "$(dirname "$0")" && pwd)
B=$NEBULA_BINS/$1; T=$2; S=${3:-90}; G=${4:-EggStarGalaxy}
W0=$NEBULA_RUNS/lg_$T; rm -rf "$W0"; mkdir -p "$W0"; W=$(cygpath -w "$W0"); cp -r $NEBULA_TEST_SAVES "$W0/saves"
export VERIFY_DIR=$V NEBULA_WORK="$W0" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_SAVES="$W\saves"
. $V/common.sh; VERIFY=$V
F=$(cygpath -w $(cd "$(dirname "$0")" && pwd)/tours/tour_one_$G.txt)
run_build "$B" r $S GALAXY_TEST_GALAXY_TOUR="$F" GALAXY_TEST_GALAXY_TOUR_FRAMES=2400 GALAXY_TEST_EVENTS=luigi@60/kill@240 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000 >/dev/null 2>&1
L="$RUNS/r/run.log"
echo "$T $1: crash $(grep -ci 'crash:' "$L") $(grep -a 'NEB-' "$L" | tail -1 | cut -c10-60) | $(grep -a 'out of memory:' "$RUNS"/r/*.txt 2>/dev/null | tail -1 | cut -c50-130) | $(tail -1 "$L" | cut -c1-40)"
