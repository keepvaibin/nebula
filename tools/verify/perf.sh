#!/bin/sh
# perf.sh BIN TAG: the fixed-scene GPU-timer measurement each pushed batch records (perf.tsv): the low-end stand-in
# (NEBULA_GPU, else the integrated GPU; 4 CPU threads) at the Battlerock stop,
# 1280x720, 1x, 60 fps, 75 s, in the build's own app-data. Run it once to warm the build's caches, then measure; prints
# the median GPU busy time per second and per frame and the render and game threads' CPU after the stop is reached.
. "$(dirname "$0")/common.sh"
bin=$1
tag=$2
export NEBULA_GPU=${NEBULA_GPU:-1002:13c0:7e491462} NEBULA_DIAGNOSTICS=present-stats,thread-cpu
run_build "$bin" "$tag" 75 GALAXY_TEST_GALAXY_TOUR="$(tour_file tour_battle1.txt)" GALAXY_TEST_GALAXY_TOUR_FRAMES=900 \
    GALAXY_TEST_EVENTS=wait@5400 \
    GALAXY_TEST_CPU_AFFINITY=F GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000
python -I "$VERIFY/perf_summary.py" "$RUNS/$tag/run.log" "$tag"
