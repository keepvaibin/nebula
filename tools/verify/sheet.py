"""sheet.py RUN OUT frame...: old | new | difference x4 for each frame, one row each."""
import sys
from PIL import Image, ImageChops
R, out = sys.argv[1], sys.argv[2]
rows = sys.argv[3:]
w, h = 480, 270
S = Image.new('RGB', (w * 3, h * len(rows)))
for i, n in enumerate(rows):
    a = Image.open(f'{R}/old/runs/fp/present_{n}.ppm').convert('RGB')
    b = Image.open(f'{R}/new/runs/fp/present_{n}.ppm').convert('RGB')
    d = ImageChops.difference(a, b).point(lambda v: min(255, v * 4))
    for j, x in enumerate((a, b, d)):
        S.paste(x.resize((w, h)), (j * w, i * h))
S.save(out)
