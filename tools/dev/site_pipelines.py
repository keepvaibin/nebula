#!/usr/bin/env python3
"""Merges the pipelines dev runs registered (GALAXY_PIPELINE_LIST_OUT) into crates/nebula-import/data/site_pipelines.txt,
leaving out what the cook's own list has.

site_pipelines.py PIPELINES_BIN RUN_LIST...

PIPELINES_BIN is a cooked store's NebulaData/Pipelines.bin: the cook's own pipelines and this file's. The cook's own are
the ones this file does not hold.
"""
import os
import sys

RECORD = 320
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "crates", "nebula-import", "data", "site_pipelines.txt")


def records(path):
    with open(path, "rb") as f:
        data = f.read()
    return {data[i:i + RECORD] for i in range(0, len(data), RECORD)}


def main():
    kept = set()
    if os.path.exists(OUT):
        with open(OUT, encoding="ascii") as f:
            kept = {bytes.fromhex(line.strip()) for line in f if line.strip()}
    cooked = records(sys.argv[1]) - kept
    for path in sys.argv[2:]:
        kept |= records(path) - cooked
    with open(OUT, "w", encoding="ascii", newline="\n") as f:
        for r in sorted(kept):
            f.write(r.hex() + "\n")
    print("cook", len(cooked), "others", len(kept))


if __name__ == "__main__":
    main()
