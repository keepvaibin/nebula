#!/bin/sh
# audit_conf.sh STEP [LANE/SUITE ...]: the old conformance suites against the current tree's build, from a copy.
#   copy   copies the lanes to $L (paths in their scripts rewritten to the copy); the originals are not touched
#   mods   builds every lane's test mods against the current tree's headers
#   run    runs the named suites (default: all), one game run at a time, under build/measure.lock
T=D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean
SRC=D:/NebulaWork/agents/luma
B=D:/NebulaWork/build
L=$B/audit-conf/luma
D=${BIN:-$B/luma-bins/fp1}
W=${WORK:-$B/audit-conf/work}
OUT=${OUTDIR:-$B/audit-conf/out}
PROG=${PROGRESS:-$B/audit-conf/progress.txt}
LOCK=$B/measure.lock
mkdir -p $B/audit-conf $OUT; LOCK=${LOCKFILE:-$LOCK}
say() { echo "$(date '+%H:%M:%S') $*" >> $PROG; echo "$*"; }
step=$1; shift
case $step in
copy)
    rm -rf $L; mkdir -p $L
    for lane in f fb g h j k; do
        mkdir -p $L/$lane
        (cd $SRC/$lane && tar cf - --exclude=runs --exclude=build --exclude=__pycache__ --exclude='*.pdb' .) | (cd $L/$lane && tar xf -)
    done
    cp $SRC/perf_guard.sh $L/
    grep -rlI "D:/NebulaWork/agents/luma" $L --include=*.sh --include=*.py --include=*.ps1 2>/dev/null | while read f; do
        sed -i "s|D:/NebulaWork/agents/luma|$L|g" "$f"
    done
    say "copied: $(find $L -name test.json | wc -l) test.json, $(ls $L/*/tests/*/suite.json | wc -l) suites"
    ;;
mods)
    for s in $L/*/mods-src/*/build.sh; do
        name=$(echo $s | sed "s|$L/||; s|/build.sh||")
        r=$(LUMA_INCLUDE=$T/native/include LUMA_TEST_INCLUDE=$T/tools/test_driver/extension sh $s 2>&1)
        code=$?
        echo "$r" > $OUT/mods-$(echo $name | tr '/' '-').txt
        say "mods $name exit $code: $(echo "$r" | grep -c 'exit 0') built, $(echo "$r" | grep -c 'exit [1-9]') failed"
    done
    ;;
run)
    if [ -e $LOCK ]; then say "measure.lock stands: $(cat $LOCK); nothing run"; exit 75; fi
    echo "audit $(date '+%m-%d %H:%M')" > $LOCK
    trap 'rm -f $LOCK' EXIT INT TERM
    if [ $# = 0 ]; then set -- $(ls $L/*/tests/*/suite.json | sed "s|$L/||; s|/tests/|/|; s|/suite.json||"); fi
    for ls_ in "$@"; do
        lane=${ls_%%/*}; suite=${ls_##*/}
        case "$suite" in probe|patch|macho) continue ;; esac
        s=$L/$lane/tests/$suite/suite.json
        [ -f $s ] || s=$L/$lane/tests/$suite
        o=$OUT/$lane-$suite.txt
        (cd D:/NebulaWork && python -X utf8 -I $T/tools/lumacore/luma_test.py run $s --bin $D --work $W --who audit \
            --lock $B/audit-conf/run.lock --commit audit-fp1 ${RENDERER:+--renderer $RENDERER} > $o 2>&1)
        rc=$?
        # The run's copies of the programs are not evidence: removed so the drive does not fill (logs and results stay).
        find $W/runs/$suite -type d -name bin -prune -exec rm -rf {} + 2>/dev/null
        say "run $lane/$suite exit $rc: $(grep -cE ' pass( |$)' $o) pass lines, $(grep -cE ' (fail|setup-failed|crash|timeout|refused)( |$)' $o) not-pass lines"
    done
    say "run done"
    ;;
esac
