#!/bin/sh
# audit_examples.sh: every example mod of the docs (copies in build/audit-examples), built for all nine platforms,
# checked, and its tests run in the game on the current build. Then the k lane's LumaScript tests.
LL=/d/NebulaWork/lumalink-work/lumalink-sweep/target/release/lumalink.exe
T=D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean
BIN=D:/NebulaWork/build/luma-bins/fp1
E=/d/NebulaWork/build/audit-examples
P=$E/progress.txt
: > $P
for d in $E/*/; do
    n=$(basename $d)
    "$LL" build --names $BIN/luma $d > $E/$n.build.txt 2>&1; b=$?
    "$LL" check $d > $E/$n.check.txt 2>&1; c=$?
    "$LL" test $d --bin $BIN --runner $T/tools/lumacore/luma_test.py --work D:/NebulaWork/build/audit-examples-work/$n \
        --lock D:/NebulaWork/build/audit-examples-work/run.lock > $E/$n.test.txt 2>&1; t=$?
    find /d/NebulaWork/build/audit-examples-work/$n -type d -name bin -prune -exec rm -rf {} + 2>/dev/null
    echo "$(date +%H:%M:%S) $n: build exit $b ($(grep -c 'bytes, from' $E/$n.build.txt) libraries), check exit $c ($(tail -1 $E/$n.check.txt)), test exit $t ($(grep -cE '^[^ ].* \(d3d12\): pass' $E/$n.test.txt) pass, $(grep -cE '^[^ ].* \(d3d12\): (fail|crashed|setup-failed|timeout)' $E/$n.test.txt) not)" >> $P
done
echo "$(date +%H:%M:%S) examples done" >> $P
