#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_NAND (an empty NAND folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_NAND:?NEBULA_TEST_NAND is not set}"
# sizes.sh BIN TAG GALAXY: the game at window sizes and shapes, a picture from each; crash and loss lines.
V=$(cd "$(dirname "$0")" && pwd)
B=$NEBULA_BINS/$1; T=$2; G=${3:-HeavenlyBeachGalaxy}
F=$(cygpath -w $(cd "$(dirname "$0")" && pwd)/tours/tour_one_$G.txt)
for s in 640x480 800x600 1366x768 1920x1080 2560x1080 3440x1440 3840x2160 1080x1350; do
  (
    w=${s%x*}; h=${s#*x}
    W0=$NEBULA_RUNS/sz_$T/$s; rm -rf "$W0"; mkdir -p "$W0"; W=$(cygpath -w "$W0"); cp -r $NEBULA_TEST_NAND "$W0/nand"
    export VERIFY_DIR=$V NEBULA_WORK="$W0" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_NAND="$W\nand"
    . $V/common.sh; VERIFY=$V
    run_build "$B" r 70 GALAXY_TEST_GALAXY_TOUR="$F" GALAXY_TEST_GALAXY_TOUR_FRAMES=2400 GALAXY_TEST_EVENTS=wait@5400 GALAXY_MAX_FPS=60 GALAXY_WINDOW_WIDTH=$w GALAXY_WINDOW_HEIGHT=$h GALAXY_PRESENT_SNAPSHOT_EVERY=1 GALAXY_PRESENT_SNAPSHOT_FIRST=2400 GALAXY_PRESENT_SNAPSHOT_LAST=2400 >/dev/null 2>&1
    L="$RUNS/r/run.log"
    echo "$s: crash $(grep -ci 'crash:' "$L") $(grep -a '\[loss\] draws' "$L" | tail -1 | cut -c8-80) $(python -c "from PIL import Image;print(Image.open(r'$(cygpath -w "$RUNS/r/present_2400.ppm")').size)" 2>&1 | tail -1 | cut -c1-40)"
  ) &
done
wait
python - "$T" <<'PY'
import sys
import os
from PIL import Image
RUNS=os.environ['NEBULA_RUNS']
T=sys.argv[1]
sizes=['640x480','800x600','1366x768','1920x1080','2560x1080','3440x1440','3840x2160','1080x1350']
S=Image.new('RGB',(4*400,2*300),(40,40,40))
for i,s in enumerate(sizes):
    try:
        im=Image.open(f'{RUNS}/sz_{T}/{s}/runs/r/present_2400.ppm').convert('RGB')
    except Exception as e:
        continue
    im.thumbnail((396,296))
    S.paste(im,((i%4)*400+2,(i//4)*300+2))
S.save(f'{RUNS}/sz_{T}/sheet.png')
PY
