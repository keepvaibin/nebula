"""gaps.py run.log: game frames over 25 ms from the [latency] lines (GALAXY_TRACE_VI_TIMES), each with the last scene state seen."""
import re
import sys

QPC = 1e7  # the system's performance counter runs at 10 MHz
state = ""
last = None
gaps = []
frames = 0
for s in open(sys.argv[1], errors="replace"):
    m = re.search(r"scene: (\[.*\])|tour: (\d+/\d+ \S+ \d+)", s)
    if m:
        state = m.group(1) or m.group(2)
    m = re.search(r"\[latency\] retrace vi=(\d+) qpc=(\d+)", s)
    if m:
        t = int(m.group(2)) / QPC
        frames += 1
        if last is not None and (t - last) * 1000 > 25:
            gaps.append(((t - last) * 1000, int(m.group(1)), state))
        last = t
print("frames", frames, "over 25 ms", len(gaps))
for g in gaps[:60]:
    print(f"{g[0]:7.1f} ms frame {g[1]} {g[2][:50]}")
