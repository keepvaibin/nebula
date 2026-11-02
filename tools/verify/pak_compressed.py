"""pak_compressed.py GAME.PAK: every file of a cooked game.pak and every archive member (recursively): counts them and
lists any that is still compressed (a Yaz0/Yay0 magic, or a compressed flag on a member). Exit 1 when one is found."""
import struct
import sys

COMPRESSED = (b"Yaz0", b"Yay0")
FLAG_FILE = 0x01
FLAG_COMPRESSED = 0x04
FLAG_YAZ0 = 0x80


class Report:
    files = 0
    members = 0
    archives = 0
    bad = []


def scan_bytes(name, data, report):
    if data[:4] in COMPRESSED:
        report.bad.append(f"{name}: {data[:4].decode()}")
    elif data[:4] == b"RARC":
        report.bad.append(f"{name}: archive not in host order")
    elif data[:4] == b"CRAR":
        scan_archive(name, data, report)


def scan_archive(name, data, report):
    report.archives += 1
    data_start = struct.unpack_from("<I", data, 0x0C)[0] + 0x20
    files = struct.unpack_from("<I", data, 0x28)[0]
    file_at = struct.unpack_from("<I", data, 0x2C)[0] + 0x20
    strings_at = struct.unpack_from("<I", data, 0x34)[0] + 0x20
    for i in range(files):
        at = file_at + 0x14 * i
        word = struct.unpack_from("<I", data, at + 4)[0]
        flags = word >> 24
        if flags & FLAG_FILE == 0:
            continue
        name_at = strings_at + (word & 0xFFFFFF)
        member = data[name_at : data.index(b"\0", name_at)].decode("latin-1")
        offset, size = struct.unpack_from("<II", data, at + 8)
        report.members += 1
        if flags & (FLAG_COMPRESSED | FLAG_YAZ0):
            report.bad.append(f"{name}/{member}: compressed flags {flags:#x}")
        scan_bytes(f"{name}/{member}", data[data_start + offset : data_start + offset + size], report)


def main():
    report = Report()
    with open(sys.argv[1], "rb") as pak:
        pak.seek(0x424)
        fst_at, fst_size = (v << 2 for v in struct.unpack(">II", pak.read(8)))
        pak.seek(fst_at)
        fst = pak.read(fst_size)
        count = struct.unpack_from(">I", fst, 8)[0]
        names = count * 12
        folders = []
        for n in range(1, count):
            while folders and n >= folders[-1][1]:
                folders.pop()
            word0, a, b = struct.unpack_from(">III", fst, n * 12)
            at = names + (word0 & 0xFFFFFF)
            label = fst[at : fst.index(b"\0", at)].decode("latin-1")
            if word0 >> 24:
                folders.append((label, b))
                continue
            path = "/".join([f for f, _ in folders] + [label])
            pak.seek(a << 2)
            report.files += 1
            scan_bytes(path, pak.read(b), report)
    print(f"{sys.argv[1]}: {report.files} files, {report.archives} archives, {report.members} members, "
          f"{len(report.bad)} compressed")
    for line in report.bad[:50]:
        print(" ", line)
    sys.exit(1 if report.bad else 0)


if __name__ == "__main__":
    main()
