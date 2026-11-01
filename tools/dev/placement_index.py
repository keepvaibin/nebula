#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Which galaxies place an object: scans each stage archive under StageData (Yaz0 + RARC) for the names given.

    placement_index.py STAGEDATA NAME [NAME...]

Prints `archive name` for every archive whose decompressed bytes hold a name as a whole string (the
placement tables' string pools and object lists name their objects that way).
"""
import pathlib
import re
import sys


def yaz0(data):
    if data[:4] != b'Yaz0':
        return data
    size = int.from_bytes(data[4:8], 'big')
    out = bytearray()
    src = 16
    while len(out) < size:
        code = data[src]
        src += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if code & (0x80 >> bit):
                out.append(data[src])
                src += 1
            else:
                b1, b2 = data[src], data[src + 1]
                src += 2
                dist = ((b1 & 0x0F) << 8 | b2) + 1
                count = b1 >> 4
                if count == 0:
                    count = data[src] + 0x12
                    src += 1
                else:
                    count += 2
                start = len(out) - dist
                for i in range(count):
                    out.append(out[start + i])
    return bytes(out)


def main():
    root = pathlib.Path(sys.argv[1])
    names = [n.encode() for n in sys.argv[2:]]
    patterns = [re.compile(rb'(?<![A-Za-z0-9_])' + re.escape(n) + rb'(?![A-Za-z0-9_])') for n in names]
    # SMG keeps a stage or zone as StageData/<name>.arc; SMG2 as StageData/<name>/<name>*.arc.
    for arc in sorted(root.rglob('*.arc')):
        data = yaz0(arc.read_bytes())
        for name, pattern in zip(names, patterns):
            if pattern.search(data):
                print(arc.relative_to(root).as_posix(), name.decode())


if __name__ == '__main__':
    main()
