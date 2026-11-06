#!/usr/bin/env python3
"""Compares two inspector dumps by item key and prints each item's first differing value.

inspect_diff.py A B [--names forms.tsv] [--max N]  (A and B: files, or folders of inspect_*.tsv)
"""
import os
import sys


def read(path):
    passes = []
    items = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if parts[0] == "P":
                passes.append(parts[1:])
            elif parts[0] == "I":
                items[tuple(parts[1:5])] = parts[5:]
    return passes, items


def names_of(path):
    out = {}
    if not path:
        return out
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2:
                out[parts[0].lower()] = parts[1]
    return out


def compare(a, b, names, limit):
    pa, ia = read(a)
    pb, ib = read(b)
    shown = 0
    differing = 0
    for n, (x, y) in enumerate(zip(pa, pb)):
        if x != y:
            print(f"pass {n}: {x} / {y}")
            differing += 1
            break
    if len(pa) != len(pb):
        print(f"passes: {len(pa)} / {len(pb)}")
        differing += 1
    only_a = [k for k in ia if k not in ib]
    only_b = [k for k in ib if k not in ia]
    for key, values in ia.items():
        other = ib.get(key)
        if other is None or other == values:
            continue
        differing += 1
        if shown >= limit:
            continue
        shown += 1
        va = dict(v.split("=", 1) for v in values if "=" in v)
        vb = dict(v.split("=", 1) for v in other if "=" in v)
        for field in list(va) + [f for f in vb if f not in va]:
            if field in ("index", "slot", "generation"):
                continue
            if va.get(field) != vb.get(field):
                name = names.get(key[0], key[0])
                print(f"{name} part {key[1]} layer {key[2]} #{key[3]}: {field}: {va.get(field)} / {vb.get(field)}")
                break
    print(f"{os.path.basename(a)}: items {len(ia)} / {len(ib)}, only first {len(only_a)}, only second {len(only_b)}, differing {differing}")
    for k in only_a[:limit]:
        print("  only first:", names.get(k[0], k[0]), *k[1:])
    for k in only_b[:limit]:
        print("  only second:", names.get(k[0], k[0]), *k[1:])
    return differing + len(only_a) + len(only_b)


def main():
    args = sys.argv[1:]
    names = ""
    limit = 40
    if "--names" in args:
        i = args.index("--names")
        names = args[i + 1]
        del args[i:i + 2]
    if "--max" in args:
        i = args.index("--max")
        limit = int(args[i + 1])
        del args[i:i + 2]
    a, b = args
    table = names_of(names)
    total = 0
    if os.path.isdir(a):
        for name in sorted(os.listdir(a)):
            if name.startswith("inspect_") and os.path.exists(os.path.join(b, name)):
                total += compare(os.path.join(a, name), os.path.join(b, name), table, limit)
    else:
        total = compare(a, b, table, limit)
    print("total", total)
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
