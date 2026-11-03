#!/usr/bin/env python3
"""cast_check.py [REPO]: fails on a number read or written by casting a pointer and dereferencing it in the game.

On the console these were how bytes became values, and some kept the console's byte order: UTF-16 read a byte at a time gave
empty sound names on a PC. A number at a byte position goes through readBytes/writeBytes (Game/Util/ByteView.hpp) or a typed
struct. Each exception below says why it cannot change a value. Exit 1 on anything not listed, or on a listed file with none."""
import os
import re
import subprocess
import sys

REVIEWED = {
}
CAST = re.compile(
    r"\*\s*(?:reinterpret_cast<\s*(?:const\s+)?(?:u8|s8|u16|s16|u32|s32|u64|s64|f32|f64|char16_t|wchar)\s*\*\s*>\s*\(|"
    r"\(\s*(?:const\s+)?(?:u16|s16|u32|s32|u64|s64|f32|f64)\s*\*\s*\))"
)


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else "."
    files = subprocess.run(["git", "-C", repo, "ls-files", "native/petari"], capture_output=True, text=True, check=True).stdout.split()
    found = {}
    for rel in files:
        if not rel.endswith((".cpp", ".hpp", ".h", ".c", ".inc")):
            continue
        path = os.path.join(repo, rel)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as source:
            for number, line in enumerate(source, 1):
                if CAST.search(line) and not line.lstrip().startswith("//"):
                    found.setdefault(rel, []).append((number, line.strip()))
    failed = False
    for rel, lines in sorted(found.items()):
        if rel in REVIEWED:
            continue
        failed = True
        for number, line in lines:
            print(f"{rel}:{number}: {line[:110]}")
    for rel in REVIEWED:
        if rel not in found:
            failed = True
            print(f"{rel}: listed, has none: take it off the list")
    print(f"cast_check: {sum(len(v) for v in found.values())} dereferenced casts, {len(found)} files, {len(REVIEWED)} reviewed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
