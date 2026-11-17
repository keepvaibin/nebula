#!/bin/sh
# audit_tour.sh: the 102-stop galaxy tour on each Windows renderer, one after another, each with its own work folder
# and fresh empty saves. Results in D:/NebulaWork/build/audit-tour/progress.txt.
T=D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean
BIN=${BIN:-D:/NebulaWork/build/luma-bins/fp1}
ROOT=D:/NebulaWork/build/audit-tour
PROG=$ROOT/progress.txt
mkdir -p $ROOT
for r in ${RENDERERS:-d3d12 vk d3d11}; do
    case $r in d3d12) exe=Nebula.exe ;; vk) exe=NebulaVk.exe ;; d3d11) exe=NebulaD11.exe ;; esac
    W=$ROOT/$r
    rm -rf $W; mkdir -p $W/saves $W/settings
    export NEBULA_WORK=$W NEBULA_EXE=$exe
    export NEBULA_PAK="$(tr -d '\r\n' < $BIN/pak.path)"
    export NEBULA_SAVES="$(cygpath -w $W/saves)" NEBULA_SETTINGS="$(cygpath -w $W/settings)"
    echo "$(date '+%H:%M:%S') $r start" >> $PROG
    sh $T/tools/verify/tour.sh $BIN tour_$r > $ROOT/tour_$r.out 2>&1
    echo "$(date '+%H:%M:%S') $r exit $?: $(tail -4 $ROOT/tour_$r.out | tr '\n' ' ' | cut -c1-400)" >> $PROG
done
echo "$(date '+%H:%M:%S') tours done" >> $PROG
