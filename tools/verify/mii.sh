#!/bin/sh
# mii.sh BASE_BIN NEW_BIN TAG [FIRST LAST]: the Mii picker in both builds (1280x720, 1x, 60 fps, 60 s): A+B at the
# title (frame 760), A twice on the first file, the pointer moved onto "Yes" (frame 1800) before the third press, the
# scripted input on the game-frame clock so the presses meet the same frames in every run. NEBULA_MII_NAND names a
# test NAND holding at least two Miis (each build runs on its own copy). Every present from FIRST to LAST (default
# 2380-2420) is snapshotted; the picker's backdrop turns on its own clock, so the test is the two Mii icons
# (region_ab.py; a build against itself: max 13, at most 6 pixels over 8). The whole picture is printed for reference.
. "$(dirname "$0")/common.sh"
: "${NEBULA_MII_NAND:?NEBULA_MII_NAND is not set}"
base=$1
new=$2
tag=$3
first=${4:-2380}
last=${5:-2420}
i=0
for bin in "$base" "$new"; do
    nand="$NEBULA_WORK/nand_mii_${tag}_$i"
    rm -rf "$nand"
    cp -r "$(cygpath -u "$NEBULA_MII_NAND")" "$nand"
    # shellcheck disable=SC2086
    NEBULA_NAND="$(cygpath -w "$nand")" run_build "$bin" "mii_${tag}_$i" 60 GALAXY_MAX_FPS=60 \
        GALAXY_TEST_SCRIPT_CLOCK=frames 'GALAXY_INPUT_AUTOPRESS_SCRIPT=760:30:A+B;1300:10:A;1700:10:A;1900:10:A' \
        GALAXY_INPUT_POINTER_X=-0.26 GALAXY_INPUT_POINTER_Y=0.3 GALAXY_INPUT_POINTER2_X=0.28 \
        GALAXY_INPUT_POINTER2_Y=0.6 GALAXY_INPUT_POINTER2_VI=1800 GALAXY_PRESENT_SNAPSHOT_EVERY=1 \
        GALAXY_PRESENT_SNAPSHOT_FIRST=$first GALAXY_PRESENT_SNAPSHOT_LAST=$last $EXTRA
    i=$((i + 1))
done
echo "$tag Mii picker: crash $(grep -ci 'crash:' "$RUNS/mii_${tag}_1/run.log") $(python -I "$VERIFY/align_frames.py" "$RUNS/mii_${tag}_0" "$RUNS/mii_${tag}_1" $((first + 4)) $((last - 4)) | tail -1)"
echo "$tag Mii icons: $(python -I "$VERIFY/region_ab.py" "$RUNS/mii_${tag}_0" "$RUNS/mii_${tag}_1" $((first + 10)) $((last - 10)) 255,335,345,440 465,565,340,445)"
