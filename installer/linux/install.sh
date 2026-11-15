#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Installs Nebula, or updates an installed copy.
#
#   ./install.sh [--disc <image or folder>] [--portable <folder>] [--yes]
#
# --disc      Your Super Mario Galaxy disc image (.iso, .rvz, .wbfs and others) or extracted game folder. Not
#             needed when updating an install that already has the game.
# --portable  Install into this folder. Saves and settings are kept in the folder.
#             Without --portable, Nebula installs to ~/.local/share/Nebula and adds a menu entry.
# --yes       Do not ask questions.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
version=$(cat "$here/VERSION")
disc=
portable=
ask=1
[ -t 0 ] || ask=0
while [ $# -gt 0 ]; do
    case $1 in
        --disc) disc=${2:?--disc needs a disc image or folder}; shift 2 ;;
        --portable) portable=${2:?--portable needs a folder}; shift 2 ;;
        --yes) ask=0; shift ;;
        -h | --help) sed -n '3,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install.sh: unknown option $1 (see ./install.sh --help)" >&2; exit 2 ;;
    esac
done

say() { printf '%s\n' "$*"; }
fail() { printf 'Nebula was not installed: %s\n' "$*" >&2; exit 1; }

# A file chooser when the desktop has one, else the terminal.
choose_disc() {
    if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && command -v zenity >/dev/null 2>&1; then
        zenity --file-selection --title="Choose your Super Mario Galaxy disc image" 2>/dev/null || true
    elif [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && command -v kdialog >/dev/null 2>&1; then
        kdialog --getopenfilename "$HOME" "*.iso *.rvz *.wbfs *.ciso *.gcz *.wia|Disc images" 2>/dev/null || true
    else
        printf 'Path to your Super Mario Galaxy disc image (.iso, .rvz, .wbfs) or extracted game folder: ' >&2
        read -r answer
        printf '%s' "$answer"
    fi
}

say "Nebula $version"
if [ -z "$portable" ] && [ "$ask" = 1 ]; then
    say ""
    say "How do you want to install Nebula?"
    say "  1) Install (recommended)"
    say "  2) Portable (choose a folder)"
    printf 'Choose 1 or 2 [1]: '
    read -r mode
    if [ "${mode:-1}" = 2 ]; then
        printf 'Folder for portable Nebula [%s]: ' "$PWD/Nebula"
        read -r portable
        [ -n "$portable" ] || portable=$PWD/Nebula
    fi
fi

if [ -n "$portable" ]; then
    mkdir -p "$portable" || fail "the folder $portable cannot be made"
    root=$(cd "$portable" && pwd)
    # An existing install for this user is not a portable folder.
    if [ -f "$root/current.json" ] && [ ! -f "$root/portable.txt" ]; then
        fail "$root holds an installed Nebula, not a portable one: choose another folder"
    fi
else
    root=${XDG_DATA_HOME:-$HOME/.local/share}/Nebula
    [ -f "$root/portable.txt" ] && fail "$root is a portable Nebula: give --portable $root to update it"
    mkdir -p "$root" || fail "the folder $root cannot be made"
fi

previous=
[ -f "$root/current.json" ] && previous=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$root/current.json")
have_game=0
[ -f "$root/content/game.pak" ] && have_game=1
if [ -z "$disc" ] && [ "$have_game" = 0 ]; then
    [ "$ask" = 1 ] || fail "give your disc image or extracted game folder with --disc"
    say ""
    say "Choose your Super Mario Galaxy disc image."
    disc=$(choose_disc)
fi
if [ -n "$disc" ]; then
    [ -f "$disc" ] || [ -d "$disc" ] || fail "$disc is not a disc image or folder"
    disc=$(cd "$(dirname "$disc")" && pwd)/$(basename "$disc")
fi

# The program: versions/<version>, the running one named by current.json.
target=$root/versions/$version
rm -rf "$target.new"
mkdir -p "$target.new"
cp -R "$here/files/." "$target.new/" || fail "the files could not be copied to $target"
rm -rf "$target"
mv "$target.new" "$target"
chmod 755 "$target/nebula" "$target/nebula-import"
if [ -n "$portable" ]; then
    printf 'Portable Nebula. Saves and settings are in the data folder.\n' > "$root/portable.txt"
fi

# The game, cooked from the disc image (the Windows Setup's import).
if [ -n "$disc" ]; then
    say ""
    say "Reading the disc image and preparing the game. This takes a few minutes."
    LD_LIBRARY_PATH="$target${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$target/nebula-import" import "$disc" --output "$root/content" ||
        fail "the disc image could not be read (is it Super Mario Galaxy?)"
fi
[ -f "$root/content/game.pak" ] || fail "there is no game in $root/content"

if [ -n "$previous" ] && [ "$previous" != "$version" ]; then
    printf '{\n  "version": "%s",\n  "previous": "%s"\n}\n' "$version" "$previous" > "$root/current.json"
else
    printf '{\n  "version": "%s",\n  "previous": null\n}\n' "$version" > "$root/current.json"
fi
cp "$here/nebula-launch" "$here/uninstall.sh" "$root/"
chmod 755 "$root/nebula-launch" "$root/uninstall.sh"

if [ -z "$portable" ]; then
    # The menu entry, the icon, .luma files and nebula:// links opening in Nebula, and nebula-launch on PATH.
    share=${XDG_DATA_HOME:-$HOME/.local/share}
    mkdir -p "$share/applications" "$share/mime/packages" "$share/icons/hicolor/256x256/apps" "$HOME/.local/bin"
    sed "s#^Exec=nebula-launch#Exec=\"$root/nebula-launch\"#" "$here/dev.nebula.galaxy.desktop" > "$share/applications/dev.nebula.galaxy.desktop"
    cp "$here/dev.nebula.galaxy.xml" "$share/mime/packages/dev.nebula.galaxy.xml"
    cp "$here/dev.nebula.galaxy.png" "$share/icons/hicolor/256x256/apps/dev.nebula.galaxy.png"
    ln -sf "$root/nebula-launch" "$HOME/.local/bin/nebula-launch"
    command -v update-mime-database >/dev/null 2>&1 && update-mime-database "$share/mime" >/dev/null 2>&1 || true
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$share/applications" >/dev/null 2>&1 || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q "$share/icons/hicolor" >/dev/null 2>&1 || true
fi

# Older versions go once the new one is in place.
for old in "$root"/versions/*; do
    [ -d "$old" ] && [ "$old" != "$target" ] && rm -rf "$old"
done

say ""
if [ -n "$portable" ]; then
    say "Portable Nebula $version is ready. To play, run: $root/nebula-launch"
else
    say "Nebula $version is installed. Start it from your app menu, or run: nebula-launch"
fi
