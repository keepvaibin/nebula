"""objdiff.py A_BUILD B_BUILD: COFF objects present in both build folders whose code or data differ (section bytes, without debug and linker sections); a count line at the end."""
import os
import struct
import sys


def sections(path):
    d = open(path, "rb").read()
    if d[:4] == b"\x00\x00\xff\xff":  # bigobj
        count = struct.unpack_from("<I", d, 44)[0]
        at = 56
    else:
        count = struct.unpack_from("<H", d, 2)[0]
        at = 20 + struct.unpack_from("<H", d, 16)[0]
    out = []
    for i in range(count):
        name = d[at:at + 8].rstrip(b"\0")
        size, ptr = struct.unpack_from("<II", d, at + 16)
        at += 40
        if name.startswith((b".debug", b".drectve", b".llvm", b".xdata", b".pdata")) or ptr == 0:
            continue
        out.append((name, d[ptr:ptr + size]))
    return out


a, b = sys.argv[1], sys.argv[2]
same = 0
diff = []
missing = 0
for root, _, files in os.walk(a):
    for f in files:
        if not f.endswith(".obj"):
            continue
        pa = os.path.join(root, f)
        pb = os.path.join(b, os.path.relpath(pa, a))
        if not os.path.exists(pb):
            missing += 1
            continue
        if sections(pa) == sections(pb):
            same += 1
        else:
            diff.append(os.path.relpath(pa, a).replace("\\", "/"))
for p in sorted(diff):
    print(p)
print(f"same {same} different {len(diff)} missing {missing}")
