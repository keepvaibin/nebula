"""Symbolizes the module's thread report (nebula_game_debug_dump lines in a host log) with llvm-symbolizer and the
module's PDB: each '+offset' becomes function and source line.

    python symbolize_dump.py <host log> <smg_game.dll> [llvm-symbolizer]
"""
import pathlib
import re
import subprocess
import sys

IMAGE_BASE = 0x180000000  # the DLL's preferred base, which llvm-symbolizer's addresses are relative to


def main():
    log, dll = pathlib.Path(sys.argv[1]), sys.argv[2]
    tool = sys.argv[3] if len(sys.argv) > 3 else "llvm-symbolizer"
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    threads = [l for l in lines if re.search(r"prio \d+/\d+ state", l)]
    offsets = sorted({int(m, 16) for l in threads for m in re.findall(r"\+([0-9a-f]+)", l.split(":", 3)[-1])})
    query = "\n".join(hex(IMAGE_BASE + o) for o in offsets) + "\n"
    out = subprocess.run([tool, "--obj=" + dll, "--functions=short", "--no-inlines"], input=query, capture_output=True,
                         text=True).stdout.split("\n\n")
    names = {}
    for o, block in zip(offsets, out):
        parts = block.strip().splitlines()
        func = parts[0] if parts else "?"
        where = pathlib.Path(parts[1].split(":")[0]).name + ":" + parts[1].split(":")[1] if len(parts) > 1 and ":" in parts[1] else ""
        names[o] = f"{func} ({where})"
    for l in threads:
        head, _, tail = l.rpartition(":")
        print(head.split("game:")[-1].strip())
        seen = set()
        for m in re.findall(r"\+([0-9a-f]+)", tail):
            o = int(m, 16)
            n = names.get(o, "?")
            if n in seen or n.startswith("?"):
                continue
            seen.add(n)
            print("     ", n)


if __name__ == "__main__":
    main()
