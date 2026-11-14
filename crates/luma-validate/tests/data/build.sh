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
variants="GOOD MISSING WRONG_SIGNATURE NO_SIGNATURE OPTIONAL_MISSING CONSTRUCTOR_WRAP CONSTRUCTOR_AFTER REPLACE BAD_ROW NEWER SHORT_INFO NO_IMPORTS NO_EXPORT SERVICE SERVICE_BAD SERVICE_MORE MOD_FUNCTION MOD_FUNCTION_BAD MOD_FUNCTION_OTHER_SIGNATURE MOD_FUNCTION_MISSING LIBRARY_MOD METHOD METHOD_BAD"
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
# luma.h's runners read the bounds of luma_ctors and luma_dtors, which luma_ctors.ld makes.
ctors=$(cygpath -m "$root/native/include/luma_ctors.ld" 2>/dev/null || echo "$root/native/include/luma_ctors.ld")
elf="-shared -fPIC -fuse-ld=lld -Wl,--build-id=none -Wl,-T,$ctors"
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DGOOD -o elf-x64/good.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DMISSING -o elf-x64/missing.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DNO_EXPORT -o elf-x64/no_export.so lib.c
# The extra library is no mod: it has no runners, and no .data for luma_ctors.ld to follow.
"$cc" --target=x86_64-unknown-linux-gnu $common -shared -fPIC -fuse-ld=lld -Wl,--build-id=none -Wl,-soname,extra.so -DEXTRA -o elf-x64/extra.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DUSES_EXTRA -o elf-x64/uses_extra.so lib.c elf-x64/extra.so
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -Wl,-z,pack-relative-relocs -DGOOD -o elf-x64-relr/good.so lib.c
"$cc" --target=aarch64-unknown-linux-gnu $common $elf -DGOOD -o elf-arm64/good.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DGOOD -DRUNS_AT_LOAD -o elf-x64/runs_at_load.so lib.c
# Code the system runs when it closes the library (A-1): DT_FINI_ARRAY, DT_FINI; Mach-O __mod_term_func below.
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DGOOD -DRUNS_AT_CLOSE -o elf-x64/runs_at_close.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DSERVICE -o elf-x64/service.so lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DMOD_FUNCTION -o elf-x64/mod_function.so lib.c
# A library mod's exports and the mods importing from it (L505), on each format.
for v in MOD_FUNCTION_OTHER_SIGNATURE MOD_FUNCTION_MISSING LIBRARY_MOD; do
    "$cc" --target=x86_64-unknown-linux-gnu $common $elf -D$v -o "elf-x64/$(lower $v).so" lib.c
done
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
for v in MOD_FUNCTION MOD_FUNCTION_OTHER_SIGNATURE MOD_FUNCTION_MISSING LIBRARY_MOD; do
    "$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -D$v -o "macho-arm64/$(lower $v).dylib" lib.c
done
"$cc" --target=arm64-apple-macos13 $common $macho -Wl,-fixup_chains -DLIBRARY_MOD -o macho-arm64-chained/library_mod.dylib lib.c
# A library that needs another (L504): an extra library; one that binds a symbol of it, in the bind table and in the
# chained fixups' imports; one that only loads it.
"$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -Wl,-install_name,@loader_path/extra.dylib -DEXTRA -o macho-arm64/extra.dylib lib.c
"$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -DUSES_EXTRA -DEXTRA_BY_POINTER -o macho-arm64/uses_extra.dylib lib.c macho-arm64/extra.dylib
"$cc" --target=arm64-apple-macos13 $common $macho -Wl,-fixup_chains -DUSES_EXTRA -DEXTRA_BY_POINTER -o macho-arm64-chained/uses_extra.dylib lib.c macho-arm64/extra.dylib
"$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -Wl,-needed_library,macho-arm64/extra.dylib -DGOOD -o macho-arm64/needs_extra.dylib lib.c

# A LumaScript runtime layout in LumaModInfo (L513): a library mod of layout 1, and mods importing from it of layout 2
# (refused) and 1 (bound), on each format.
"$cc" --target=x86_64-pc-windows-msvc -shared $common $win -Wl,/NOIMPLIB -DLIBRARY_MOD -DRUNTIME_LAYOUT=1 -o pe-x64/library_mod_layout1.dll lib.c
"$cc" --target=x86_64-unknown-linux-gnu $common $elf -DLIBRARY_MOD -DRUNTIME_LAYOUT=1 -o elf-x64/library_mod_layout1.so lib.c
"$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -DLIBRARY_MOD -DRUNTIME_LAYOUT=1 -o macho-arm64/library_mod_layout1.dylib lib.c
"$cc" --target=arm64-apple-macos13 $common $macho -Wl,-fixup_chains -DLIBRARY_MOD -DRUNTIME_LAYOUT=1 -o macho-arm64-chained/library_mod_layout1.dylib lib.c
for n in 1 2; do
    "$cc" --target=x86_64-pc-windows-msvc -shared $common $win -Wl,/NOIMPLIB -DMOD_FUNCTION -DRUNTIME_LAYOUT=$n -o pe-x64/mod_function_layout$n.dll lib.c
    "$cc" --target=x86_64-unknown-linux-gnu $common $elf -DMOD_FUNCTION -DRUNTIME_LAYOUT=$n -o elf-x64/mod_function_layout$n.so lib.c
    "$cc" --target=arm64-apple-macos11 $common $macho -Wl,-no_fixup_chains -DMOD_FUNCTION -DRUNTIME_LAYOUT=$n -o macho-arm64/mod_function_layout$n.dylib lib.c
done

rm -rf tmp
find . -type f ! -name '*.c' ! -name '*.sh' | sort | while read -r f; do
    echo "$(sha256sum "$f" | cut -c1-16) $(wc -c < "$f") $f"
done
