#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Packs a Linux build of Nebula for players: Nebula-<version>-linux-<arch>.tar.gz. Unpacked, its install.sh asks for
# the player's disc image and installs for the user (~/.local/share/Nebula, a menu entry, .luma files and nebula://
# links opening in Nebula) or as a portable folder, in the layout LumaLink reads (current.json, versions/<version>,
# content/game.pak). The game ships no test parts (tools/release_check.ps1); LumaLink's Setup has the test kit.
#
#   installer/package-linux.sh --bin <folder> --version <version> [--shipped <folder>] [--output <folder>]
#
# --bin      a Linux build's program folder: nebula, libsmg_game.so, libsmg_game_plain.so, libSDL3.so.0,
#            nebula-import, libnebula_import.so, and the game's self-description (luma/ or the files it is made of)
# --shipped  the mods the game comes with (*.luma: LumaKit), installed in luma/mods
# --output   where the package goes (default: dist/)
set -eu

bin=
version=
shipped=
output=
while [ $# -gt 0 ]; do
    case $1 in
        --bin) bin=${2:?}; shift 2 ;;
        --version) version=${2:?}; shift 2 ;;
        --shipped) shipped=${2:?}; shift 2 ;;
        --output) output=${2:?}; shift 2 ;;
        *) echo "package-linux.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done
[ -n "$bin" ] && [ -n "$version" ] || { echo "package-linux.sh: --bin and --version are required" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/.." && pwd)
bin=$(cd "$bin" && pwd)
[ -n "$output" ] || output=$repo/dist
mkdir -p "$output"
output=$(cd "$output" && pwd)

case $(file -b "$bin/nebula" 2>/dev/null) in
    *aarch64* | *ARM*) arch=arm64 ;;
    *) arch=x64 ;;
esac
name=Nebula-$version-linux-$arch
work=$(mktemp -d "${TMPDIR:-/tmp}/nebula-package.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM
pkg=$work/$name
files=$pkg/files
mkdir -p "$files/luma"

for f in nebula libsmg_game.so libsmg_game_plain.so nebula-import libnebula_import.so; do
    [ -f "$bin/$f" ] || { echo "package-linux.sh: $bin has no $f" >&2; exit 1; }
    cp "$bin/$f" "$files/"
done
cp -P "$bin"/libSDL3.so* "$files/" 2>/dev/null || { echo "package-linux.sh: $bin has no libSDL3.so" >&2; exit 1; }
for f in libnebula_test_extension.so luma-test.so; do
    [ -f "$bin/$f" ] && echo "package-linux.sh: $f is a test part: left out"
done
# The self-description LumaLink and the game read from luma/.
if [ -d "$bin/luma" ]; then
    cp -R "$bin/luma/." "$files/luma/"
else
    for f in luma_types.json luma_functions.json luma_numbers.json smg_game.luma-placement.json smg_game.luma-self.json luma_services.json; do
        [ -f "$bin/$f" ] && cp "$bin/$f" "$files/luma/"
    done
fi
rm -rf "$files/luma/test"
mkdir -p "$files/luma/sdk"
cp "$repo"/native/include/luma*.h "$repo"/native/include/luma*.ld "$files/luma/sdk/" 2>/dev/null || cp "$repo"/native/include/luma*.h "$files/luma/sdk/"
if [ -n "$shipped" ]; then
    mkdir -p "$files/luma/mods"
    cp "$shipped"/*.luma "$files/luma/mods/"
fi
for f in LICENSE THIRD-PARTY-NOTICES.md README.md; do cp "$repo/$f" "$files/"; done

cp "$here/linux/install.sh" "$here/linux/uninstall.sh" "$here/linux/nebula-launch" "$pkg/"
chmod 755 "$pkg/install.sh" "$pkg/uninstall.sh" "$pkg/nebula-launch" "$files/nebula" "$files/nebula-import"
cp "$repo/platform/linux/dev.nebula.galaxy.desktop" "$repo/platform/linux/dev.nebula.galaxy.xml" "$pkg/"
printf '%s\n' "$version" > "$pkg/VERSION"
# The icon: the largest picture in installer/assets/nebula.ico (each is a PNG or a BMP; the PNG is taken).
python3 - "$repo/installer/assets/nebula.ico" "$pkg/dev.nebula.galaxy.png" <<'EOF'
import struct, sys
data = open(sys.argv[1], "rb").read()
count = struct.unpack_from("<H", data, 4)[0]
best = None
for i in range(count):
    w, h, _, _, _, _, size, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
    png = data[offset:offset + size]
    if png[:8] == b"\x89PNG\r\n\x1a\n" and (best is None or (w or 256) > best[0]):
        best = (w or 256, png)
if best is None:
    sys.exit("nebula.ico has no PNG picture")
open(sys.argv[2], "wb").write(best[1])
EOF
cat > "$pkg/README.txt" <<EOF
Nebula $version for Linux ($arch)

To install, open a terminal in this folder and run:

    ./install.sh

It asks for your Super Mario Galaxy disc image, then whether to install for this user (with a menu entry) or as a
portable folder. ./install.sh --help lists the options. To remove Nebula later, run uninstall.sh in the folder it was
installed to.
EOF

tar -C "$work" -czf "$output/$name.tar.gz" "$name"
if command -v sha256sum >/dev/null; then sum=$(sha256sum "$output/$name.tar.gz"); else sum=$(shasum -a 256 "$output/$name.tar.gz"); fi
echo "$output/$name.tar.gz  $(wc -c < "$output/$name.tar.gz" | tr -d ' ') bytes  SHA-256 ${sum%% *}"
