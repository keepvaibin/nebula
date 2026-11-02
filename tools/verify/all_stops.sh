#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_NAND (an empty NAND folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_NAND:?NEBULA_TEST_NAND is not set}"
# all_stops.sh OLD NEW TAG [FRAMES] [EVERY]: both builds, deterministic, through all 102 stops; a picture every EVERY frames; compare_stops.py ranks the differences.
V=$(cd "$(dirname "$0")" && pwd)
OLD=$1; NEW=$2; T=$3; FR=${4:-300}; EV=${5:-100}
F=$(cygpath -w $V/tours/tour_enterable.txt)
for side in old new; do
    bin=$OLD; [ $side = new ] && bin=$NEW
    (
        B=$NEBULA_BINS/$bin
        OUT=$NEBULA_RUNS/stops_$T/$side
        rm -rf "$OUT"; mkdir -p "$OUT"; W=$(cygpath -w "$OUT")
        cp -r $NEBULA_TEST_NAND "$OUT/nand"
        export VERIFY_DIR=$V NEBULA_WORK="$OUT" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_NAND="$W\nand"
        . $V/common.sh; VERIFY=$V
        run_build "$B" fp 1500 GALAXY_TEST_GALAXY_TOUR="$F" GALAXY_TEST_GALAXY_TOUR_FRAMES=$FR GALAXY_TEST_DETERMINISTIC=1 GALAXY_MAX_FPS=60 \
            GALAXY_PRESENT_SNAPSHOT_EVERY=$EV GALAXY_PRESENT_SNAPSHOT_FIRST=100 GALAXY_PRESENT_SNAPSHOT_LAST=100000 >/dev/null 2>&1
    ) &
done
wait
R=$NEBULA_RUNS/stops_$T
for side in old new; do echo "$side: crash $(grep -ci 'crash:' $R/$side/runs/fp/run.log) frames $(ls $R/$side/runs/fp/present_*.ppm 2>/dev/null | grep -vc xfb) $(grep -c 'tour\b\|\[tour\]' $R/$side/runs/fp/run.log) lines; $(tail -1 $R/$side/runs/fp/run.log | cut -c1-80)"; done
python -I $V/compare_stops.py $R
