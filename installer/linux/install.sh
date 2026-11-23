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
#
# Started in a terminal, it asks there. Started without one (a file manager, a launcher), it asks and answers in windows
# of the desktop (zenity or kdialog, and notify-send or xmessage for answers); with none of those it writes its answer to
# nebula-message.txt beside this file. It never ends without saying why.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
support=$here/support
told=0

# Says `$2` where the person will see it: `$1` is "error" or "info". In a terminal: there. Else in a window of the desktop
# (zenity, then kdialog; then notify-send or xmessage); else in nebula-message.txt beside this file (or in the home folder).
# NEBULA_TERMINAL=1 says there is a terminal (the package's own test).
terminal() { [ -t 0 ] || [ -t 1 ] || [ -t 2 ] || [ -n "${NEBULA_TERMINAL:-}" ]; }
has_screen() { [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; }
has_window_tool() { has_screen && { command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1; }; }
tell() {
    told=1
    if terminal; then
        if [ "$1" = error ]; then printf '%s\n' "$2" >&2; else printf '%s\n' "$2"; fi
        return 0
    fi
    if has_screen; then
        if command -v zenity >/dev/null 2>&1; then
            status=0
            zenity "--$1" --no-markup --title=Nebula --text="$2" >/dev/null 2>&1 || status=$?
            if [ "$status" -gt 1 ]; then
                status=0
                zenity "--$1" --title=Nebula --text="$2" >/dev/null 2>&1 || status=$?
            fi
            [ "$status" -le 1 ] && return 0
        fi
        if command -v kdialog >/dev/null 2>&1; then
            if [ "$1" = error ]; then kind=--error; else kind=--msgbox; fi
            kdialog --title Nebula "$kind" "$2" >/dev/null 2>&1 && return 0
        fi
        if command -v notify-send >/dev/null 2>&1; then
            notify-send Nebula "$2" >/dev/null 2>&1 && return 0
        fi
        if command -v xmessage >/dev/null 2>&1; then
            xmessage -center "$2" >/dev/null 2>&1 && return 0
        fi
    fi
    file=$here/nebula-message.txt
    printf '%s\n' "$2" > "$file" 2>/dev/null || { file=${HOME:-/tmp}/nebula-message.txt; printf '%s\n' "$2" > "$file" 2>/dev/null || true; }
    return 0
}
# A note while something takes long, where a person without a terminal can see it (it does not wait for an answer).
announce() {
    if terminal; then printf '%s\n' "$1"; return 0; fi
    has_screen || return 0
    if command -v notify-send >/dev/null 2>&1; then notify-send Nebula "$1" >/dev/null 2>&1 || true
    elif command -v kdialog >/dev/null 2>&1; then kdialog --title Nebula --passivepopup "$1" 15 >/dev/null 2>&1 &
    elif command -v zenity >/dev/null 2>&1; then zenity --info --no-wrap --title=Nebula --text="$1" --timeout=15 >/dev/null 2>&1 &
    fi
    return 0
}
say() { printf '%s\n' "$*"; }
fail() { tell error "Nebula was not installed: $*"; exit 1; }
# A command that ended the script with no word of its own: the person is told it stopped.
trap 'code=$?; if [ "$code" != 0 ] && [ "$told" = 0 ]; then told=1; tell error "Nebula was not installed: the installer stopped with an error (code $code). Run ./install.sh in a terminal to see why."; fi' EXIT

[ -f "$support/VERSION" ] || fail "this is not the whole download: the folder support is missing beside install.sh. Unpack the archive again and run install.sh from the folder it made."
version=$(cat "$support/VERSION")
disc=
portable=
ask=0
windows=0
if [ -t 0 ]; then
    ask=1
elif has_window_tool; then
    ask=1
    windows=1
fi
while [ $# -gt 0 ]; do
    case $1 in
        --disc) [ $# -ge 2 ] || fail "--disc needs a disc image or folder after it"; disc=$2; shift 2 ;;
        --portable) [ $# -ge 2 ] || fail "--portable needs a folder after it"; portable=$2; shift 2 ;;
        --yes) ask=0; shift ;;
        -h | --help) sed -n '3,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) fail "unknown option $1 (see ./install.sh --help)" ;;
    esac
done

# A file chooser when the desktop has one, else the terminal.
choose_disc() {
    if has_screen && command -v zenity >/dev/null 2>&1; then
        zenity --file-selection --title="Choose your Super Mario Galaxy disc image" 2>/dev/null || true
    elif has_screen && command -v kdialog >/dev/null 2>&1; then
        kdialog --getopenfilename "$HOME" "*.iso *.rvz *.wbfs *.ciso *.gcz *.wia|Disc images" 2>/dev/null || true
    elif [ -t 0 ]; then
        printf 'Path to your Super Mario Galaxy disc image (.iso, .rvz, .wbfs) or extracted game folder: ' >&2
        read -r answer
        printf '%s' "$answer"
    fi
}

say "Nebula $version"
if [ -z "$portable" ] && [ "$ask" = 1 ] && [ "$windows" = 0 ]; then
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
    [ "$ask" = 1 ] || fail "give your disc image or extracted game folder with --disc (./install.sh --disc <file>), or run ./install.sh in a terminal to be asked"
    say ""
    say "Choose your Super Mario Galaxy disc image."
    disc=$(choose_disc)
    [ -n "$disc" ] || fail "no disc image was chosen. Run install.sh again and choose your Super Mario Galaxy disc image (.iso, .rvz or .wbfs)."
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
    announce "Reading the disc image and preparing the game. This takes a few minutes. Nebula tells you when it is done."
    LD_LIBRARY_PATH="$target${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$target/nebula-import" import "$disc" --output "$root/content" ||
        fail "the disc image could not be read (is it Super Mario Galaxy?)"
fi
[ -f "$root/content/game.pak" ] || fail "there is no game in $root/content"

if [ -n "$previous" ] && [ "$previous" != "$version" ]; then
    printf '{\n  "version": "%s",\n  "previous": "%s"\n}\n' "$version" "$previous" > "$root/current.json"
else
    printf '{\n  "version": "%s",\n  "previous": null\n}\n' "$version" > "$root/current.json"
fi
cp "$support/nebula-launch" "$support/uninstall.sh" "$root/"
chmod 755 "$root/nebula-launch" "$root/uninstall.sh"

if [ -z "$portable" ]; then
    # The menu entry, the icon, .luma files and nebula:// links opening in Nebula, and nebula-launch on PATH.
    share=${XDG_DATA_HOME:-$HOME/.local/share}
    mkdir -p "$share/applications" "$share/mime/packages" "$share/icons/hicolor/256x256/apps" "$HOME/.local/bin"
    sed "s#^Exec=nebula-launch#Exec=\"$root/nebula-launch\"#" "$support/dev.nebula.galaxy.desktop" > "$share/applications/dev.nebula.galaxy.desktop"
    # The .luma type: a package with no mod support has no type file.
    [ ! -f "$support/dev.nebula.galaxy.xml" ] || cp "$support/dev.nebula.galaxy.xml" "$share/mime/packages/dev.nebula.galaxy.xml"
    cp "$support/dev.nebula.galaxy.png" "$share/icons/hicolor/256x256/apps/dev.nebula.galaxy.png"
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
    tell info "Portable Nebula $version is ready. To play, run: $root/nebula-launch"
else
    tell info "Nebula $version is installed. Start it from your app menu (Nebula), or run: nebula-launch"
fi
