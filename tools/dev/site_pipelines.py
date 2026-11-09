#!/usr/bin/env python3
"""Merges the pipelines runs registered (--diagnostics pipelines: pipelines-<pid>.bin in the diagnostics folder) into crates/nebula-import/data/site_pipelines.txt,
leaving out what the cook's own list has and what names a program the program pack does not have (runtime/src/render/programs/
table.tsv and the written shaders): a pipeline of a program text that no longer exists is refused by every run.

site_pipelines.py PIPELINES_BIN RUN_LIST...

PIPELINES_BIN is a cooked store's NebulaData/Pipelines.bin: the cook's own pipelines and this file's. The cook's own are
the ones this file does not hold. A run list is Pipelines.bin's format (nebula_frame_list.h NEBULA_PIPELINE_FILE_MAGIC): a
16-byte header, then records of 28 bytes and 12 bytes for each of the input count at offset 20. A file of the fixed-size records
before the list was counted (320 bytes each, no header) reads too; the lines written are counted records.
"""
import os
import struct
import sys

MAGIC = b"NBPIPEL2"
HEADER = 16
RECORD_HEADER = 28
INPUT = 12
FIXED_RECORD = 320
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "crates", "nebula-import", "data", "site_pipelines.txt")
TABLE = os.path.join(HERE, "..", "..", "runtime", "src", "render", "programs", "table.tsv")
SITE = 1 << 63
WRITTEN_LIMIT = 256  # the written shaders' ids are SITE | number << 1 (compile_programs.py WRITTEN), a vertex form also | 1


def shipped():
    """The (stage, hash) pairs the program pack holds: stage "v" vertex, "p" pixel."""
    found = set()
    with open(TABLE, encoding="utf-8") as f:
        for line in f:
            fields = line.split()
            if len(fields) == 3:
                found.add((fields[0], int(fields[1], 16)))
    return found


def has_programs(record, table):
    """Both programs of a record are written shaders or in the table."""
    vertex, pixel = struct.unpack_from("<QQ", record, 0)
    return all(i & SITE and i & ~SITE < WRITTEN_LIMIT or (stage, i) in table for stage, i in (("v", vertex), ("p", pixel)))


def from_fixed(old):
    """A fixed-size record (24 inputs of a byte-wide semantic) as a counted one."""
    count = min(struct.unpack_from("<I", old, 20)[0], 24)
    out = bytearray(old[:RECORD_HEADER])
    struct.pack_into("<I", out, 20, count)
    for i in range(count):
        at = RECORD_HEADER + i * INPUT
        out += bytes([old[at], 0, old[at + 1], old[at + 2]]) + old[at + 4:at + 12]
    return bytes(out)


def records(path):
    with open(path, "rb") as f:
        data = f.read()
    found = set()
    if data[:8] != MAGIC:
        for i in range(0, len(data) - FIXED_RECORD + 1, FIXED_RECORD):
            found.add(from_fixed(data[i:i + FIXED_RECORD]))
        return found
    count = struct.unpack_from("<I", data, 8)[0]
    at = HEADER
    for _ in range(count):
        inputs = struct.unpack_from("<I", data, at + 20)[0]
        end = at + RECORD_HEADER + inputs * INPUT
        found.add(data[at:end])
        at = end
    return found


def main():
    kept = set()
    if os.path.exists(OUT):
        with open(OUT, encoding="ascii") as f:
            kept = set()
        for line in f:
            if line.strip():
                raw = bytes.fromhex(line.strip())
                kept.add(from_fixed(raw) if len(raw) == FIXED_RECORD else raw)
    cooked = records(sys.argv[1]) - kept
    for path in sys.argv[2:]:
        kept |= records(path) - cooked
    table = shipped()
    stale = {r for r in kept if not has_programs(r, table)}
    kept -= stale
    with open(OUT, "w", encoding="ascii", newline="\n") as f:
        for r in sorted(kept):
            f.write(r.hex() + "\n")
    print("cook", len(cooked), "others", len(kept), "dropped, no program:", len(stale))


if __name__ == "__main__":
    main()
