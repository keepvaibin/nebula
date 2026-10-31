"""pe_sections.py A.dll B.dll: per section of two Windows builds, its size and whether its bytes match, and for a
.text that differs, how many bytes and at which addresses (llvm-objdump -d --start-address shows them). A change
meant to leave the code alone (headers, names) is proved by every section matching, or by differences that are
__LINE__ constants only."""
import hashlib
import struct
import sys


def section_table(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<Q", data, pe + 24 + 24)[0]
    for i in range(count):
        name, vsize, vaddr, rsize, rptr = struct.unpack_from("<8sIIII", data, pe + 24 + opt + 40 * i)
        yield name.rstrip(b"\0").decode(), vaddr + image_base, data[rptr:rptr + min(vsize, rsize)]


a = {n: (va, raw) for n, va, raw in section_table(open(sys.argv[1], "rb").read())}
b = {n: (va, raw) for n, va, raw in section_table(open(sys.argv[2], "rb").read())}
for name in sorted(set(a) | set(b)):
    sa, sb = a.get(name), b.get(name)
    same = sa is not None and sb is not None and hashlib.sha1(sa[1]).digest() == hashlib.sha1(sb[1]).digest()
    print(f"{name:10} {len(sa[1]) if sa else '-':>10} {len(sb[1]) if sb else '-':>10} {'same' if same else 'DIFFERS'}")
ta, tb = a.get(".text"), b.get(".text")
if ta and tb and ta[1] != tb[1] and len(ta[1]) == len(tb[1]):
    diff = [i for i in range(len(ta[1])) if ta[1][i] != tb[1][i]]
    print(f".text: {len(diff)} bytes differ, at", " ".join(hex(ta[0] + i) for i in diff[:16]))
