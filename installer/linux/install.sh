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

# Started from a file manager (no terminal, no options): again in a terminal when the desktop has one, so that its
# questions and its outcome are seen; the window stays until Enter.
if [ ! -t 0 ] && [ "$#" = 0 ] && [ -z "${NEBULA_INSTALL_TERMINAL:-}" ] && [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    export NEBULA_INSTALL_TERMINAL=1
    for terminal in x-terminal-emulator gnome-terminal konsole xfce4-terminal xterm; do
        command -v "$terminal" >/dev/null 2>&1 || continue
        case $terminal in
            gnome-terminal) exec gnome-terminal --wait -- "$here/install.sh" ;;
            *) exec "$terminal" -e "$here/install.sh" ;;
        esac
    done
fi

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
# The end of a run in a terminal of its own (above): kept open until the player has read it.
done_reading() {
    if [ -n "${NEBULA_INSTALL_TERMINAL:-}" ] && [ -t 0 ]; then
        printf 'Press Enter to close this window.'
        read -r _ || true
    fi
}
# With no terminal at all, the desktop's own message box says it.
fail() {
    printf 'Nebula was not installed: %s\n' "$*" >&2
    if [ ! -t 2 ] && [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
        if command -v zenity >/dev/null 2>&1; then
            zenity --error --title=Nebula --text="Nebula was not installed: $*" 2>/dev/null || true
        elif command -v kdialog >/dev/null 2>&1; then
            kdialog --title Nebula --error "Nebula was not installed: $*" 2>/dev/null || true
        fi
    fi
    done_reading
    exit 1
}

# The package is for one processor (package-linux.sh): its programs' ELF machine against this one's.
machine_of() {
    od -An -tx1 -j18 -N2 "$1" 2>/dev/null | tr -d ' \n'
}
case $(uname -m) in
    x86_64 | amd64) wanted=3e00 ;;
    aarch64 | arm64) wanted=b700 ;;
    *) wanted= ;;
esac
have=$(machine_of "$here/files/nebula")
if [ -n "$wanted" ] && [ -n "$have" ] && [ "$have" != "$wanted" ]; then
    case $have in 3e00) built="x86-64 (Intel and AMD)" ;; b700) built="ARM64" ;; *) built="another processor" ;; esac
    fail "this package is for $built PCs, and this one is $(uname -m): download the package for $(uname -m)"
fi

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

# The game, cooked from the disc image (the Windows Setup's import), beside the game there is: it takes the game's
# place only once whole, so a disc that cannot be read leaves the game there was.
if [ -n "$disc" ]; then
    say ""
    say "Reading the disc image and preparing the game. This takes a few minutes."
    rm -rf "$root/content.new" "$root/content.old"
    LD_LIBRARY_PATH="$target${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$target/nebula-import" import "$disc" --output "$root/content.new" ||
        { rm -rf "$root/content.new"; fail "the disc image could not be read (is it Super Mario Galaxy?)"; }
    [ -f "$root/content.new/game.pak" ] || { rm -rf "$root/content.new"; fail "the disc image gave no game"; }
    [ -d "$root/content" ] && mv "$root/content" "$root/content.old"
    mv "$root/content.new" "$root/content"
    rm -rf "$root/content.old"
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

# Older versions go once the new one is in place, except the one that ran before it: an update from inside the game
# (the update's install step) runs this while that version still runs and may still load files from its folder.
for old in "$root"/versions/*; do
    [ -d "$old" ] || continue
    case ${old##*/} in
        "$version" | "$previous") ;;
        *) rm -rf "$old" ;;
    esac
done

say ""
if [ -n "$portable" ]; then
    say "Portable Nebula $version is ready. To play, run: $root/nebula-launch"
else
    say "Nebula $version is installed. Start it from your app menu, or run: nebula-launch"
fi
