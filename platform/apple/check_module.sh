#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The checks of a packed Nebula.app that need a Mac: its program finds its game module and the system loads it. Run by
# release.yml and check.yml on the app as unpacked from the zip.
#
#   check_module.sh <Nebula.app> <scratch folder> [<command that starts a program for one processor, e.g. "arch -x86_64">]
#
# 1. The program starts with no game files and loads the module it would load at a start (--module-check 1, host_main.cpp):
#    "module check: <file> loaded". A module the program does not find, or the system refuses, ends it with the reason.
# 2. The same check must refuse a module that is not there (a --module in Contents/MacOS, where the beta's app was looked
#    for it) and a file that is no module, each with the sentence a player's report would carry (game_module.h load_failure).
#    A check that cannot fail is no check.
set -u
app=$1
scratch=$2
launcher=${3:-}
program="$app/Contents/MacOS/Nebula"
mkdir -p "$scratch/data" "$scratch/settings"
# --hidden 1: a failed load must not open the player's crash dialog and wait for a click.
common="--hidden 1 --data $scratch/data --settings $scratch/settings --crash-dumps $scratch"

# shellcheck disable=SC2086
if ! output=$($launcher "$program" --module-check 1 $common 2>&1); then
    echo "$output"
    echo "check_module.sh: the program did not load its game module"
    exit 1
fi
echo "$output"
case "$output" in
    *"module check: "*" loaded ("*) ;;
    *) echo "check_module.sh: no 'module check: ... loaded' line"; exit 1 ;;
esac

# shellcheck disable=SC2086
if output=$($launcher "$program" --module-check 1 --module "$app/Contents/MacOS/libsmg_game.dylib" $common 2>&1); then
    echo "$output"
    echo "check_module.sh: a module that is not in Contents/MacOS was loaded"
    exit 1
fi
case "$output" in
    *"cannot load the game module libsmg_game"*": the file is not there (Contents/MacOS)"*) echo "refused as it should: a module that is not there" ;;
    *) echo "$output"; echo "check_module.sh: the refusal of a missing module does not say so"; exit 1 ;;
esac

fake="$scratch/not-a-module"
rm -rf "$fake"
mkdir -p "$fake"
echo "this is not a Mach-O file" > "$fake/libsmg_game_plain.dylib"
# shellcheck disable=SC2086
if output=$($launcher "$program" --module-check 1 --module "$fake/libsmg_game.dylib" $common 2>&1); then
    echo "$output"
    echo "check_module.sh: a file that is no module was loaded"
    exit 1
fi
case "$output" in
    *"cannot load the game module libsmg_game"*": the system refused it: "*) echo "refused as it should: a file that is no module" ;;
    *) echo "$output"; echo "check_module.sh: the refusal of a bad file does not say so"; exit 1 ;;
esac
echo "check_module.sh: the module loads, and a missing or bad one is refused with its reason"
