"""Capture existing once-per-second IPC without suspending gameplay."""
import json
import sys
import time
from nebula_frame_telemetry import Client

pid, seconds, output = int(sys.argv[1]), float(sys.argv[2]), sys.argv[3]
if not 1 <= seconds <= 60:
    raise SystemExit('Capture duration must be 1..60 seconds')
client = Client()
rows = []
start = time.monotonic()
while time.monotonic() - start < seconds:
    row = client.read(pid)
    if row and (not rows or row['sequence'] != rows[-1]['sequence']):
        rows.append(dict(row, elapsed=time.monotonic()-start))
    time.sleep(0.25)
client.close()
with open(output, 'x') as f:
    json.dump(rows, f, indent=2)
if rows:
    print(json.dumps({k: sum(r[k] for r in rows)/len(rows) for k in
                      ('copy_hz', 'first_return_hz', 'game_return_hz', 'repeat_hz')}))
else:
    raise SystemExit('No fresh telemetry; no FPS claim')
