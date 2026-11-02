#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_NAND (an empty NAND folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_NAND:?NEBULA_TEST_NAND is not set}"
# extended.sh BIN TAG: what the gate does not cover: the ending to its save prompt, Luigi, a slow drive through the stops. Crash lines, loss counters, and how far each got.
V=$(cd "$(dirname "$0")" && pwd)
BN=$1; T=$2; B=$NEBULA_BINS/$BN
OUT=$NEBULA_RUNS/ext_$T; mkdir -p "$OUT"
one() { # name seconds env...
  N=$1; S=$2; shift 2
  (
    W0="$OUT/$N"; rm -rf "$W0"; mkdir -p "$W0"; W=$(cygpath -w "$W0"); cp -r $NEBULA_TEST_NAND "$W0/nand"
    export VERIFY_DIR=$V NEBULA_WORK="$W0" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_NAND="$W\nand"
    . $V/common.sh; VERIFY=$V
    run_build "$B" r $S "$@" GALAXY_PRESENT_SNAPSHOT_EVERY=1000000 >/dev/null 2>&1
    L="$RUNS/r/run.log"
    echo "$T $N: crash $(grep -ci 'crash:' "$L") dumps $(ls "$RUNS"/r/*.dmp 2>/dev/null | wc -l) last $(grep -a 'presence:' "$L" | tail -1 | sed 's/^.*presence: //' | cut -c1-40) | $(grep -a '\[loss\] draws' "$L" | tail -1 | cut -c8-90) | movies $(grep -c 'movie' "$L") ended $(grep -c 'the game ended' "$L")"
  )
}
tourfile() { cygpath -w $(cd "$(dirname "$0")" && pwd)/tours/$1; }
one ending 900 GALAXY_TEST_GALAXY_TOUR="$(tourfile tour_one_KoopaBattleVs3Galaxy.txt)" GALAXY_TEST_GALAXY_TOUR_FRAMES=900 GALAXY_TEST_EVENTS=grandstar@2400/wait@48000 GALAXY_MAX_FPS=60 &
one luigi 90 GALAXY_TEST_GALAXY_TOUR="$(tourfile tour_one_EggStarGalaxy.txt)" GALAXY_TEST_GALAXY_TOUR_FRAMES=2400 GALAXY_TEST_EVENTS=luigi@60/kill@240 GALAXY_MAX_FPS=60 &
one slowdisc 900 GALAXY_TEST_GALAXY_TOUR="$(cygpath -w $V/tours/tour_enterable.txt)" GALAXY_TEST_GALAXY_TOUR_FRAMES=90 GALAXY_TEST_DISC_DELAY_US=4000 GALAXY_MAX_FPS=60 &
wait
