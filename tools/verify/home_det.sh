#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_NAND (an empty NAND folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_NAND:?NEBULA_TEST_NAND is not set}"
# home_det.sh OLD NEW TAG OLD_PRESS NEW_PRESS: HOME open and closed, deterministic, Battlerock window; the press per build (B-13 moved the game one frame).
V=$(cd "$(dirname "$0")" && pwd)
O=$1; N=$2; T=$3; PO=$4; PN=$5
for side in old new; do
  bin=$O; press=$PO; [ $side = new ] && bin=$N && press=$PN
  (
    B=$NEBULA_BINS/$bin
    OUT=$NEBULA_RUNS/hd_$T/$side
    rm -rf "$OUT"; mkdir -p "$OUT"; W=$(cygpath -w "$OUT"); cp -r $NEBULA_TEST_NAND "$OUT/nand"
    export VERIFY_DIR=$V NEBULA_WORK="$OUT" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_NAND="$W\nand"
    . $V/common.sh; VERIFY=$V
    run_build "$B" fp 75 GALAXY_TEST_GALAXY_TOUR="$(tour_file tour_battle1.txt)" GALAXY_TEST_GALAXY_TOUR_FRAMES=900 GALAXY_TEST_EVENTS=wait@5400 GALAXY_MAX_FPS=60 GALAXY_TEST_DETERMINISTIC=1 GALAXY_INPUT_AUTOPRESS_SCRIPT=$press:5:HOME GALAXY_TEST_HOME_ROW=0:240 GALAXY_PRESENT_SNAPSHOT_EVERY=1 GALAXY_PRESENT_SNAPSHOT_FIRST=2380 GALAXY_PRESENT_SNAPSHOT_LAST=2680 >/dev/null 2>&1
  ) &
done
wait
R=$NEBULA_RUNS/hd_$T
python -I $V/align_frames.py $R/old/runs/fp $R/new/runs/fp - - 5 | tail -1 | cut -c1-130
