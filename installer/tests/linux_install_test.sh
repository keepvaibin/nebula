#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# installer/linux/install.sh with a stand-in package (P-126): a disc that cannot be read leaves the game there was; a
# package for another processor is refused; a portable install and an update work.
#
#   linux_install_test.sh <installer/linux folder>
set -eu

scripts=$(cd "$1" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failed=0
check() {
    if eval "$2"; then :; else printf 'FAILED: %s\n' "$1" >&2; failed=1; fi
}

# A program's head as ELF says it: the machine at byte 18 (x86-64 0x3e, ARM64 0xb7).
elf() {
    case $2 in
        3e00) machine='\076\000' ;;
        b700) machine='\267\000' ;;
        *) machine='\003\000' ;;
    esac
    printf '\177ELF\002\001\001\000\000\000\000\000\000\000\000\000\002\000' > "$1"
    printf "$machine" >> "$1"
    chmod 755 "$1"
}
case $(uname -m) in
    x86_64 | amd64) here=3e00; other=b700 ;;
    *) here=b700; other=3e00 ;;
esac

package=$work/package
mkdir -p "$package/files"
cp "$scripts/install.sh" "$scripts/uninstall.sh" "$scripts/nebula-launch" "$package/"
for file in dev.nebula.galaxy.desktop dev.nebula.galaxy.xml dev.nebula.galaxy.png; do : > "$package/$file"; done
printf '9.9.9\n' > "$package/VERSION"
elf "$package/files/nebula" "$here"
# The importer: a disc named bad*.iso writes part of a game and fails; any other writes "game <disc name>".
cat > "$package/files/nebula-import" <<'IMPORT'
#!/bin/sh
disc=$2
output=$4
mkdir -p "$output"
case $(basename "$disc") in
    bad*) printf 'part' > "$output/game.pak"; exit 1 ;;
esac
printf 'game %s' "$(basename "$disc")" > "$output/game.pak"
IMPORT
chmod 755 "$package/files/nebula-import"
: > "$work/first.iso"
: > "$work/bad.iso"
: > "$work/second.iso"

root=$work/portable
sh "$package/install.sh" --yes --portable "$root" --disc "$work/first.iso" < /dev/null > "$work/out.txt" 2>&1 ||
    { cat "$work/out.txt" >&2; failed=1; }
check "a portable install has its game" '[ "$(cat "$root/content/game.pak")" = "game first.iso" ]'
check "and its program" '[ -f "$root/versions/9.9.9/nebula" ] && [ -f "$root/portable.txt" ]'

if sh "$package/install.sh" --yes --portable "$root" --disc "$work/bad.iso" < /dev/null > "$work/out.txt" 2>&1; then
    printf 'FAILED: an unreadable disc did not fail\n' >&2
    failed=1
fi
check "an unreadable disc leaves the game there was" '[ "$(cat "$root/content/game.pak")" = "game first.iso" ]'
check "and nothing half made" '[ ! -e "$root/content.new" ] && [ ! -e "$root/content.old" ]'
check "and says why" 'grep -q "could not be read" "$work/out.txt"'

sh "$package/install.sh" --yes --portable "$root" --disc "$work/second.iso" < /dev/null > "$work/out.txt" 2>&1 ||
    { cat "$work/out.txt" >&2; failed=1; }
check "a new disc replaces the game" '[ "$(cat "$root/content/game.pak")" = "game second.iso" ]'

elf "$package/files/nebula" "$other"
if sh "$package/install.sh" --yes --portable "$work/other" --disc "$work/first.iso" < /dev/null > "$work/out.txt" 2>&1; then
    printf 'FAILED: a package for another processor was installed\n' >&2
    failed=1
fi
check "another processor's package is refused" 'grep -q "this package is for" "$work/out.txt"'
check "before anything is written" '[ ! -e "$work/other/versions" ]'

if [ "$failed" = 0 ]; then echo "linux install: every case holds"; else echo "linux install: FAILED"; fi
exit "$failed"
