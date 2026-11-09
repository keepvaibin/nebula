"""stopcross.py RUN [LIMIT]: D3D12 against Vulkan, picture by picture, for a stopcross.sh run.

RUN holds dx/ and vk/ (each a run folder with runs/fp/present_*.ppm). For every picture both backends kept: how many are
identical, then the worst by pixels over 8 levels with the box they lie in, and the water-line summary (the number of
pictures whose differing pixels all lie within 24 levels). With LIMIT (pixels over 8 levels in the worst picture) the exit
code is 1 when the worst picture is above it.
"""
import glob
import os
import sys

from PIL import Image, ImageChops

LEVELS = 8
WATER_LEVELS = 24

run = sys.argv[1]
limit = int(sys.argv[2]) if len(sys.argv) > 2 else None
dx_dir = os.path.join(run, 'dx', 'runs', 'fp')
vk_dir = os.path.join(run, 'vk', 'runs', 'fp')
names = sorted(os.path.basename(f) for f in glob.glob(os.path.join(dx_dir, 'present_*.ppm'))
               if 'display' not in f and os.path.exists(os.path.join(vk_dir, os.path.basename(f))))
if not names:
    print('no picture both backends kept in', run)
    sys.exit(2)

rows = []
identical = 0
within_water = 0
for name in names:
    a = Image.open(os.path.join(dx_dir, name)).convert('RGB')
    b = Image.open(os.path.join(vk_dir, name)).convert('RGB')
    if a.size != b.size:
        rows.append((1 << 40, name, 'size %s against %s' % (a.size, b.size), None))
        continue
    diff = ImageChops.difference(a, b).convert('L')
    most = diff.getextrema()[1]
    if most == 0:
        identical += 1
        continue
    histogram = diff.histogram()
    over = sum(histogram[LEVELS + 1:])
    if most <= WATER_LEVELS:
        within_water += 1
    box = diff.point(lambda v: 255 if v > LEVELS else 0).getbbox()
    rows.append((over, name, most, box))

rows.sort(key=lambda r: -r[0])
worst = rows[0][0] if rows else 0
print('pictures', len(names), 'identical', identical, 'differ', len(rows),
      'differ by at most %d levels' % WATER_LEVELS, within_water)
for over, name, most, box in rows[:40]:
    print('%10d px over %d  %s  max %s  box %s' % (over, LEVELS, name, most, box))
print('worst picture: %d px over %d levels' % (worst, LEVELS))
if limit is not None and worst > limit:
    print('above the limit of', limit)
    sys.exit(1)
