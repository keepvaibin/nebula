#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_NAND (an empty NAND folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_NAND:?NEBULA_TEST_NAND is not set}"
# validate.sh BIN TAG ENV=value: the tour under the graphics API's validation; unique error lines from the log.
V=$(cd "$(dirname "$0")" && pwd)
B=$NEBULA_BINS/$1; T=$2; shift 2
W0=$NEBULA_RUNS/val_$T; rm -rf "$W0"; mkdir -p "$W0"; W=$(cygpath -w "$W0"); cp -r $NEBULA_TEST_NAND "$W0/nand"
export VERIFY_DIR=$V NEBULA_WORK="$W0" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_NAND="$W\nand"
. $V/common.sh; VERIFY=$V
F=$(cygpath -w $V/tours/tour_enterable.txt)
run_build "$B" r 1200 GALAXY_TEST_GALAXY_TOUR="$F" GALAXY_TEST_GALAXY_TOUR_FRAMES=60 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000 "$@" >/dev/null 2>&1
L="$RUNS/r/run.log"
echo "$T: crash $(grep -ci 'crash:' "$L") stops $(grep -c 'tour: [0-9]*/102' "$L") lines $(wc -l < "$L") ended $(grep -c 'the game ended' "$L")"
grep -aE "D3D12 (ERROR|WARNING|CORRUPTION)|\[d3d12|validation|VUID|Validation Error|debug layer|InfoQueue" "$L" | sed -E 's/0x[0-9A-Fa-f]+/0x/g; s/[0-9]{3,}/N/g; s/^\[ *[0-9.]+\] //' | cut -c1-170 | sort | uniq -c | sort -rn | head -14
