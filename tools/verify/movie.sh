#!/bin/sh
# movie.sh BASE_BIN NEW_BIN TAG [FIRST LAST]: the ending movie (THP through the disc) in both builds: Bowser's last
# battle, its Grand Star, then the movie (1280x720, 1x, 60 fps, 140 s), every present from FIRST to LAST (default
# 6000-6040) snapshotted, the new build's widened by MARGIN (default 200: the movie starts up to ~130 presents apart
# between runs of one build, as the battle's end and the stream's start vary), then align_frames.py (identity).
. "$(dirname "$0")/common.sh"
base=$1
new=$2
tag=$3
first=${4:-6000}
last=${5:-6040}
margin=${MARGIN:-200}
common="GALAXY_TEST_GALAXY_TOUR=$(tour_file tour_one_KoopaBattleVs3Galaxy.txt) GALAXY_TEST_GALAXY_TOUR_FRAMES=900 GALAXY_TEST_EVENTS=grandstar@2400/wait@48000 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1"
# shellcheck disable=SC2086
run_build "$base" "m_${tag}_0" 140 $common GALAXY_PRESENT_SNAPSHOT_FIRST=$first GALAXY_PRESENT_SNAPSHOT_LAST=$last $EXTRA
# shellcheck disable=SC2086
run_build "$new" "m_${tag}_1" 140 $common GALAXY_PRESENT_SNAPSHOT_FIRST=$((first - margin)) \
    GALAXY_PRESENT_SNAPSHOT_LAST=$((last + margin)) $EXTRA
echo "$tag movie: crash $(grep -ci 'crash:' "$RUNS/m_${tag}_1/run.log") $(python -I "$VERIFY/align_frames.py" "$RUNS/m_${tag}_0" "$RUNS/m_${tag}_1" - - "$margin" | tail -1)"
