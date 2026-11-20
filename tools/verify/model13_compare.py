"""model13_compare.py OLD.PAK NEW.PAK: model 13's first landing (FRAME-MODEL 2.8) moves no array. For every model of
OLD (version 12, the importer before the landing) the model of the same path in NEW (version 13, the same disc cooked
by the landing's importer) holds 12's bytes from the header's end (byte 112) to 12's end, its section table names
each array where 12's header put it with 12's counts, and MODL holds 12's two scalars. Exit 1 on any difference,
naming the model and the first field or byte that differs."""
import struct
import sys

import pakfiles

MAGIC = 0x4C444D4E
HEADER = 112
# 12's header: (word, section, what of it) for each count and place it held.
COUNTS = {"JNTS": 4, "STEP": 5, "JMSH": 6, "DMTX": 7, "ENVL": 9, "MIXS": 10, "MESH": 11, "MT12": 12, "TEXR": 13}
PLACES = {"JNTS": 15, "STEP": 16, "JMSH": 17, "DMTX": 18, "ENVL": 19, "MIXS": 20, "IBND": 21, "IMPT": 22,
          "MESH": 23, "MT12": 24, "TEXR": 25, "GEOM": 26}


def words(block, count):
    return struct.unpack_from("<%dI" % count, block, 0)


def sections(block):
    count, at = struct.unpack_from("<II", block, 16)
    out = {}
    for i in range(count):
        name, flags, place, size, entries, stride = struct.unpack_from("<4sIIIII", block, at + i * 24)
        out[name.decode("latin-1")] = (place, size, entries, stride)
    return out


def compare(path, old, new):
    w = words(old, 28)
    end = w[2]
    if new[HEADER:end] != old[HEADER:end]:
        first = next(i for i in range(HEADER, end) if i >= len(new) or new[i] != old[i])
        return f"{path}: byte {first} differs (12's end {end})"
    n = words(new, 6)
    if n[1] != 13 or n[2] != len(new) or any(words(new, 28)[6:]):
        return f"{path}: not a model 13 header {n}"
    table = sections(new)
    for name, word in PLACES.items():
        if table[name][0] != w[word]:
            return f"{path}: {name} at {table[name][0]}, 12 put it at {w[word]}"
    for name, word in COUNTS.items():
        if table[name][2] != w[word]:
            return f"{path}: {name} holds {table[name][2]}, 12 said {w[word]}"
    if table["GEOM"][1] != w[27]:
        return f"{path}: GEOM is {table['GEOM'][1]} bytes, 12 said {w[27]}"
    if table["DMTX"][2] != table["IMPT"][2]:
        return f"{path}: {table['IMPT'][2]} important joints for {table['DMTX'][2]} draw matrices"
    modl = table["MODL"][0]
    if struct.unpack_from("<II", new, modl) != (w[8], w[14]):
        return f"{path}: MODL holds {struct.unpack_from('<II', new, modl)}, 12 said {(w[8], w[14])}"
    if table["TIMG"][0] + table["TIMG"][1] != end:
        return f"{path}: TIMG ends at {table['TIMG'][0] + table['TIMG'][1]}, 12 ended at {end}"
    return None


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    faults, models = [], 0
    with open(sys.argv[1], "rb") as old_pak, open(sys.argv[2], "rb") as new_pak:
        old_files, new_files = pakfiles.files(old_pak), pakfiles.files(new_pak)
        for path, (position, size) in old_files.items():
            if size < HEADER:
                continue
            old_pak.seek(position)
            old = old_pak.read(size)
            if struct.unpack_from("<I", old)[0] != MAGIC:
                continue
            models += 1
            if path not in new_files:
                faults.append(f"{path}: not in the new store")
                continue
            new_pak.seek(new_files[path][0])
            fault = compare(path, old, new_pak.read(new_files[path][1]))
            if fault:
                faults.append(fault)
    print(f"{models} models compared, {len(faults)} differ")
    for line in faults[:40]:
        print(" ", line)
    return 1 if faults or models == 0 else 0


if __name__ == "__main__":
    sys.exit(main())
