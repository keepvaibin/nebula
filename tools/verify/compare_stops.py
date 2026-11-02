"""compare_stops.py RUN: every picture both sides kept: identical count, then the worst by pixels over 8 levels."""
import sys, glob, os
from PIL import Image, ImageChops
R = sys.argv[1]
names = sorted(os.path.basename(f) for f in glob.glob(R + '/new/runs/fp/present_*.ppm') if 'xfb' not in f and os.path.exists(R + '/old/runs/fp/' + os.path.basename(f)))
rows = []
same = 0
for n in names:
    a = Image.open(R + '/old/runs/fp/' + n).convert('RGB')
    b = Image.open(R + '/new/runs/fp/' + n).convert('RGB')
    if a.size != b.size:
        rows.append((1e9, n, 'size', None))
        continue
    d = ImageChops.difference(a, b).convert('L')
    mx = d.getextrema()[1]
    if mx == 0:
        same += 1
        continue
    h = d.histogram()
    over = sum(h[9:])
    t = d.point(lambda v: 255 if v > 8 else 0)
    rows.append((over, n, mx, t.getbbox()))
rows.sort(key=lambda r: -r[0])
print('pictures', len(names), 'identical', same, 'differ', len(rows))
for r in rows[:40]:
    print('%8d px over 8  %s  max %s  box %s' % (r[0], r[1], r[2], r[3]))
