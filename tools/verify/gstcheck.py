"""gstcheck.py STORE: every ghost recording in a store, walked as the game reads it (host order): each packet's fields must end at its size; frames must count up; positions must move little. Prints a summary per file."""
import re
import struct
import sys

f = open(sys.argv[1], "rb")
head = f.read(64)
count = struct.unpack_from("<I", head, 12)[0]
nodes_at, hashes_at, names_at = struct.unpack_from("<QQQ", head, 16)
f.seek(nodes_at)
nodes = f.read(40 * count)
f.seek(names_at)
names = f.read()


def name_of(off):
    end = names.index(b"\0", off)
    return names[off:end].decode("latin-1")


arcs = []
for i in range(count):
    pos, size, name, parent, flags, first, children, res = struct.unpack_from("<QQIIIIII", nodes, i * 40)
    if not flags & 1 and name_of(name).startswith("GhostData"):
        arcs.append((name_of(name), pos, size))
print(len(arcs), "ghost archives")
bad = 0
for arc, pos, size in arcs:
    f.seek(pos)
    data = f.read(size)
    # Recordings by their first packet's shape: find the member by name in the archive's strings, sizes from the file table are cooked; walk from each aligned offset where a long chain of packets holds.
    best = None
    for start in range(0, len(data) - 4, 32):
        at = start
        packets = exact = 0
        last = None
        while at + 4 <= len(data):
            frame, length = data[at], data[at + 1]
            flags = struct.unpack_from("<H", data, at + 2)[0]
            if length < 4 or at + length > len(data):
                break
            p = at + 4
            if flags & 1:
                p += 6
            for flag, n in ((0x800, 3), (0x400, 3), (2, 1), (4, 1), (8, 1)):
                if flags & flag:
                    p += n
            if flags & 0x10:
                e = data.find(b"\0", p, at + length)
                if e < 0:
                    break
                p = e + 1
            if flags & 0x2000:
                p += 4
            if flags & 0x20:
                p += 2
            for flag in (0x40, 0x80, 0x100, 0x200, 0x1000):
                if flags & flag:
                    p += 1
            if p > at + length:
                break
            exact += p == at + length
            packets += 1
            at += length
        if best is None or packets > best[0]:
            best = (packets, exact, start, at)
    packets, exact, start, at = best
    # Positions over the best chain: host order must give steps a walking Mario can make.
    moves = []
    q = start
    last = None
    frames = []
    anims = set()
    for _ in range(packets):
        frame, length = data[q], data[q + 1]
        flags = struct.unpack_from("<H", data, q + 2)[0]
        frames.append(frame)
        p = q + 4
        if flags & 1:
            v = struct.unpack_from("<hhh", data, p)
            if last is not None:
                moves.append(max(abs(a - b) for a, b in zip(v, last)))
            last = v
        if flags & 0x10:
            s = p
            if flags & 1:
                s += 6
            for flag, n in ((0x800, 3), (0x400, 3), (2, 1), (4, 1), (8, 1)):
                if flags & flag:
                    s += n
            anims.add(data[s:data.find(b"\0", s)].decode("latin-1")[:12])
        q += length
    moves.sort()
    median = moves[len(moves) // 2] if moves else -1
    steps_ok = sum(((b - a) & 255) in (0, 1) for a, b in zip(frames, frames[1:]))
    print(f"{arc}: {packets} packets, {exact} end exactly at their size, frames counting {steps_ok}/{max(packets - 1, 1)}, position step median {median} max {moves[-1] if moves else -1}, animations {len(anims)}")
    if packets < 50 or exact != packets:
        bad += 1
print("bad", bad)
