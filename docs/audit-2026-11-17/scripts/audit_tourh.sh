#!/bin/sh
# audit_tourh.sh [SUITE]: the 102-stop tour as harness tests, on each renderer in turn. Out: build/audit-tourh/<suite>-<renderer>.txt
T=D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean
TB=D:/NebulaWork/build/audit-tourh
SUITE=${1:-tour}
P=$TB/progress-$SUITE.txt
cd D:/NebulaWork
for r in ${RENDERERS:-d3d12 vulkan d3d11}; do
    echo "$(date +%H:%M:%S) $SUITE $r start" >> $P
    python -X utf8 -I $T/tools/lumacore/luma_test.py run $TB/tests/$SUITE/suite.json --bin D:/NebulaWork/build/luma-bins/fp1 \
        --work $TB/work-$SUITE-$r --who audit --lock $TB/run-$SUITE.lock --commit audit-fp1 --renderer $r > $TB/$SUITE-$r.txt 2>&1
    rc=$?
    find $TB/work-$SUITE-$r/runs -type d -name bin -prune -exec rm -rf {} + 2>/dev/null
    echo "$(date +%H:%M:%S) $SUITE $r exit $rc: $(grep -cE ' pass( |$)' $TB/$SUITE-$r.txt) pass, $(grep -cE ' (fail|crashed|setup-failed|timeout|harness-error)( |$)' $TB/$SUITE-$r.txt) not" >> $P
done
echo "$(date +%H:%M:%S) $SUITE done" >> $P
