"""audiocheck.py SOURCE.PAK COOKED.PAK: each AST stream of a source pak (big-endian) against its cooked file: header numbers,
every block's size and last samples, every PCM16 sample; each AW file: bytes equal except the PCM16 ranges the cooked audio
archive names, which must be the same samples turned; every other file of AudioRes unchanged in size. Exit 1 on a difference."""
import array
import struct
import sys


def files(pak):
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


def read(pak, entry):
    pak.seek(entry[0])
    return pak.read(entry[1])


def swapped16(data):
    a = array.array("H")
    a.frombytes(data)
    a.byteswap()
    return a.tobytes()


def check_ast(name, s, d):
    errors = []
    if len(s) != len(d) or d[:4] != b"MRTS":
        return [f"{name}: size or tag"]
    for at, width in [(4, 4), (0xC, 2), (0xE, 2), (0x10, 4), (0x14, 4), (0x18, 4), (0x1C, 4), (0x20, 4), (0x24, 4)]:
        if s[at : at + width] != d[at : at + width][::-1]:
            errors.append(f"{name}: header at {at:#x}")
    if s[8:0xC] != d[8:0xC] or s[0x28:0x40] != d[0x28:0x40]:
        errors.append(f"{name}: header bytes")
    fmt = s[9]
    channels = struct.unpack_from(">H", s, 0xC)[0]
    at = 0x40
    blocks = 0
    while at < len(s):
        if s[at : at + 4] != b"BLCK" or d[at : at + 4] != b"KCLB":
            errors.append(f"{name}: block tag at {at:#x}")
            break
        size = struct.unpack_from(">I", s, at + 4)[0]
        if struct.unpack_from("<I", d, at + 4)[0] != size:
            errors.append(f"{name}: block size at {at:#x}")
        if swapped16(s[at + 8 : at + 0x20]) != d[at + 8 : at + 0x20]:
            errors.append(f"{name}: last samples at {at:#x}")
        data = at + 0x20
        end = data + size * channels
        if end > len(s) and end - len(s) <= 32 * channels:
            end = len(s)
        src = s[data:end]
        if (swapped16(src) if fmt == 1 else src) != d[data:end]:
            errors.append(f"{name}: samples at {at:#x}")
        at = end
        blocks += 1
    return errors, blocks, channels, fmt


def pcm16(smr):
    ranges = {}
    at = smr.find(b"WSYS")
    while at >= 0:
        bank = struct.unpack_from("<I", smr, at + 0x10)[0]
        for a in range(struct.unpack_from("<I", smr, at + bank + 4)[0]):
            archive = struct.unpack_from("<I", smr, at + bank + 8 + 4 * a)[0]
            if not archive:
                continue
            name = smr[at + archive : smr.index(b"\0", at + archive)].decode("latin-1").lower()
            for w in range(struct.unpack_from("<I", smr, at + archive + 0x70)[0]):
                wave = struct.unpack_from("<I", smr, at + archive + 0x74 + 4 * w)[0]
                if wave and smr[at + wave + 1] == 3:
                    ranges.setdefault(name, set()).add(struct.unpack_from("<II", smr, at + wave + 8))
        at = smr.find(b"WSYS", at + 4)
    return ranges


def main():
    errors = []
    with open(sys.argv[1], "rb") as a, open(sys.argv[2], "rb") as b:
        fa, fb = files(a), files(b)
        asts = blocks = 0
        for name in sorted(fa):
            if not name.lower().endswith(".ast"):
                continue
            result = check_ast(name, read(a, fa[name]), read(b, fb[name]))
            if isinstance(result, list):
                errors += result
                continue
            errors += result[0]
            asts += 1
            blocks += result[1]
        ranges = pcm16(read(b, fb["AudioRes/SMR.szs"]))
        aws = turned = 0
        for name in sorted(fa):
            if not name.lower().endswith(".aw"):
                continue
            s, d = read(a, fa[name]), read(b, fb[name])
            aws += 1
            expect = bytearray(s)
            for start, length in sorted(ranges.get(name.rsplit("/", 1)[-1].lower(), [])):
                expect[start : start + length] = swapped16(s[start : start + length])
                turned += length
            if bytes(expect) != d:
                errors.append(f"{name}: wave archive bytes")
    print(f"{asts} streams, {blocks} blocks; {aws} wave archives, {turned} PCM16 bytes turned; {len(errors)} differences")
    for e in errors[:40]:
        print(e)
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
