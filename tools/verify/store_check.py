"""store_check.py A.PAK B.PAK: every file of two game.paks, by path, byte for byte; each may be a store or a partition. Lists
what one has and the other lacks and every file that differs. Exit 1 on a difference."""
import sys

import pakfiles

CHUNK = 1 << 24


def same(a, entry_a, b, entry_b):
    if entry_a[1] != entry_b[1]:
        return False
    a.seek(entry_a[0])
    b.seek(entry_b[0])
    left = entry_a[1]
    while left:
        n = min(left, CHUNK)
        if a.read(n) != b.read(n):
            return False
        left -= n
    return True


def main():
    with open(sys.argv[1], "rb") as a, open(sys.argv[2], "rb") as b:
        fa, fb = pakfiles.files(a), pakfiles.files(b)
        only_a = sorted(set(fa) - set(fb))
        only_b = sorted(set(fb) - set(fa))
        differ = [p for p in fa if p in fb and not same(a, fa[p], b, fb[p])]
    print(f"{len(fa)} and {len(fb)} files: only first {len(only_a)}, only second {len(only_b)}, differ {len(differ)}")
    for label, paths in (("only first", only_a), ("only second", only_b), ("differ", differ)):
        for path in paths[:40]:
            print(f"  {label}: {path}")
    sys.exit(1 if only_a or only_b or differ else 0)


if __name__ == "__main__":
    main()
