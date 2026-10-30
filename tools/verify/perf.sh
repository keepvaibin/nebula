#!/bin/sh
# perf.sh BIN TAG: the fixed-scene GPU-timer measurement each pushed batch records (perf.tsv): the low-end stand-in
# (GALAXY_GPU_PREFERENCE=low, the integrated GPU; 4 CPU threads) at the Battlerock stop, 1280x720, 1x, 60 fps, 75 s, in
# the build's own app-data. Run it once to warm the build's caches, then measure; prints the median GPU busy time
# per second and per frame and the render and game threads' CPU after the stop is reached.
. "$(dirname "$0")/common.sh"
bin=$1
tag=$2
run_build "$bin" "$tag" 75 GALAXY_TEST_GALAXY_TOUR="$(tour_file tour_battle1.txt)" GALAXY_TEST_GALAXY_TOUR_FRAMES=900 \
    GALAXY_TRACE_PRESENT_STATS=1 GALAXY_TRACE_THREAD_CPU=1 GALAXY_TEST_EVENTS=wait@5400 \
    GALAXY_GPU_PREFERENCE=low GALAXY_TEST_CPU_AFFINITY=F GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000
python -I "$VERIFY/perf_summary.py" "$RUNS/$tag/run.log" "$tag"
