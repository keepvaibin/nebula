"""Makes every model, texture and piece of music of the galaxy from numbers: run it from the mod's folder.

python source/content/make_content.py

It writes layers/base/assets/: NebulaPlanet.glb (the planet you walk on), NebulaPlanet.kcl.glb (its ground),
Spikeroll.glb (the enemy, skinned, clips Wait and Roll), OrbKing.glb (the boss, skinned, clips Wait, Slam, Dizzy,
Hurt), NebulaTheme.wav and BossTheme.wav (the music) and their .sound.json. Python 3, standard library only.
"""
import json
import math
import os
import struct
import wave
import zlib

OUT = os.path.join("layers", "base", "assets")


# ---- glTF ------------------------------------------------------------------------------------------------------------

class Gltf:
    def __init__(self):
        self.doc = {"asset": {"version": "2.0", "generator": "make_content.py"}, "scene": 0, "scenes": [{"nodes": []}],
                    "nodes": [], "meshes": [], "materials": [], "accessors": [], "bufferViews": [], "buffers": []}
        self.bin = bytearray()

    def view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.doc["bufferViews"].append(v)
        return len(self.doc["bufferViews"]) - 1

    def accessor(self, values, kind, comp=5126, target=34962, minmax=False):
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[kind]
        fmt = {5126: "f", 5123: "H", 5125: "I", 5121: "B"}[comp]
        flat = [x for v in values for x in (v if isinstance(v, (list, tuple)) else [v])]
        a = {"bufferView": self.view(struct.pack("<%d%s" % (len(flat), fmt), *flat), target), "componentType": comp,
             "count": len(values), "type": kind}
        if minmax:
            a["min"] = [min(v[i] for v in values) for i in range(width)]
            a["max"] = [max(v[i] for v in values) for i in range(width)]
        self.doc["accessors"].append(a)
        return len(self.doc["accessors"]) - 1

    def material(self, name, color, metallic, rough, texture=None, emissive=None):
        m = {"name": name, "pbrMetallicRoughness": {"baseColorFactor": color, "metallicFactor": metallic,
                                                    "roughnessFactor": rough}}
        if texture is not None:
            m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": texture}
        if emissive:
            m["emissiveFactor"] = emissive
        self.doc["materials"].append(m)
        return len(self.doc["materials"]) - 1

    def texture(self, png):
        d = self.doc
        d.setdefault("images", []).append({"bufferView": self.view(png), "mimeType": "image/png"})
        d.setdefault("samplers", []).append({"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497})
        d.setdefault("textures", []).append({"source": len(d["images"]) - 1, "sampler": len(d["samplers"]) - 1})
        return len(d["textures"]) - 1

    def primitive(self, mesh, material, joints=None):
        p = {"attributes": {"POSITION": self.accessor(mesh.pos, "VEC3", minmax=True),
                            "NORMAL": self.accessor(mesh.nrm, "VEC3"),
                            "TEXCOORD_0": self.accessor(mesh.uv, "VEC2")},
             "indices": self.accessor(mesh.idx, "SCALAR", 5125, 34963), "material": material, "mode": 4}
        if joints is not None:
            p["attributes"]["JOINTS_0"] = self.accessor([[j, 0, 0, 0] for j in mesh.joint], "VEC4", 5121)
            p["attributes"]["WEIGHTS_0"] = self.accessor([[1.0, 0.0, 0.0, 0.0] for _ in mesh.joint], "VEC4")
        return p

    def node(self, **kw):
        self.doc["nodes"].append(kw)
        return len(self.doc["nodes"]) - 1

    def write(self, path):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.doc["buffers"] = [{"byteLength": len(self.bin)}]
        text = json.dumps(self.doc, separators=(",", ":")).encode()
        while len(text) % 4:
            text += b" "
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(self.bin)))
            f.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
            f.write(struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin))


# ---- meshes ----------------------------------------------------------------------------------------------------------

class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx, self.joint = [], [], [], [], []

    def add(self, other, joint=0):
        base = len(self.pos)
        self.pos += other.pos
        self.nrm += other.nrm
        self.uv += other.uv
        self.idx += [i + base for i in other.idx]
        self.joint += [joint] * len(other.pos)


def sphere(radius, rings, segments, centre=(0.0, 0.0, 0.0), bump=None):
    """A UV sphere wound counter-clockwise seen from outside; `bump(theta, phi)` scales the radius."""
    m = Mesh()
    for r in range(rings + 1):
        phi = math.pi * r / rings
        for s in range(segments + 1):
            theta = 2.0 * math.pi * s / segments
            d = (math.sin(phi) * math.cos(theta), math.cos(phi), math.sin(phi) * math.sin(theta))
            k = radius * (bump(theta, phi) if bump else 1.0)
            m.pos.append([centre[i] + d[i] * k for i in range(3)])
            m.uv.append([s / segments, r / rings])
    for r in range(rings):
        for s in range(segments):
            a = r * (segments + 1) + s
            b = a + segments + 1
            m.idx += [a, a + 1, b, a + 1, b + 1, b]
    smooth_normals(m)
    return m


def cone(base_centre, tip, radius, segments):
    m = Mesh()
    axis = [tip[i] - base_centre[i] for i in range(3)]
    n = normalise(axis)
    side = normalise(cross(n, [0.0, 1.0, 0.0] if abs(n[1]) < 0.9 else [1.0, 0.0, 0.0]))
    up = cross(side, n)
    for s in range(segments):
        a0 = 2.0 * math.pi * s / segments
        a1 = 2.0 * math.pi * (s + 1) / segments
        p0 = [base_centre[i] + (side[i] * math.cos(a0) + up[i] * math.sin(a0)) * radius for i in range(3)]
        p1 = [base_centre[i] + (side[i] * math.cos(a1) + up[i] * math.sin(a1)) * radius for i in range(3)]
        face = normalise(cross([p1[i] - p0[i] for i in range(3)], [tip[i] - p0[i] for i in range(3)]))
        b = len(m.pos)
        m.pos += [p0, p1, list(tip)]
        m.nrm += [face, face, face]
        m.uv += [[s / segments, 1.0], [(s + 1) / segments, 1.0], [(s + 0.5) / segments, 0.0]]
        m.idx += [b, b + 1, b + 2]
    return m


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def normalise(v):
    k = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / k for x in v]


def smooth_normals(m):
    acc = [[0.0, 0.0, 0.0] for _ in m.pos]
    for t in range(0, len(m.idx), 3):
        a, b, c = m.idx[t:t + 3]
        n = cross([m.pos[b][i] - m.pos[a][i] for i in range(3)], [m.pos[c][i] - m.pos[a][i] for i in range(3)])
        for v in (a, b, c):
            for i in range(3):
                acc[v][i] += n[i]
    # The seam's twin vertices and the poles share one normal, so the sphere has no crease.
    by_place = {}
    for v, p in enumerate(m.pos):
        by_place.setdefault(tuple(round(x, 3) for x in p), []).append(v)
    for twins in by_place.values():
        total = [sum(acc[v][i] for v in twins) for i in range(3)]
        for v in twins:
            acc[v] = total
    m.nrm = [normalise(n) for n in acc]


# ---- textures --------------------------------------------------------------------------------------------------------

def png(width, height, pixel):
    rows = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def planet_pixel(x, y):
    # Bands of moss and stone, broken by a little interference so no two tiles look alike.
    v = 0.5 + 0.25 * math.sin(x * 0.39) * math.cos(y * 0.27) + 0.25 * math.sin((x + 2 * y) * 0.11)
    return (int(60 + 70 * v), int(120 + 90 * v), int(110 + 60 * (1 - v)))


# ---- skinning and clips ----------------------------------------------------------------------------------------------

def skinned(g, name, joints, parts, clips):
    """joints: [(name, parent index or None, local translation)]; parts: [(Mesh, material, joint)];
    clips: {name: {joint: (path, times, values)}} with path rotation or scale or translation."""
    nodes = []
    for jn, parent, t in joints:
        nodes.append(g.node(name=jn, translation=list(t)))
    for k, (jn, parent, t) in enumerate(joints):
        if parent is not None:
            g.doc["nodes"][nodes[parent]].setdefault("children", []).append(nodes[k])
    # World positions for the inverse bind matrices (translations only at rest).
    world = []
    for jn, parent, t in joints:
        base = world[parent] if parent is not None else [0.0, 0.0, 0.0]
        world.append([base[i] + t[i] for i in range(3)])
    ibm = [[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -w[0], -w[1], -w[2], 1] for w in world]
    g.doc["skins"] = [{"joints": nodes, "skeleton": nodes[0], "inverseBindMatrices": g.accessor(ibm, "MAT4", target=None)}]
    mesh = {"name": name, "primitives": []}
    for part, material, joint in parts:
        # Each part is made about its joint; at rest it stands where the joint does.
        m = Mesh()
        m.add(part, joint)
        m.pos = [[p[i] + world[joint][i] for i in range(3)] for p in m.pos]
        mesh["primitives"].append(g.primitive(m, material, joints=True))
    g.doc["meshes"].append(mesh)
    body = g.node(name=name, mesh=0, skin=0)
    g.doc["scenes"][0]["nodes"] = [nodes[0], body]
    animations = []
    for clip, tracks in clips.items():
        samplers, channels = [], []
        for joint, (path, times, values) in tracks.items():
            kind = "VEC4" if path == "rotation" else "VEC3"
            samplers.append({"input": g.accessor(times, "SCALAR", target=None, minmax=False),
                             "output": g.accessor(values, kind, target=None), "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": nodes[joint], "path": path}})
        for s in samplers:
            a = g.doc["accessors"][s["input"]]
            a["min"], a["max"] = [min(times_of(g, s))], [max(times_of(g, s))]
        animations.append({"name": clip, "samplers": samplers, "channels": channels})
    g.doc["animations"] = animations


def times_of(g, sampler):
    a = g.doc["accessors"][sampler["input"]]
    v = g.doc["bufferViews"][a["bufferView"]]
    return struct.unpack_from("<%df" % a["count"], g.bin, v["byteOffset"])


def quat_axis(axis, angle):
    s = math.sin(angle / 2.0)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2.0)]


def frames(n):
    return [k / 60.0 for k in range(n + 1)]


# ---- the models ------------------------------------------------------------------------------------------------------

def planet():
    def bump(theta, phi):
        return 1.0 + 0.03 * math.sin(5 * theta) * math.sin(4 * phi) + 0.02 * math.cos(9 * theta + 3 * phi)

    look = sphere(10.0, 48, 96, bump=bump)
    g = Gltf()
    tex = g.texture(png(64, 64, planet_pixel))
    mat = g.material("NebulaMoss", [1.0, 1.0, 1.0, 1.0], 0.0, 0.85, texture=tex)
    g.doc["meshes"].append({"name": "NebulaPlanet", "primitives": [g.primitive(look, mat)]})
    g.doc["scenes"][0]["nodes"] = [g.node(name="NebulaPlanet", mesh=0)]
    g.write(os.path.join(OUT, "NebulaPlanet.glb"))
    # The ground: the same surface, coarser, no material needed by the cook beyond one.
    ground = sphere(10.0, 24, 48, bump=bump)
    c = Gltf()
    cmat = c.material("Ground", [1.0, 1.0, 1.0, 1.0], 0.0, 1.0)
    c.doc["meshes"].append({"name": "NebulaPlanetGround", "primitives": [c.primitive(ground, cmat)]})
    c.doc["scenes"][0]["nodes"] = [c.node(name="NebulaPlanetGround", mesh=0)]
    c.write(os.path.join(OUT, "NebulaPlanet.kcl.glb"))


def spiky_ball(radius, spikes, length, centre):
    parts = sphere(radius, 16, 32, centre=centre)
    points = []
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for k in range(spikes):
        y = 1.0 - 2.0 * (k + 0.5) / spikes
        r = math.sqrt(1.0 - y * y)
        points.append([math.cos(golden * k) * r, y, math.sin(golden * k) * r])
    cones = Mesh()
    for d in points:
        base = [centre[i] + d[i] * radius * 0.92 for i in range(3)]
        tip = [centre[i] + d[i] * (radius + length) for i in range(3)]
        cones.add(cone(base, tip, radius * 0.22, 10))
    return parts, cones


def spikeroll():
    g = Gltf()
    shell = g.material("SpikerollShell", [0.32, 0.12, 0.55, 1.0], 0.6, 0.35)
    spike = g.material("SpikerollSpike", [0.98, 0.82, 0.18, 1.0], 0.9, 0.25)
    eye = g.material("SpikerollEye", [1.0, 1.0, 1.0, 1.0], 0.0, 0.2, emissive=[0.6, 0.6, 0.6])
    body, cones = spiky_ball(0.7, 14, 0.35, (0.0, 0.0, 0.0))
    eyes = Mesh()
    eyes.add(sphere(0.14, 8, 12, centre=(0.25, 0.2, 0.62)))
    eyes.add(sphere(0.14, 8, 12, centre=(-0.25, 0.2, 0.62)))
    roll = {1: ("rotation", frames(30), [quat_axis([1, 0, 0], 2 * math.pi * k / 30) for k in range(31)])}
    wait = {1: ("translation", frames(60), [[0.0, 1.05 + 0.06 * math.sin(2 * math.pi * k / 60), 0.0] for k in range(61)])}
    down = {1: ("scale", frames(30), [[1.0 + 0.4 * k / 30, 1.0 - 0.8 * k / 30, 1.0 + 0.4 * k / 30] for k in range(31)])}
    skinned(g, "Spikeroll", [("Root", None, (0, 0, 0)), ("Body", 0, (0, 1.05, 0))],
            [(body, shell, 1), (cones, spike, 1), (eyes, eye, 1)], {"Wait": wait, "Roll": roll, "Down": down})
    g.write(os.path.join(OUT, "Spikeroll.glb"))


def orb_king():
    g = Gltf()
    shell = g.material("OrbKingShell", [0.12, 0.2, 0.62, 1.0], 0.7, 0.3)
    crown = g.material("OrbKingCrown", [1.0, 0.75, 0.2, 1.0], 1.0, 0.2, emissive=[0.25, 0.18, 0.02])
    eye = g.material("OrbKingEye", [1.0, 0.25, 0.25, 1.0], 0.0, 0.2, emissive=[0.9, 0.2, 0.2])
    body = sphere(2.4, 24, 48)
    eyes = Mesh()
    eyes.add(sphere(0.35, 10, 16, centre=(0.8, 0.6, 2.15)))
    eyes.add(sphere(0.35, 10, 16, centre=(-0.8, 0.6, 2.15)))
    points = Mesh()
    for k in range(7):
        a = 2.0 * math.pi * k / 7
        points.add(cone([math.cos(a) * 0.9, 0.0, math.sin(a) * 0.9], [math.cos(a) * 1.0, 1.1, math.sin(a) * 1.0], 0.3, 10))
    ring = sphere(1.0, 6, 24, bump=lambda t, p: 1.0)
    ring.pos = [[x, y * 0.15, z] for x, y, z in ring.pos]
    points.add(ring)
    n = 60
    wait = {1: ("scale", frames(n), [[1.0 + 0.03 * math.sin(2 * math.pi * k / n), 1.0 - 0.03 * math.sin(2 * math.pi * k / n), 1.0]
                                     for k in range(n + 1)])}
    slam = {1: ("translation", frames(40), [[0.0, 2.6 + 1.5 * math.sin(math.pi * k / 40), 0.0] for k in range(41)])}
    dizzy = {2: ("rotation", frames(30), [quat_axis([0, 1, 0], 2 * math.pi * k / 30) for k in range(31)])}
    hurt = {1: ("translation", frames(20), [[0.25 * math.sin(k * 1.9), 2.6, 0.0] for k in range(21)])}
    down = {1: ("scale", frames(60), [[1.0 - 0.95 * k / 60] * 3 for k in range(61)])}
    skinned(g, "OrbKing", [("Root", None, (0, 0, 0)), ("Body", 0, (0, 2.6, 0)), ("Crown", 1, (0, 2.3, 0))],
            [(body, shell, 1), (eyes, eye, 1), (points, crown, 2)],
            {"Wait": wait, "Slam": slam, "Dizzy": dizzy, "Hurt": hurt, "Down": down})
    g.write(os.path.join(OUT, "OrbKing.glb"))


# ---- music -----------------------------------------------------------------------------------------------------------

RATE = 32000


def note(n):
    return 440.0 * 2.0 ** ((n - 69) / 12.0)


def song(path, tempo, chords, bars, lead_shape):
    """Stereo 16-bit PCM: a soft square lead playing each chord's notes up and down, a sine bass on the root, and a
    noise hat on the off beats. `chords` are MIDI notes, one chord a bar, repeated for `bars`."""
    beat = 60.0 / tempo
    total = int(RATE * beat * 4 * bars)
    frames_out = bytearray()
    for i in range(total):
        t = i / RATE
        bar = int(t / (beat * 4))
        chord = chords[bar % len(chords)]
        step = int(t / (beat / 2))
        arp = chord + [chord[1] + 12]
        lead_note = arp[step % len(arp)] if lead_shape == "up" else arp[-1 - step % len(arp)]
        into = (t % (beat / 2)) / (beat / 2)
        env = math.exp(-3.0 * into)
        phase = (t * note(lead_note + 12)) % 1.0
        lead = (0.6 if phase < 0.5 else -0.6) * env
        bass = math.sin(2 * math.pi * note(chord[0] - 12) * t) * 0.5
        hat = 0.0
        if (t % beat) / beat > 0.5:
            x = (i * 1103515245 + 12345) & 0x7FFFFFFF
            hat = ((x / 0x7FFFFFFF) * 2 - 1) * 0.12 * math.exp(-40 * ((t % beat) / beat - 0.5))
        left = 0.35 * lead + 0.45 * bass + hat
        right = 0.25 * lead + 0.45 * bass + hat * 0.7
        frames_out += struct.pack("<hh", int(max(-1, min(1, left)) * 30000), int(max(-1, min(1, right)) * 30000))
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(frames_out))
    return total


def music():
    # A calm major loop for the galaxy, a driving minor loop for the boss.
    song(os.path.join(OUT, "NebulaTheme.wav"), 104, [[60, 64, 67], [57, 60, 64], [65, 69, 72], [67, 71, 74]], 4, "up")
    song(os.path.join(OUT, "BossTheme.wav"), 150, [[57, 60, 64], [53, 57, 60], [55, 59, 62], [52, 56, 59]], 8, "down")
    for name in ("NebulaTheme", "BossTheme"):
        with open(os.path.join(OUT, name + ".sound.json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump({"kind": "effect", "file": name + ".wav", "bus": "music", "positioned": False, "loop": True}, f,
                      indent=2)
            f.write("\n")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    planet()
    spikeroll()
    orb_king()
    music()
    print("wrote", ", ".join(sorted(os.listdir(OUT))))
