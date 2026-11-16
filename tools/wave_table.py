"""Prints the ray-traced sea's wave table (runtime/src/render/rt/rt_surface.hlsl kSeaWave, kSeaPhase):

    python tools/wave_table.py

The table: N directional waves, a wind sea's equilibrium range (elevation spectrum ~ k^-3, so
equal slope variance a log step of k). Lengths geometric from L_MAX down to L_MIN, each jittered inside its own
step so no two are commensurate; directions about the wind (a
spread wide for every length: short crests, so a far view, which keeps only the longest waves, shows no parallel
bands); phases irregular. Deterministic (fixed seed)."""
import math
import random

N = 32
L_MAX, L_MIN = 900.0, 36.0
RMS = 0.30  # the sea's root mean square slope

rng = random.Random(20261116)
ratio = (L_MIN / L_MAX) ** (1.0 / (N - 1))
w = math.sqrt(2.0 * RMS * RMS / N)  # each wave's steepness a k: sum of (a k)^2 / 2 = RMS^2
rows = []
for i in range(N):
    length = L_MAX * ratio ** (i + rng.uniform(-0.35, 0.35))
    spread = math.radians(75.0)  # half-width: a short-crested wind sea, no long parallel crests
    # A cos^2 lobe sampled by its inverse: most waves near the wind, a few wide.
    u = rng.uniform(-1.0, 1.0)
    angle = spread * (1.0 - math.sqrt(1.0 - abs(u))) * 1.6 * (1.0 if i % 2 == 0 else -1.0)  # sides alternate: balanced about the wind
    phase = rng.uniform(0.0, 2.0 * math.pi)
    rows.append((math.cos(angle), math.sin(angle), length, w, phase))

total = math.sqrt(sum(r[3] ** 2 / 2 for r in rows))
print('// %d waves, rms slope %.3f, lengths %.0f..%.0f' % (N, total, rows[0][2], rows[-1][2]))
for r in rows:
    print('    float4(%.4ff, %.4ff, %.1ff, %.4ff),' % r[:4])
for r in rows:
    print('%.4ff, ' % r[4], end='')
print()
