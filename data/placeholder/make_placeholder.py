# SPDX-License-Identifier: GPL-3.0-only
"""Writes Nebula's placeholder sources (contract references, section 4) beside this file, from nothing but the standard
library, so the bytes are the same on every machine:

  texture.png  64 x 64, magenta and black 8-pixel checker, opaque
  face.png     the model's face: the same checker with a white "?" in a black outline
  model.gltf   a 100-unit cube centred on its origin's base (it stands on the placement), every face face.png,
               nearest filtering, the default material's values (no metal, fully rough)
  sound.wav    120 ms soft two-note chime, PCM 16-bit, 32 kHz, mono

The importer cooks them into the store with the rest of Nebula's data (G's cook list). Run it again only to change
them: python -X utf8 -I data/placeholder/make_placeholder.py
"""

import base64
import json
import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

MAGENTA = (255, 0, 255)
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)

# "?" on a 5 x 7 grid, top row first.
QUESTION = [
    ".###.",
    "#...#",
    "....#",
    "...#.",
    "..#..",
    ".....",
    "..#..",
]


def checker(size=64, cell=8):
    return [[MAGENTA if ((x // cell) + (y // cell)) % 2 == 0 else BLACK for x in range(size)] for y in range(size)]


def with_question(pixels, scale=6):
    size = len(pixels)
    width, height = len(QUESTION[0]) * scale, len(QUESTION) * scale
    left, top = (size - width) // 2, (size - height) // 2

    def lit(x, y):
        gx, gy = (x - left) // scale, (y - top) // scale
        return 0 <= x - left < width and 0 <= y - top < height and QUESTION[gy][gx] == "#"

    out = [row[:] for row in pixels]
    # A black outline one pixel wide, so the mark reads on magenta and on black alike.
    for y in range(size):
        for x in range(size):
            if lit(x, y):
                continue
            if any(lit(x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                out[y][x] = BLACK
    for y in range(size):
        for x in range(size):
            if lit(x, y):
                out[y][x] = WHITE
    return out


def png(pixels):
    height, width = len(pixels), len(pixels[0])
    raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in pixels)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)  # 8-bit RGB
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def cube_gltf(image_uri, half=50.0):
    # Four vertices a face, so each face has its own normal and the whole image.
    faces = [
        ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
        ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
        ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
        ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
        ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
        ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for normal, right, up in faces:
        base = len(positions)
        for su, sv, u, v in ((-1, -1, 0.0, 1.0), (1, -1, 1.0, 1.0), (1, 1, 1.0, 0.0), (-1, 1, 0.0, 0.0)):
            p = [half * (normal[i] + su * right[i] + sv * up[i]) for i in range(3)]
            p[1] += half  # stands on its base
            positions.append(p)
            normals.append(list(normal))
            uvs.append([u, v])
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    blob = b"".join(struct.pack("<3f", *p) for p in positions)
    blob += b"".join(struct.pack("<3f", *n) for n in normals)
    blob += b"".join(struct.pack("<2f", *t) for t in uvs)
    blob += b"".join(struct.pack("<H", i) for i in indices)
    count = len(positions)
    pos_bytes, nrm_bytes, uv_bytes = count * 12, count * 12, count * 8
    lows = [min(p[i] for p in positions) for i in range(3)]
    highs = [max(p[i] for p in positions) for i in range(3)]
    return {
        "asset": {"version": "2.0", "generator": "Nebula make_placeholder.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "NebulaPlaceholder", "mesh": 0}],
        "meshes": [{"name": "NebulaPlaceholder", "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{"name": "NebulaPlaceholder", "pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 1.0}}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"uri": image_uri}],
        "buffers": [{"byteLength": len(blob),
                     "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode("ascii")}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": pos_bytes, "target": 34962},
            {"buffer": 0, "byteOffset": pos_bytes, "byteLength": nrm_bytes, "target": 34962},
            {"buffer": 0, "byteOffset": pos_bytes + nrm_bytes, "byteLength": uv_bytes, "target": 34962},
            {"buffer": 0, "byteOffset": pos_bytes + nrm_bytes + uv_bytes, "byteLength": len(indices) * 2, "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3", "min": lows, "max": highs},
            {"bufferView": 1, "componentType": 5126, "count": count, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": count, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
    }


def chime(rate=32000, length_ms=120):
    # Two soft sine notes (G5, then C6), each with a 4 ms rise and an exponential fall; no click at either end.
    samples = []
    total = rate * length_ms // 1000
    half = total // 2
    for n in range(total):
        note, start = (784.0, 0) if n < half else (1046.5, half)
        t = (n - start) / rate
        rise = min(1.0, t / 0.004)
        fall = math.exp(-t / 0.025)
        end = min(1.0, (total - n) / (rate * 0.004))
        value = 0.25 * rise * fall * end * math.sin(2.0 * math.pi * note * t)
        samples.append(max(-32768, min(32767, int(round(value * 32767.0)))))
    data = b"".join(struct.pack("<h", s) for s in samples)
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    return header + b"data" + struct.pack("<I", len(data)) + data


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else HERE
    files = {
        "texture.png": png(checker()),
        "face.png": png(with_question(checker())),
        "model.gltf": (json.dumps(cube_gltf("face.png"), indent=1, sort_keys=True) + "\n").encode("utf-8"),
        "sound.wav": chime(),
    }
    for name, data in files.items():
        with open(os.path.join(out, name), "wb") as f:
            f.write(data)
        print(f"{name}: {len(data)} bytes")


if __name__ == "__main__":
    main()
