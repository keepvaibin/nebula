#!/bin/sh
# audit_pace.sh NAME EXE [GALAXY_KEY=value ...]: one real-time run of the build under test with a VISIBLE window and
# no sound device, from the title into a stage (the audit-pace mod), with scripted stick and jumps, the pacing
# statistics on stderr and the frame timeline written at exit. Out: build/audit-pace/NAME/out.
# Env: STAGE (HoneyBeeKingdomGalaxy), SCENARIO (1), RUN_SECONDS (95), DIAG.
HERE=$(cd "$(dirname "$0")" && pwd)
T=D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean
BIN=D:/NebulaWork/build/luma-bins/fp1
LL=/d/NebulaWork/lumalink-work/lumalink-sweep/target/release/lumalink.exe
ROOT=D:/NebulaWork/build/audit-pace
NAME=$1; EXE=$2; shift 2
STAGE=${STAGE:-HoneyBeeKingdomGalaxy}; SCENARIO=${SCENARIO:-1}
PAK=$(tr -d '\r\n' < $BIN/pak.path)
M=$ROOT/mod-$STAGE-$SCENARIO
mkdir -p $ROOT
if [ ! -f $M/code/windows-x64/mod.dll ]; then
    python -X utf8 -I "$HERE/audit_pace_setup.py" mod $M $STAGE $SCENARIO || exit 2
    "$LL" build --names $BIN/luma $M > $ROOT/mod-$STAGE-$SCENARIO.build.txt 2>&1 || { tail -20 $ROOT/mod-$STAGE-$SCENARIO.build.txt; exit 2; }
fi
W=$ROOT/$NAME
rm -rf $W; mkdir -p $W/settings/mods $W/appdata $W/out $W/saves
cp -r $M $W/settings/mods/audit-pace
python -X utf8 -I "$HERE/audit_pace_setup.py" run $W || exit 2
python -I "$(cygpath -w $T/tools/verify/settings_args.py)" "$(cygpath -w $W/settings)" GALAXY_WINDOW_WIDTH=2560 GALAXY_WINDOW_HEIGHT=1440 \
    GALAXY_DISPLAY_MODE=borderless "$@" || exit 2
# The stick walks round the compass in 150-frame legs from frame 1500 on; a jump every 400 frames from 1700.
STICK=""; f=1500; for d in "0.9:0.0" "0.6:0.6" "0.0:0.9" "-0.6:0.6" "-0.9:0.0" "-0.6:-0.6" "0.0:-0.9" "0.6:-0.6"; do :; done
i=0; while [ $f -lt 7000 ]; do
    case $((i % 8)) in 0) d="0.9:0.0";; 1) d="0.6:0.6";; 2) d="0.0:0.9";; 3) d="-0.6:0.6";; 4) d="-0.9:0.0";; 5) d="-0.6:-0.6";; 6) d="0.0:-0.9";; 7) d="0.6:-0.6";; esac
    STICK="$STICK$f:150:$d;"; f=$((f + 150)); i=$((i + 1))
done
PRESS=""; f=1700; while [ $f -lt 7000 ]; do PRESS="$PRESS$f:6:A;"; f=$((f + 400)); done
echo "$(date +%H:%M:%S) $NAME start: $EXE $*" >> $ROOT/progress.txt
env NEBULA_WORK="$(cygpath -w $W)" NEBULA_SETTINGS="$(cygpath -w $W/settings)" NEBULA_APPDATA="$(cygpath -w $W/appdata)" \
    NEBULA_SAVES="$(cygpath -w $W/saves)" LOCALAPPDATA="$(cygpath -w $W/appdata)" GALAXY_AUDIO_WAV_DUMP="$(cygpath -w $W/out/audio.wav)" GALAXY_AUDIO_WAV_DUMP_ONLY=1 \
    GALAXY_AUDIO_WAV_DUMP_SECONDS=2 GALAXY_INPUT_STICK_SCRIPT="$STICK" GALAXY_INPUT_AUTOPRESS_SCRIPT="$PRESS" \
    "$BIN/$EXE" --pak "$PAK" --saves "$(cygpath -w $W/saves)" --settings "$(cygpath -w $W/settings)" \
    --crash-dumps "$(cygpath -w $W/out)" --extension "$(cygpath -w $BIN/nebula_test_extension.dll)" \
    --diagnostics ${DIAG:-present-stats,timeline,thread-cpu,frame-tails} --timeline "$(cygpath -w $W/out/timeline.bin)" \
    --seconds ${RUN_SECONDS:-95} > $W/out/run.log 2>&1
echo "$(date +%H:%M:%S) $NAME exit $?: $(ls $W/out | tr '\n' ' ')" >> $ROOT/progress.txt
