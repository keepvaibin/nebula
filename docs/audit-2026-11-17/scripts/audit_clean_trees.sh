#!/bin/sh
# audit_clean_trees.sh OUT: for every git work tree of the Nebula repository and every LumaLink checkout, one line:
# path | branch or "detached" | files changed and not committed | whether its commit is on a branch that is on GitHub
# | whether it is on any local branch. Reads only: nothing is changed.
OUT=$1
: > $OUT
one() {
    d=$1
    [ -e "$d/.git" ] || { echo "$d | not a git tree | - | - | -" >> $OUT; return; }
    branch=$(git -C "$d" symbolic-ref -q --short HEAD 2>/dev/null || echo detached)
    head=$(git -C "$d" rev-parse --short HEAD 2>/dev/null)
    dirty=$(git -C "$d" status --porcelain 2>/dev/null | wc -l)
    remote=$(git -C "$d" branch -r --contains HEAD 2>/dev/null | head -1 | tr -d ' ')
    local_=$(git -C "$d" branch --contains HEAD 2>/dev/null | grep -v "detached" | head -1 | tr -d ' *+')
    echo "$d | $branch $head | $dirty | ${remote:-NOT-ON-GITHUB} | ${local_:-no-local-branch}" >> $OUT
}
git -C /d/NebulaWork/MarioGalaxyRecomp/Nebula-Clean worktree list --porcelain | sed -n 's/^worktree //p' | while read -r w; do one "$w"; done
for d in /d/NebulaWork/lumalink-work/*/; do one "${d%/}"; done
# Folders beside the work trees that are not registered work trees.
for d in /d/NebulaWork/MarioGalaxyRecomp/*/; do
    d=${d%/}
    grep -q "^$(cygpath -m "$d") |" $OUT 2>/dev/null || grep -qi "$(basename "$d") |" $OUT || one "$d"
done
echo done >> $OUT.done
