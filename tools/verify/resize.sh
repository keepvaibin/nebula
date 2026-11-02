#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_NAND (an empty NAND folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_NAND:?NEBULA_TEST_NAND is not set}"
# resize.sh BIN TAG GALAXY: one run, the window resized live every 240 presents through small, wide, tall, 4K and back,
# with a picture 60 presents after each resize: the picture has the window's shape, the effects cover it, no crash.
V=$(cd "$(dirname "$0")" && pwd)
B=$NEBULA_BINS/$1; T=$2; G=${3:-HeavenlyBeachGalaxy}
F=$(cygpath -w "$V/tours/tour_one_$G.txt")
W0=$NEBULA_RUNS/rz_$T; rm -rf "$W0"; mkdir -p "$W0"; W=$(cygpath -w "$W0"); cp -r "$NEBULA_TEST_NAND" "$W0/nand"
SIZES=""; AT=1500
for s in 640x480 1920x1080 800x600 3440x1440 1080x1350 3840x2160 1366x768 2560x1080 320x240 1280x720; do
  SIZES="$SIZES,$AT:$s"; AT=$((AT + 240))
done
export VERIFY_DIR=$V NEBULA_WORK="$W0" NEBULA_PAK="$(cat "$B/pak.path")" NEBULA_SETTINGS="$W\settings" NEBULA_NAND="$W\nand"
. "$V/common.sh"; VERIFY=$V
run_build "$B" r 90 GALAXY_TEST_GALAXY_TOUR="$F" GALAXY_TEST_GALAXY_TOUR_FRAMES=3600 GALAXY_TEST_EVENTS=wait@5400 GALAXY_MAX_FPS=60 \
  GALAXY_TEST_WINDOW_SIZES="${SIZES#,}" GALAXY_PRESENT_SNAPSHOT_EVERY=60 GALAXY_PRESENT_SNAPSHOT_FIRST=1500 GALAXY_PRESENT_SNAPSHOT_LAST=4000 >/dev/null 2>&1
L="$RUNS/r/run.log"
echo "$T: crash $(grep -ci 'crash:' "$L") refused $(grep -c 'exceeds the video-memory' "$L") $(grep -a '\[loss\] draws' "$L" | tail -1 | cut -c8-80)"
python -I - "$RUNS/r" <<'PY'
import glob, os, sys, collections
try:
    from PIL import Image
except ImportError:
    sys.exit(0)
seen = collections.Counter(); order = []
for p in sorted(glob.glob(sys.argv[1] + '/present_*.ppm'), key=lambda p: int(p.rsplit('_', 1)[1].split('.')[0])):
    s = Image.open(p).size
    if not order or order[-1] != s: order.append(s)
    seen[s] += 1
print('picture sizes in order:', ' '.join('%dx%d' % s for s in order))
PY
