"""Lists layout panes authored near the left or right edge of the 608-wide canvas that no fit rule moves (on the pane or
an ancestor), from GALAXY_TEST_DUMP_LAYOUTS=1 logs: candidates for an edge rule in LayoutAspect.cpp. Translations are
summed down the tree (layouts scale and rotate little); a pane counts as edge-placed when its centre lies beyond
EDGE of the canvas centre. Only the outermost such pane of a branch is listed.
    layout_edge_audit.py <run.log> [...]"""
import pathlib
import re
import sys

EDGE = 190.0
line_re = re.compile(r"layout (\S+): (\s*)(\S+) t=\(([-0-9.]+),([-0-9.]+)\) size=\(([-0-9.]+),([-0-9.]+)\) base=(\d+)\s*(hidden)?")
rules_src = pathlib.Path(__file__).resolve().parents[1] / "petari/src/Game/Screen/LayoutAspect.cpp"
rules = set(re.findall(r'\{"(\w+)", "(\w+)", Fit', rules_src.read_text()))

trees = {}
for path in sys.argv[1:]:
    seen = set()
    for raw in open(path, encoding="utf-8", errors="replace"):
        m = line_re.search(raw)
        if not m:
            continue
        layout, depth, pane = m.group(1), len(m.group(2)) // 2, m.group(3)
        if depth == 0:
            if layout in trees:
                seen.add(layout)  # one dump per layout
                continue
            trees[layout] = []
        if layout in seen:
            continue
        trees[layout].append((depth, pane, float(m.group(4)), float(m.group(6)), m.group(9) is None))

for layout in sorted(trees):
    stack = []  # (depth, x, ruled, shown, reported)
    hits = []
    for depth, pane, tx, width, visible in trees[layout]:
        while stack and stack[-1][0] >= depth:
            stack.pop()
        parent = stack[-1] if stack else (-1, 0.0, False, True, False)
        x = parent[1] + tx
        ruled = parent[2] or (layout, pane) in rules
        shown = parent[3] and visible
        edge = depth >= 1 and shown and not ruled and abs(x) > EDGE and width < 400
        report = edge and not parent[4]
        if report:
            hits.append(f"{pane} x={x:+.0f}")
        stack.append((depth, x, ruled, shown, parent[4] or report))
    if hits:
        print(f"{layout}: " + ", ".join(hits))
