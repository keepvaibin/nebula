#!/bin/sh
# rt_inputs.sh BIN TAG: what ray tracing reads of each frame list, over the 102-stop tour (--diagnostics rt-inputs,
# docs/DIAGNOSTICS.md): the world's lights, checked against their rules and counted every 300 lists. Prints each
# kind's range over the tour and the faults; the lines themselves stay in the run's log. Exit 1 when a rule was
# broken or no list was checked.
set -u
B=$1
T=$2
DIR=$(cd "$(dirname "$0")" && pwd)
# The tour prints its stops, then what its checks found: the first line here, the rest in the run folder.
NEBULA_DIAGNOSTICS=rt-inputs sh "$DIR/tour.sh" "$B" "$T" GALAXY_TEST_DETERMINISTIC=1 | sed -n 1p
. "$DIR/common.sh"
L="$RUNS/$T/run.log"
tr -d '\000' < "$L" | grep "^\[rt-inputs\] lights" | awk '
    function range(name, value) {
        if (!(name in low) || value < low[name]) low[name] = value
        if (!(name in high) || value > high[name]) high[name] = value
    }
    {
        lines++
        for (i = 1; i < NF; i++) {
            v = $(i + 1) + 0
            if ($i == "holds") range("lights a list", v)
            if ($i == "(most" && v > most) most = v
            if ($i == "key" || $i == "fill" || $i == "point" || $i == "request" || $i == "sun" || $i == "core") range($i, v)
            if ($i == "camera") range("keys following the camera %", v)
            if ($i == "light" && $(i - 1) == "stage") range("lists with a stage light %", v)
            if ($i == "faults") faults += v
        }
    }
    END {
        printf "lines %d, most lights in a list %d, faults %d\n", lines, most, faults
        for (name in low) printf "  %s: %.2f to %.2f\n", name, low[name], high[name]
        exit !(lines > 0 && faults == 0)
    }'
