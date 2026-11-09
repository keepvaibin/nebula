#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path, NebulaNative.exe and NebulaNativeVk.exe), NEBULA_RUNS (output), NEBULA_TEST_SAVES (an empty saves folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_SAVES:?NEBULA_TEST_SAVES is not set}"
# stopcross.sh BIN TAG [FRAMES] [EVERY] [LIMIT]: one build on D3D12 and on Vulkan, deterministic, through all 102 stops; a
# picture every EVERY frames; stopcross.py ranks the differences (exit 1 when the worst picture is over LIMIT pixels past 8 levels).
V=$(cd "$(dirname "$0")" && pwd)
B=$NEBULA_BINS/$1; T=$2; FR=${3:-300}; EV=${4:-100}; LIMIT=$5
F=$(cygpath -w $V/tours/tour_enterable.txt)
for side in dx vk; do
    exe=NebulaNative.exe; [ $side = vk ] && exe=NebulaNativeVk.exe
    (
        OUT=$NEBULA_RUNS/cross_$T/$side
        rm -rf "$OUT"; mkdir -p "$OUT"; W=$(cygpath -w "$OUT")
        cp -r $NEBULA_TEST_SAVES "$OUT/saves"
        export VERIFY_DIR=$V NEBULA_WORK="$OUT" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_SAVES="$W\saves" NEBULA_EXE=$exe
        . $V/common.sh; VERIFY=$V
        run_build "$B" fp 1500 GALAXY_TEST_GALAXY_TOUR="$F" GALAXY_TEST_GALAXY_TOUR_FRAMES=$FR GALAXY_TEST_DETERMINISTIC=1 GALAXY_MAX_FPS=60 \
            GALAXY_PRESENT_SNAPSHOT_EVERY=$EV GALAXY_PRESENT_SNAPSHOT_FIRST=100 GALAXY_PRESENT_SNAPSHOT_LAST=100000 >/dev/null 2>&1
    ) &
done
wait
R=$NEBULA_RUNS/cross_$T
for side in dx vk; do echo "$side: crash $(grep -ci 'crash:' $R/$side/runs/fp/run.log) frames $(ls $R/$side/runs/fp/present_*.ppm 2>/dev/null | grep -vc ".display") $(grep -c 'tour\b\|\[tour\]' $R/$side/runs/fp/run.log) lines; $(tail -1 $R/$side/runs/fp/run.log | cut -c1-80)"; done
python -I $V/stopcross.py $R $LIMIT
