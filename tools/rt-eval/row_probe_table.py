"""row_probe_table.py <in.tsv> <out.tsv>: every data row's roughness becomes (line index) / 65534, so
GALAXY_RT_REFLECT_VIEW=7 shows the row a draw matched as three hex digits (rt_reflections.hlsl row_colour)."""
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src, encoding="utf-8", newline="").read().split("\n")
out = []
for k, line in enumerate(lines):
    cols = line.split("\t")
    if k == 0 or len(cols) < 5 or line.startswith("#"):
        out.append(line)
        continue
    cols[4] = f"{k / 65534.0:.9f}"
    out.append("\t".join(cols))
open(dst, "w", encoding="utf-8", newline="").write("\n".join(out))
