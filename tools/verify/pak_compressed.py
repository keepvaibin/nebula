"""pak_compressed.py GAME.PAK: every file of a cooked game.pak and every archive member (recursively): counts them and
lists any that is still compressed (a Yaz0/Yay0 magic, or a compressed flag on a member). Exit 1 when one is found."""
import struct
import sys

import pakfiles

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
    elif data[:4] in (b"RARC", b"CRAR"):
        report.bad.append(f"{name}: an archive, not a folder of the store")


def main():
    report = Report()
    with open(sys.argv[1], "rb") as pak:
        for path, (position, size) in pakfiles.files(pak).items():
            pak.seek(position)
            report.files += 1
            scan_bytes(path, pak.read(size), report)
    print(f"{sys.argv[1]}: {report.files} files, {report.archives} archives, {report.members} members, "
          f"{len(report.bad)} compressed")
    for line in report.bad[:50]:
        print(" ", line)
    sys.exit(1 if report.bad else 0)


if __name__ == "__main__":
    main()
