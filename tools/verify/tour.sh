#!/bin/sh
# tour.sh BIN TAG [NAME=value ...]: the 102-stop galaxy tour (every enterable stop, 600 frames each), hidden and silent,
# in the build's own app-data; prints the stops reached, whether it finished, crash lines and dumps.
. "$(dirname "$0")/common.sh"
bin=$1
tag=$2
shift 2
run_build "$bin" "$tag" 3000 GALAXY_TEST_GALAXY_TOUR="$(tour_file tour_enterable.txt)" GALAXY_TEST_GALAXY_TOUR_FRAMES=600 \
    GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000 "$@"
log="$RUNS/$tag/run.log"
echo "$tag: stops $(grep -c 'tour: [0-9]*/' "$log"), $(grep -c 'tour: done' "$log") done, $(grep -ci 'exception\|crash:\|DEVICE_REMOVED\|DEVICE_LOST\|fatal' "$log") crash lines, $(ls "$RUNS/$tag"/*.dmp 2>/dev/null | wc -l) dumps"
