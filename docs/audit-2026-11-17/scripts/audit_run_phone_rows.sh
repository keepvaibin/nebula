#!/bin/sh
# run_phone_rows.sh NAME SERIAL: the Android rows on a real phone, with the test build only (dev.nebula.galaxy.test,
# which sits beside an installed Nebula and cannot replace it or open its data). Never touches another package.
#   1. installs build/NAME-apk/Nebula-debug.apk for user 0 and checks the installed id is the test id;
#   2. its store: game.pak (STORE, default the merged build's build/all1-pak/game.pak) and the install.properties
#      setup writes, streamed into the test app's own files/game (adb exec-in run-as; push and copy without it);
#   3. 8.2 a: the launcher and the game started from the shell, a picture of each;
#   4. 8.2 b: the loader suite (S/f/tests/loader); 8.2 c: microphone and camera (S/h/tests/audio 2606, 2607), the
#      phone's own devices, permissions granted by the suite with pm grant to the test id;
#   5. 8.5: the one-luma files (ONE) on the device's platform;
#   6. the test app's log lines (logcat, its tags only) and the pictures in W-phone/;
#   7. the test package removed with its data (KEEP=1 keeps it).
# ABI: arm64-v8a (default: a phone; bins build/NAME-android-bin-arm64) or x86_64 (the emulator; bins
# build/NAME-android-bin). Ends "exit N", the number of steps that failed.
export MSYS_NO_PATHCONV=1
N=$1
SERIAL=$2
B=D:/NebulaWork/build
S=${S:-$B/md-pa-src}
W=${W:-$B/md-pa}
ONE=${ONE:-$B/one-luma-pa}
T=D:/NebulaWork/MarioGalaxyRecomp/${TREE:-Nebula-MdPa}
STORE=${STORE:-$B/all1-pak/game.pak}
D=D:/NebulaWork/agents/luma/d/platforms
ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
PKG=dev.nebula.galaxy.test
export NEBULA_ANDROID_PACKAGE=$PKG
ABI=${ABI:-arm64-v8a}
case $ABI in arm64-v8a) BIN=$B/$N-android-bin-arm64; SYSTEM=android-arm64 ;; x86_64) BIN=$B/$N-android-bin; SYSTEM=android ;; *) echo "ABI: arm64-v8a or x86_64"; exit 1 ;; esac
APK=$B/$N-apk/Nebula-debug.apk
P=$W-phone
failed=0
step() { name=$1; shift; echo "$(date +%H:%M:%S) $name"; if "$@"; then echo "$name: ok"; else echo "$name: FAILED ($?)"; failed=$((failed + 1)); fi; }
a() { "$ADB" -s $SERIAL "$@"; }
[ -n "$SERIAL" ] && [ -s "$APK" ] && [ -d "$BIN" ] || { echo "usage: run_phone_rows.sh NAME SERIAL (no $APK or $BIN)"; echo "exit 1"; exit 1; }
# This run's own adb server (a server another command started can end with that command, mid-run), and the device seen
# by it before the first step: a server just started lists a USB device only after a moment.
"$ADB" kill-server 2>/dev/null
"$ADB" start-server
timeout 60 "$ADB" -s $SERIAL wait-for-device || { echo "the device $SERIAL is not seen by adb"; echo "exit 1"; exit 1; }
mkdir -p $P

# 1. The apk's id must be the test id before anything is installed.
AAPT=$(ls -d "$LOCALAPPDATA"/Android/Sdk/build-tools/*/aapt2.exe 2>/dev/null | tail -1)
id=$("$AAPT" dump badging "$APK" 2>/dev/null | sed -n "s/^package: name='\([^']*\)'.*/\1/p")
echo "apk id: $id"
[ "$id" = "$PKG" ] || { echo "the apk is $id, not $PKG: nothing installed"; echo "exit 1"; exit 1; }
step install a install --user 0 -r "$APK"
a shell pm list packages --user 0 | grep -i nebula > $P/packages.txt
cat $P/packages.txt
grep -qx "package:$PKG" $P/packages.txt || { echo "$PKG not installed"; echo "exit 1"; exit 1; }

# 2. The store, in the test app's own folder.
store() {
    # Streamed straight into the app's folder (no second copy on the device's storage) for a store under 1 GB; a
    # larger one is pushed and copied: streaming 4 GB through exec-in ends the adb server (std::bad_alloc in adb.log).
    a shell "run-as $PKG mkdir -p files/game" || return 1
    if [ "$(stat -c %s "$STORE")" -lt 1073741824 ] && a exec-in run-as $PKG sh -c 'cat > files/game/game.pak' < "$STORE" 2> $P/push.txt; then
        printf '#Nebula setup\ngameId=RMGE01\nbytes=%s\ninstalledAt=%s000\nformat=RVZ\nsource=pushed by the test\ntitle=SUPER MARIO GALAXY\n' \
            "$(stat -c %s "$STORE")" "$(date +%s)" > $P/install.properties
        a exec-in run-as $PKG sh -c 'cat > files/game/install.properties' < $P/install.properties || return 1
        a shell "run-as $PKG ls -l files/game" | tee $P/store.txt
        grep -q "$(stat -c %s "$STORE") .*game.pak" $P/store.txt && return
        # A stream that ended early (seen with a 4 GB store: exec-in returned 0 at 68 MB) is pushed again whole.
        echo "the streamed store is short; pushing it instead"
        a shell "run-as $PKG rm -f files/game/game.pak files/game/install.properties"
    fi
    a shell mkdir -p /data/local/tmp/nebula-test || return 1
    a push "$STORE" /data/local/tmp/nebula-test/game.pak > $P/push.txt 2>&1 || return 1
    # What setup writes after game.pak is complete (Store.kt Install): its presence means a playable game.
    printf '#Nebula setup\ngameId=RMGE01\nbytes=%s\ninstalledAt=%s000\nformat=RVZ\nsource=pushed by the test\ntitle=SUPER MARIO GALAXY\n' \
        "$(stat -c %s "$STORE")" "$(date +%s)" > $P/install.properties
    a push $P/install.properties /data/local/tmp/nebula-test/install.properties >> $P/push.txt 2>&1 || return 1
    a shell chmod 755 /data/local/tmp/nebula-test
    a shell chmod 644 /data/local/tmp/nebula-test/game.pak /data/local/tmp/nebula-test/install.properties
    a shell "run-as $PKG sh -c 'mkdir -p files/game && cp /data/local/tmp/nebula-test/game.pak /data/local/tmp/nebula-test/install.properties files/game/'" || return 1
    a shell rm -rf /data/local/tmp/nebula-test
    a shell "run-as $PKG ls -l files/game" | tee $P/store.txt
    grep -q "$(stat -c %s "$STORE") .*game.pak" $P/store.txt
}
step store store

# 3. 8.2 a: starts and draws.
start() {
    a logcat -c
    a shell am force-stop $PKG
    a shell am start -W -n $PKG/dev.nebula.galaxy.LauncherActivity > $P/start-launcher.txt 2>&1
    sleep 8
    a exec-out screencap -p > $P/launcher.png
    a shell am start -W -n $PKG/dev.nebula.galaxy.GameActivity > $P/start-game.txt 2>&1
    sleep 40
    a exec-out screencap -p > $P/game.png
    a shell pidof $PKG:game > $P/game-pid.txt
    a shell am force-stop $PKG
    a logcat -d -v time -s Nebula NebulaGame NebulaLauncher NebulaWiimote SDL libc DEBUG AndroidRuntime > $P/logcat-start.txt
    [ -s $P/launcher.png ] && [ -s $P/game.png ] && [ -s $P/game-pid.txt ]
}
step start start

# 4. 8.2 b and c.
rm -rf $W-suite $W-cap
step suite python -X utf8 -I $D/android/suite_android.py $S/f/tests/loader/suite.json --tree $T --bin $BIN --work $W-suite \
    --serial $SERIAL --abi $ABI --who P
echo "capture tests (microphone, camera): not run in this audit"
# 5. 8.5.
step one-luma python -X utf8 -I $D/one_luma_all_five.py run --out $ONE --system $SYSTEM --tree $T --bin $BIN --serial $SERIAL

# 6. The log of the runs.
a logcat -d -v time -s Nebula NebulaGame NebulaLauncher SDL libc DEBUG AndroidRuntime > $P/logcat-runs.txt

# 7. The test package goes, with its data.
if [ -z "${KEEP:-}" ]; then
    a shell pm uninstall --user 0 $PKG > $P/uninstall.txt 2>&1
    a shell pm list packages --user 0 | grep -i nebula > $P/packages-after.txt
    cat $P/uninstall.txt $P/packages-after.txt
fi
echo "exit $failed"
exit $failed
