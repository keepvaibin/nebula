"""make_lift.py: writes the lift's model and its ground, layers/base/assets/SkyLift.glb and SkyLift.kcl.glb.

    python -X utf8 -I source/make_lift.py

A slab 3 m square and 0.3 m thick, its top at the model's origin, so the lift stands where it is placed and Mario
stands on its placed point. The look: a yellow top with a dark rim, blue-grey sides. The ground: the same slab, one
material (kind of ground 0, sound 1, the game's plain floor). glTF 2.0 binary, Python's standard library only.
"""
import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "layers", "base", "assets")
HALF, THICK, RIM = 1.5, 0.3, 0.12


class Gltf:
    def __init__(self):
        self.doc = {"asset": {"version": "2.0", "generator": "make_lift.py"}, "scene": 0, "scenes": [{"nodes": []}],
                    "nodes": [], "meshes": [], "materials": [], "accessors": [], "bufferViews": [], "buffers": []}
        self.bin = bytearray()

    def accessor(self, values, kind, comp, target, minmax=False):
        fmt = {5126: "f", 5125: "I"}[comp]
        flat = [x for v in values for x in (v if isinstance(v, (list, tuple)) else [v])]
        data = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        while len(self.bin) % 4:
            self.bin.append(0)
        self.doc["bufferViews"].append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data),
                                        "target": target})
        self.bin += data
        a = {"bufferView": len(self.doc["bufferViews"]) - 1, "componentType": comp, "count": len(values), "type": kind}
        if minmax:
            a["min"] = [min(v[i] for v in values) for i in range(3)]
            a["max"] = [max(v[i] for v in values) for i in range(3)]
        self.doc["accessors"].append(a)
        return len(self.doc["accessors"]) - 1

    def material(self, name, color, extras=None):
        m = {"name": name, "pbrMetallicRoughness": {"baseColorFactor": color, "metallicFactor": 0.0,
                                                    "roughnessFactor": 0.6}}
        if extras is not None:
            m["extras"] = extras
        self.doc["materials"].append(m)
        return len(self.doc["materials"]) - 1

    def primitive(self, quads, material):
        pos, nrm, uv, idx = [], [], [], []
        for corners, normal in quads:
            base = len(pos)
            pos += corners
            nrm += [normal] * 4
            uv += [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]]
            idx += [base, base + 1, base + 2, base, base + 2, base + 3]
        return {"attributes": {"POSITION": self.accessor(pos, "VEC3", 5126, 34962, True),
                               "NORMAL": self.accessor(nrm, "VEC3", 5126, 34962),
                               "TEXCOORD_0": self.accessor(uv, "VEC2", 5126, 34962)},
                "indices": self.accessor(idx, "SCALAR", 5125, 34963), "material": material, "mode": 4}

    def write(self, name, primitives):
        self.doc["meshes"].append({"name": name, "primitives": primitives})
        self.doc["nodes"].append({"name": name, "mesh": 0})
        self.doc["scenes"][0]["nodes"] = [0]
        while len(self.bin) % 4:
            self.bin.append(0)
        self.doc["buffers"] = [{"byteLength": len(self.bin)}]
        text = json.dumps(self.doc, separators=(",", ":")).encode()
        while len(text) % 4:
            text += b" "
        path = os.path.join(OUT, name + ".glb")
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(self.bin)))
            f.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
            f.write(struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin))
        print(path)


def top(b):
    """The top's square, b each side of the middle, counter-clockwise seen from above."""
    return [[-b, 0.0, -b], [-b, 0.0, b], [b, 0.0, b], [b, 0.0, -b]], [0.0, 1.0, 0.0]


def ring(inner, outer):
    """The top's rim: four quads between the inner and the outer square."""
    quads = []
    for (x0, z0), (x1, z1) in (((-1, -1), (-1, 1)), ((-1, 1), (1, 1)), ((1, 1), (1, -1)), ((1, -1), (-1, -1))):
        quads.append(([[x0 * outer, 0.0, z0 * outer], [x1 * outer, 0.0, z1 * outer], [x1 * inner, 0.0, z1 * inner],
                       [x0 * inner, 0.0, z0 * inner]], [0.0, 1.0, 0.0]))
    return quads


def sides_and_bottom(h):
    quads = []
    for (nx, nz) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        # Two corners along the side, seen from outside, counter-clockwise.
        tx, tz = -nz, nx
        a = [nx * h + tx * h, nz * h + tz * h]
        b = [nx * h - tx * h, nz * h - tz * h]
        quads.append(([[a[0], 0.0, a[1]], [a[0], -THICK, a[1]], [b[0], -THICK, b[1]], [b[0], 0.0, b[1]]],
                      [float(nx), 0.0, float(nz)]))
    quads.append(([[-h, -THICK, -h], [h, -THICK, -h], [h, -THICK, h], [-h, -THICK, h]], [0.0, -1.0, 0.0]))
    return quads


inner = HALF - RIM
look = Gltf()
gold = look.material("SkyLiftTop", [0.98, 0.80, 0.22, 1.0])
rim = look.material("SkyLiftRim", [0.18, 0.20, 0.30, 1.0])
side = look.material("SkyLiftSide", [0.42, 0.50, 0.66, 1.0])
look.write("SkyLift", [look.primitive([top(inner)], gold), look.primitive(ring(inner, HALF), rim),
                       look.primitive(sides_and_bottom(HALF), side)])

ground = Gltf()
floor = ground.material("SkyLiftGround", [1.0, 1.0, 1.0, 1.0],
                        {"Floor_code": 0, "Wall_code": 0, "Sound_code": 1, "Camera_id": -1, "Camera_through": 0})
ground.write("SkyLift.kcl", [ground.primitive([top(HALF)] + sides_and_bottom(HALF), floor)])
