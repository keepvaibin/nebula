#!/bin/sh
# audit_devices.sh tablet|emulator: the earlier lane's device suites with this audit's test build.
B=D:/NebulaWork/build
D=/d/NebulaWork/agents/luma/d/platforms/android
export S=$B/audit-and-src ONE=$B/audit-one-luma TREE=Nebula-Clean STORE=$B/all1-pak-34/game.pak
SP=/c/Users/<user>/AppData/Local/Temp/claude/9d73c8bc852b5521/1261821a-bc5f-4a25-91d3-ad9793865d41/scratchpad
case $1 in
tablet)
    export W=$B/audit-tab
    KEEP=1 sh "$SP/audit_run_phone_rows.sh" audit R52WA0FXE5J > $B/audit-tablet.txt 2>&1
    echo "$(date +%H:%M:%S) tablet: $(tail -1 $B/audit-tablet.txt)" >> $B/audit-devices-progress.txt ;;
emulator)
    export W=$B/audit-emu INSTALL=1 SERIAL=emulator-5554
    sh "$SP/audit_run_android_rows.sh" audit > $B/audit-emulator.txt 2>&1
    echo "$(date +%H:%M:%S) emulator: $(tail -1 $B/audit-emulator.txt)" >> $B/audit-devices-progress.txt ;;
esac
