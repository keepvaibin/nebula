#!/bin/sh
# galaxy.sh BASE_BIN NEW_BIN TAG FIRST LAST GALAXY...: window.sh's comparison at each galaxy's first star (one-stop tour
# tours/tour_one_<Galaxy>.txt; its gameplay at presents ~2380-2440, the run held there by a wait event as window.sh's).
# SHIFT, MARGIN (default 200), MODE as in window.sh.
. "$(dirname "$0")/common.sh"
base=$1
new=$2
tag=$3
first=$4
last=$5
shift 5
shift_by=${SHIFT:-0}
margin=${MARGIN:-200}
tool=align_frames.py
[ "${MODE:-identity}" = "tolerance" ] && tool=align_tol.py
for g in "$@"; do
    if [ ! -f "$VERIFY/tours/tour_one_$g.txt" ]; then
        echo "$g: no tour file tours/tour_one_$g.txt"
        continue
    fi
    common="GALAXY_TEST_GALAXY_TOUR=$(tour_file "tour_one_$g.txt") GALAXY_TEST_GALAXY_TOUR_FRAMES=900 GALAXY_TEST_EVENTS=wait@5400 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1"
    # shellcheck disable=SC2086
    run_build "$base" "g_${tag}_${g}_0" 75 $common GALAXY_PRESENT_SNAPSHOT_FIRST=$first GALAXY_PRESENT_SNAPSHOT_LAST=$last
    # shellcheck disable=SC2086
    run_build "$new" "g_${tag}_${g}_1" 75 $common GALAXY_PRESENT_SNAPSHOT_FIRST=$((first - shift_by - margin)) \
        GALAXY_PRESENT_SNAPSHOT_LAST=$((last - shift_by + margin))
    echo "$g: crash $(grep -ci 'crash:' "$RUNS/g_${tag}_${g}_1/run.log") $(python -I "$VERIFY/$tool" "$RUNS/g_${tag}_${g}_0" "$RUNS/g_${tag}_${g}_1" - - $((shift_by + margin)) | tail -1)"
done
