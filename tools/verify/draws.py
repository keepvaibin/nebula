"""draws.py RUN_LOG...: draws and draw batches a frame from [gx-frame-timing] lines (GALAXY_GX_FRAME_TIMING_SAMPLE): the median of the last 10 samples (in the stage) and the whole run's largest."""
import re, sys
for path in sys.argv[1:]:
    d, b = [], []
    for line in open(path, encoding='utf8', errors='replace'):
        if '[gx-frame-timing]' in line:
            m = re.search(r' draws=(\d+) draw-batches=(\d+)', line)
            if m:
                d.append(int(m.group(1)))
                b.append(int(m.group(2)))
    med = lambda v: sorted(v)[len(v) // 2] if v else 0
    print(path[-50:], 'samples', len(d), 'draws median', med(d[-10:]), 'max', max(d or [0]), 'batches median', med(b[-10:]), 'max', max(b or [0]))
