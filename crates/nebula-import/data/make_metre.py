"""The 1 m model of the 3208-model-metre test (content/models.md section 3: a glTF metre is 100 game units).

One node, `metre`, 1 m up from the origin (its translation [0, 1, 0]), carrying a 1 m cube that hangs below it to the
origin (x and z -0.5..0.5, y -1..0). Cooked as a new model it is one joint: joint 0 is the node, standing 100 units
over the model's origin, and the cube stands 100 units tall on it. Attached at the player's base (follow actor),
model.pose's gap of joint 0 is 100.

python -I -X utf8 make_metre.py <out.gltf>"""
import base64
import json
import struct
import sys

FACES = [  # (normal, four corners counter-clockwise seen from outside)
    ((1, 0, 0), [(0.5, -1, -0.5), (0.5, 0, -0.5), (0.5, 0, 0.5), (0.5, -1, 0.5)]),
    ((-1, 0, 0), [(-0.5, -1, 0.5), (-0.5, 0, 0.5), (-0.5, 0, -0.5), (-0.5, -1, -0.5)]),
    ((0, 1, 0), [(-0.5, 0, -0.5), (-0.5, 0, 0.5), (0.5, 0, 0.5), (0.5, 0, -0.5)]),
    ((0, -1, 0), [(-0.5, -1, 0.5), (-0.5, -1, -0.5), (0.5, -1, -0.5), (0.5, -1, 0.5)]),
    ((0, 0, 1), [(-0.5, -1, 0.5), (0.5, -1, 0.5), (0.5, 0, 0.5), (-0.5, 0, 0.5)]),
    ((0, 0, -1), [(0.5, -1, -0.5), (-0.5, -1, -0.5), (-0.5, 0, -0.5), (0.5, 0, -0.5)]),
]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def main():
    positions, normals, indices = [], [], []
    for normal, corners in FACES:
        # Each face winds counter-clockwise seen from outside, as glTF says.
        e1 = tuple(corners[1][i] - corners[0][i] for i in range(3))
        e2 = tuple(corners[2][i] - corners[0][i] for i in range(3))
        assert sum(x * y for x, y in zip(cross(e1, e2), normal)) > 0, normal
        base = len(positions)
        positions += corners
        normals += [normal] * 4
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    pos = b"".join(struct.pack("<3f", *p) for p in positions)
    nor = b"".join(struct.pack("<3f", *n) for n in normals)
    ind = b"".join(struct.pack("<H", i) for i in indices)
    data = pos + nor + ind
    doc = {
        "asset": {"version": "2.0", "generator": "LumaCore G make_metre.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "metre", "translation": [0, 1, 0], "mesh": 0}],
        "meshes": [{"name": "cube", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]}],
        "materials": [{"name": "metre", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.8, 0.1, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.6}}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": [-0.5, -1, -0.5], "max": [0.5, 0, 0.5]},
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
        json.dump(doc, f, indent=1)
        f.write("\n")


main()
