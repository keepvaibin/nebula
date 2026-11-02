"""pakfiles.py: a game.pak's files, {path: (position, size)}: the asset store (crates/nebula-import/src/cook/store.rs), or
a raw disc partition, the cooker's input."""
import struct


def store_files(pak):
    pak.seek(0)
    header = pak.read(64)
    count = struct.unpack_from("<I", header, 12)[0]
    nodes_at, _, names_at, names_size = struct.unpack_from("<QQQQ", header, 16)
    pak.seek(nodes_at)
    nodes = pak.read(count * 40)
    pak.seek(names_at)
    names = pak.read(names_size)
    paths = {0: ""}
    out = {}
    for n in range(count):
        position, size, name, parent, flags, _, _, _ = struct.unpack_from("<QQIIIIII", nodes, n * 40)
        if n == 0:
            continue
        label = names[name : names.index(b"\0", name)].decode("latin-1")
        path = paths[parent] + "/" + label if paths[parent] else label
        paths[n] = path
        if not flags & 1:
            out[path] = (position, size)
    return out


def partition_files(pak):
    pak.seek(0x424)
    at, size = (v << 2 for v in struct.unpack(">II", pak.read(8)))
    pak.seek(at)
    fst = pak.read(size)
    count = struct.unpack_from(">I", fst, 8)[0]
    names = count * 12
    out = {}
    folders = []
    for n in range(1, count):
        while folders and n >= folders[-1][1]:
            folders.pop()
        w, a, b = struct.unpack_from(">III", fst, n * 12)
        at = names + (w & 0xFFFFFF)
        label = fst[at : fst.index(b"\0", at)].decode("latin-1")
        if w >> 24:
            folders.append((label, b))
        else:
            out["/".join([f for f, _ in folders] + [label])] = (a << 2, b)
    return out


def files(pak):
    pak.seek(0)
    return store_files(pak) if pak.read(8) == b"NBSTORE\0" else partition_files(pak)
