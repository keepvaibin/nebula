"""cropseq.py <snap dir> <first> <count> <x0,y0,x1,y1 in 960x540> <out.png> [scale]: consecutive present frames'
crop side by side (two rows), each scaled up, to see what changes from frame to frame."""
import os
import sys

from PIL import Image

src, first, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
x0, y0, x1, y1 = (int(v) for v in sys.argv[4].split(","))
scale = int(sys.argv[6]) if len(sys.argv) > 6 else 3
tiles = []
for i in range(count):
    im = Image.open(os.path.join(src, f"present_{first + i}.ppm")).convert("RGB")
    k = im.width / 960.0
    c = im.crop((int(x0 * k), int(y0 * k), int(x1 * k), int(y1 * k)))
    tiles.append(c.resize(((x1 - x0) * scale, (y1 - y0) * scale), Image.NEAREST))
cols = (count + 1) // 2
tw, th = tiles[0].size
out = Image.new("RGB", (cols * tw + (cols - 1) * 4, 2 * th + 4), (255, 255, 255))
for i, t in enumerate(tiles):
    out.paste(t, ((i % cols) * (tw + 4), (i // cols) * (th + 4)))
out.save(sys.argv[5])
print(out.size, Image.open(os.path.join(src, f"present_{first}.ppm")).size)
