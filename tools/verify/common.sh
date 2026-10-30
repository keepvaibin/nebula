#!/bin/sh
# Shared by the verify scripts: NEBULA_WORK (a work folder) holds runs/<tag> and appdata/<build>, one app-data folder
# per build (its bin folder's name), so two builds' shader caches never mix. NEBULA_PAK, NEBULA_NAND and NEBULA_SETTINGS
# as run.sh takes them.
VERIFY=$(cd "$(dirname "$0")" && pwd)
: "${NEBULA_WORK:?NEBULA_WORK is not set}"
RUNS="$NEBULA_WORK/runs"
mkdir -p "$RUNS" "$NEBULA_WORK/appdata"

# run_build BIN TAG SECONDS [NAME=value ...]: run.sh with the build's own app-data; the run lands in $RUNS/TAG.
# (sh functions share the caller's variables: its own are _rb_*.)
run_build() {
    _rb_bin=$1
    _rb_tag=$2
    _rb_seconds=$3
    shift 3
    _rb_appdata="$NEBULA_WORK/appdata/$(basename "$_rb_bin")"
    mkdir -p "$_rb_appdata"
    # A build's first run compiles its shaders cold, which moves its presents against a warm run: one discarded
    # warm-up run (the Battlerock stop) fills the cache before anything is compared.
    if [ ! -f "$_rb_appdata/.warm" ]; then
        rm -rf "$RUNS/warmup_$(basename "$_rb_bin")"
        NEBULA_BIN="$_rb_bin" NEBULA_APPDATA="$(cygpath -w "$_rb_appdata")" sh "$VERIFY/run.sh" 75 \
            "$RUNS/warmup_$(basename "$_rb_bin")" GALAXY_TEST_GALAXY_TOUR="$(tour_file tour_battle1.txt)" \
            GALAXY_TEST_GALAXY_TOUR_FRAMES=900 GALAXY_MAX_FPS=60 GALAXY_PRESENT_SNAPSHOT_EVERY=1000000
        touch "$_rb_appdata/.warm"
    fi
    rm -rf "$RUNS/$_rb_tag"
    NEBULA_BIN="$_rb_bin" NEBULA_APPDATA="$(cygpath -w "$_rb_appdata")" sh "$VERIFY/run.sh" "$_rb_seconds" "$RUNS/$_rb_tag" "$@"
}

tour_file() {
    cygpath -w "$VERIFY/tours/$1"
}
