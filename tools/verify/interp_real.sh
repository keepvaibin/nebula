#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_NAND (an empty NAND folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_NAND:?NEBULA_TEST_NAND is not set}"
# interp_real.sh OLD NEW TAG [FRAMES] [EVERY]: the pictures the game makes, with interpolation off and on. OLD runs the stops at 60
# fps (no generated frames); NEW runs them at 120 (2x: one generated frame between two game frames). NEW and OLD may be the same
# build. Both capture every EVERY-th present with GALAXY_PRESENT_SNAPSHOT_GAME_ONLY=1, which neither captures nor counts a
# generated present, so the numbers match: the game frames of the second run are compared with the first's, picture by picture
# (compare_stops.py: pictures identical, the worst by pixels), then the offset-free align_frames.py over every picture kept.
# run.log lines "[snapshot] present=N qpc=T kind=game|repeat|generated" tell each present's kind; the number of
# generated ones in NEW is printed, so a run that made none (interpolation not on) is not mistaken for a pass.
V=$(cd "$(dirname "$0")" && pwd)
OLD=$1; NEW=$2; T=$3; FR=${4:-300}; EV=${5:-100}
F=$(cygpath -w $V/tours/tour_enterable.txt)
for side in old new; do
    bin=$OLD; fps=60
    [ $side = new ] && bin=$NEW && fps=120
    (
        B=$NEBULA_BINS/$bin
        OUT=$NEBULA_RUNS/interp_$T/$side
        rm -rf "$OUT"; mkdir -p "$OUT"; W=$(cygpath -w "$OUT")
        cp -r $NEBULA_TEST_NAND "$OUT/nand"
        export VERIFY_DIR=$V NEBULA_WORK="$OUT" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_NAND="$W\nand"
        . $V/common.sh; VERIFY=$V
        run_build "$B" fp 1500 GALAXY_TEST_GALAXY_TOUR="$F" GALAXY_TEST_GALAXY_TOUR_FRAMES=$FR GALAXY_TEST_DETERMINISTIC=1 GALAXY_MAX_FPS=$fps \
            GALAXY_PRESENT_SNAPSHOT_GAME_ONLY=1 GALAXY_PRESENT_SNAPSHOT_EVERY=$EV GALAXY_PRESENT_SNAPSHOT_FIRST=100 GALAXY_PRESENT_SNAPSHOT_LAST=100000 >/dev/null 2>&1
    ) &
done
wait
R=$NEBULA_RUNS/interp_$T
for side in old new; do
    L=$R/$side/runs/fp/run.log
    echo "$side: crash $(grep -ci 'crash:' $L) pictures $(ls $R/$side/runs/fp/present_*.ppm 2>/dev/null | grep -vc xfb) game $(grep -c 'kind=game' $L) repeat $(grep -c 'kind=repeat' $L) generated-left-out-lines $(grep -c "generated presents left out" $L)"
done
python -I $V/compare_stops.py $R
python -I $V/align_frames.py $R/old/runs/fp $R/new/runs/fp - - 2
