"""make_clock.py: writes the timer's clock, layers/base/files/LayoutData/LevelTimer.arc/timg/Clock.png.

    python -X utf8 -I source/make_clock.py

A 32 by 32 clock face: a white ring, a clear inside, two hands at ten past two. RGBA PNG, written with zlib alone.
"""
import math
import os
import struct
import zlib

SIZE = 32
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "layers", "base", "files", "LayoutData", "LevelTimer.arc", "timg", "Clock.png")


def pixel(x, y):
    cx = cy = (SIZE - 1) / 2.0
    dx, dy = x - cx, cy - y
    r = math.hypot(dx, dy)
    if 12.5 <= r <= 15.0:
        return (255, 255, 255, 255)
    for angle, length in ((math.radians(90 - 60), 10.0), (math.radians(90 - 12 * 6 / 5), 7.0)):
        # Distance from the hand's line, within its length.
        ux, uy = math.cos(angle), math.sin(angle)
        along = dx * ux + dy * uy
        if 0.0 <= along <= length and abs(dx * uy - dy * ux) <= 1.1:
            return (255, 224, 96, 255)
    return (0, 0, 0, 0)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


rows = b"".join(b"\x00" + bytes(c for x in range(SIZE) for c in pixel(x, y)) for y in range(SIZE))
png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "wb") as f:
    f.write(png)
print(OUT)
