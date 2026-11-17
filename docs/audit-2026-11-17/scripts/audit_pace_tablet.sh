#!/bin/sh
# audit_pace_tablet.sh [SERIAL]: real-time pacing runs on the tablet with the audit's test app (its own package id,
# beside the installed Nebula, which is never touched). Installs the test app and its game data, then for each run
# writes the game's settings into the app's own profiling folder, starts the game as a recording run
# (the debug build's nebula.profile start: --diagnostics 1, which includes present-stats), lets it play from the
# title into a stage (the audit-pace mod; no scripted input on Android, so Mario stands), and keeps the game's log.
# The game's own volume is set to 0 in its settings file. The test app is removed at the end.
# Out: build/audit-pace-tablet/<run>/logcat.txt
export MSYS_NO_PATHCONV=1
HERE=$(cd "$(dirname "$0")" && pwd -W)   # a Windows path: path conversion is off above, for adb's sake
SERIAL=${1:-R52WA0FXE5J}
B=D:/NebulaWork/build
T=D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean
ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
LL=D:/NebulaWork/lumalink-work/lumalink-sweep/target/release/lumalink.exe
PKG=dev.nebula.galaxy.test
APK=$B/audit-apk/Nebula-debug.apk
STORE=$(tr -d '\r\n' < $B/luma-bins/fp1/pak.path)
OUT=$B/audit-pace-tablet
P=$OUT/progress.txt
a() { "$ADB" -s $SERIAL "$@"; }
mkdir -p $OUT
echo "$(date +%H:%M:%S) start" > $P
AAPT=$(ls -d "$LOCALAPPDATA"/Android/Sdk/build-tools/*/aapt2.exe 2>/dev/null | tail -1)
id=$("$AAPT" dump badging "$APK" 2>/dev/null | sed -n "s/^package: name='\([^']*\)'.*/\1/p")
[ "$id" = "$PKG" ] || { echo "the apk is $id, not $PKG: nothing installed" >> $P; exit 1; }
a install --user 0 -r "$APK" > $OUT/install.txt 2>&1 || { echo "install failed" >> $P; exit 1; }
a shell pm list packages --user 0 | tr -d '\r' | grep -qx "package:$PKG" || { echo "$PKG not installed" >> $P; exit 1; }
echo "$(date +%H:%M:%S) installed" >> $P
# The game data: pushed, then copied into the app's own folder.
size=$(stat -c %s "$STORE")
if a shell "run-as $PKG ls -l files/game" 2>/dev/null | tr -d '\r' | grep -q "$size .*game.pak"; then
    echo "$(date +%H:%M:%S) game data already there" >> $P
else
a shell mkdir -p /data/local/tmp/nebula-test
a push "$STORE" /data/local/tmp/nebula-test/game.pak > $OUT/push.txt 2>&1 || { echo "push failed" >> $P; exit 1; }
printf '#Nebula setup\ngameId=RMGE01\nbytes=%s\ninstalledAt=%s000\nformat=RVZ\nsource=pushed by the test\ntitle=SUPER MARIO GALAXY\n' \
    "$size" "$(date +%s)" > $OUT/install.properties
a push $OUT/install.properties /data/local/tmp/nebula-test/install.properties >> $OUT/push.txt 2>&1
a shell chmod 755 /data/local/tmp/nebula-test
a shell chmod 644 /data/local/tmp/nebula-test/game.pak /data/local/tmp/nebula-test/install.properties
a shell "run-as $PKG sh -c 'mkdir -p files/game && cp /data/local/tmp/nebula-test/game.pak /data/local/tmp/nebula-test/install.properties files/game/'"
a shell rm -rf /data/local/tmp/nebula-test
a shell "run-as $PKG ls -l files/game" | tr -d '\r' > $OUT/store.txt
grep -q "$size .*game.pak" $OUT/store.txt || { echo "the game data is not whole on the device" >> $P; exit 1; }
echo "$(date +%H:%M:%S) game data in place" >> $P
fi

# run NAME MAX_FPS VSYNC SECONDS STAGE SCENARIO [EXTRA_CONFIG_LINE]
run() {
    name=$1; fps=$2; vsync=$3; seconds=$4; stage=$5; scenario=$6; extra=$7
    # The tablet's first start spends about 20 s on its boot screen, so this mod acts later than the PC's.
    MOD=$B/audit-pace/mod-$stage-$scenario-late
    if [ ! -f $MOD/code/android-arm64/mod.so ]; then
        python -X utf8 -I "$HERE/audit_pace_setup.py" mod $MOD $stage $scenario 2000 2300
        "$LL" build --names $B/luma-bins/fp1/luma $MOD > $MOD.build.txt 2>&1
    fi
    W=$OUT/$name
    rm -rf $W; mkdir -p $W/profiling/mods
    # The whole mod as built: its manifest names every platform's library.
    cp -r $MOD $W/profiling/mods/audit-pace
    rm -rf $W/profiling/mods/audit-pace/source
    printf 'max_fps=%s\nvsync_interval=%s\n' "$fps" "$vsync" > $W/profiling/config.ini
    [ -n "$extra" ] && echo "$extra" >> $W/profiling/config.ini
    printf 'audio_master_gain=0\n' > $W/profiling/runtime_settings.ini
    token=$(python -X utf8 -I -c "import sys, pathlib; sys.path.insert(0, r'$T/tools/lumacore'); import luma_test; print(luma_test.code_token(pathlib.Path(sys.argv[1])))" \
        $W/profiling/mods/audit-pace/code/android-arm64/mod.so)
    printf '; Nebula mods\n[audit-pace]\nenabled=1\norder=1\ncode_ok=%s\n' "$token" > $W/profiling/mods.ini
    a shell am force-stop $PKG
    # The pipeline cache of the run before is kept; the save, the mod, the settings and the log are this run's.
    a shell "run-as $PKG rm -rf files/profiling/saves files/profiling/mods files/profiling/mods.ini files/profiling/config.ini files/profiling/logs"
    (cd $W && tar -cf - profiling) | a exec-in run-as $PKG tar -xf - -C files
    a logcat -c
    echo "$(date +%H:%M:%S) $name start (max_fps=$fps vsync=$vsync $stage $scenario $extra)" >> $P
    a shell am start -W -n $PKG/dev.nebula.galaxy.GameActivity --ez nebula.profile true > $W/start.txt 2>&1
    sleep $seconds
    a exec-out screencap -p > $W/screen.png
    a shell "dumpsys display | grep -E 'mActiveModeId|renderFrameRate' | head -3" | tr -d '\r' > $W/display.txt
    a shell "dumpsys battery | grep -E 'temperature|level'; dumpsys thermalservice | grep -E 'Thermal Status' | head -1" | tr -d '\r' > $W/heat.txt
    a shell am force-stop $PKG
    a logcat -d -v time -s Nebula NebulaGame SDL libc DEBUG AndroidRuntime > $W/logcat.txt
    a shell "run-as $PKG sh -c 'cat files/profiling/logs/*.log 2>/dev/null'" | tr -d '\r' > $W/gamelog.txt
    echo "$(date +%H:%M:%S) $name done: $(grep -a -c 'interp-stats' $W/logcat.txt) statistics lines, $(grep -a -c 'pace:' $W/logcat.txt) mod lines, $(grep -a -c 'HAS ERRORS' $W/logcat.txt) mod errors" >> $P
}
if [ -n "$PILOT" ]; then
    run pilot 0 1 95 HoneyBeeKingdomGalaxy 1 ""
else
    run hb-v1-match 0 1 100 HoneyBeeKingdomGalaxy 1 ""
    run hb-v0-match 0 0 100 HoneyBeeKingdomGalaxy 1 ""
    run hb-v1-60 60 1 90 HoneyBeeKingdomGalaxy 1 ""
    run op-v1-match 0 1 110 OceanPhantomCaveGalaxy 4 ""
    run op-v1-match-dr 0 1 110 OceanPhantomCaveGalaxy 4 "dynamic_resolution=1"
    run op-v1-60 60 1 90 OceanPhantomCaveGalaxy 4 ""
fi
if [ -z "$KEEP" ]; then
    a shell pm uninstall --user 0 $PKG | tr -d '\r' > $OUT/uninstall.txt 2>&1
    a shell pm list packages --user 0 | tr -d '\r' | grep -i "nebula.galaxy" > $OUT/packages-after.txt
    echo "$(date +%H:%M:%S) done: $(cat $OUT/uninstall.txt); left: $(tr '\n' ' ' < $OUT/packages-after.txt)" >> $P
else
    echo "$(date +%H:%M:%S) done: test app kept" >> $P
fi
