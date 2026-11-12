#!/bin/sh
# Builds the libraries tests/libraries.rs reads from lib.c, with LLVM (clang, lld-link, ld.lld, ld64.lld), against
# native/include/luma.h, reproducibly, into this folder; then prints each file's sha256 prefix. Run from anywhere:
#   sh crates/luma-validate/tests/data/build.sh
# The outputs are committed so the tests need no compiler; rebuild after lib.c or luma.h changes.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
llvm=${LLVM:-"/c/Program Files/LLVM/bin"}
cc="$llvm/clang.exe"
[ -x "$cc" ] || cc="$llvm/clang"
# A Windows clang reads Windows paths (Git Bash gives /d/...).
inc=$(cygpath -m "$root/native/include" 2>/dev/null || echo "$root/native/include")
cd "$here"
rm -rf pe-x64 pe-arm64 elf-x64 elf-arm64 elf-x64-relr macho-arm64 macho-arm64-chained macho-x64
mkdir -p pe-x64 pe-arm64 elf-x64 elf-arm64 elf-x64-relr macho-arm64 macho-arm64-chained macho-x64 tmp

common="-O2 -nostdlib -ffreestanding -fno-stack-protector -fvisibility=hidden -Wall -Wextra -Werror -I $inc"
win="-fuse-ld=lld -Wl,/NOENTRY -Wl,/Brepro"
variants="GOOD MISSING WRONG_SIGNATURE NO_SIGNATURE OPTIONAL_MISSING CONSTRUCTOR_WRAP CONSTRUCTOR_AFTER REPLACE BAD_ROW NEWER SHORT_INFO NO_IMPORTS NO_EXPORT SERVICE SERVICE_BAD SERVICE_MORE MOD_FUNCTION MOD_FUNCTION_BAD METHOD METHOD_BAD"
lower() { echo "$1" | tr 'A-Z' 'a-z'; }

# Windows x64: every variant, an extra library pair, and a program.
for v in $variants; do
    "$cc" --target=x86_64-pc-windows-msvc -shared $common $win -Wl,/NOIMPLIB -D$v -o "pe-x64/$(lower $v).dll" lib.c
done
"$cc" --target=x86_64-pc-windows-msvc -shared $common $win -Wl,/IMPLIB:tmp/extra.lib -DEXTRA -o pe-x64/extra.dll lib.c
"$cc" --target=x86_64-pc-windows-msvc -shared $common $win -Wl,/NOIMPLIB -DUSES_EXTRA -o pe-x64/uses_extra.dll lib.c tmp/extra.lib
"$cc" --target=x86_64-pc-windows-msvc -shared $common $win -Wl,/NOIMPLIB -DHELPER -o pe-x64/helper.dll lib.c tmp/extra.lib
"$cc" --target=x86_64-pc-windows-msvc $common -fuse-ld=lld -Wl,/ENTRY:entry -Wl,/SUBSYSTEM:CONSOLE -Wl,/Brepro -DPROGRAM -o pe-x64/program.exe lib.c
"$cc" --target=aarch64-pc-windows-msvc -shared $common $win -Wl,/NOIMPLIB -DGOOD -o pe-arm64/good.dll lib.c
# Code the system runs at load (an entry point), refused (L509 Lib.RunsAtLoad); likewise for ELF and Mach-O below.
"$cc" --target=x86_64-pc-windows-msvc -shared $common -fuse-ld=lld -Wl,/ENTRY:dll_entry -Wl,/Brepro -Wl,/NOIMPLIB -DGOOD -DRUNS_AT_LOAD -o pe-x64/runs_at_load.dll lib.c
# A TLS directory: data alone loads; a callback runs at load and close and is refused (A-3).
"$cc" --target=x86_64-pc-windows-msvc -shared $common $win -Wl,/NOIMPLIB -Wl,/INCLUDE:_tls_used -DGOOD -DTLS_DATA -o pe-x64/tls_data.dll lib.c
"$cc" --target=x86_64-pc-windows-msvc -shared $common $win -Wl,/NOIMPLIB -Wl,/INCLUDE:_tls_used -DGOOD -DTLS_CALLBACK -o pe-x64/tls_callback.dll lib.c

# ELF: x64 with RELA relocations, x64 with RELR (packed relative), arm64; an extra library pair on x64.
elf="-shared -fPIC -fuse-ld=lld -Wl,--build-id=none"
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DGOOD -o elf-x64/good.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DMISSING -o elf-x64/missing.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DNO_EXPORT -o elf-x64/no_export.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -Wl,-soname,extra.so -DEXTRA -o elf-x64/extra.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DUSES_EXTRA -o elf-x64/uses_extra.so lib.c elf-x64/extra.so
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -Wl,-z,pack-relative-relocs -DGOOD -o elf-x64-relr/good.so lib.c
"$cc" --target=aarch64-unknown-linux-gnu $common $elf -DGOOD -o elf-arm64/good.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DGOOD -DRUNS_AT_LOAD -o elf-x64/runs_at_load.so lib.c
# Code the system runs when it closes the library (A-1): DT_FINI_ARRAY, DT_FINI; Mach-O __mod_term_func below.
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DGOOD -DRUNS_AT_CLOSE -o elf-x64/runs_at_close.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DSERVICE -o elf-x64/service.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DMOD_FUNCTION -o elf-x64/mod_function.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -Wl,-fini,fini_function -DGOOD -DFINI_FUNCTION -o elf-x64/fini.so lib.c

# Mach-O: arm64 with the classic rebase opcodes, arm64 with chained fixups, x64.
macho="-shared -fuse-ld=lld -Wl,-undefined,error"
"$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -DGOOD -o macho-arm64/good.dylib lib.c
"$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -DMISSING -o macho-arm64/missing.dylib lib.c
"$cc" --target=arm64-apple-macos13 $common $macho -Wl,-fixup_chains -DGOOD -o macho-arm64-chained/good.dylib lib.c
"$cc" --target=x86_64-apple-macos11 $common $macho -Wl,-no_fixup_chains -DGOOD -o macho-x64/good.dylib lib.c
"$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -DGOOD -DRUNS_AT_LOAD -o macho-arm64/runs_at_load.dylib lib.c
"$cc" --target=arm64-apple-macos13 $common $macho -Wl,-fixup_chains -DGOOD -DRUNS_AT_LOAD -o macho-arm64-chained/runs_at_load.dylib lib.c
"$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -DGOOD -DRUNS_AT_CLOSE -o macho-arm64/runs_at_close.dylib lib.c

rm -rf tmp
find . -type f ! -name '*.c' ! -name '*.sh' | sort | while read -r f; do
    echo "$(sha256sum "$f" | cut -c1-16) $(wc -c < "$f") $f"
done
