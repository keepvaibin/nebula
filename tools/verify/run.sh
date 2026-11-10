#!/bin/sh
# run.sh SECONDS OUT [NAME=value ...]: runs a frozen build of the native host for a test, hidden and silent: the window
# hidden, the sound to OUT/audio.wav, present snapshots and crash dumps into OUT, private settings and app-data.
# Windows (Git Bash). The machine's paths come from the environment:
#   NEBULA_BIN       folder with NebulaNative.exe and smg_game.dll (and tables/*.tsv when the build has them)
#   NEBULA_EXE       the host to run, NebulaNative.exe (D3D12) unless set, e.g. NebulaNativeVk.exe (Vulkan)
#   NEBULA_PAK       the game content (game.pak); a build whose folder holds pak.path runs that file's game.pak
#                    instead (a cooked candidate beside a base on content packed before cooking)
#   NEBULA_SAVES     a test saves folder (never a player's)
#   NEBULA_APPDATA   a test app-data folder (shader caches; one per build when builds are compared)
#   NEBULA_SETTINGS  a test settings folder (config.ini, runtime_settings.ini)
#   NEBULA_GPU       optional: the GPU to run on, as config.ini's gpu= names it (render_config.h gpu_identity)
#   NEBULA_DIAGNOSTICS optional: what the run records (--diagnostics, docs/DIAGNOSTICS.md)
# The run's own copy of the binaries goes to OUT/bin and is removed afterwards unless the run left a crash dump.
set -e
seconds=$1
out=$2
shift 2
for v in NEBULA_BIN NEBULA_PAK NEBULA_SAVES NEBULA_APPDATA NEBULA_SETTINGS; do
    eval "value=\${$v:-}"
    if [ -z "$value" ]; then
        echo "run.sh: $v is not set" >&2
        exit 2
    fi
done
if [ -f "$NEBULA_BIN/pak.path" ]; then
    NEBULA_PAK=$(tr -d '\r\n' < "$NEBULA_BIN/pak.path")
fi
exe=${NEBULA_EXE:-NebulaNative.exe}
mkdir -p "$out/bin" "$(cygpath -u "$NEBULA_APPDATA")" "$(cygpath -u "$NEBULA_SETTINGS")"
cp "$NEBULA_BIN/$exe" "$NEBULA_BIN"/smg_game.dll "$out/bin/"
cp "$NEBULA_BIN"/*.pdb "$out/bin/" 2>/dev/null || true
if [ -d "$NEBULA_BIN/tables" ]; then
    cp "$NEBULA_BIN"/tables/*.tsv "$out/bin/"
fi
settings=$(cygpath -w "$NEBULA_SETTINGS")
# The GPU: the settings' own choice (config.ini gpu=), which the game keeps.
if [ -n "${NEBULA_GPU:-}" ]; then
    config="$(cygpath -u "$NEBULA_SETTINGS")/config.ini"
    if [ -f "$config" ] && grep -q '^gpu=' "$config"; then
        sed -i "s/^gpu=.*/gpu=$NEBULA_GPU/" "$config"
    else
        echo "gpu=$NEBULA_GPU" >> "$config"
    fi
fi
# The run's folders are arguments (docs/COMMAND_LINE.md); a build from before them reads the environment names below.
folders=""
if grep -qa -- "--crash-dumps" "$out/bin/$exe"; then
    folders="--settings $settings --crash-dumps $(cygpath -w "$out")"
fi
# GALAXY_TEST_CPU_AFFINITY=<hex mask> among the assignments: the processors of this shell, which the game inherits.
for a in "$@"; do
    case "$a" in
        GALAXY_TEST_CPU_AFFINITY=?*)
            powershell.exe -NoProfile -Command "(Get-Process -Id $(cat /proc/$$/winpid)).ProcessorAffinity = 0x${a#*=}" ;;
    esac
done
# Settings named GALAXY_<KEY> (the window size here, any among the assignments or exported) go into the run's
# config.ini and runtime_settings.ini for this run (settings_args.py): the game reads no setting from its environment.
python -I "$(cygpath -w "$(dirname "$0")")\\settings_args.py" "$settings" GALAXY_WINDOW_WIDTH=1280 GALAXY_WINDOW_HEIGHT=720 "$@"
# The test library (tools/test_driver/extension): the host loads it with --extension for the controls, pictures and
# sound; the run's mods run it as a plugin for the tour and events (test_plugin.py installs it, its code allowed).
cp "$NEBULA_BIN"/nebula_test_extension.dll "$out/bin/"
python -I "$(cygpath -w "$(dirname "$0")")\\test_plugin.py" "$settings" "$(cygpath -w "$out/bin/nebula_test_extension.dll")"
# The game starts hidden (start_hidden.py: STARTUPINFO's SW_HIDE), as a program a shortcut sets to hidden.
env GALAXY_RENDER_CONFIG_PATH="$settings\\config.ini" \
    GALAXY_RUNTIME_SETTINGS_PATH="$settings\\runtime_settings.ini" \
    LOCALAPPDATA="$NEBULA_APPDATA" GALAXY_CRASH_DUMP_DIR="$(cygpath -w "$out")" \
    GALAXY_PRESENT_SNAPSHOT_DIR="$(cygpath -w "$out")" GALAXY_PRESENT_SNAPSHOT_EVERY=60 \
    GALAXY_AUDIO_WAV_DUMP_ONLY=1 GALAXY_AUDIO_WAV_DUMP="$(cygpath -w "$out")\\audio.wav" \
    "$@" \
    python -I "$(cygpath -w "$(dirname "$0")")\\start_hidden.py" "$out/bin/$exe" --pak "$NEBULA_PAK" --saves "$NEBULA_SAVES" --seconds "$seconds" $folders \
    --extension "$(cygpath -w "$out/bin/nebula_test_extension.dll")" ${NEBULA_DIAGNOSTICS:+--diagnostics "$NEBULA_DIAGNOSTICS"} \
    > "$out/run.log" 2>&1 || echo "exit $?" >> "$out/run.log"
if ! ls "$out"/*.dmp >/dev/null 2>&1; then
    rm -f "$out"/bin/*.exe "$out"/bin/*.dll "$out"/bin/*.pdb
fi
