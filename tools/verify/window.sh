#!/bin/sh
# window.sh BASE_BIN NEW_BIN TAG FIRST LAST [SHIFT]: both builds at the Battlerock stop (1280x720, 1x, 60 fps, 75 s),
# every present from FIRST to LAST snapshotted (the new build's widened by SHIFT and MARGIN, default 200: present counts before the window vary by up to ~140 between runs of one build, with
# present count before the window moved), then the frame-aligned comparison: align_frames.py (identity, the default)
# or, with MODE=tolerance, align_tol.py (differences meant to be a step or two). Extra NAME=value settings in EXTRA.
. "$(dirname "$0")/common.sh"
base=$1
new=$2
tag=$3
first=$4
last=$5
shift_by=${6:-0}
margin=${MARGIN:-200}
common="GALAXY_TEST_GALAXY_TOUR=$(tour_file tour_battle1.txt) GALAXY_TEST_GALAXY_TOUR_FRAMES=900 GALAXY_TEST_EVENTS=wait@5400 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1"
# shellcheck disable=SC2086
run_build "$base" "w_${tag}_0" 75 $common GALAXY_PRESENT_SNAPSHOT_FIRST=$first GALAXY_PRESENT_SNAPSHOT_LAST=$last $EXTRA
# shellcheck disable=SC2086
run_build "$new" "w_${tag}_1" 75 $common GALAXY_PRESENT_SNAPSHOT_FIRST=$((first - shift_by - margin)) \
    GALAXY_PRESENT_SNAPSHOT_LAST=$((last - shift_by + margin)) $EXTRA
tool=align_frames.py
[ "${MODE:-identity}" = "tolerance" ] && tool=align_tol.py
echo "$tag: crash $(grep -ci 'crash:' "$RUNS/w_${tag}_1/run.log") $(python -I "$VERIFY/$tool" "$RUNS/w_${tag}_0" "$RUNS/w_${tag}_1" - - $((shift_by + margin)) | tail -1)"
