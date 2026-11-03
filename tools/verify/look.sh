#!/bin/sh
# look.sh BIN TAG: BIN through the tour with pictures kept, look_audit.py against the fixed reference (build n12: a bug already there does not show), then look_check.py: every flagged stop needs a line of a person or agent who looked at its sheet.
# Sheets and pictures in $NEBULA_WORK/look_TAG; the lines in agents/look-reviews/TAG.txt. REF=folder replaces the reference; CONTROL=folder its control.
B=$1
T=$2
REF=${REF:-D:/NebulaWork/port/reference/n12/dx/runs/fp}
CONTROL=${CONTROL:-D:/NebulaWork/port/reference/n12/dx_control/runs/fp}
DIR=$(cd "$(dirname "$0")" && pwd)
OUT=$NEBULA_WORK/look_$T
rm -rf "$OUT"
mkdir -p "$OUT"
sh "$DIR/tour.sh" "$B" "${T}_look" GALAXY_TEST_GALAXY_TOUR_FRAMES=300 GALAXY_TEST_DETERMINISTIC=1 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=100 GALAXY_PRESENT_SNAPSHOT_FIRST=100 GALAXY_PRESENT_SNAPSHOT_LAST=100000 > "$OUT/tour.out" 2>&1
tail -n 1 "$OUT/tour.out"
python -I "$DIR/look_audit.py" "$REF" "$NEBULA_WORK/runs/${T}_look" "$OUT" --control "$CONTROL" > "$OUT/audit.txt" 2>&1
grep -E "^control|^pictures" "$OUT/audit.txt"
python -I "$DIR/look_check.py" "$OUT/flagged.tsv" "D:/NebulaWork/agents/look-reviews/$T.txt"
