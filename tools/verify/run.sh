#!/bin/sh
# run.sh SECONDS OUT [NAME=value ...]: runs a frozen build of the native host for a test, hidden and silent: the window
# hidden, the sound to OUT/audio.wav, present snapshots and crash dumps into OUT, private settings and app-data.
# Windows (Git Bash). The machine's paths come from the environment:
#   NEBULA_BIN       folder with NebulaNative.exe and smg_game.dll (and tables/*.tsv when the build has them)
#   NEBULA_PAK       the game content (game.pak); a build whose folder holds pak.path runs that file's game.pak
#                    instead (a cooked candidate beside a base on content packed before cooking)
#   NEBULA_NAND      a test NAND folder (never a player's)
#   NEBULA_APPDATA   a test app-data folder (shader caches; one per build when builds are compared)
#   NEBULA_SETTINGS  a test settings folder (config.ini, runtime_settings.ini)
# The run's own copy of the binaries goes to OUT/bin and is removed afterwards unless the run left a crash dump.
set -e
seconds=$1
out=$2
shift 2
for v in NEBULA_BIN NEBULA_PAK NEBULA_NAND NEBULA_APPDATA NEBULA_SETTINGS; do
    eval "value=\${$v:-}"
    if [ -z "$value" ]; then
        echo "run.sh: $v is not set" >&2
        exit 2
    fi
done
if [ -f "$NEBULA_BIN/pak.path" ]; then
    NEBULA_PAK=$(tr -d '\r\n' < "$NEBULA_BIN/pak.path")
fi
mkdir -p "$out/bin" "$(cygpath -u "$NEBULA_APPDATA")" "$(cygpath -u "$NEBULA_SETTINGS")"
cp "$NEBULA_BIN"/NebulaNative.exe "$NEBULA_BIN"/smg_game.dll "$out/bin/"
cp "$NEBULA_BIN"/*.pdb "$out/bin/" 2>/dev/null || true
if [ -d "$NEBULA_BIN/tables" ]; then
    cp "$NEBULA_BIN"/tables/*.tsv "$out/bin/"
fi
settings=$(cygpath -w "$NEBULA_SETTINGS")
env GALAXY_HIDE_WINDOW=1 GALAXY_XINPUT_REQUIRE_FOCUS=1 \
    GALAXY_RENDER_CONFIG_PATH="$settings\\config.ini" \
    GALAXY_RUNTIME_SETTINGS_PATH="$settings\\runtime_settings.ini" \
    LOCALAPPDATA="$NEBULA_APPDATA" GALAXY_CRASH_DUMP_DIR="$(cygpath -w "$out")" \
    GALAXY_WINDOW_WIDTH=1280 GALAXY_WINDOW_HEIGHT=720 GALAXY_EFB_SCALE=1 GALAXY_FULLSCREEN=0 \
    GALAXY_PRESENT_SNAPSHOT_DIR="$(cygpath -w "$out")" GALAXY_PRESENT_SNAPSHOT_EVERY=60 \
    GALAXY_AUDIO_WAV_DUMP_ONLY=1 GALAXY_AUDIO_WAV_DUMP="$(cygpath -w "$out")\\audio.wav" \
    GALAXY_MODULE_PATH="$(cygpath -w "$out/bin/smg_game.dll")" \
    "$@" \
    "$out/bin/NebulaNative.exe" --pak "$NEBULA_PAK" --nand "$NEBULA_NAND" --seconds "$seconds" \
    > "$out/run.log" 2>&1 || echo "exit $?" >> "$out/run.log"
if ! ls "$out"/*.dmp >/dev/null 2>&1; then
    rm -f "$out"/bin/*.exe "$out"/bin/*.dll "$out"/bin/*.pdb
fi
