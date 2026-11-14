"""make_balloon.py: writes the mod's balloon, layers/base/assets/Balloon.glb, from shapes in code.

    python -X utf8 -I tools/make_balloon.py

A red balloon 80 units across on a string 40 units long, 130 units tall in all, standing on its string's end (the
model's origin). Binary glTF 2.0: positions, normals, texture coordinates, two metal-roughness materials, a two-joint
skin (Root at the string's end, Body at the balloon) and a looping 3-second clip "Float" that bobs the balloon 6 units
up and down. glTF's metre is 100 game units; +Y up.
"""
import json
import math
import os
import struct
import sys


class Part:
    """Triangles of one material: positions, normals, uvs, the joint each vertex is on, indices (counter-clockwise)."""

    def __init__(self, material):
        self.material = material
        self.p, self.n, self.t, self.j, self.i = [], [], [], [], []

    def vertex(self, p, n, t, joint):
        self.p.append(p)
        self.n.append(n)
        self.t.append(t)
        self.j.append(joint)
        return len(self.p) - 1


def norm(v):
    d = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / d for c in v)


def ellipsoid(part, centre, radii, joint, rings, segments):
    rows = []
    for r in range(rings + 1):
        a = math.pi * r / rings
        row = []
        for s in range(segments + 1):
            b = 2 * math.pi * s / segments
            unit = (math.sin(a) * math.sin(b), math.cos(a), math.sin(a) * math.cos(b))
            p = tuple(u * k + c for u, k, c in zip(unit, radii, centre))
            n = norm(tuple(u / k for u, k in zip(unit, radii)))
            row.append(part.vertex(p, n, (s / segments, r / rings), joint))
        rows.append(row)
    for r in range(rings):
        for s in range(segments):
            a, b, c, d = rows[r][s], rows[r][s + 1], rows[r + 1][s + 1], rows[r + 1][s]
            part.i += [a, d, c, a, c, b]


def cylinder(part, y0, height, r_bottom, r_top, joint, segments, caps):
    slope = (r_bottom - r_top) / height
    ring = []
    for s in range(segments + 1):
        b = 2 * math.pi * s / segments
        sx, sz = math.sin(b), math.cos(b)
        n = norm((sx, slope, sz))
        lo = part.vertex((sx * r_bottom, y0, sz * r_bottom), n, (s / segments, 1.0), joint)
        hi = part.vertex((sx * r_top, y0 + height, sz * r_top), n, (s / segments, 0.0), joint)
        ring.append((lo, hi))
    for s in range(segments):
        (a, b), (c, d) = ring[s], ring[s + 1]
        part.i += [a, c, d, a, d, b]
    if not caps:
        return
    for y, r, up in ((y0, r_bottom, -1), (y0 + height, r_top, 1)):
        centre = part.vertex((0, y, 0), (0, up, 0), (0.5, 0.5), joint)
        edge = [part.vertex((math.sin(2 * math.pi * s / segments) * r, y, math.cos(2 * math.pi * s / segments) * r),
                            (0, up, 0), (0.5, 0.5), joint) for s in range(segments + 1)]
        for s in range(segments):
            part.i += [centre, edge[s], edge[s + 1]] if up > 0 else [centre, edge[s + 1], edge[s]]


class Glb:
    def __init__(self):
        self.bin = bytearray()
        self.doc = {"asset": {"version": "2.0", "generator": "make_balloon.py"}, "scene": 0, "scenes": [{"nodes": []}],
                    "nodes": [], "meshes": [], "materials": [], "accessors": [], "bufferViews": [], "buffers": []}

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.doc["bufferViews"].append(v)
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, kind, ctype, rows, fmt, target=None, minmax=False):
        rows = [r if isinstance(r, (tuple, list)) else (r,) for r in rows]
        data = b"".join(struct.pack("<" + fmt, *r) for r in rows)
        a = {"bufferView": self.view(data, target), "componentType": ctype, "count": len(rows), "type": kind}
        if minmax:
            cols = list(zip(*rows))
            a["min"] = [min(c) for c in cols]
            a["max"] = [max(c) for c in cols]
        self.doc["accessors"].append(a)
        return len(self.doc["accessors"]) - 1

    def material(self, name, colour, metal, rough, emissive):
        self.doc["materials"].append({"name": name, "emissiveFactor": list(emissive),
                                      "pbrMetallicRoughness": {"baseColorFactor": list(colour), "metallicFactor": metal,
                                                               "roughnessFactor": rough}})
        return len(self.doc["materials"]) - 1

    def bytes(self):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.doc["buffers"] = [{"byteLength": len(self.bin)}]
        js = json.dumps(self.doc, separators=(",", ":")).encode("utf-8")
        while len(js) % 4:
            js += b" "
        body = struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin)
        return struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body


def balloon():
    g = Glb()
    rubber = Part(g.material("Balloon", (0.85, 0.08, 0.12, 1.0), 0.0, 0.25, (0.08, 0.0, 0.0)))
    string = Part(g.material("String", (0.9, 0.9, 0.85, 1.0), 0.0, 0.9, (0.0, 0.0, 0.0)))
    ellipsoid(rubber, (0, 0.90, 0), (0.40, 0.46, 0.40), 1, 16, 24)       # the balloon, 80 units across
    cylinder(rubber, 0.40, 0.06, 0.02, 0.05, 1, 10, True)                  # its knot
    cylinder(string, 0.0, 0.40, 0.006, 0.006, 1, 6, False)                 # the string, down to the origin
    joints = [("Root", None, (0.0, 0.0, 0.0)), ("Body", 0, (0.0, 0.5, 0.0))]
    index = []
    for name, parent, at in joints:
        local = at if parent is None else tuple(a - b for a, b in zip(at, joints[parent][2]))
        g.doc["nodes"].append({"name": name, "translation": list(local)})
        index.append(len(g.doc["nodes"]) - 1)
    g.doc["nodes"][index[0]]["children"] = [index[1]]
    inverse = [(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -at[0], -at[1], -at[2], 1) for _, _, at in joints]
    g.doc["skins"] = [{"joints": index, "skeleton": index[0], "inverseBindMatrices": g.accessor("MAT4", 5126, inverse, "16f")}]
    prims = []
    for part in (rubber, string):
        attrs = {"POSITION": g.accessor("VEC3", 5126, part.p, "3f", 34962, True),
                 "NORMAL": g.accessor("VEC3", 5126, part.n, "3f", 34962),
                 "TEXCOORD_0": g.accessor("VEC2", 5126, part.t, "2f", 34962),
                 "JOINTS_0": g.accessor("VEC4", 5123, [(j, 0, 0, 0) for j in part.j], "4H", 34962),
                 "WEIGHTS_0": g.accessor("VEC4", 5126, [(1.0, 0.0, 0.0, 0.0)] * len(part.j), "4f", 34962)}
        prims.append({"attributes": attrs, "indices": g.accessor("SCALAR", 5125, part.i, "I", 34963),
                      "material": part.material, "mode": 4})
    g.doc["meshes"].append({"name": "Balloon", "primitives": prims})
    g.doc["nodes"].append({"name": "Balloon", "mesh": 0, "skin": 0})
    g.doc["scenes"][0]["nodes"] = [index[0], len(g.doc["nodes"]) - 1]
    # Float: the balloon's joint bobs 6 units over 3 seconds and comes back where it began.
    times = [k * 0.25 for k in range(13)]
    g.doc["animations"] = [{"name": "Float",
                            "samplers": [{"input": g.accessor("SCALAR", 5126, times, "f", None, True),
                                          "output": g.accessor("VEC3", 5126, [(0, 0.5 + 0.06 * math.sin(t * 2 * math.pi / 3), 0)
                                                                              for t in times], "3f"),
                                          "interpolation": "LINEAR"}],
                            "channels": [{"sampler": 0, "target": {"node": index[1], "path": "translation"}}]}]
    return g


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "layers", "base", "assets", "Balloon.glb")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    data = balloon().bytes()
    with open(out, "wb") as f:
        f.write(data)
    print("layers/base/assets/Balloon.glb: %d bytes" % len(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
