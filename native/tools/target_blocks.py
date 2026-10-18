"""Prints every preprocessor block conditioned on TARGET_PC (both branches), for review.

    python target_blocks.py <petari> [--context N] > blocks.txt
"""
import argparse
import pathlib
import re

COND = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b(.*)$")
OPEN = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b")
CLOSE = re.compile(r"^\s*#\s*endif\b")
EXTS = {".c", ".cpp", ".h", ".hpp", ".inc"}


def blocks(lines):
    """(start, end) line indexes of each outermost #if group whose condition names TARGET_PC."""
    out = []
    i = 0
    while i < len(lines):
        m = COND.match(lines[i])
        if m and m.group(1) != "elif" and "TARGET_PC" in m.group(2):
            depth, j = 0, i
            while j < len(lines):
                if OPEN.match(lines[j]):
                    depth += 1
                elif CLOSE.match(lines[j]):
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            out.append((i, j))
            i = j + 1
        else:
            i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--context", type=int, default=2)
    args = ap.parse_args()
    root = pathlib.Path(args.root)
    total = 0
    for path in sorted(root.rglob("*")):
        if path.suffix not in EXTS or not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if rel.startswith(("build/", "tools/")) or "MetroTRK" in rel:
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for start, end in blocks(lines):
            total += 1
            lo, hi = max(0, start - args.context), min(len(lines), end + 1 + args.context)
            print(f"=== {rel}:{start + 1}-{end + 1}")
            for k in range(lo, hi):
                print(f"{k + 1:5} {lines[k]}")
    print(f"blocks {total}")


if __name__ == "__main__":
    main()
