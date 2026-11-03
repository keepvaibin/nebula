"""loads.py run.log: the load lines of a run (load:, scene:, boot:, tour:, context, crash), each with the time since the one before."""
import re
import sys

last = None
for s in open(sys.argv[1], errors="replace"):
    m = re.match(r"\[\s*([0-9.]+)\] (?:game: )?((?:load|scene|tour|crash|exception|fatal): .*|boot: title.*|boot: first 3D.*|save: .*)", s)
    if not m:
        continue
    t = float(m.group(1))
    print(f"{t:8.3f} {'' if last is None else f'+{(t - last) * 1000:6.0f}'} {m.group(2)[:80]}")
    last = t
