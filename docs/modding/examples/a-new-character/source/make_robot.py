"""make_robot.py: writes the character's source model, source/Robot.glb, and its voice, layers/base/assets/voice/*.wav.

    python -X utf8 -I source/make_robot.py

The model is what a 3D tool exports for a character: a skinned mesh on a skeleton with the joint names most tools
use (Mixamo's: Hips, Spine, LeftArm, LeftForeArm...), standing in a T-pose, facing +Z, in metres. It is not yet
the game's: `lumalink add character source/Robot.glb . --replaces mario --as Robot` maps its joints to Mario's,
scales it to his height and binds it to his skeleton (layers/base/assets/Robot.glb). Each part is a box that moves
with one joint: a grey body, a blue visor, dark hands and feet.

The voice: short synthesized beeps, 48,000 Hz 16-bit mono WAV, with the .sound.json beside each.
glTF 2.0 binary and WAV, Python's standard library only.
"""
import json
import math
import os
import struct
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "layers", "base", "assets")

# The skeleton: name, parent, place in the T-pose (metres, +Y up, facing +Z; the character's left is +X).
JOINTS = [
    ("Hips", None, (0.0, 0.90, 0.0)),
    ("Spine", "Hips", (0.0, 1.00, 0.0)),
    ("Spine1", "Spine", (0.0, 1.15, 0.0)),
    ("Neck", "Spine1", (0.0, 1.36, 0.0)),
    ("Head", "Neck", (0.0, 1.42, 0.0)),
    ("LeftShoulder", "Spine1", (0.07, 1.32, 0.0)),
    ("LeftArm", "LeftShoulder", (0.18, 1.32, 0.0)),
    ("LeftForeArm", "LeftArm", (0.42, 1.32, 0.0)),
    ("LeftHand", "LeftForeArm", (0.64, 1.32, 0.0)),
    ("RightShoulder", "Spine1", (-0.07, 1.32, 0.0)),
    ("RightArm", "RightShoulder", (-0.18, 1.32, 0.0)),
    ("RightForeArm", "RightArm", (-0.42, 1.32, 0.0)),
    ("RightHand", "RightForeArm", (-0.64, 1.32, 0.0)),
    ("LeftUpLeg", "Hips", (0.10, 0.86, 0.0)),
    ("LeftLeg", "LeftUpLeg", (0.10, 0.48, 0.0)),
    ("LeftFoot", "LeftLeg", (0.10, 0.08, 0.0)),
    ("RightUpLeg", "Hips", (-0.10, 0.86, 0.0)),
    ("RightLeg", "RightUpLeg", (-0.10, 0.48, 0.0)),
    ("RightFoot", "RightLeg", (-0.10, 0.08, 0.0)),
]
INDEX = {name: i for i, (name, _, _) in enumerate(JOINTS)}

GREY, VISOR, DARK = 0, 1, 2
# The parts: the joint it moves with, the material, and the box (lowest corner, highest corner) in the T-pose.
PARTS = [
    ("Hips", GREY, (-0.16, 0.80, -0.10), (0.16, 1.00, 0.10)),
    ("Spine1", GREY, (-0.18, 1.00, -0.11), (0.18, 1.36, 0.11)),
    ("Head", GREY, (-0.14, 1.38, -0.13), (0.14, 1.68, 0.13)),
    ("Head", VISOR, (-0.11, 1.50, 0.13), (0.11, 1.60, 0.15)),
    ("LeftArm", GREY, (0.18, 1.28, -0.04), (0.42, 1.36, 0.04)),
    ("LeftForeArm", GREY, (0.42, 1.285, -0.035), (0.64, 1.355, 0.035)),
    ("LeftHand", DARK, (0.64, 1.27, -0.05), (0.74, 1.37, 0.05)),
    ("RightArm", GREY, (-0.42, 1.28, -0.04), (-0.18, 1.36, 0.04)),
    ("RightForeArm", GREY, (-0.64, 1.285, -0.035), (-0.42, 1.355, 0.035)),
    ("RightHand", DARK, (-0.74, 1.27, -0.05), (-0.64, 1.37, 0.05)),
    ("LeftUpLeg", GREY, (0.05, 0.48, -0.05), (0.15, 0.86, 0.05)),
    ("LeftLeg", GREY, (0.055, 0.08, -0.045), (0.145, 0.48, 0.045)),
    ("LeftFoot", DARK, (0.04, 0.0, -0.06), (0.16, 0.08, 0.16)),
    ("RightUpLeg", GREY, (-0.15, 0.48, -0.05), (-0.05, 0.86, 0.05)),
    ("RightLeg", GREY, (-0.145, 0.08, -0.045), (-0.055, 0.48, 0.045)),
    ("RightFoot", DARK, (-0.16, 0.0, -0.06), (-0.04, 0.08, 0.16)),
]


def box_quads(lo, hi):
    """The six faces of a box, each four corners counter-clockwise seen from outside, with its normal."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    return [
        ([[x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1]], [1.0, 0.0, 0.0]),
        ([[x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [x0, y0, z0]], [-1.0, 0.0, 0.0]),
        ([[x0, y1, z0], [x0, y1, z1], [x1, y1, z1], [x1, y1, z0]], [0.0, 1.0, 0.0]),
        ([[x0, y0, z1], [x0, y0, z0], [x1, y0, z0], [x1, y0, z1]], [0.0, -1.0, 0.0]),
        ([[x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [x0, y0, z1]], [0.0, 0.0, 1.0]),
        ([[x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [x1, y0, z0]], [0.0, 0.0, -1.0]),
    ]


class Glb:
    def __init__(self):
        self.doc = {"asset": {"version": "2.0", "generator": "make_robot.py"}, "scene": 0, "scenes": [{"nodes": []}],
                    "nodes": [], "meshes": [], "materials": [], "skins": [], "accessors": [], "bufferViews": [],
                    "buffers": []}
        self.bin = bytearray()

    def accessor(self, values, kind, comp, target=None, minmax=False):
        fmt = {5126: "f", 5125: "I", 5123: "H"}[comp]
        flat = [x for v in values for x in (v if isinstance(v, (list, tuple)) else [v])]
        data = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        while len(self.bin) % 4:
            self.bin.append(0)
        view = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            view["target"] = target
        self.doc["bufferViews"].append(view)
        self.bin += data
        a = {"bufferView": len(self.doc["bufferViews"]) - 1, "componentType": comp, "count": len(values), "type": kind}
        if minmax:
            a["min"] = [min(v[i] for v in values) for i in range(3)]
            a["max"] = [max(v[i] for v in values) for i in range(3)]
        self.doc["accessors"].append(a)
        return len(self.doc["accessors"]) - 1

    def save(self, path):
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
        print(os.path.normpath(path))


def robot():
    g = Glb()
    for name, color in (("RobotBody", [0.62, 0.66, 0.72, 1.0]), ("RobotVisor", [0.15, 0.65, 0.95, 1.0]),
                        ("RobotDark", [0.12, 0.13, 0.16, 1.0])):
        g.doc["materials"].append({"name": name, "pbrMetallicRoughness": {
            "baseColorFactor": color, "metallicFactor": 0.0, "roughnessFactor": 0.5}})
    primitives = []
    for material in (GREY, VISOR, DARK):
        pos, nrm, uv, joints, weights, idx = [], [], [], [], [], []
        for joint, m, lo, hi in PARTS:
            if m != material:
                continue
            for corners, normal in box_quads(lo, hi):
                base = len(pos)
                pos += corners
                nrm += [normal] * 4
                uv += [[0.0, 0.0], [1.0, 0.0], [1.0, 1.0], [0.0, 1.0]]
                joints += [[INDEX[joint], 0, 0, 0]] * 4
                weights += [[1.0, 0.0, 0.0, 0.0]] * 4
                idx += [base, base + 1, base + 2, base, base + 2, base + 3]
        primitives.append({"attributes": {
            "POSITION": g.accessor(pos, "VEC3", 5126, 34962, True), "NORMAL": g.accessor(nrm, "VEC3", 5126, 34962),
            "TEXCOORD_0": g.accessor(uv, "VEC2", 5126, 34962), "JOINTS_0": g.accessor(joints, "VEC4", 5123, 34962),
            "WEIGHTS_0": g.accessor(weights, "VEC4", 5126, 34962)},
            "indices": g.accessor(idx, "SCALAR", 5125, 34963), "material": material, "mode": 4})
    g.doc["meshes"].append({"name": "Robot", "primitives": primitives})
    # The joints' nodes: each placed from its parent, no turn (a T-pose with every bone's frame the world's).
    for name, parent, at in JOINTS:
        base = JOINTS[INDEX[parent]][2] if parent else (0.0, 0.0, 0.0)
        node = {"name": name, "translation": [at[i] - base[i] for i in range(3)]}
        children = [INDEX[n] for n, p, _ in JOINTS if p == name]
        if children:
            node["children"] = children
        g.doc["nodes"].append(node)
    inverse = [[1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -at[0], -at[1], -at[2], 1.0]
               for _, _, at in JOINTS]
    g.doc["skins"].append({"name": "Robot", "joints": list(range(len(JOINTS))), "skeleton": 0,
                           "inverseBindMatrices": g.accessor(inverse, "MAT4", 5126)})
    g.doc["nodes"].append({"name": "Robot", "mesh": 0, "skin": 0})
    g.doc["scenes"][0]["nodes"] = [0, len(JOINTS)]
    g.save(os.path.join(HERE, "Robot.glb"))


def beep(name, notes, seconds):
    """A voice line: square-ish tones gliding through `notes` (Hz), with a soft start and end."""
    rate = 48000
    n = int(rate * seconds)
    folder = os.path.join(ASSETS, "voice")
    os.makedirs(folder, exist_ok=True)
    frames = bytearray()
    phase = 0.0
    for i in range(n):
        t = i / n
        k = min(int(t * len(notes)), len(notes) - 1)
        frac = t * len(notes) - k
        f = notes[k] if k + 1 >= len(notes) else notes[k] + (notes[k + 1] - notes[k]) * frac
        phase += 2 * math.pi * f / rate
        tone = math.sin(phase) + 0.3 * math.sin(3 * phase) + 0.15 * math.sin(5 * phase)
        envelope = min(1.0, i / 480.0, (n - i) / 1440.0)
        frames += struct.pack("<h", int(max(-1.0, min(1.0, 0.45 * tone * envelope)) * 32767))
    path = os.path.join(folder, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))
    with open(os.path.join(folder, name + ".sound.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"kind": "effect", "file": name + ".wav", "bus": "voice", "distance": [300, 3000]}, f)
        f.write("\n")
    print(os.path.normpath(path))


robot()
beep("Hup", [520.0, 880.0], 0.16)
beep("HupHigh", [620.0, 1180.0, 1400.0], 0.24)
beep("Spin", [900.0, 600.0, 900.0, 600.0], 0.28)
beep("Ouch", [700.0, 260.0], 0.30)
