#!/bin/sh
# audit_bench.sh: the built-in benchmark on each Windows renderer, one at a time, hidden, with present statistics,
# per-pass GPU times, frame-interval histograms and per-thread CPU recorded. Out: build/audit-bench/<renderer>/.
T=D:/NebulaWork/MarioGalaxyRecomp/Nebula-Clean
BIN=D:/NebulaWork/build/luma-bins/fp1
ROOT=${ROOT:-D:/NebulaWork/build/audit-bench}
PAK=$(tr -d '\r\n' < $BIN/pak.path)
P=$ROOT/progress.txt
mkdir -p $ROOT
for r in ${RENDERERS:-d3d12 vulkan d3d11}; do
    case $r in d3d12) exe=Nebula.exe ;; vulkan) exe=NebulaVk.exe ;; d3d11) exe=NebulaD11.exe ;; esac
    W=$ROOT/$r
    rm -rf $W; mkdir -p $W/settings $W/appdata $W/out
    [ -n "${WIDTH:-}" ] && python -I "$(cygpath -w $T/tools/verify/settings_args.py)" "$(cygpath -w $W/settings)" GALAXY_WINDOW_WIDTH=$WIDTH GALAXY_WINDOW_HEIGHT=$HEIGHT ${EXTRA:-}
    echo "$(date +%H:%M:%S) $r start ${WIDTH:-}x${HEIGHT:-}" >> $P
    env LOCALAPPDATA="$(cygpath -w $W/appdata)" \
        python -I "$(cygpath -w $T/tools/verify/start_hidden.py)" "$BIN/$exe" --pak "$PAK" --benchmark 1 \
        --benchmark-out "$(cygpath -w $W/out/benchmark.json)" --settings "$(cygpath -w $W/settings)" \
        --crash-dumps "$(cygpath -w $W/out)" --diagnostics ${DIAG:-present-stats,gpu-passes,frame-tails,thread-cpu} \
        > $W/out/run.log 2>&1
    echo "$(date +%H:%M:%S) $r exit $?: $(ls $W/out | tr '\n' ' ')" >> $P
done
echo "$(date +%H:%M:%S) bench done" >> $P
