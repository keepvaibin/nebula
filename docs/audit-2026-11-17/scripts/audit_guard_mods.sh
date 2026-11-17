#!/bin/sh
. D:/NebulaWork/build/audit-conf/luma/perf_guard.sh

# build_guard_mods.sh KERNEL_COMMIT [TARGET]: the guard's test mods (agents/luma/g/mods-src/guard, contract guard.md 4)
# against luma.h, luma_services.h and luma_time.h of KERNEL_COMMIT (git show from Nebula-LG, never a tree a build
# reads), into build/lg-tests/guard/<target>/<mod id>.dll (.so for linux, aarch64). TARGET: x86_64-pc-windows-msvc
# (default), x86_64-linux-gnu, aarch64-linux-gnu. Divide (FAULT 3) is x86-64 only. Prints each library's sha256 prefix.
set -e
commit=current
target=${2:-x86_64-pc-windows-msvc}
G="/c/Program Files/Git/cmd/git.exe"
C="/c/Program Files/LLVM/bin/clang.exe"
inc=/d/NebulaWork/build/audit-conf/guard-inc
out=/d/NebulaWork/build/audit-conf/guard-mods/$target
mkdir -p "$inc" "$out"
for h in luma.h luma_services.h luma_time.h luma_ctors.ld; do
    cp /d/NebulaWork/MarioGalaxyRecomp/Nebula-Clean/native/include/$h "$inc/$h"
done
case $target in
    *windows*) ext=dll; link="-fuse-ld=lld -Wl,/NOENTRY -Wl,/NOIMPLIB -Wl,/Brepro" ;;
    *) ext=so; link="-fuse-ld=lld -fPIC -fno-use-init-array -Wl,--build-id=none -Wl,-T,$(cygpath -m "$inc/luma_ctors.ld")" ;;
esac
src=/d/NebulaWork/build/audit-conf/luma/g/mods-src/guard
one() { # id, source, defines...
    perf_guard_check  # between compiles: stops the script when perf.lock or perf.pending stands
    id=$1; file=$2; shift 2
    MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' "$C" --target=$target -O2 -shared -nostdlib -ffreestanding \
        -fno-stack-protector -fvisibility=hidden $link -Wall -Wextra -Werror -I "$(cygpath -m "$inc")" "$@" \
        -o "$(cygpath -m "$out/$id.$ext")" "$(cygpath -m "$src/$file")"
    echo "$id $(sha256sum "$out/$id.$ext" | cut -c1-16)"
}
faults="1 2 3"
case $target in aarch64*) faults="1 2" ;; esac
for where in 1 2 3 4 5 6 7 8 9; do
    for fault in $faults; do
        one "guard-$where-$fault" guard.c -DWHERE=$where -DFAULT=$fault
    done
done
one guard-nest-a guard_nest.c -DCASE=1
for fault in $faults; do
    one "guard-nest-b-$fault" guard_nest.c -DCASE=2 -DFAULT=$fault
done
one guard-deep guard_dead.c -DCASE=1
one guard-game guard_dead.c -DCASE=2
