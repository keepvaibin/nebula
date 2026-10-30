#!/bin/sh
# save.sh BASE_BIN NEW_BIN TAG: a save written and loaded by each build. In a copy of NEBULA_NAND with no save, the
# build clears Egg Star Galaxy's star and answers Yes to the save prompt (the pointer on Yes, A every 90 frames); the
# save must then be GameData.bin of 48,640 bytes. The build then boots that save and presses A+B at the title: the
# file select, which shows File 1 with Mario, is compared between the builds (align_frames.py, identity).
. "$(dirname "$0")/common.sh"
base=$1
new=$2
tag=$3
presses=""
frame=3600
while [ $frame -le 4950 ]; do
    presses="$presses$frame:10:A;"
    frame=$((frame + 90))
done
status=0
i=0
for bin in "$base" "$new"; do
    nand="$NEBULA_WORK/nand_${tag}_$i"
    rm -rf "$nand"
    cp -r "$(cygpath -u "$NEBULA_NAND")" "$nand"
    find "$nand" -name GameData.bin -delete
    NEBULA_NAND="$(cygpath -w "$nand")" run_build "$bin" "s_${tag}_${i}_save" 140 \
        GALAXY_TEST_GALAXY_TOUR="$(tour_file tour_one_EggStarGalaxy.txt)" GALAXY_TEST_GALAXY_TOUR_FRAMES=900 \
        GALAXY_TEST_EVENTS=star@600/wait@8000 "GALAXY_INPUT_AUTOPRESS_SCRIPT=${presses%;}" \
        GALAXY_INPUT_POINTER_X=0.28 GALAXY_INPUT_POINTER_Y=0.6 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000
    save=$(find "$nand" -name GameData.bin)
    size=$( [ -n "$save" ] && wc -c < "$save" || echo 0)
    echo "$tag build $i: GameData.bin $size bytes"
    [ "$size" -eq 48640 ] || status=1
    NEBULA_NAND="$(cygpath -w "$nand")" run_build "$bin" "s_${tag}_${i}_load" 45 \
        GALAXY_INPUT_AUTOPRESS_SCRIPT=760:30:A+B GALAXY_PRESENT_SNAPSHOT_EVERY=1 \
        GALAXY_PRESENT_SNAPSHOT_FIRST=4700 GALAXY_PRESENT_SNAPSHOT_LAST=4800
    grep -q "presence: Choosing a file" "$RUNS/s_${tag}_${i}_load/run.log" || { echo "$tag build $i: no file select"; status=1; }
    i=$((i + 1))
done
echo "$tag file select: $(python -I "$VERIFY/align_frames.py" "$RUNS/s_${tag}_0_load" "$RUNS/s_${tag}_1_load" - - 40 | tail -1)"
exit $status
