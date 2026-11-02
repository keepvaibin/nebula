#!/usr/bin/env python3
# water_summary.py RUN_ROOT: per tour stop, water bodies, placements in water, frames swum in that stop.
import glob, re, sys, collections
rows = collections.OrderedDict()
for log in sorted(glob.glob(sys.argv[1] + '/s*/runs/r/run.log')):
    stop = None; last = 0
    for line in open(log, errors='replace'):
        m = re.search(r'tour: \d+/\d+ (\S+ \d+)', line)
        if m:
            stop = m[1]; rows.setdefault(stop, [0, 0, 0, set()])
        m = re.search(r'test: water (\d+)/(\d+) (\S+) .* in (\d) swam (\d+)', line)
        if m and stop:
            r = rows[stop]; r[0] = int(m[2]); r[1] += int(m[4]); r[3].add(m[3])
            s = int(m[5]); r[2] += max(0, s - last); last = s
wet = {k: v for k, v in rows.items() if v[0] > 0}
print(len(rows), 'stops;', len(wet), 'with water;', sum(1 for v in wet.values() if v[2] > 0), 'swam')
for k, v in sorted(wet.items()):
    print(k, 'bodies', v[0], ','.join(sorted(v[3])), 'placed-in', v[1], 'swam', v[2])
