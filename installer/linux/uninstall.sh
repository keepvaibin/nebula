#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Removes Nebula from this folder. Saves, settings, mods and the prepared game stay unless --all is given.
#
#   ./uninstall.sh [--all]
set -eu
root=$(cd "$(dirname "$(readlink -f "$0" 2>/dev/null || printf '%s' "$0")")" && pwd)
all=0
[ "${1:-}" = "--all" ] && all=1
rm -rf "$root/versions" "$root/current.json" "$root/nebula-launch"
if [ ! -f "$root/portable.txt" ]; then
    share=${XDG_DATA_HOME:-$HOME/.local/share}
    rm -f "$share/applications/dev.nebula.galaxy.desktop" "$share/mime/packages/dev.nebula.galaxy.xml" \
        "$share/icons/hicolor/256x256/apps/dev.nebula.galaxy.png"
    [ -L "$HOME/.local/bin/nebula-launch" ] && rm -f "$HOME/.local/bin/nebula-launch"
    command -v update-mime-database >/dev/null 2>&1 && update-mime-database "$share/mime" >/dev/null 2>&1 || true
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$share/applications" >/dev/null 2>&1 || true
fi
if [ "$all" = 1 ]; then
    rm -rf "$root/content" "$root/saves" "$root/settings" "$root/mods" "$root/data" "$root/portable.txt"
    echo "Nebula and its saves, settings, mods and game files were removed."
else
    echo "Nebula was removed. Your saves, settings, mods and game files are still in $root."
fi
rm -f "$root/uninstall.sh"
rmdir "$root" 2>/dev/null || true
