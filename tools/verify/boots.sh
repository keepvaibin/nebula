#!/bin/sh
# Folders: NEBULA_BINS (frozen builds, one folder each with pak.path), NEBULA_RUNS (output), NEBULA_TEST_SAVES (an empty saves folder to copy).
: "${NEBULA_BINS:?NEBULA_BINS is not set}"; : "${NEBULA_RUNS:?NEBULA_RUNS is not set}"; : "${NEBULA_TEST_SAVES:?NEBULA_TEST_SAVES is not set}"
# boots.sh BIN TAG COUNT PARALLEL: many boots, each to the title within 25 s or it counts as hung; the log of each hang stays.
V=$(cd "$(dirname "$0")" && pwd)
B=$NEBULA_BINS/$1; T=$2; N=$3; P=$4
R=$NEBULA_RUNS/bt_$T; rm -rf "$R"; mkdir -p "$R"
one() {
  W0=$R/$1; mkdir -p "$W0"; W=$(cygpath -w "$W0"); cp -r $NEBULA_TEST_SAVES "$W0/saves"
  export VERIFY_DIR=$V NEBULA_WORK="$W0" NEBULA_PAK="$(cat $B/pak.path)" NEBULA_SETTINGS="$W\settings" NEBULA_SAVES="$W\saves"
  . $V/common.sh; VERIFY=$V
  run_build "$B" r 25 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000 $5 >/dev/null 2>&1
  L="$RUNS/r/run.log"
  if grep -q "boot: title screen" "$L"; then rm -rf "$W0"; else echo "HANG $1: $(grep -a 'host: ' "$L" | tail -1 | cut -c1-60)"; fi
}
i=0
while [ $i -lt $N ]; do
  j=0
  while [ $j -lt $P ] && [ $i -lt $N ]; do one $i & i=$((i+1)); j=$((j+1)); done
  wait
done
echo "boots $N done, hangs $(ls $R | wc -l)"
