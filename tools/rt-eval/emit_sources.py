"""emit_sources.py <err.txt> <rt-materials.tsv> [blocks]: the last [rt-emit] found blocks of a run with each light's
material id (m<id>) named by its table row (id = order of first appearance of the row's hash, as
RtMaterialTable::read assigns it)."""
import re
import sys

err, table = sys.argv[1], sys.argv[2]
blocks = int(sys.argv[3]) if len(sys.argv) > 3 else 1

names = {}
first_row = {}
with open(table, encoding="utf-8") as f:
    header = None
    for line in f:
        line = line.rstrip("\r\n")
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if header is None:
            header = [c.lower() for c in cols]
            continue
        # parse_hash: the first 16-hex-digit part between underscores of the hash column.
        hcol = header.index("hash") if "hash" in header else 0
        parts = [p.lower() for p in (cols[hcol] if len(cols) > hcol else "").split("_")]
        key = next((p for p in parts if re.fullmatch(r"[0-9a-f]{16}", p)), None)
        if key is None:
            continue
        if key not in first_row:
            first_row[key] = len(first_row) + 1
        mid = first_row[key]
        obj = cols[2] if len(cols) > 2 else ""
        cls = cols[3] if len(cols) > 3 else ""
        inten = cols[header.index("intensity")] if "intensity" in header and len(cols) > header.index("intensity") else ""
        prev = names.get(mid)
        label = f"{obj or '-'} [{cls}{(' i=' + inten) if inten else ''}]"
        names[mid] = label if prev is None or obj not in ("", "-") else prev

lines = open(err, encoding="utf-8", errors="replace").read().splitlines()
starts = [i for i, l in enumerate(lines) if l.startswith("[rt-emit] found=")]
for s in starts[-blocks:]:
    frame = next((l for l in reversed(lines[:s]) if l.startswith("[rt-emit] frame=")), "")
    print(frame[:120])
    print(lines[s])
    for l in lines[s + 1:]:
        if not l.startswith("[rt-emit]   "):
            break
        m = re.search(r" m(\d+) ", l)
        name = names.get(int(m.group(1)), "?") if m and m.group(1) != "0" else ""
        print(l.replace("[rt-emit]   ", "  ")[:150], "|", name)
