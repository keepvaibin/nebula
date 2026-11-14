#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# LumaLink for Linux and macOS: install.sh [--window] [--command] [--path] [--uninstall]
#
# Run from the unpacked archive (lumalink: the command line, lumalink-gui: the window). With no option it asks for
# each: the window, the command line, and whether the command line's folder goes on PATH. Everything goes into the
# user's own folders; nothing needs root.
#   Linux:  ~/.local/share/lumalink, and the window's menu entry ~/.local/share/applications/lumalink.desktop
#   macOS:  ~/Applications/LumaLink
# PATH: one line in the shell's profile (~/.zprofile for zsh, else ~/.profile), marked so --uninstall takes it out.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
case "$(uname -s)" in
    Darwin) dest="$HOME/Applications/LumaLink" ;;
    *) dest="${XDG_DATA_HOME:-$HOME/.local/share}/lumalink" ;;
esac
desktop="${XDG_DATA_HOME:-$HOME/.local/share}/applications/lumalink.desktop"
mark="# LumaLink on PATH (install.sh)"
case "${SHELL:-}" in
    */zsh) profile="$HOME/.zprofile" ;;
    *) profile="$HOME/.profile" ;;
esac

drop_path_line() {
    for file in "$HOME/.zprofile" "$HOME/.profile"; do
        [ -f "$file" ] && grep -qF "$mark" "$file" || continue
        { grep -vF "$mark" "$file" || true; } > "$file.lumalink-tmp"
        mv "$file.lumalink-tmp" "$file"
    done
}

ask() {  # ask QUESTION: 0 for yes
    printf '%s [y/n] ' "$1"
    read -r answer || answer=n
    case "$answer" in y|Y|yes|Yes) return 0 ;; *) return 1 ;; esac
}

window=0 command=0 path=0 asked=0
for option in "$@"; do
    case "$option" in
        --window) window=1; asked=1 ;;
        --command) command=1; asked=1 ;;
        --path) path=1; asked=1 ;;
        --uninstall)
            rm -rf "$dest"
            rm -f "$desktop"
            drop_path_line
            echo "LumaLink removed from $dest."
            exit 0 ;;
        *) echo "usage: install.sh [--window] [--command] [--path] [--uninstall]" >&2; exit 2 ;;
    esac
done
if [ "$asked" = 0 ]; then
    ask "Install the LumaLink window?" && window=1
    ask "Install the LumaLink command line?" && command=1
    [ "$command" = 1 ] && ask "Add LumaLink to PATH?" && path=1
fi
[ "$command" = 1 ] || path=0
[ "$window" = 1 ] || [ "$command" = 1 ] || { echo "Nothing chosen."; exit 0; }

mkdir -p "$dest"
rm -f "$dest/lumalink" "$dest/lumalink-gui"
if [ "$command" = 1 ]; then
    cp "$here/lumalink" "$dest/lumalink"
    chmod 755 "$dest/lumalink"
fi
if [ "$window" = 1 ]; then
    cp "$here/lumalink-gui" "$dest/lumalink-gui"
    chmod 755 "$dest/lumalink-gui"
    if [ "$(uname -s)" != Darwin ]; then
        mkdir -p "$(dirname "$desktop")"
        printf '[Desktop Entry]\nType=Application\nName=LumaLink\nComment=Make, check and pack mods\nExec="%s"\nTerminal=false\nCategories=Development;Game;\n' \
            "$dest/lumalink-gui" > "$desktop"
    fi
else
    rm -f "$desktop"
fi
for file in LICENSE README.md; do [ -f "$here/$file" ] && cp "$here/$file" "$dest/"; done

drop_path_line
if [ "$path" = 1 ]; then
    printf 'export PATH="%s:$PATH" %s\n' "$dest" "$mark" >> "$profile"
    echo "Added to PATH in $profile: open a new terminal to use lumalink."
fi
echo "LumaLink installed in $dest."
