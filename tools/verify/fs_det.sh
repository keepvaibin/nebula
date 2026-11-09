#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_SAVES (an empty saves folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_SAVES:?NEBULA_TEST_SAVES is not set}"
# fs_det.sh OLD NEW TAG: the file select with File 1 saved, deterministic, frames 2300-2400, both builds: the gate's real-time row measured without the animation's phase.
V=$(cd "$(dirname "$0")" && pwd)
O=$1; N=$2; T=$3
for side in old new; do
  bin=$O; [ $side = new ] && bin=$N
  (
    B=$NEBULA_BINS/$bin
    OUT=$NEBULA_RUNS/fs_$T/$side
    rm -rf "$OUT"; mkdir -p "$OUT"; W=$(cygpath -w "$OUT"); cp -r $NEBULA_RUNS/gate_qk3/saves "$OUT/saves"
    export VERIFY_DIR=$V NEBULA_WORK="$OUT" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_SAVES="$W\saves"
    . $V/common.sh; VERIFY=$V
    run_build "$B" fp 60 GALAXY_TEST_DETERMINISTIC=1 GALAXY_INPUT_AUTOPRESS_SCRIPT=760:30:A+B GALAXY_PRESENT_SNAPSHOT_EVERY=1 GALAXY_PRESENT_SNAPSHOT_FIRST=2300 GALAXY_PRESENT_SNAPSHOT_LAST=2400 >/dev/null 2>&1
  ) &
done
wait
R=$NEBULA_RUNS/fs_$T
grep -c "Choosing a file" $R/old/runs/fp/run.log $R/new/runs/fp/run.log | sed 's/.*fs_//' | tr '\n' ' '
python -I $V/align_frames.py $R/old/runs/fp $R/new/runs/fp - - 5 | tail -1 | cut -c1-130
