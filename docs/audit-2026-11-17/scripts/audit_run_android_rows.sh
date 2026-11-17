#!/bin/sh
# run_android_rows.sh NAME (8.2 b, 8.2 c, 8.5 Android, the launcher in another language): everything that runs on the
# emulator after build_int_android.sh NAME, one after the other on one device (the suite's lock keeps others off).
#   S: the folder with copies of the loader and capture tests, their mods built with TARGETS="windows-x64 android-x64"
#      (default build/md-p-and-src); W: the prefix of the work folders (default build/md-p-and); ONE: the 8.5 folder
#      (default build/one-luma).
# INSTALL=1: the suite installs the apk first (adb install -r); without it the installed app is the one tested.
# Writes $W-suite/out.txt, $W-cap/<test>/..., build/one-luma/runs/android/out.txt, and $W-locale/ (screen and UI
# dump of the launcher in Japanese and German). Ends "exit N", the number of steps that failed.
. D:/NebulaWork/agents/luma/perf_guard.sh
# Device paths (/data/local/tmp) must reach adb as written, not as Git Bash rewrites them.
export MSYS_NO_PATHCONV=1
N=$1
B=D:/NebulaWork/build
S=${S:-$B/md-p-and-src}
W=${W:-$B/md-p-and}
ONE=${ONE:-$B/one-luma}
T=D:/NebulaWork/MarioGalaxyRecomp/${TREE:-Nebula-MdP}
D=D:/NebulaWork/agents/luma/d/platforms
ADB="$LOCALAPPDATA/Android/Sdk/platform-tools/adb.exe"
SERIAL=${SERIAL:-emulator-5554}
BIN=$B/$N-android-bin
APK=$B/$N-apk/Nebula-debug.apk
failed=0
step() { name=$1; shift; echo "$(date +%H:%M:%S) $name"; if "$@"; then echo "$name: ok"; else echo "$name: FAILED ($?)"; failed=$((failed + 1)); fi; }
[ -s "$APK" ] && [ -d "$BIN" ] || { echo "no $APK or $BIN"; echo "exit 1"; exit 1; }

# 8.2 b: the loader suite (installs the apk once).
rm -rf $W-suite
step suite python -X utf8 -I $D/android/suite_android.py $S/f/tests/loader/suite.json --tree $T --bin $BIN --work $W-suite \
    --serial $SERIAL ${INSTALL:+--apk $APK} --who P
# 8.2 c: microphone and camera, one test each.
rm -rf $W-cap
echo "capture tests (microphone, camera): not run in this audit"
# 8.5: the frozen .luma files' run tests.
step one-luma python -X utf8 -I $D/one_luma_all_five.py run --out $ONE --system android --tree $T --bin $BIN --serial $SERIAL

# The launcher in another language: the app's own locale, its first screen, a picture and the UI tree.
rm -rf $W-locale && mkdir -p $W-locale
for locale in ja de; do
    "$ADB" -s $SERIAL shell am force-stop dev.nebula.galaxy
    "$ADB" -s $SERIAL shell cmd locale set-app-locales dev.nebula.galaxy --locales $locale
    "$ADB" -s $SERIAL shell am start -W -n dev.nebula.galaxy/.LauncherActivity > $W-locale/start-$locale.txt 2>&1 ||
        "$ADB" -s $SERIAL shell monkey -p dev.nebula.galaxy -c android.intent.category.LAUNCHER 1 >> $W-locale/start-$locale.txt 2>&1
    sleep 6
    "$ADB" -s $SERIAL exec-out screencap -p > $W-locale/launcher-$locale.png
    "$ADB" -s $SERIAL shell uiautomator dump /data/local/tmp/ui-$locale.xml > /dev/null 2>&1
    "$ADB" -s $SERIAL pull /data/local/tmp/ui-$locale.xml $W-locale/ui-$locale.xml > /dev/null 2>&1
    "$ADB" -s $SERIAL shell rm -f /data/local/tmp/ui-$locale.xml
    [ -s $W-locale/launcher-$locale.png ] && [ -s $W-locale/ui-$locale.xml ] || { echo "locale $locale: no picture or UI dump"; failed=$((failed + 1)); }
done
"$ADB" -s $SERIAL shell am force-stop dev.nebula.galaxy
"$ADB" -s $SERIAL shell cmd locale set-app-locales dev.nebula.galaxy --locales ""
echo "exit $failed"
exit $failed
