#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Builds Nebula for macOS and packs it as Nebula.app: the Metal renderer and Apple's own
# host, the game module beside it. Nothing of another system's is in the app.
#
#   build_macos.sh -t <tools> -w <work folder> [-a "x86_64 arm64"] [-r] [-s <macOS SDK>] [-j <jobs>] [-x <Xcode>]
#                  [-m <shipped mods>] [-n]
#
#   <tools>  holds toolchain/llvm (LLVM 19+), toolchain/libcxx (build_libcxx.sh's output) and deps (the dependency
#            checkouts platform/apple/CMakeLists.txt names). cmake, ninja and cargo are taken from PATH.
#   -a       the processors to build for (default: this machine's). Two make a universal app, which runs on Intel
#            Macs and on Apple silicon. The build machine's own comes first: its build makes the shader tools and the
#            shaders, which the other one takes.
#   -j       how many compiles run at once (default: the build tool's own choice).
#   -x       an Xcode's Contents/Developer folder, for its Metal compiler, when it is not the machine's selected one:
#            the programs are then compiled into the app's Metal libraries.
#   -m       a folder of the .luma files that come with the game (LumaKit, installer/lumakit): the app carries them in
#            Contents/Resources/luma/mods, where the game installs them from at start.
#   -n       an app with no mod support (CMake NEBULA_MODS=OFF; the environment variable NEBULA_MODS=OFF does the same): only
#            the plain game module, no LumaKit (-m is refused), no .luma type, mod archive type or nebula: link in the
#            Info.plist, no microphone or camera entitlement and no usage text.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
# Plan 2.9: every platform's release items (permissions, usage texts, file and link associations) are in the tree.
python3 "$root/tools/check_platform_items.py" "$root"
tools=
work=
archs=$(uname -m)
sdk=
jobs=
xcode=
shipped=
nomods=
[ "${NEBULA_MODS:-}" = OFF ] && nomods=1
while getopts "t:w:a:rs:j:x:m:n" option; do
    case "$option" in
    t) tools=$OPTARG ;;
    w) work=$OPTARG ;;
    a) archs=$OPTARG ;;
    s) sdk=$OPTARG ;;
    j) jobs="--parallel $OPTARG" ;;
    x) xcode=$OPTARG ;;
    m) shipped=$OPTARG ;;
    n) nomods=1 ;;
    *) sed -n '6,23p' "$0" >&2; exit 2 ;;
    esac
done
if [ -z "$tools" ] || [ -z "$work" ]; then
    sed -n '6,23p' "$0" >&2
    exit 2
fi
if [ -n "$nomods" ]; then
    # Every CMake project reads this (cmake/nebula_options.cmake).
    NEBULA_MODS=OFF
    export NEBULA_MODS
    [ -z "$shipped" ] || { echo "build_macos.sh: -n (no mod support) and -m (the shipped mods) do not go together" >&2; exit 2; }
fi
if [ -n "$shipped" ] && ! ls "$shipped"/*.luma > /dev/null 2>&1; then
    echo "the shipped mods folder has no .luma file: $shipped" >&2
    exit 2
fi
[ -n "$sdk" ] || sdk=$(xcrun --sdk macosx --show-sdk-path)

host_arch=$(uname -m)
# The build machine's own processor first.
ordered=
for arch in $archs; do
    [ "$arch" = "$host_arch" ] && ordered="$arch"
done
for arch in $archs; do
    [ "$arch" = "$host_arch" ] || ordered="$ordered $arch"
done

host_tree=
for arch in $ordered; do
    common="-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$here/toolchain.cmake -DNEBULA_APPLE_LLVM=$tools/toolchain/llvm -DNEBULA_APPLE_LIBCXX=$tools/toolchain/libcxx -DNEBULA_APPLE_PLATFORM=macos -DNEBULA_APPLE_ARCH=$arch -DNEBULA_APPLE_SDK=$sdk"
    # The players' build: ThinLTO for the game module (native/CMakeLists.txt NEBULA_LTO; plain where the linker cannot).
    # shellcheck disable=SC2086
    cmake -S "$root/native" -B "$work/macos-$arch/game" $common -DNEBULA_LTO=ON
    # shellcheck disable=SC2086
    # Both game modules: the host loads libsmg_game_plain.dylib unless a mod's code needs the rows (no mod support: the
    # plain one only).
    if [ -n "$nomods" ]; then
        cmake --build "$work/macos-$arch/game" --target smg_game_plain $jobs
    else
        cmake --build "$work/macos-$arch/game" --target smg_game smg_game_plain $jobs
    fi
    tree="$work/macos-$arch/host"
    if [ "$arch" = "$host_arch" ]; then
        # shellcheck disable=SC2086
        cmake -S "$here" -B "$tree" $common "-DNEBULA_DEPS=$tools/deps" "-DNEBULA_METAL_DEVELOPER_DIR=$xcode"
        host_tree=$tree
    else
        if [ -z "$host_tree" ]; then
            echo "a build for $arch on a $host_arch machine needs the $host_arch build too (-a \"$host_arch $arch\"): it makes the shader tools" >&2
            exit 2
        fi
        # The shaders are the same for every processor: the other build's are taken as they are (as new as its own
        # tools left them), so they are not compiled a second time.
        mkdir -p "$tree/sdl/generated" "$tree/generated"
        cp -Rp "$host_tree/sdl/generated/." "$tree/sdl/generated/"
        cp -Rp "$host_tree/generated/." "$tree/generated/"
        # shellcheck disable=SC2086
        cmake -S "$here" -B "$tree" $common "-DNEBULA_DEPS=$tools/deps" "-DNEBULA_HOST_TOOLS=$host_tree/host-tools" \
            "-DNEBULA_METAL_DEVELOPER_DIR=$xcode"
    fi
    # shellcheck disable=SC2086
    cmake --build "$tree" --target nebula_native $jobs
done

# The app: the program, and in Contents/Frameworks the game module.
app="$work/Nebula.app"
rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Frameworks" "$app/Contents/Resources"
# shellcheck disable=SC2086
set -- $ordered
if [ $# -gt 1 ]; then
    programs=
    modules=
    plains=
    for arch in $ordered; do
        programs="$programs $work/macos-$arch/host/Nebula"
        modules="$modules $work/macos-$arch/game/libsmg_game.dylib"
        plains="$plains $work/macos-$arch/game/libsmg_game_plain.dylib"
    done
    # shellcheck disable=SC2086
    lipo -create $programs -output "$app/Contents/MacOS/Nebula"
    if [ -z "$nomods" ]; then
        # shellcheck disable=SC2086
        lipo -create $modules -output "$app/Contents/Frameworks/libsmg_game.dylib"
    fi
    # shellcheck disable=SC2086
    lipo -create $plains -output "$app/Contents/Frameworks/libsmg_game_plain.dylib"
else
    cp "$work/macos-$1/host/Nebula" "$app/Contents/MacOS/Nebula"
    [ -n "$nomods" ] || cp "$work/macos-$1/game/libsmg_game.dylib" "$app/Contents/Frameworks/libsmg_game.dylib"
    cp "$work/macos-$1/game/libsmg_game_plain.dylib" "$app/Contents/Frameworks/libsmg_game_plain.dylib"
fi

# The programs' Metal libraries, when the build machine had Apple's compiler to make them (the same for every
# processor: the first build's).
for library in programs_2_4.metallib programs_3_0.metallib; do
    if [ -f "$host_tree/generated/$library" ]; then
        cp "$host_tree/generated/$library" "$app/Contents/Resources/$library"
    fi
done

# The mods that come with the game (the game installs them from here at start: galaxy/platform_paths.h
# package_directory).
if [ -n "$shipped" ]; then
    mkdir -p "$app/Contents/Resources/luma/mods"
    cp "$shipped"/*.luma "$app/Contents/Resources/luma/mods/"
fi

version=$(sed -n 's/^project(Nebula VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
# The system reads the three numbers as the app's version (a pre-release label is no part of CFBundleShortVersionString);
# the game's own version, with the label, is in the program (NEBULA_VERSION).
if [ -n "$nomods" ]; then
    # No mod support: the Info.plist has no .luma type, mod archive type, nebula: link or usage text.
    sed "s/@NEBULA_VERSION@/$version/g" "$here/Info.macos.plist.in" | sed '/<!-- mods:begin/,/<!-- mods:end -->/d' > "$app/Contents/Info.plist"
    python3 "$root/tools/package_text.py" apple "$app/Contents/Resources" --no-mods
else
    sed "s/@NEBULA_VERSION@/$version/g" "$here/Info.macos.plist.in" > "$app/Contents/Info.plist"
    # The Info.plist's player texts in every language the game ships.
    python3 "$root/tools/package_text.py" apple "$app/Contents/Resources"
fi

# The icon: every size of the iconset from the 1024 px picture.
iconset="$work/Nebula.iconset"
rm -rf "$iconset"
mkdir -p "$iconset"
icon="$root/installer/assets/nebula-1024.png"
if [ -f "$icon" ]; then
    for size in 16 32 128 256 512; do
        sips -z $size $size "$icon" --out "$iconset/icon_${size}x${size}.png" >/dev/null
        double=$((size * 2))
        sips -z $double $double "$icon" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
    done
    iconutil -c icns "$iconset" -o "$app/Contents/Resources/Nebula.icns" || echo "no icon: iconutil could not write it" >&2
else
    echo "no icon: installer/assets/nebula-1024.png is missing" >&2
fi

# Signed for this machine (an ad-hoc signature, which Apple silicon requires of every program it runs). An app given
# to someone else is opened the first time from System Settings > Privacy & Security ("Open Anyway"), or signed with
# a Developer ID and notarized instead.
# The hardened runtime, with Nebula.entitlements: the app is refused everything the system guards that it does not
# name there, without the player being asked (the microphone above all, which macOS otherwise asks for whenever the
# sound goes to a device that also has inputs).
[ -n "$nomods" ] || codesign --force --options runtime --sign - "$app/Contents/Frameworks/libsmg_game.dylib"
codesign --force --options runtime --sign - "$app/Contents/Frameworks/libsmg_game_plain.dylib"
entitlements="$here/Nebula.entitlements"
[ -z "$nomods" ] || entitlements="$here/Nebula-no-mods.entitlements"
codesign --force --options runtime --entitlements "$entitlements" --sign - "$app"
# A signature that does not hold over every file is what makes macOS call a downloaded app "damaged".
codesign --verify --deep --strict "$app"

# What is given to another Mac: the app zipped by ditto, which keeps the bundle and its signature as they are, and
# how to open it the first time.
package="$work/Nebula-macOS"
rm -rf "$package" "$work/Nebula-macOS.zip"
mkdir -p "$package"
ditto --norsrc --noextattr "$app" "$package/Nebula.app"
case " $ordered " in
    *" arm64 "*" x86_64 "* | *" x86_64 "*" arm64 "*) macs="Intel and Apple silicon" ;;
    *" arm64 "*) macs="Apple silicon" ;;
    *) macs="Intel" ;;
esac
echo "Nebula for Mac ($macs, macOS 12 or later)" > "$package/How to open Nebula.txt"
cat >> "$package/How to open Nebula.txt" <<'TEXT'

1. Unzip the download by opening it in the Finder (macOS's own unzip), and move Nebula.app to your Applications folder.
   Another program's unzip can break the app's signature.
2. Open it. macOS says it cannot check the app for malicious software (it is not from the App Store or a registered
   developer): choose Done, then open System Settings > Privacy & Security, scroll down, and choose "Open Anyway"
   next to Nebula. On macOS 14 or earlier you can instead Control-click the app and choose Open.
3. The first start asks for your Super Mario Galaxy disc image (.iso, or a folder it was extracted to) and sets the
   game up from it, once. Then the game prepares its graphics for your Mac (a bar shows how far) and starts.

The game's data, saves and settings are in ~/Library/Application Support/Nebula.

If macOS will not open Nebula, or Nebula says that the system refused a part of it: macOS marks every downloaded file as
coming from the internet, and it can refuse files with that mark that are not signed by Apple. Open Terminal and run the
next line (change the folder if Nebula.app is not in Applications), then open Nebula again:

    xattr -dr com.apple.quarantine /Applications/Nebula.app

It only removes that mark from Nebula's files. If Nebula still does not start, send us the file
~/Library/Application Support/Nebula/logs/game.log
TEXT
# No file's extended attributes go in: an unzip tool other than macOS's own writes them as "._" files inside the app,
# which breaks its signature. The signature needs none of them.
xattr -cr "$package"
ditto -c -k --norsrc --noextattr --keepParent "$package" "$work/Nebula-macOS.zip"
codesign --verify --deep --strict "$package/Nebula.app"
echo "$app"
echo "$work/Nebula-macOS.zip"
