#!/bin/sh
# look.sh BIN TAG: BIN through the tour with pictures kept, look_audit.py against the fixed reference (a bug already in the reference does not show), then look_check.py: every flagged stop needs a line of a person or agent who looked at its sheet.
# Sheets and pictures in $NEBULA_WORK/look_TAG; the reviewers' lines in $LOOK_REVIEWS/TAG.txt. REF=folder (the reference run's runs/fp folder) and CONTROL=folder (its control: a second run of the reference build) are required;
# LOOK_REVIEWS is a folder for the review files.
B=$1
T=$2
REF=${REF:?REF is not set}
CONTROL=${CONTROL:?CONTROL is not set}
LOOK_REVIEWS=${LOOK_REVIEWS:?LOOK_REVIEWS is not set}
: "${NEBULA_WORK:?NEBULA_WORK is not set}"
DIR=$(cd "$(dirname "$0")" && pwd)
OUT=$NEBULA_WORK/look_$T
rm -rf "$OUT"
mkdir -p "$OUT"
sh "$DIR/tour.sh" "$B" "${T}_look" GALAXY_TEST_GALAXY_TOUR_FRAMES=300 GALAXY_TEST_DETERMINISTIC=1 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=100 GALAXY_PRESENT_SNAPSHOT_FIRST=100 GALAXY_PRESENT_SNAPSHOT_LAST=100000 > "$OUT/tour.out" 2>&1
tail -n 1 "$OUT/tour.out"
python -I "$DIR/look_audit.py" "$REF" "$NEBULA_WORK/runs/${T}_look" "$OUT" --control "$CONTROL" > "$OUT/audit.txt" 2>&1
grep -E "^control|^pictures" "$OUT/audit.txt"
python -I "$DIR/look_check.py" "$OUT/flagged.tsv" "$LOOK_REVIEWS/$T.txt"
