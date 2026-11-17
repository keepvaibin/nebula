#!/bin/sh
# audit_tablet_tour.sh [SERIAL] [ABI]: the 102-stop harness tour on an Android device with the audit's test app
# (dev.nebula.galaxy.test, installed and holding its game data), the game's own log lines kept from logcat.
export MSYS_NO_PATHCONV=1
SERIAL=${1:-R52WA0FXE5J}
ABI=${2:-arm64-v8a}
B=D:/NebulaWork/build
T=D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean
D=D:/NebulaWork/agents/luma/d/platforms
ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
case $ABI in arm64-v8a) BIN=$B/audit-android-bin-arm64 ;; *) BIN=$B/audit-android-bin ;; esac
OUT=$B/audit-tab-tour-$SERIAL
export NEBULA_ANDROID_PACKAGE=dev.nebula.galaxy.test
rm -rf $OUT; mkdir -p $OUT
"$ADB" -s $SERIAL logcat -c
"$ADB" -s $SERIAL logcat -v time -s Nebula NebulaGame libc DEBUG AndroidRuntime > $OUT/logcat.txt 2>&1 &
LC=$!
echo "$(date +%H:%M:%S) start" > $OUT/progress.txt
python -X utf8 -I $D/android/suite_android.py $B/audit-tourh/tests/tour/suite.json --tree $T --bin $BIN --work $OUT/work \
    --serial $SERIAL --abi $ABI --who audit > $OUT/out.txt 2>&1
echo "$(date +%H:%M:%S) exit $?: $(grep -cE ' pass( |$)' $OUT/out.txt) pass, $(grep -cE ' (fail|crashed|setup-failed|timeout|harness-error)( |$)' $OUT/out.txt) not" >> $OUT/progress.txt
kill $LC 2>/dev/null
echo "$(date +%H:%M:%S) done" >> $OUT/progress.txt
