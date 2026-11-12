"""The model of the conformance test metal-smooth (materials contract): a 1 m sphere, metal 1, roughness 0.05.

One node, `sphere`, half a metre up from the origin (translation [0, 0.5, 0]), carrying a UV sphere of radius 0.5 m
centred on it, so the sphere rests on the model's origin. Smooth normals (the sphere's own directions), counter-
clockwise from outside as glTF says. Material: base colour 0.95 grey, metallic 1, roughness 0.05.

python -I -X utf8 make_metal_smooth.py <out.gltf>"""
import base64
import json
import math
import struct
import sys

RINGS = 24     # latitude bands
SEGMENTS = 48  # longitude slices
RADIUS = 0.5


def main():
    positions, normals, indices = [], [], []
    for r in range(RINGS + 1):
        theta = math.pi * r / RINGS  # 0 at the top
        for s in range(SEGMENTS + 1):
            phi = 2 * math.pi * s / SEGMENTS
            n = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
            normals.append(n)
            positions.append(tuple(RADIUS * c for c in n))
    row = SEGMENTS + 1
    for r in range(RINGS):
        for s in range(SEGMENTS):
            a, b = r * row + s, (r + 1) * row + s
            # Seen from outside, (a, a+1, b) and (a+1, b+1, b) wind counter-clockwise.
            if r != 0:
                indices += [a, a + 1, b]
            if r != RINGS - 1:
                indices += [a + 1, b + 1, b]
    # Check the winding: each triangle's face normal points away from the centre.
    for t in range(0, len(indices), 3):
        p0, p1, p2 = (positions[i] for i in indices[t:t + 3])
        e1 = [p1[k] - p0[k] for k in range(3)]
        e2 = [p2[k] - p0[k] for k in range(3)]
        face = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        centre = [(p0[k] + p1[k] + p2[k]) / 3 for k in range(3)]
        assert sum(face[k] * centre[k] for k in range(3)) > 0, t
    pos = b"".join(struct.pack("<3f", *p) for p in positions)
    nor = b"".join(struct.pack("<3f", *n) for n in normals)
    ind = b"".join(struct.pack("<H", i) for i in indices)
    pad = b"\0" * (-len(ind) % 4)
    data = pos + nor + ind + pad
    doc = {
        "asset": {"version": "2.0", "generator": "LumaCore G make_metal_smooth.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "sphere", "translation": [0, 0.5, 0], "mesh": 0}],
        "meshes": [{"name": "sphere", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
        "materials": [{"name": "metal_smooth", "pbrMetallicRoughness": {"baseColorFactor": [0.95, 0.95, 0.95, 1.0], "metallicFactor": 1.0, "roughnessFactor": 0.05}}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": [-RADIUS, -RADIUS, -RADIUS], "max": [RADIUS, RADIUS, RADIUS]},
            {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pos), "target": 34962},
            {"buffer": 0, "byteOffset": len(pos), "byteLength": len(nor), "target": 34962},
            {"buffer": 0, "byteOffset": len(pos) + len(nor), "byteLength": len(ind), "target": 34963},
        ],
        "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}],
    }
    with open(sys.argv[1], "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, separators=(",", ":"))
        f.write("\n")


main()
