#!/bin/sh
# build_int_android.sh NAME COMMIT (row 8.2 a): the integration commit's Android x86_64 build in Nebula-IntA, for the
# emulator suite (8.2 b) and the backends (8.2 c):
#   1. Nebula-IntA (detached) at COMMIT; with TREE=<worktree folder name> that worktree instead, which must already be
#      at COMMIT with nothing uncommitted (it is not checked out: a lane's branch stays where it is);
#   2. platform/android/build_apk.ps1 -Debuggable -ReleaseKey '' (no release-key signature: the key is the owner's,
#      and the script's default reads it from his profile): game modules, SDL host, importers, Gradle (build/NAME-apk), its
#      shader programs compiled by HOST_TOOLS: the Vulkan host build of the same commit (galaxy_native_spirv;
#      f/build-host.ps1 -Vulkan). Another commit's tools refuse its programs (int0421's: no location for the M1
#      materials' BLENDWEIGHT, 704 of 31780 stages);
#   3. the test extension (libnebula_test_extension.so) and the game half (luma-test.so) with the NDK;
#   4. the extension put beside the app's libraries and Gradle run again: NAME-apk/Nebula-debug.apk;
#   5. the suite's bin folder build/NAME-android-bin: libsmg_game.so, the extension, luma-test.so, luma_types.json.
# SHIPPED: a folder of .luma files the app carries (LumaKit), passed as -ShippedMods. The test runner (Harness.java)
# is always in (-Harness).
# ABIS: the app's ABIs, default x86_64 (the emulator); "x86_64,arm64-v8a" adds the phone's, its bins in
# build/NAME-android-bin-arm64 and its test libraries in build/NAME-android-test-arm64.
# Writes build/NAME-android.txt; its last line "android ok COMMIT" or "android FAILED: <step>".
set -u
N=$1
C=$2
B=D:/NebulaWork/build
T=D:/NebulaWork/MarioGalaxyRecomp/${TREE:-Nebula-IntA}
G="/c/Program Files/Git/cmd/git.exe"
NB="$LOCALAPPDATA/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin"
L=$B/$N-android.txt
W=$B/$N-apk
O=$B/$N-android-test
BIN=$B/$N-android-bin
ABIS=${ABIS:-x86_64}
HT=${HOST_TOOLS:?HOST_TOOLS: the Vulkan host build of COMMIT, e.g. D:/NebulaWork/build/NAME-vk}
say() { echo "$(date +%H:%M:%S) $*" >> $L; }
fail() { say "android FAILED: $*"; exit 1; }
: > $L
say "build $N at $C, host tools $HT"
[ -f "$HT/galaxy_native_spirv.exe" ] || fail "no galaxy_native_spirv.exe in $HT"
cd $T || fail "no tree"
if [ -z "${TREE:-}" ]; then
    "$G" checkout -q --detach "$C" >> $L 2>&1 || fail "checkout"
else
    :
    :
fi
say "tree at $("$G" log --oneline -1)"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File platform/android/build_apk.ps1 -Deps 'D:\NebulaWork\deps' -Work "D:\\NebulaWork\\build\\$N-apk" -HostTools "$(cygpath -w "$HT")" -Abis $ABIS -Debuggable -TestTools -Harness "$(cygpath -w "$T/tools/test_driver/android/java")" ${SHIPPED:+-ShippedMods "$(cygpath -w "$SHIPPED")"} -ReleaseKey '' > $B/$N-apk.txt 2>&1 || fail "build_apk (see $N-apk.txt)"
say "apk: $(tail -1 $B/$N-apk.txt)"
for abi in $(echo $ABIS | tr ',' ' '); do
    case $abi in x86_64) triple=x86_64-linux-android31; sfx= ;; arm64-v8a) triple=aarch64-linux-android31; sfx=-arm64 ;; *) fail "unknown ABI $abi" ;; esac
    mkdir -p $O$sfx
    "$NB/clang++.exe" --target=$triple -std=c++20 -O2 -fPIC -shared -static-libstdc++ -Wl,--no-undefined -Iruntime/include -Inative/include tools/test_driver/extension/test_extension.cpp tools/test_driver/extension/harness.cpp tools/test_driver/extension/test_services.cpp tools/test_driver/extension/test_plugin.cpp -ldl -llog -o $O$sfx/libnebula_test_extension.so > $O$sfx/ext.txt 2>&1 || fail "test extension $abi (see $O$sfx/ext.txt)"
    "$NB/clang.exe" --target=$triple -O2 -shared -fPIC -nostdlib -ffreestanding -fno-stack-protector -fvisibility=hidden -fno-use-init-array -fuse-ld=lld -Wl,-T,native/include/luma_ctors.ld -Wl,--no-undefined -Wl,-z,defs -Wall -Wextra -Inative/include tools/test_driver/luma_test/luma_test.c -o $O$sfx/luma-test.so > $O$sfx/half.txt 2>&1 || fail "game half $abi (see $O$sfx/half.txt)"
    cp $O$sfx/libnebula_test_extension.so platform/android/jniLibs/$abi/ || fail "extension copy $abi"
done
say "extension and half built ($ABIS)"
(cd platform/android && JAVA_HOME="${JAVA_HOME:-C:/Program Files/Android/Android Studio/jbr}" ANDROID_HOME="$LOCALAPPDATA/Android/Sdk" ./gradlew.bat --no-daemon assembleRelease -PnebulaAbis=$ABIS "-Pnebula.sdlDir=D:\\NebulaWork\\deps\\SDL3-3.4.18" "-Pnebula.jniDir=$T/platform/android/jniLibs" "-Pnebula.assetsDir=D:\\NebulaWork\\build\\$N-apk\\assets" -Pnebula.debuggable=true "-Pnebula.harness=$(cygpath -w "$T/tools/test_driver/android/java")" > $B/$N-apk-debug.txt 2>&1) || fail "debug apk (see $N-apk-debug.txt)"
for abi in $(echo $ABIS | tr ',' ' '); do rm -f platform/android/jniLibs/$abi/libnebula_test_extension.so; done
cp platform/android/app/build/outputs/apk/release/app-release.apk $W/Nebula-debug.apk || fail "debug apk copy"
for abi in $(echo $ABIS | tr ',' ' '); do
    case $abi in x86_64) sfx= ;; arm64-v8a) sfx=-arm64 ;; esac
    mkdir -p $BIN$sfx
    cp $W/module-$abi/libsmg_game.so $W/module-$abi/libsmg_game_plain.so $O$sfx/libnebula_test_extension.so $O$sfx/luma-test.so $W/module-$abi/luma_types/luma_types.json $BIN$sfx/ || fail "bin folder $abi"
done
say "android ok $C"
