"""Ranks translated RMGE01 functions by game-thread samples for the module's link order.

Reads the [sim-profile-guest] rows that a session run with GALAXY_SIM_PROFILE=1 and
GALAXY_SIM_PROFILE_ROWS=100000 prints when it exits, adds up the samples of each
function over every log given, and writes the table the translator uses
(crates/nebula-recomp-core/src/hot_functions.rs).

usage: python hot_functions.py OUTPUT.rs SESSION_STDERR.txt [SESSION_STDERR.txt ...]
"""
import re
import sys
from collections import Counter

ROW = re.compile(r'^\[sim-profile-guest\] fn=0x([0-9A-Fa-f]{8}) samples=(\d+)')
PER_LINE = 7

HEADER = '''//! Translated RMGE01 functions ranked by game-thread samples, most-run first.
//!
//! Written by `tools/perf/hot_functions.py` from the `[sim-profile-guest]` rows of
//! sessions run with `GALAXY_SIM_PROFILE=1` and `GALAXY_SIM_PROFILE_ROWS=100000`.
//! The module build links these functions first, in this order.

/// Guest addresses of the {count} functions that received samples, most-run first.
#[rustfmt::skip]
pub const RMGE01_HOT_FUNCTIONS: &[u32] = &[
'''


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    samples = Counter()
    for path in argv[2:]:
        with open(path, encoding='utf-8', errors='replace') as log:
            for line in log:
                match = ROW.match(line)
                if match:
                    samples[int(match.group(1), 16)] += int(match.group(2))
    if not samples:
        sys.exit('no [sim-profile-guest] rows in the given logs')
    ranked = sorted(samples, key=lambda address: (-samples[address], address))
    literals = ['0x%04X_%04X' % (address >> 16, address & 0xFFFF) for address in ranked]
    rows = ['    ' + ', '.join(literals[i:i + PER_LINE]) + ','
            for i in range(0, len(literals), PER_LINE)]
    with open(argv[1], 'w', encoding='utf-8', newline='\n') as out:
        out.write(HEADER.format(count=len(ranked)))
        out.write('\n'.join(rows) + '\n];\n')
    total = sum(samples.values())
    covered = 0
    for share in (0.5, 0.8, 0.9, 0.99):
        count = 0
        covered = 0
        for address in ranked:
            if covered >= share * total:
                break
            covered += samples[address]
            count += 1
        print('%d%% of samples: %d functions' % (share * 100, count))
    print('%d functions, %d samples, %d logs' % (len(ranked), total, len(argv) - 2))


if __name__ == '__main__':
    main(sys.argv)
