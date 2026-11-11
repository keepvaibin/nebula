#!/bin/sh
# static_analysis.sh BUILD_DIR [OUT_FILE]: clang-tidy over the game module (native/petari/src), with the checks of the
# .clang-tidy at the repository root, from the compile_commands.json in BUILD_DIR (configure the game module with
# -DCMAKE_EXPORT_COMPILE_COMMANDS=ON). Every finding goes to OUT_FILE (default BUILD_DIR/static_analysis.txt), grouped by
# source file, with a count by check at the end; the counts are printed too. Environment:
#   CLANG_TIDY   the clang-tidy to run (default: clang-tidy on PATH)
#   FILTER       only the database files whose path holds this text (default native/petari/src/; the host: runtime/src/ or
#                native/host/, which read runtime/.clang-tidy and native/host/.clang-tidy: the root list plus narrowing,
#                unchecked results of C file and allocation calls, and the analyzer's C++ and unix groups)
#   TESTS=1      also check the *_tests.cpp files (default: left out)
#   JOBS         files checked at once (default: the processor count)
#   STRICT=1     exit 1 when any finding is left (default: exit 0 after writing the report)
# This script was written without being run: the first run shows which checks are too noisy for this code, and the
# .clang-tidy list is then narrowed, not this script.
#
# Why each group of checks is on (the list is in .clang-tidy):
#   bugprone-*            Defects clang's warnings do not cover: use after move, string and memory function misuse
#                         (sizeof of a pointer, strncpy without a terminator), integer division where a float was meant,
#                         assignments in conditions, switches without a case, suspicious comparisons, infinite loops.
#                         The game came from code that was only ever built for a fixed compiler; this is the group that
#                         finds what a second compiler stopped hiding. Off inside it, for noise on this code:
#                         easily-swappable-parameters (every two-int function), implicit-widening-of-multiplication-result
#                         (index arithmetic everywhere), macro-parentheses (the game's own macros), narrowing-conversions
#                         (the f32/s32 mixing is audited by native/tools/narrowing.py and narrow_float_audit.py) and
#                         reserved-identifier (the C library layer of the module).
#   performance-*         Avoidable cost: copies of large parameters and loop variables,
#                         needless value parameters, string and container misuse, move of a const. Off:
#                         no-int-to-ptr (the host calls hand pointers over as integers by design).
#   modernize-use-nullptr  NULL and 0 used as pointers: an ambiguity in overload resolution, a real bug class, not style.
#   modernize-use-override A virtual function that no longer overrides anything after a base change silently becomes a
#                         new function. Every override has the keyword; this keeps it so.
#   readability-*         Only checks that find mistakes: container-size-empty (size() == 0 on a container whose empty()
#                         is cheaper), delete-null-pointer (an if around delete), duplicate-include, misleading-indentation
#                         (an if body that is not the next line), redundant-control-flow, redundant-smartptr-get,
#                         simplify-boolean-expr (a condition that always has one value), string-compare (compare() == 0
#                         where == is meant). Naming, braces and magic-number checks stay off: they only churn.
#   clang-analyzer-core.* The static analyzer's null dereference, division by zero, uninitialized value and stack address
#                         checks along each path: what the compiler's warnings cannot see across a call. Slow, so only
#                         core; DeadStores for assigned values never read. The cplusplus and unix groups stay off for the
#                         game module: its operator new returns null when full (-fcheck-new), which they report on every
#                         new. The host (runtime/, native/host) turns them on in its own .clang-tidy, with narrowing
#                         conversions and cert-err33-c on a short list of C calls (use after move, leaks, an ignored
#                         fclose or fread, a size narrowed to int).
set -u

BUILD=${1:-}
if [ -z "$BUILD" ]; then
    echo "usage: static_analysis.sh BUILD_DIR [OUT_FILE]" >&2
    exit 2
fi
OUT=${2:-$BUILD/static_analysis.txt}
DB=$BUILD/compile_commands.json
if [ ! -f "$DB" ]; then
    echo "static_analysis.sh: $DB not found (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)" >&2
    exit 2
fi
TIDY=${CLANG_TIDY:-clang-tidy}
if ! command -v "$TIDY" >/dev/null 2>&1; then
    echo "static_analysis.sh: $TIDY not found; set CLANG_TIDY" >&2
    exit 2
fi
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
if [ ! -f "$REPO/.clang-tidy" ]; then
    echo "static_analysis.sh: $REPO/.clang-tidy not found" >&2
    exit 2
fi
FILTER=${FILTER:-native/petari/src/}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

# The database's files (a JSON string per "file" key; Windows paths may carry doubled backslashes), one per line.
sed -n 's/^ *"file": *"\([^"]*\)".*$/\1/p' "$DB" | sed 's|\\\\|/|g' | grep -F "$FILTER" | sort -u > "$WORK/files.txt"
if [ "${TESTS:-0}" != "1" ]; then
    grep -v '_tests\.cpp$' "$WORK/files.txt" > "$WORK/files.notests" || true
    mv "$WORK/files.notests" "$WORK/files.txt"
fi
COUNT=$(wc -l < "$WORK/files.txt" | tr -d ' ')
if [ "$COUNT" -eq 0 ]; then
    echo "static_analysis.sh: no file in $DB holds \"$FILTER\"" >&2
    exit 2
fi

# One input file per source, so the workers write apart and the report keeps the database's order.
n=0
while IFS= read -r file; do
    n=$((n + 1))
    printf '%s\n' "$file" > "$WORK/$n.src"
done < "$WORK/files.txt"

export TIDY BUILD WORK
find "$WORK" -name '*.src' | xargs -P "$JOBS" -I{} sh -c 'f=$(cat "$1"); "$TIDY" -p "$BUILD" --quiet "$f" > "$1.out" 2>&1' sh {}

: > "$OUT"
i=1
while [ "$i" -le "$n" ]; do
    {
        printf '== %s\n' "$(cat "$WORK/$i.src")"
        cat "$WORK/$i.src.out"
    } >> "$OUT"
    i=$((i + 1))
done

# The findings by check ("... [bugprone-foo,bugprone-bar]" ends a finding's first line). A header finding repeats in
# every file that includes it; it is counted once by place and check.
grep -E '^[^ ].*:[0-9]+:[0-9]+: (warning|error): .*\[[A-Za-z0-9_.,-]+\]$' "$OUT" | sort -u > "$WORK/findings.txt"
TOTAL=$(wc -l < "$WORK/findings.txt" | tr -d ' ')
{
    echo
    echo "== $COUNT files checked, $TOTAL findings (by check):"
    sed 's/^.*\[\([A-Za-z0-9_.,-]*\)\]$/\1/' "$WORK/findings.txt" | tr ',' '\n' | sort | uniq -c | sort -rn
} >> "$OUT"
tail -n +"$(grep -n '^== [0-9]* files checked' "$OUT" | tail -1 | cut -d: -f1)" "$OUT"
echo "report: $OUT"

if [ "${STRICT:-0}" = "1" ] && [ "$TOTAL" -ne 0 ]; then
    exit 1
fi
exit 0
